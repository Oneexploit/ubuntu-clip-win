#include "SingleInstance.h"

#include "RuntimeLog.h"

#include <QLocalSocket>
#include <QTimer>

SingleInstance::SingleInstance(QObject *parent) : QObject(parent) {
    RuntimeLog::write(QStringLiteral("SingleInstance"), QStringLiteral("constructed socket=%1").arg(socketName()));
    connect(&server_, &QLocalServer::newConnection, this, &SingleInstance::handleNewConnection);
}

SingleInstance::~SingleInstance() {
    RuntimeLog::write(QStringLiteral("SingleInstance"), QStringLiteral("destructing"));
    server_.close();
}

QString SingleInstance::socketName() {
    const QString user = QString::fromLocal8Bit(qgetenv("USER"));
    return QStringLiteral("ubuntu-clip-win-") + (user.isEmpty() ? QStringLiteral("user") : user);
}

bool SingleInstance::sendMessage(const QString &message) {
    RuntimeLog::write(QStringLiteral("SingleInstance"), QStringLiteral("send-message begin socket=%1 message=%2").arg(socketName()).arg(message));
    QLocalSocket socket;
    socket.connectToServer(socketName());
    if (!socket.waitForConnected(150)) {
        RuntimeLog::write(QStringLiteral("SingleInstance"), QStringLiteral("send-message failed reason=connect-timeout socket=%1 message=%2").arg(socketName()).arg(message));
        return false;
    }
    socket.write(message.toUtf8());
    socket.write("\n");
    socket.flush();
    socket.waitForBytesWritten(150);
    socket.disconnectFromServer();
    RuntimeLog::write(QStringLiteral("SingleInstance"), QStringLiteral("send-message success socket=%1 message=%2").arg(socketName()).arg(message));
    return true;
}

bool SingleInstance::listen() {
    if (server_.listen(socketName())) {
        RuntimeLog::write(QStringLiteral("SingleInstance"), QStringLiteral("listen success socket=%1").arg(socketName()));
        return true;
    }

    // Remove a stale socket left after a crash and try once more.
    RuntimeLog::write(QStringLiteral("SingleInstance"), QStringLiteral("listen retry removing-stale-socket socket=%1 serverError=%2").arg(socketName()).arg(server_.errorString()));
    QLocalServer::removeServer(socketName());
    const bool ok = server_.listen(socketName());
    RuntimeLog::write(QStringLiteral("SingleInstance"), QStringLiteral("listen second-attempt success=%1 socket=%2").arg(ok ? QStringLiteral("true") : QStringLiteral("false")).arg(socketName()));
    return ok;
}

void SingleInstance::handleNewConnection() {
    RuntimeLog::write(QStringLiteral("SingleInstance"), QStringLiteral("handle-new-connection pending=%1").arg(server_.hasPendingConnections() ? QStringLiteral("true") : QStringLiteral("false")));
    while (QLocalSocket *socket = server_.nextPendingConnection()) {
        RuntimeLog::write(QStringLiteral("SingleInstance"), QStringLiteral("accepted-connection"));
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            const QString message = QString::fromUtf8(socket->readAll()).trimmed();
            if (!message.isEmpty()) {
                RuntimeLog::write(QStringLiteral("SingleInstance"), QStringLiteral("message-received message=%1").arg(message));
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
                RuntimeLog::write(QStringLiteral("SingleInstance"), QStringLiteral("connection-timeout-disconnect"));
                socket->disconnectFromServer();
            }
        });
    }
}
