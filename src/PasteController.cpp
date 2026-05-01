#include "PasteController.h"

#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QThread>

namespace {
bool runXdotool(const QStringList &arguments, int timeoutMs = 1000) {
    const QString xdotool = QStandardPaths::findExecutable(QStringLiteral("xdotool"));
    if (xdotool.isEmpty()) {
        return false;
    }

    QProcess process;
    process.start(xdotool, arguments);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(100);
        return false;
    }

    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
} // namespace

QString PasteController::xdotoolPath() {
    return QStandardPaths::findExecutable(QStringLiteral("xdotool"));
}

bool PasteController::isX11() {
    const QString sessionType = QString::fromLocal8Bit(qgetenv("XDG_SESSION_TYPE")).toLower();
    return sessionType == QStringLiteral("x11");
}

QString PasteController::activeWindowId() {
    if (!isX11()) {
        return {};
    }
    const QString xdotool = xdotoolPath();
    if (xdotool.isEmpty()) {
        return {};
    }

    QProcess process;
    process.start(xdotool, {QStringLiteral("getactivewindow")});
    if (!process.waitForFinished(300)) {
        process.kill();
        process.waitForFinished(100);
        return {};
    }

    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
}

bool PasteController::tryPasteToWindow(const QString &windowId) {
    if (!isX11()) {
        return false;
    }
    const QString xdotool = xdotoolPath();
    if (xdotool.isEmpty()) {
        return false;
    }

    const QString target = windowId.trimmed();
    if (!target.isEmpty()) {
        runXdotool({QStringLiteral("windowactivate"), QStringLiteral("--sync"), target}, 1200);
        runXdotool({QStringLiteral("windowfocus"), QStringLiteral("--sync"), target}, 1200);
        QThread::msleep(140);

        const QString active = activeWindowId();
        if (!active.isEmpty() && active != target) {
            return runXdotool({
                QStringLiteral("key"),
                QStringLiteral("--window"),
                target,
                QStringLiteral("--clearmodifiers"),
                QStringLiteral("ctrl+v")
            }, 1200);
        }

        return runXdotool({
            QStringLiteral("key"),
            QStringLiteral("--clearmodifiers"),
            QStringLiteral("ctrl+v")
        }, 1200);
    }

    return tryPasteToActiveApplication();
}

bool PasteController::tryPasteToActiveApplication() {
    if (!isX11()) {
        return false;
    }

    const QString xdotool = xdotoolPath();
    if (xdotool.isEmpty()) {
        return false;
    }

    QThread::msleep(120);
    return runXdotool({
        QStringLiteral("key"),
        QStringLiteral("--clearmodifiers"),
        QStringLiteral("ctrl+v")
    }, 1200);
}
