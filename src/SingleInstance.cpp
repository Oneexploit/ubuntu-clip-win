#include "SingleInstance.h"

#include <QLocalSocket>
#include <QTimer>

SingleInstance::SingleInstance(QObject *parent) : QObject(parent) {
    connect(&server_, &QLocalServer::newConnection, this, &SingleInstance::handleNewConnection);
}

SingleInstance::~SingleInstance() {
    server_.close();
}

QString SingleInstance::socketName() {
    const QString user = QString::fromLocal8Bit(qgetenv("USER"));
    return QStringLiteral("ubuntu-clip-win-") + (user.isEmpty() ? QStringLiteral("user") : user);
}

bool SingleInstance::sendMessage(const QString &message) {
    QLocalSocket socket;
    socket.connectToServer(socketName());
    if (!socket.waitForConnected(150)) {
        return false;
    }
    socket.write(message.toUtf8());
    socket.write("\n");
    socket.flush();
    socket.waitForBytesWritten(150);
    socket.disconnectFromServer();
    return true;
}

bool SingleInstance::listen() {
    if (server_.listen(socketName())) {
        return true;
    }

    // Remove a stale socket left after a crash and try once more.
    QLocalServer::removeServer(socketName());
    return server_.listen(socketName());
}

void SingleInstance::handleNewConnection() {
    while (QLocalSocket *socket = server_.nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            const QString message = QString::fromUtf8(socket->readAll()).trimmed();
            if (!message.isEmpty()) {
                emit messageReceived(message);
                if (message == QStringLiteral("show")) {
                    emit showRequested();
                } else if (message == QStringLiteral("settings")) {
                    emit settingsRequested();
                } else if (message.startsWith(QStringLiteral("show|"))) {
                    emit showRequestedForWindow(message.mid(5).trimmed());
                }
            }
            socket->disconnectFromServer();
        });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(500, socket, [socket]() {
            if (socket->state() != QLocalSocket::UnconnectedState) {
                socket->disconnectFromServer();
            }
        });
    }
}
