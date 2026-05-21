#include "PasteController.h"

#include "RuntimeLog.h"

#include <QCursor>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QThread>

namespace {
bool runXdotool(const QStringList &arguments, int timeoutMs = 1000) {
    const QString xdotool = QStandardPaths::findExecutable(QStringLiteral("xdotool"));
    if (xdotool.isEmpty()) {
        RuntimeLog::write(QStringLiteral("PasteController"), QStringLiteral("run-xdotool skipped reason=not-found args=%1").arg(arguments.join(QStringLiteral(" "))));
        return false;
    }

    RuntimeLog::write(QStringLiteral("PasteController"),
                      QStringLiteral("run-xdotool start program=%1 args=%2 timeoutMs=%3")
                          .arg(xdotool)
                          .arg(arguments.join(QStringLiteral(" ")))
                          .arg(timeoutMs));
    QProcess process;
    process.start(xdotool, arguments);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(100);
        RuntimeLog::write(QStringLiteral("PasteController"),
                          QStringLiteral("run-xdotool timeout args=%1").arg(arguments.join(QStringLiteral(" "))));
        return false;
    }

    const bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    RuntimeLog::write(QStringLiteral("PasteController"),
                      QStringLiteral("run-xdotool finished ok=%1 exitCode=%2 stdout=%3 stderr=%4")
                          .arg(ok ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(process.exitCode())
                          .arg(QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed())
                          .arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed()));
    return ok;
}

bool movePointerToPoint(const QPoint &screenPoint) {
    if (screenPoint.x() < 0 || screenPoint.y() < 0) {
        RuntimeLog::write(QStringLiteral("PasteController"),
                          QStringLiteral("move-pointer skipped x=%1 y=%2").arg(screenPoint.x()).arg(screenPoint.y()));
        return false;
    }

    return runXdotool({
        QStringLiteral("mousemove"),
        QStringLiteral("--sync"),
        QString::number(screenPoint.x()),
        QString::number(screenPoint.y())
    }, 1200);
}

bool focusStoredPoint(const QString &targetWindowId,
                      const QPoint &screenPoint,
                      bool requireFocusedPointTarget) {
    if (screenPoint.x() < 0 || screenPoint.y() < 0) {
        RuntimeLog::write(QStringLiteral("PasteController"),
                          QStringLiteral("focus-stored-point skipped invalid-point targetWindowId=%1").arg(targetWindowId));
        return false;
    }

    const bool moved = movePointerToPoint(screenPoint);
    if (!moved) {
        RuntimeLog::write(QStringLiteral("PasteController"), QStringLiteral("focus-stored-point failed reason=move-pointer targetWindowId=%1").arg(targetWindowId));
        return false;
    }

    QThread::msleep(80);
    const bool clicked = runXdotool({QStringLiteral("click"), QStringLiteral("1")}, 1200);
    if (!clicked) {
        RuntimeLog::write(QStringLiteral("PasteController"), QStringLiteral("focus-stored-point failed reason=click targetWindowId=%1").arg(targetWindowId));
        return false;
    }

    if (!requireFocusedPointTarget) {
        return true;
    }

    QThread::msleep(110);
    const QString activeAfterClick = PasteController::activeWindowId();
    RuntimeLog::write(QStringLiteral("PasteController"),
                      QStringLiteral("focus-stored-point post-click targetWindowId=%1 activeAfterClick=%2 requireFocusedPointTarget=%3")
                          .arg(targetWindowId)
                          .arg(activeAfterClick)
                          .arg(requireFocusedPointTarget ? QStringLiteral("true") : QStringLiteral("false")));
    return !targetWindowId.trimmed().isEmpty()
        && !activeAfterClick.isEmpty()
        && activeAfterClick == targetWindowId.trimmed();
}
} // namespace

QString PasteController::xdotoolPath() {
    return QStandardPaths::findExecutable(QStringLiteral("xdotool"));
}

bool PasteController::isX11Session() {
    const QString sessionType = QString::fromLocal8Bit(qgetenv("XDG_SESSION_TYPE")).toLower();
    return sessionType == QStringLiteral("x11");
}

bool PasteController::isWaylandSession() {
    const QString sessionType = QString::fromLocal8Bit(qgetenv("XDG_SESSION_TYPE")).toLower();
    return sessionType == QStringLiteral("wayland");
}

bool PasteController::canAutoPaste() {
    return isX11Session() && !xdotoolPath().isEmpty();
}

QString PasteController::activeWindowId() {
    if (!canAutoPaste()) {
        RuntimeLog::write(QStringLiteral("PasteController"), QStringLiteral("active-window-id skipped reason=auto-paste-unavailable"));
        return {};
    }

    const QString xdotool = xdotoolPath();
    QProcess process;
    process.start(xdotool, {QStringLiteral("getactivewindow")});
    if (!process.waitForFinished(300)) {
        process.kill();
        process.waitForFinished(100);
        RuntimeLog::write(QStringLiteral("PasteController"), QStringLiteral("active-window-id timeout"));
        return {};
    }

    const QString windowId = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    RuntimeLog::write(QStringLiteral("PasteController"), QStringLiteral("active-window-id result=%1").arg(windowId));
    return windowId;
}

bool PasteController::tryPasteToWindow(const QString &windowId,
                                       const QPoint &screenPoint,
                                       bool requireFocusedPointTarget) {
    if (!canAutoPaste()) {
        RuntimeLog::write(QStringLiteral("PasteController"), QStringLiteral("try-paste-to-window skipped reason=auto-paste-unavailable"));
        return false;
    }

    const QString target = windowId.trimmed();
    RuntimeLog::write(QStringLiteral("PasteController"),
                      QStringLiteral("try-paste-to-window begin target=%1 point=%2,%3 requireFocusedPointTarget=%4")
                          .arg(target)
                          .arg(screenPoint.x())
                          .arg(screenPoint.y())
                          .arg(requireFocusedPointTarget ? QStringLiteral("true") : QStringLiteral("false")));
    if (!target.isEmpty()) {
        const QPoint restorePoint = QCursor::pos();
        runXdotool({QStringLiteral("windowactivate"), QStringLiteral("--sync"), target}, 1200);
        runXdotool({QStringLiteral("windowfocus"), QStringLiteral("--sync"), target}, 1200);
        QThread::msleep(140);

        if (focusStoredPoint(target, screenPoint, requireFocusedPointTarget)) {
            QThread::msleep(90);
        } else if (requireFocusedPointTarget) {
            movePointerToPoint(restorePoint);
            RuntimeLog::write(QStringLiteral("PasteController"), QStringLiteral("try-paste-to-window failed reason=focus-stored-point target=%1").arg(target));
            return false;
        }

        const QString active = activeWindowId();
        bool pasted = false;
        if (!active.isEmpty() && active != target) {
            pasted = runXdotool({
                QStringLiteral("key"),
                QStringLiteral("--window"),
                target,
                QStringLiteral("--clearmodifiers"),
                QStringLiteral("ctrl+v")
            }, 1200);
        } else {
            pasted = runXdotool({
                QStringLiteral("key"),
                QStringLiteral("--clearmodifiers"),
                QStringLiteral("ctrl+v")
            }, 1200);
        }

        movePointerToPoint(restorePoint);
        RuntimeLog::write(QStringLiteral("PasteController"),
                          QStringLiteral("try-paste-to-window finished target=%1 pasted=%2 activeWindowAfter=%3")
                              .arg(target)
                              .arg(pasted ? QStringLiteral("true") : QStringLiteral("false"))
                              .arg(active));
        return pasted;
    }

    RuntimeLog::write(QStringLiteral("PasteController"), QStringLiteral("try-paste-to-window no-target falling-back-to-active-app"));
    return tryPasteToActiveApplication();
}

bool PasteController::tryPasteToActiveApplication() {
    if (!canAutoPaste()) {
        RuntimeLog::write(QStringLiteral("PasteController"), QStringLiteral("try-paste-to-active-app skipped reason=auto-paste-unavailable"));
        return false;
    }

    QThread::msleep(120);
    const bool ok = runXdotool({
        QStringLiteral("key"),
        QStringLiteral("--clearmodifiers"),
        QStringLiteral("ctrl+v")
    }, 1200);
    RuntimeLog::write(QStringLiteral("PasteController"), QStringLiteral("try-paste-to-active-app finished pasted=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false")));
    return ok;
}
