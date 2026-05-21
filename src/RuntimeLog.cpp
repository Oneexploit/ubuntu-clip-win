#include "RuntimeLog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QtGlobal>

namespace {
constexpr qint64 kRuntimeLogRotateBytes = 8 * 1024 * 1024;

struct RuntimeLogState {
    QMutex mutex;
    QString logPath;
    QtMessageHandler previousMessageHandler = nullptr;
    bool initialized = false;
    bool messageHandlerInstalled = false;
    quint64 sequence = 0;
};

RuntimeLogState &state() {
    static RuntimeLogState instance;
    return instance;
}

QString resolveLogPathLocked() {
    const QString overrideDbPath = QString::fromLocal8Bit(qgetenv("UBUNTU_CLIP_WIN_DB_PATH")).trimmed();
    if (!overrideDbPath.isEmpty()) {
        return QDir(QFileInfo(overrideDbPath).absolutePath()).filePath(QStringLiteral("runtime-debug.log"));
    }

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(appData).filePath(QStringLiteral("runtime-debug.log"));
}

void ensureInitializedLocked() {
    RuntimeLogState &logState = state();
    if (logState.initialized && !logState.logPath.trimmed().isEmpty()) {
        return;
    }

    logState.logPath = resolveLogPathLocked();
    const QFileInfo info(logState.logPath);
    QDir().mkpath(info.absolutePath());
    logState.initialized = true;
}

QString messageTypeName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("debug");
    case QtInfoMsg:
        return QStringLiteral("info");
    case QtWarningMsg:
        return QStringLiteral("warning");
    case QtCriticalMsg:
        return QStringLiteral("critical");
    case QtFatalMsg:
        return QStringLiteral("fatal");
    }

    return QStringLiteral("unknown");
}

void rotateIfNeededLocked() {
    RuntimeLogState &logState = state();
    const QFileInfo info(logState.logPath);
    if (!info.exists() || info.size() < kRuntimeLogRotateBytes) {
        return;
    }

    const QString rotatedPath = logState.logPath + QStringLiteral(".1");
    QFile::remove(rotatedPath);
    QFile::rename(logState.logPath, rotatedPath);
}

void appendLocked(const QString &component, const QString &message, const QString &text = QString()) {
    RuntimeLogState &logState = state();
    ensureInitializedLocked();
    rotateIfNeededLocked();

    QFile file(logState.logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    ++logState.sequence;

    QByteArray data;
    data += QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8();
    data += " #";
    data += QByteArray::number(logState.sequence);
    data += " [";
    data += component.toUtf8();
    data += "] ";
    data += message.toUtf8();
    data += "\n";

    if (!text.isNull()) {
        data += "TEXT_BEGIN\n";
        data += text.toUtf8();
        data += "\nTEXT_END\n";
    }

    file.write(data);
}

void runtimeQtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    QtMessageHandler previousHandler = nullptr;
    thread_local bool reentrant = false;
    if (!reentrant) {
        reentrant = true;
        {
            QMutexLocker locker(&state().mutex);
            QString details = QStringLiteral("[%1] %2").arg(messageTypeName(type), message);
            if (context.file && *context.file) {
                details += QStringLiteral(" | file=%1").arg(QString::fromUtf8(context.file));
            }
            if (context.line > 0) {
                details += QStringLiteral(" | line=%1").arg(context.line);
            }
            if (context.function && *context.function) {
                details += QStringLiteral(" | function=%1").arg(QString::fromUtf8(context.function));
            }
            appendLocked(QStringLiteral("Qt"), details);
            previousHandler = state().previousMessageHandler;
        }
        reentrant = false;
    } else {
        QMutexLocker locker(&state().mutex);
        previousHandler = state().previousMessageHandler;
    }

    if (previousHandler) {
        previousHandler(type, context, message);
    }
}
} // namespace

void RuntimeLog::initialize() {
    QMutexLocker locker(&state().mutex);
    ensureInitializedLocked();
}

void RuntimeLog::installQtMessageHandler() {
    QMutexLocker locker(&state().mutex);
    ensureInitializedLocked();
    if (state().messageHandlerInstalled) {
        return;
    }

    state().previousMessageHandler = qInstallMessageHandler(runtimeQtMessageHandler);
    state().messageHandlerInstalled = true;
}

QString RuntimeLog::logFilePath() {
    QMutexLocker locker(&state().mutex);
    ensureInitializedLocked();
    return state().logPath;
}

void RuntimeLog::write(const QString &component, const QString &message, const QString &text) {
    QMutexLocker locker(&state().mutex);
    appendLocked(component.trimmed().isEmpty() ? QStringLiteral("app") : component.trimmed(), message, text);
}
