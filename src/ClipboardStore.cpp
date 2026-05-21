#include "ClipboardStore.h"

#include "AppSettings.h"
#include "ClipMime.h"
#include "RuntimeLog.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QMap>
#include <QProcess>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVariant>

#include <utility>

namespace {
constexpr int kClipboardReadRetryCount = 16;
constexpr int kClipboardReadRetryDelayMs = 15;

QString nonNullString(const QString &value) {
    return value.isNull() ? QStringLiteral("") : value;
}

QString normalizedStoredText(QString text) {
    return ClipMime::normalizedText(nonNullString(text));
}

QString clipboardModeName(QClipboard::Mode mode) {
    switch (mode) {
    case QClipboard::Clipboard:
        return QStringLiteral("Clipboard");
    case QClipboard::Selection:
        return QStringLiteral("Selection");
    case QClipboard::FindBuffer:
        return QStringLiteral("FindBuffer");
    }

    return QStringLiteral("Unknown");
}

QString mimeFormatsLabel(const QMimeData *mime) {
    if (!mime) {
        return QStringLiteral("<null>");
    }

    const QStringList formats = mime->formats();
    return formats.isEmpty() ? QStringLiteral("<none>") : formats.join(QStringLiteral(", "));
}

QString previewForLog(QString text, int maxChars = 180) {
    text = ClipMime::elidedPreview(std::move(text), maxChars);
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return text;
}

QString captureSourceLabel(bool usedFallback) {
    return usedFallback ? QStringLiteral("fallback") : QStringLiteral("mime");
}

QString quotedIdentifier(const QString &identifier) {
    QString escaped = identifier;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

QStringList tableColumns(QSqlDatabase &db, const QString &tableName) {
    QStringList columns;
    QSqlQuery tableInfo(db);
    if (!tableInfo.exec(QStringLiteral("PRAGMA table_info(%1)").arg(quotedIdentifier(tableName)))) {
        return columns;
    }

    while (tableInfo.next()) {
        columns << tableInfo.value(1).toString();
    }
    return columns;
}

bool hasColumn(const QStringList &columns, const QString &columnName) {
    return columns.contains(columnName, Qt::CaseInsensitive);
}

bool isTextOnlySchema(const QStringList &columns) {
    static const QStringList kExpectedColumns = {
        QStringLiteral("id"),
        QStringLiteral("text"),
        QStringLiteral("pinned"),
        QStringLiteral("created_at"),
        QStringLiteral("updated_at"),
        QStringLiteral("hash")
    };

    if (columns.size() != kExpectedColumns.size()) {
        return false;
    }

    for (const QString &column : kExpectedColumns) {
        if (!hasColumn(columns, column)) {
            return false;
        }
    }
    return true;
}

QString selectColumnOrNull(const QStringList &columns, const QString &columnName) {
    return hasColumn(columns, columnName) ? quotedIdentifier(columnName) : QStringLiteral("NULL");
}

QString textFromLegacyMimeBundle(const QByteArray &bundle) {
    if (bundle.isEmpty()) {
        return {};
    }

    QMap<QString, QByteArray> formats;
    QBuffer buffer;
    buffer.setData(bundle);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }

    QDataStream stream(&buffer);
    stream.setVersion(QDataStream::Qt_6_2);
    stream >> formats;

    QString text = ClipMime::normalizedText(QString::fromUtf8(formats.value(QStringLiteral("text/plain"))));
    if (!text.trimmed().isEmpty()) {
        return text;
    }

    const QString html = QString::fromUtf8(formats.value(QStringLiteral("text/html")));
    if (!html.trimmed().isEmpty()) {
        return ClipMime::plainTextFromHtml(html);
    }

    return {};
}

QString textOnlyValue(QString text, const QString &html, const QByteArray &mimeBundle) {
    text = normalizedStoredText(std::move(text));
    if (!text.trimmed().isEmpty()) {
        return text;
    }

    if (!html.trimmed().isEmpty()) {
        text = ClipMime::plainTextFromHtml(html);
    }
    if (!text.trimmed().isEmpty()) {
        return normalizedStoredText(std::move(text));
    }

    return normalizedStoredText(textFromLegacyMimeBundle(mimeBundle));
}
} // namespace

ClipboardStore::ClipboardStore(QObject *parent)
    : QObject(parent),
      connectionName_(QStringLiteral("ubuntu-clip-win-store-") + QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

ClipboardStore::~ClipboardStore() {
    if (db_.isValid()) {
        const QString name = db_.connectionName();
        db_.close();
        db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
}

bool ClipboardStore::open() {
    RuntimeLog::initialize();
    const QString path = databasePath();
    const QFileInfo fileInfo(path);
    debugLogPath_ = RuntimeLog::logFilePath();
    if (!QDir(fileInfo.absolutePath()).mkpath(QStringLiteral("."))) {
        emit errorOccurred(QStringLiteral("Cannot create the clipboard data directory."));
        return false;
    }

    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    db_.setDatabaseName(path);
    if (!db_.open()) {
        emit errorOccurred(QStringLiteral("Cannot open clipboard database: %1").arg(db_.lastError().text()));
        return false;
    }

    QSqlQuery pragmas(db_);
    pragmas.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragmas.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragmas.exec(QStringLiteral("PRAGMA temp_store=MEMORY"));

    if (!ensureSchema()) {
        return false;
    }

    applyStartupRetentionPolicy();
    enforceLimit();
    logDebugEvent(QStringLiteral("[open] database=%1 log=%2 persistentHistory=%3 historyLimit=%4")
                      .arg(path,
                           debugLogPath_,
                           AppSettings::persistentHistory() ? QStringLiteral("true") : QStringLiteral("false"))
                      .arg(AppSettings::historyLimit()));
    return true;
}

bool ClipboardStore::isOpen() const {
    return db_.isValid() && db_.isOpen();
}

bool ClipboardStore::ensureSchema() {
    if (!migrateLegacySchemaIfNeeded()) {
        return false;
    }

    QSqlQuery query(db_);
    const bool ok = query.exec(QStringLiteral(R"SQL(
        CREATE TABLE IF NOT EXISTS clips (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            text TEXT NOT NULL DEFAULT '',
            pinned INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            hash TEXT NOT NULL UNIQUE
        )
    )SQL"));

    if (!ok) {
        emit errorOccurred(QStringLiteral("Cannot create clipboard table: %1").arg(query.lastError().text()));
        return false;
    }

    QSqlQuery indexHash(db_);
    indexHash.exec(QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_clips_hash ON clips(hash)"));
    QSqlQuery indexUpdated(db_);
    indexUpdated.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_clips_updated ON clips(pinned DESC, updated_at DESC)"));
    return true;
}

bool ClipboardStore::migrateLegacySchemaIfNeeded() {
    if (!db_.tables().contains(QStringLiteral("clips"))) {
        return true;
    }

    const QStringList columns = tableColumns(db_, QStringLiteral("clips"));
    if (columns.isEmpty()) {
        emit errorOccurred(QStringLiteral("Cannot inspect the clipboard schema."));
        return false;
    }

    if (isTextOnlySchema(columns)) {
        return true;
    }

    const QString legacyTableName = QStringLiteral("clips_legacy_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    QSqlQuery rename(db_);
    if (!rename.exec(QStringLiteral("ALTER TABLE clips RENAME TO %1").arg(quotedIdentifier(legacyTableName)))) {
        emit errorOccurred(QStringLiteral("Cannot migrate the old clipboard table: %1").arg(rename.lastError().text()));
        return false;
    }

    QSqlQuery create(db_);
    if (!create.exec(QStringLiteral(R"SQL(
        CREATE TABLE clips (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            text TEXT NOT NULL DEFAULT '',
            pinned INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            hash TEXT NOT NULL UNIQUE
        )
    )SQL"))) {
        emit errorOccurred(QStringLiteral("Cannot create the upgraded clipboard table: %1").arg(create.lastError().text()));
        return false;
    }

    const QString orderBy = hasColumn(columns, QStringLiteral("id"))
        ? quotedIdentifier(QStringLiteral("id")) + QStringLiteral(" ASC")
        : QStringLiteral("rowid ASC");
    QSqlQuery readLegacy(db_);
    const QString readSql = QStringLiteral(R"SQL(
        SELECT %1 AS text,
               %2 AS pinned,
               %3 AS created_at,
               %4 AS updated_at,
               %5 AS html,
               %6 AS mime_bundle
        FROM %7
        ORDER BY %8
    )SQL")
        .arg(selectColumnOrNull(columns, QStringLiteral("text")),
             selectColumnOrNull(columns, QStringLiteral("pinned")),
             selectColumnOrNull(columns, QStringLiteral("created_at")),
             selectColumnOrNull(columns, QStringLiteral("updated_at")),
             selectColumnOrNull(columns, QStringLiteral("html")),
             selectColumnOrNull(columns, QStringLiteral("mime_bundle")),
             quotedIdentifier(legacyTableName),
             orderBy);

    if (!readLegacy.exec(readSql)) {
        emit errorOccurred(QStringLiteral("Cannot read legacy clipboard items: %1").arg(readLegacy.lastError().text()));
        return false;
    }

    QList<ClipItem> migratedItems;
    QHash<QString, int> indexByHash;
    while (readLegacy.next()) {
        ClipItem item;
        item.text = textOnlyValue(readLegacy.value(0).toString(),
                                  nonNullString(readLegacy.value(4).toString()),
                                  readLegacy.value(5).toByteArray());
        if (item.text.trimmed().isEmpty()) {
            continue;
        }

        item.pinned = readLegacy.value(1).toInt() != 0;
        item.createdAt = QDateTime::fromString(readLegacy.value(2).toString(), Qt::ISODateWithMs);
        item.updatedAt = QDateTime::fromString(readLegacy.value(3).toString(), Qt::ISODateWithMs);
        item.hash = hashFor(item);

        const auto existingIt = indexByHash.constFind(item.hash);
        if (existingIt != indexByHash.cend()) {
            ClipItem &existing = migratedItems[existingIt.value()];
            existing.pinned = existing.pinned || item.pinned;
            if (item.createdAt.isValid() && (!existing.createdAt.isValid() || item.createdAt < existing.createdAt)) {
                existing.createdAt = item.createdAt;
            }
            if (item.updatedAt.isValid() && (!existing.updatedAt.isValid() || item.updatedAt > existing.updatedAt)) {
                existing.updatedAt = item.updatedAt;
            }
            continue;
        }

        indexByHash.insert(item.hash, migratedItems.size());
        migratedItems << item;
    }

    if (!db_.transaction()) {
        emit errorOccurred(QStringLiteral("Cannot start clipboard cleanup transaction: %1").arg(db_.lastError().text()));
        return false;
    }

    QSqlQuery insert(db_);
    insert.prepare(QStringLiteral(R"SQL(
        INSERT INTO clips(text, pinned, created_at, updated_at, hash)
        VALUES(:text, :pinned, :created_at, :updated_at, :hash)
    )SQL"));

    for (const ClipItem &item : migratedItems) {
        insert.bindValue(QStringLiteral(":text"), item.text);
        insert.bindValue(QStringLiteral(":pinned"), item.pinned ? 1 : 0);
        insert.bindValue(QStringLiteral(":created_at"), item.createdAt.isValid() ? item.createdAt.toString(Qt::ISODateWithMs) : nowIso());
        insert.bindValue(QStringLiteral(":updated_at"), item.updatedAt.isValid() ? item.updatedAt.toString(Qt::ISODateWithMs) : nowIso());
        insert.bindValue(QStringLiteral(":hash"), item.hash);
        if (!insert.exec()) {
            db_.rollback();
            emit errorOccurred(QStringLiteral("Cannot migrate a clipboard item: %1").arg(insert.lastError().text()));
            return false;
        }
    }

    if (!db_.commit()) {
        db_.rollback();
        emit errorOccurred(QStringLiteral("Cannot finish clipboard cleanup: %1").arg(db_.lastError().text()));
        return false;
    }

    QSqlQuery drop(db_);
    if (!drop.exec(QStringLiteral("DROP TABLE IF EXISTS %1").arg(quotedIdentifier(legacyTableName)))) {
        emit errorOccurred(QStringLiteral("Cannot remove the legacy clipboard table: %1").arg(drop.lastError().text()));
    }

    lastCapturedHash_.clear();
    return true;
}

QString ClipboardStore::databasePath() const {
    const QString overridePath = QString::fromLocal8Bit(qgetenv("UBUNTU_CLIP_WIN_DB_PATH")).trimmed();
    if (!overridePath.isEmpty()) {
        return overridePath;
    }

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(appData).filePath(QStringLiteral("clips.sqlite"));
}

QString ClipboardStore::debugLogPath() const {
    if (!debugLogPath_.trimmed().isEmpty()) {
        return debugLogPath_;
    }

    return RuntimeLog::logFilePath();
}

QString ClipboardStore::nowIso() const {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString ClipboardStore::hashFor(const ClipItem &item) const {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(nonNullString(item.text).toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

void ClipboardStore::logDebugEvent(const QString &message, const QString &text) const {
    RuntimeLog::write(QStringLiteral("ClipboardStore"), message, text);
}

void ClipboardStore::scheduleCaptureFromClipboard() {
    if (!isOpen()) {
        logDebugEvent(QStringLiteral("[schedule] ignored reason=store-not-open"));
        return;
    }

    if (suppressCapture_) {
        logDebugEvent(QStringLiteral("[schedule] ignored reason=suppress-capture pendingSerial=%1 lastHash=%2")
                          .arg(pendingCaptureSerial_)
                          .arg(lastCapturedHash_));
        return;
    }

    const quint64 serial = ++pendingCaptureSerial_;
    logDebugEvent(QStringLiteral("[schedule] begin serial=%1 mode=%2")
                      .arg(serial)
                      .arg(clipboardModeName(QClipboard::Clipboard)));
    if (tryCaptureCurrentClipboard(QClipboard::Clipboard)) {
        logDebugEvent(QStringLiteral("[schedule] immediate-success serial=%1").arg(serial));
        return;
    }

    logDebugEvent(QStringLiteral("[schedule] retry-needed serial=%1 attempts=%2 delayMs=%3")
                      .arg(serial)
                      .arg(kClipboardReadRetryCount)
                      .arg(kClipboardReadRetryDelayMs));
    scheduleClipboardRetry(QClipboard::Clipboard, serial, 1);
}

void ClipboardStore::scheduleCaptureFromSelection() {
    // PRIMARY/Selection clipboard is intentionally disabled in this build.
}

void ClipboardStore::captureFromClipboard() {
    logDebugEvent(QStringLiteral("[manual-capture] begin mode=%1").arg(clipboardModeName(QClipboard::Clipboard)));
    captureCurrentClipboardWithRetry(QClipboard::Clipboard);
}

void ClipboardStore::captureFromSelection() {
    // Disabled by default. Only the normal Clipboard mode is tracked.
}

bool ClipboardStore::captureMimeData(const QMimeData *mime, QClipboard::Mode mode) {
    bool usedFallback = false;
    auto payload = ClipMime::payloadFromMimeData(mime);
    if (!payload.has_value()) {
        payload = payloadFromClipboardFallback(mode);
        usedFallback = payload.has_value();
    }

    if (!payload.has_value()) {
        logDebugEvent(QStringLiteral("[capture] skipped reason=no-payload mode=%1 hasText=%2 hasHtml=%3 formats=%4")
                          .arg(clipboardModeName(mode))
                          .arg(mime && mime->hasText() ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(mime && mime->hasHtml() ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(mimeFormatsLabel(mime)));
        return false;
    }

    ClipItem item;
    item.text = normalizedStoredText(payload->text);
    if (item.text.trimmed().isEmpty()) {
        logDebugEvent(QStringLiteral("[capture] skipped reason=normalized-empty mode=%1 formats=%2")
                          .arg(clipboardModeName(mode))
                          .arg(mimeFormatsLabel(mime)),
                      payload->text);
        return false;
    }
    item.hash = hashFor(item);

    const bool duplicateHash = item.hash == lastCapturedHash_;
    if (duplicateHash && usedFallback) {
        logDebugEvent(QStringLiteral("[capture] skipped reason=fallback-duplicate-hash mode=%1 source=%2 hash=%3 chars=%4 preview=\"%5\"")
                          .arg(clipboardModeName(mode))
                          .arg(captureSourceLabel(usedFallback))
                          .arg(item.hash)
                          .arg(item.text.size())
                          .arg(previewForLog(item.text)),
                      item.text);
        return false;
    }

    lastCapturedHash_ = item.hash;
    touchExistingOrInsert(item);
    enforceLimit();
    const QString captureMessage = duplicateHash
        ? QStringLiteral("[capture] promoted-existing mode=%1 source=%2 hash=%3 chars=%4 preview=\"%5\"")
        : QStringLiteral("[capture] stored mode=%1 source=%2 hash=%3 chars=%4 preview=\"%5\"");
    logDebugEvent(captureMessage.arg(clipboardModeName(mode))
                      .arg(captureSourceLabel(usedFallback))
                      .arg(item.hash)
                      .arg(item.text.size())
                      .arg(previewForLog(item.text)),
                  item.text);
    emit changed();
    return true;
}

std::optional<ClipPayload> ClipboardStore::payloadFromClipboardFallback(QClipboard::Mode mode) const {
    if (mode != QClipboard::Clipboard) {
        return std::nullopt;
    }

    const QByteArray forcedText = qgetenv("UBUNTU_CLIP_WIN_CLIPBOARD_FALLBACK_TEXT");
    if (!forcedText.isEmpty()) {
        ClipPayload payload;
        payload.text = ClipMime::normalizedText(QString::fromUtf8(forcedText));
        if (!payload.text.trimmed().isEmpty()) {
            logDebugEvent(QStringLiteral("[fallback] success mode=%1 source=env chars=%2 preview=\"%3\"")
                              .arg(clipboardModeName(mode))
                              .arg(payload.text.size())
                              .arg(previewForLog(payload.text)),
                          payload.text);
            return payload;
        }
    }

    const QString sessionType = QString::fromUtf8(qgetenv("XDG_SESSION_TYPE"));
    const bool looksLikeWayland = sessionType.compare(QStringLiteral("wayland"), Qt::CaseInsensitive) == 0
        || !qgetenv("WAYLAND_DISPLAY").isEmpty();
    if (!looksLikeWayland) {
        logDebugEvent(QStringLiteral("[fallback] skipped mode=%1 reason=not-wayland").arg(clipboardModeName(mode)));
        return std::nullopt;
    }

    const QString wlPastePath = QStandardPaths::findExecutable(QStringLiteral("wl-paste"));
    if (wlPastePath.isEmpty()) {
        logDebugEvent(QStringLiteral("[fallback] unavailable mode=%1 reason=wl-paste-not-found").arg(clipboardModeName(mode)));
        return std::nullopt;
    }

    QProcess process;
    process.start(wlPastePath, {QStringLiteral("--no-newline"), QStringLiteral("--type"), QStringLiteral("text")}, QIODevice::ReadOnly);
    if (!process.waitForStarted(100)) {
        logDebugEvent(QStringLiteral("[fallback] failed mode=%1 reason=start-timeout program=%2")
                          .arg(clipboardModeName(mode))
                          .arg(wlPastePath));
        return std::nullopt;
    }

    if (!process.waitForFinished(250)) {
        process.kill();
        process.waitForFinished(100);
        logDebugEvent(QStringLiteral("[fallback] failed mode=%1 reason=read-timeout program=%2")
                          .arg(clipboardModeName(mode))
                          .arg(wlPastePath));
        return std::nullopt;
    }

    const QByteArray stdoutData = process.readAllStandardOutput();
    const QByteArray stderrData = process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        logDebugEvent(QStringLiteral("[fallback] failed mode=%1 reason=nonzero-exit exitCode=%2 stderr=\"%3\"")
                          .arg(clipboardModeName(mode))
                          .arg(process.exitCode())
                          .arg(QString::fromUtf8(stderrData).trimmed()));
        return std::nullopt;
    }

    ClipPayload payload;
    payload.text = ClipMime::normalizedText(QString::fromUtf8(stdoutData));
    if (payload.text.trimmed().isEmpty()) {
        logDebugEvent(QStringLiteral("[fallback] failed mode=%1 reason=empty-output stderr=\"%2\"")
                          .arg(clipboardModeName(mode))
                          .arg(QString::fromUtf8(stderrData).trimmed()));
        return std::nullopt;
    }

    logDebugEvent(QStringLiteral("[fallback] success mode=%1 source=wl-paste chars=%2 preview=\"%3\"")
                      .arg(clipboardModeName(mode))
                      .arg(payload.text.size())
                      .arg(previewForLog(payload.text)),
                  payload.text);
    return payload;
}

void ClipboardStore::seedLastCapturedHashFromCurrentClipboard() {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        lastCapturedHash_.clear();
        logDebugEvent(QStringLiteral("[state] seed-last-hash skipped reason=no-clipboard"));
        return;
    }

    auto payload = ClipMime::payloadFromMimeData(clipboard->mimeData(QClipboard::Clipboard));
    bool usedFallback = false;
    if (!payload.has_value()) {
        payload = payloadFromClipboardFallback(QClipboard::Clipboard);
        usedFallback = payload.has_value();
    }

    if (!payload.has_value()) {
        lastCapturedHash_.clear();
        logDebugEvent(QStringLiteral("[state] seed-last-hash cleared reason=no-payload"));
        return;
    }

    ClipItem item;
    item.text = normalizedStoredText(payload->text);
    if (item.text.trimmed().isEmpty()) {
        lastCapturedHash_.clear();
        logDebugEvent(QStringLiteral("[state] seed-last-hash cleared reason=normalized-empty source=%1")
                          .arg(captureSourceLabel(usedFallback)),
                      payload->text);
        return;
    }

    item.hash = hashFor(item);
    lastCapturedHash_ = item.hash;
    logDebugEvent(QStringLiteral("[state] seed-last-hash source=%1 hash=%2 chars=%3 preview=\"%4\"")
                      .arg(captureSourceLabel(usedFallback))
                      .arg(item.hash)
                      .arg(item.text.size())
                      .arg(previewForLog(item.text)),
                  item.text);
}

bool ClipboardStore::tryCaptureCurrentClipboard(QClipboard::Mode mode) {
    if (!isOpen() || suppressCapture_) {
        logDebugEvent(QStringLiteral("[try-capture] blocked mode=%1 isOpen=%2 suppressCapture=%3")
                          .arg(clipboardModeName(mode))
                          .arg(isOpen() ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(suppressCapture_ ? QStringLiteral("true") : QStringLiteral("false")));
        return false;
    }

    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        logDebugEvent(QStringLiteral("[try-capture] blocked mode=%1 reason=no-clipboard").arg(clipboardModeName(mode)));
        return false;
    }

    logDebugEvent(QStringLiteral("[try-capture] read mode=%1 formats=%2")
                      .arg(clipboardModeName(mode))
                      .arg(mimeFormatsLabel(clipboard->mimeData(mode))));
    return captureMimeData(clipboard->mimeData(mode), mode);
}

bool ClipboardStore::captureCurrentClipboardWithRetry(QClipboard::Mode mode) {
    if (tryCaptureCurrentClipboard(mode)) {
        logDebugEvent(QStringLiteral("[retry-capture] success mode=%1 attempt=0").arg(clipboardModeName(mode)));
        return true;
    }

    for (int attempt = 1; attempt < kClipboardReadRetryCount; ++attempt) {
        QThread::msleep(kClipboardReadRetryDelayMs);
        if (tryCaptureCurrentClipboard(mode)) {
            logDebugEvent(QStringLiteral("[retry-capture] success mode=%1 attempt=%2").arg(clipboardModeName(mode)).arg(attempt));
            return true;
        }
    }

    logDebugEvent(QStringLiteral("[retry-capture] failed mode=%1 attempts=%2").arg(clipboardModeName(mode)).arg(kClipboardReadRetryCount));
    return false;
}

void ClipboardStore::scheduleClipboardRetry(QClipboard::Mode mode, quint64 serial, int attempt) {
    if (attempt >= kClipboardReadRetryCount) {
        logDebugEvent(QStringLiteral("[schedule-retry] gave-up serial=%1 mode=%2 attempts=%3")
                          .arg(serial)
                          .arg(clipboardModeName(mode))
                          .arg(kClipboardReadRetryCount));
        return;
    }

    // Keep retries off the GUI thread so rapid clipboard updates do not stall the app.
    logDebugEvent(QStringLiteral("[schedule-retry] queued serial=%1 mode=%2 attempt=%3 delayMs=%4")
                      .arg(serial)
                      .arg(clipboardModeName(mode))
                      .arg(attempt)
                      .arg(kClipboardReadRetryDelayMs));
    QTimer::singleShot(kClipboardReadRetryDelayMs, this, [this, mode, serial, attempt]() {
        if (serial != pendingCaptureSerial_) {
            logDebugEvent(QStringLiteral("[schedule-retry] cancelled serial=%1 mode=%2 attempt=%3 reason=newer-serial pendingSerial=%4")
                              .arg(serial)
                              .arg(clipboardModeName(mode))
                              .arg(attempt)
                              .arg(pendingCaptureSerial_));
            return;
        }

        if (tryCaptureCurrentClipboard(mode)) {
            logDebugEvent(QStringLiteral("[schedule-retry] success serial=%1 mode=%2 attempt=%3")
                              .arg(serial)
                              .arg(clipboardModeName(mode))
                              .arg(attempt));
            return;
        }

        logDebugEvent(QStringLiteral("[schedule-retry] retry-again serial=%1 mode=%2 nextAttempt=%3")
                          .arg(serial)
                          .arg(clipboardModeName(mode))
                          .arg(attempt + 1));
        scheduleClipboardRetry(mode, serial, attempt + 1);
    });
}

void ClipboardStore::touchExistingOrInsert(const ClipItem &item) {
    const QString now = nowIso();

    QSqlQuery exists(db_);
    exists.prepare(QStringLiteral("SELECT id FROM clips WHERE hash = :hash"));
    exists.bindValue(QStringLiteral(":hash"), item.hash);
    if (exists.exec() && exists.next()) {
        const int existingId = exists.value(0).toInt();
        QSqlQuery update(db_);
        update.prepare(QStringLiteral(R"SQL(
            UPDATE clips
            SET text = :text,
                updated_at = :updated_at
            WHERE hash = :hash
        )SQL"));
        update.bindValue(QStringLiteral(":text"), item.text);
        update.bindValue(QStringLiteral(":updated_at"), now);
        update.bindValue(QStringLiteral(":hash"), item.hash);
        if (!update.exec()) {
            emit errorOccurred(QStringLiteral("Cannot update clipboard item: %1").arg(update.lastError().text()));
            logDebugEvent(QStringLiteral("[db] update-failed id=%1 hash=%2 error=%3")
                              .arg(existingId)
                              .arg(item.hash)
                              .arg(update.lastError().text()));
        } else {
            logDebugEvent(QStringLiteral("[db] update-existing id=%1 hash=%2 chars=%3 preview=\"%4\"")
                              .arg(existingId)
                              .arg(item.hash)
                              .arg(item.text.size())
                              .arg(previewForLog(item.text)));
        }
        return;
    }

    QSqlQuery insert(db_);
    insert.prepare(QStringLiteral(R"SQL(
        INSERT INTO clips(text, pinned, created_at, updated_at, hash)
        VALUES(:text, 0, :created_at, :updated_at, :hash)
    )SQL"));
    insert.bindValue(QStringLiteral(":text"), item.text);
    insert.bindValue(QStringLiteral(":created_at"), now);
    insert.bindValue(QStringLiteral(":updated_at"), now);
    insert.bindValue(QStringLiteral(":hash"), item.hash);
    if (!insert.exec()) {
        emit errorOccurred(QStringLiteral("Cannot insert clipboard item: %1").arg(insert.lastError().text()));
        logDebugEvent(QStringLiteral("[db] insert-failed hash=%1 error=%2")
                          .arg(item.hash)
                          .arg(insert.lastError().text()));
        return;
    }

    logDebugEvent(QStringLiteral("[db] insert-new id=%1 hash=%2 chars=%3 preview=\"%4\"")
                      .arg(insert.lastInsertId().toInt())
                      .arg(item.hash)
                      .arg(item.text.size())
                      .arg(previewForLog(item.text)));
}

void ClipboardStore::applyStartupRetentionPolicy() {
    if (AppSettings::persistentHistory()) {
        return;
    }

    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("DELETE FROM clips WHERE pinned = 0"))) {
        emit errorOccurred(QStringLiteral("Cannot reset session clipboard history: %1").arg(query.lastError().text()));
    }
}

void ClipboardStore::enforceLimit() {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(R"SQL(
        DELETE FROM clips
        WHERE pinned = 0
          AND id NOT IN (
            SELECT id
            FROM clips
            WHERE pinned = 0
            ORDER BY updated_at DESC, id DESC
            LIMIT :limit
        )
    )SQL"));
    query.bindValue(QStringLiteral(":limit"), AppSettings::historyLimit());
    if (!query.exec()) {
        emit errorOccurred(QStringLiteral("Cannot trim clipboard history: %1").arg(query.lastError().text()));
        logDebugEvent(QStringLiteral("[limit] trim-failed limit=%1 error=%2")
                          .arg(AppSettings::historyLimit())
                          .arg(query.lastError().text()));
        return;
    }

    const int removedRows = query.numRowsAffected();
    if (removedRows > 0) {
        logDebugEvent(QStringLiteral("[limit] trimmed removedRows=%1 limit=%2")
                          .arg(removedRows)
                          .arg(AppSettings::historyLimit()));
    }
}

QList<ClipItem> ClipboardStore::recentItems(const QString &search, int limit) const {
    QList<ClipItem> items;
    if (!isOpen()) {
        return items;
    }

    QSqlQuery query(db_);
    const bool hasSearch = !search.trimmed().isEmpty();
    if (hasSearch) {
        query.prepare(QStringLiteral(R"SQL(
            SELECT id, text, pinned, created_at, updated_at, hash
            FROM clips
            WHERE text LIKE :search
            ORDER BY pinned DESC, updated_at DESC, id DESC
            LIMIT :limit
        )SQL"));
        query.bindValue(QStringLiteral(":search"), QStringLiteral("%") + search.trimmed() + QStringLiteral("%"));
    } else {
        query.prepare(QStringLiteral(R"SQL(
            SELECT id, text, pinned, created_at, updated_at, hash
            FROM clips
            ORDER BY pinned DESC, updated_at DESC, id DESC
            LIMIT :limit
        )SQL"));
    }
    query.bindValue(QStringLiteral(":limit"), limit);

    if (!query.exec()) {
        return items;
    }

    while (query.next()) {
        items << readItemFromQuery(query);
    }
    return items;
}

std::optional<ClipItem> ClipboardStore::itemById(int id) const {
    if (!isOpen()) {
        return std::nullopt;
    }

    QSqlQuery query(db_);
    query.prepare(QStringLiteral(R"SQL(
        SELECT id, text, pinned, created_at, updated_at, hash
        FROM clips
        WHERE id = :id
    )SQL"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return readItemFromQuery(query);
}

ClipItem ClipboardStore::readItemFromQuery(const QSqlQuery &query) const {
    ClipItem item;
    item.id = query.value(0).toInt();
    item.text = normalizedStoredText(query.value(1).toString());
    item.pinned = query.value(2).toInt() != 0;
    item.createdAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs);
    item.updatedAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs);
    item.hash = query.value(5).toString();
    return item;
}

bool ClipboardStore::copyToClipboard(const ClipItem &item) {
    if (!item.isValid() || item.text.trimmed().isEmpty()) {
        logDebugEvent(QStringLiteral("[restore] skipped reason=invalid-item id=%1 chars=%2")
                          .arg(item.id)
                          .arg(item.text.size()),
                      item.text);
        return false;
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        logDebugEvent(QStringLiteral("[restore] skipped reason=no-clipboard id=%1 hash=%2").arg(item.id).arg(item.hash));
        return false;
    }

    ++pendingCaptureSerial_;
    suppressCapture_ = true;
    lastCapturedHash_ = item.hash;
    logDebugEvent(QStringLiteral("[restore] begin id=%1 serial=%2 hash=%3 chars=%4 preview=\"%5\"")
                      .arg(item.id)
                      .arg(pendingCaptureSerial_)
                      .arg(item.hash)
                      .arg(item.text.size())
                      .arg(previewForLog(item.text)),
                  item.text);

    clipboard->setText(item.text, QClipboard::Clipboard);

    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QSqlQuery update(db_);
    update.prepare(QStringLiteral("UPDATE clips SET updated_at = :updated_at WHERE id = :id"));
    update.bindValue(QStringLiteral(":updated_at"), nowIso());
    update.bindValue(QStringLiteral(":id"), item.id);
    update.exec();

    QTimer::singleShot(0, this, [this]() {
        suppressCapture_ = false;
        logDebugEvent(QStringLiteral("[restore] end suppressCapture=false pendingSerial=%1").arg(pendingCaptureSerial_));
        emit changed();
    });

    return true;
}

void ClipboardStore::setToClipboard(const ClipItem &item) {
    copyToClipboard(item);
}

void ClipboardStore::deleteItem(int id) {
    if (!isOpen()) {
        return;
    }

    QSqlQuery query(db_);
    query.prepare(QStringLiteral("DELETE FROM clips WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (query.exec()) {
        lastCapturedHash_.clear();
        emit changed();
    } else {
        emit errorOccurred(QStringLiteral("Cannot delete clipboard item: %1").arg(query.lastError().text()));
    }
}

void ClipboardStore::togglePinned(int id) {
    if (!isOpen()) {
        return;
    }

    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE clips SET pinned = CASE pinned WHEN 0 THEN 1 ELSE 0 END WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (query.exec()) {
        enforceLimit();
        emit changed();
    } else {
        emit errorOccurred(QStringLiteral("Cannot pin clipboard item: %1").arg(query.lastError().text()));
    }
}

void ClipboardStore::clearUnpinned() {
    if (!isOpen()) {
        return;
    }

    QSqlQuery query(db_);
    if (query.exec(QStringLiteral("DELETE FROM clips WHERE pinned = 0"))) {
        seedLastCapturedHashFromCurrentClipboard();
        emit changed();
    } else {
        emit errorOccurred(QStringLiteral("Cannot clear clipboard history: %1").arg(query.lastError().text()));
    }
}

void ClipboardStore::reloadSettings() {
    if (!isOpen()) {
        return;
    }

    enforceLimit();
    emit changed();
}
