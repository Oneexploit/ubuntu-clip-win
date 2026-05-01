#include "ClipboardStore.h"

#include <QApplication>
#include <QByteArrayView>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariant>
#include <utility>

namespace {
constexpr int kMaxStoredTextChars = 256 * 1024;
constexpr int kMaxHistoryItems = 120;

QString normalizedText(QString value) {
    value.replace(QChar::Null, QLatin1Char(' '));
    value.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    value.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (value.size() > kMaxStoredTextChars) {
        value = value.left(kMaxStoredTextChars);
    }
    return value;
}

bool looksLikeHumanText(const QString &value) {
    if (value.trimmed().isEmpty()) {
        return false;
    }

    const int sampleSize = qMin(value.size(), 4096);
    int suspicious = 0;
    for (int i = 0; i < sampleSize; ++i) {
        const ushort ch = value.at(i).unicode();
        const bool allowedControl = ch == '\n' || ch == '\r' || ch == '\t';
        if ((ch < 32 && !allowedControl) || ch == 0xfffd) {
            ++suspicious;
        }
    }

    return suspicious <= qMax(3, sampleSize / 25);
}

bool textLooksLikeOnlyLocalFiles(const QString &value) {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    const QStringList lines = trimmed.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        return false;
    }

    int fileLines = 0;
    for (QString line : lines) {
        line = line.trimmed();
        if (line == QStringLiteral("copy") || line == QStringLiteral("cut")) {
            continue;
        }

        const QUrl url(line);
        if (line.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)
            || url.isLocalFile()
            || url.scheme().compare(QStringLiteral("file"), Qt::CaseInsensitive) == 0) {
            ++fileLines;
            continue;
        }

        // Some file managers expose copied files as plain absolute paths.
        // Treat them as files only when the path really exists on disk, so code
        // snippets like "/api/v1/users" are not accidentally filtered out.
        const QString expanded = line.startsWith(QStringLiteral("~/"))
            ? QDir::homePath() + line.mid(1)
            : line;
        if ((expanded.startsWith(QLatin1Char('/')) || expanded.startsWith(QStringLiteral("./")) || expanded.startsWith(QStringLiteral("../")))
            && QFileInfo::exists(expanded)) {
            ++fileLines;
            continue;
        }

        return false;
    }

    return fileLines > 0;
}

bool formatListContains(const QMimeData *mime, const QString &needle) {
    if (!mime) {
        return false;
    }
    const QString lowerNeedle = needle.toLower();
    for (const QString &format : mime->formats()) {
        if (format.toLower().contains(lowerNeedle)) {
            return true;
        }
    }
    return false;
}

bool hasRealPlainTextFormat(const QMimeData *mime) {
    if (!mime) {
        return false;
    }
    for (const QString &format : mime->formats()) {
        const QString lower = format.toLower();
        if (lower == QStringLiteral("text/plain")
            || lower == QStringLiteral("text/plain;charset=utf-8")
            || lower == QStringLiteral("utf8_string")
            || lower == QStringLiteral("string")
            || lower.contains(QStringLiteral("text/plain"))) {
            return true;
        }
    }
    return false;
}

bool mimeIsOnlyFilesOrImages(const QMimeData *mime) {
    if (!mime) {
        return true;
    }

    if (mime->hasFormat(QStringLiteral("x-special/gnome-copied-files"))) {
        return true;
    }

    if (mime->hasUrls()) {
        const auto urls = mime->urls();
        if (!urls.isEmpty()) {
            bool allLocalFiles = true;
            for (const QUrl &url : urls) {
                if (!url.isLocalFile() && url.scheme().compare(QStringLiteral("file"), Qt::CaseInsensitive) != 0) {
                    allLocalFiles = false;
                    break;
                }
            }
            if (allLocalFiles) {
                return true;
            }
        }
    }

    const bool hasImagePayload = mime->hasImage()
        || formatListContains(mime, QStringLiteral("image/"))
        || mime->hasFormat(QStringLiteral("application/x-qt-image"));
    if (hasImagePayload && !hasRealPlainTextFormat(mime)) {
        return true;
    }

    if (mime->hasFormat(QStringLiteral("text/uri-list")) && !hasRealPlainTextFormat(mime)) {
        const QString uriText = normalizedText(QString::fromUtf8(mime->data(QStringLiteral("text/uri-list"))));
        if (textLooksLikeOnlyLocalFiles(uriText)) {
            return true;
        }
    }

    return false;
}

void removeSqliteFileSet(const QString &path) {
    if (path.isEmpty()) {
        return;
    }
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));
}

void removeLegacyPersistentDatabases() {
    QStringList bases;
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) {
        bases << appData;
    }
    bases << (QDir::homePath() + QStringLiteral("/.local/share/ubuntu-clip-win"));

    for (const QString &base : std::as_const(bases)) {
        removeSqliteFileSet(QDir(base).filePath(QStringLiteral("clips.sqlite")));
        removeSqliteFileSet(QDir(base).filePath(QStringLiteral("clips-text-session.sqlite")));
    }
}

QString textOnlyFromMimeData(const QMimeData *mime) {
    if (!mime || !mime->hasText()) {
        return {};
    }

    // This clipboard manager is intentionally text-only. File managers and
    // image tools can expose copied files/images through text/uri-list or path
    // text; those must not appear in the history.
    if (mimeIsOnlyFilesOrImages(mime)) {
        return {};
    }

    const QString text = normalizedText(mime->text());
    if (!looksLikeHumanText(text)) {
        return {};
    }

    if (textLooksLikeOnlyLocalFiles(text) && !hasRealPlainTextFormat(mime)) {
        return {};
    }

    return text;
}

QMimeData *mimeDataFromItem(const ClipItem &item) {
    auto *mime = new QMimeData();
    mime->setText(item.text);
    mime->setData(QStringLiteral("text/plain;charset=utf-8"), item.text.toUtf8());
    return mime;
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
    removeLegacyPersistentDatabases();

    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    db_.setDatabaseName(QStringLiteral(":memory:"));
    if (!db_.open()) {
        emit errorOccurred(QStringLiteral("Cannot open clipboard database: %1").arg(db_.lastError().text()));
        return false;
    }

    QSqlQuery pragmas(db_);
    pragmas.exec(QStringLiteral("PRAGMA journal_mode=MEMORY"));
    pragmas.exec(QStringLiteral("PRAGMA synchronous=OFF"));
    pragmas.exec(QStringLiteral("PRAGMA temp_store=MEMORY"));

    return ensureSchema();
}

bool ClipboardStore::isOpen() const {
    return db_.isValid() && db_.isOpen();
}

bool ClipboardStore::ensureSchema() {
    QSqlQuery query(db_);
    const bool ok = query.exec(QStringLiteral(R"SQL(
        CREATE TABLE IF NOT EXISTS clips (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            type TEXT NOT NULL DEFAULT 'text',
            text TEXT NOT NULL,
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
    indexUpdated.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_clips_updated ON clips(updated_at DESC)"));
    return true;
}

QString ClipboardStore::nowIso() const {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString ClipboardStore::hashFor(const QString &text) const {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QStringLiteral("text").toUtf8());
    hash.addData(QByteArrayView("\0", 1));
    hash.addData(text.toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

void ClipboardStore::scheduleCaptureFromClipboard() {
    if (!isOpen() || suppressCapture_) {
        return;
    }

    // Capture immediately and then sample several times. Some apps publish
    // clipboard data lazily, and rapid copy operations can arrive within a few
    // milliseconds of each other.
    captureFromClipboard();
    QTimer::singleShot(5, this, &ClipboardStore::captureFromClipboard);
    QTimer::singleShot(18, this, &ClipboardStore::captureFromClipboard);
    QTimer::singleShot(45, this, &ClipboardStore::captureFromClipboard);
    QTimer::singleShot(90, this, &ClipboardStore::captureFromClipboard);
    QTimer::singleShot(180, this, &ClipboardStore::captureFromClipboard);
    QTimer::singleShot(360, this, &ClipboardStore::captureFromClipboard);
}

void ClipboardStore::scheduleCaptureFromSelection() {
    // PRIMARY/Selection clipboard is intentionally disabled in this build.
    // It can expose non-standard transient data on some X11 setups, while this
    // application is designed to be a stable text-only Ctrl+C history manager.
}

void ClipboardStore::captureFromClipboard() {
    if (!isOpen() || suppressCapture_) {
        return;
    }

    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return;
    }

    captureMimeData(clipboard->mimeData(QClipboard::Clipboard), QClipboard::Clipboard);
}

void ClipboardStore::captureFromSelection() {
    // Disabled by default. Only the normal Clipboard mode is tracked.
}

bool ClipboardStore::captureMimeData(const QMimeData *mime, QClipboard::Mode mode) {
    Q_UNUSED(mode);

    const QString text = textOnlyFromMimeData(mime);
    if (text.trimmed().isEmpty()) {
        return false;
    }

    const QString hash = hashFor(text);
    if (hash == lastCapturedHash_) {
        return false;
    }

    lastCapturedHash_ = hash;
    touchExistingOrInsert(text, hash);
    enforceLimit(kMaxHistoryItems);
    emit changed();
    return true;
}

void ClipboardStore::touchExistingOrInsert(const QString &text, const QString &hash) {
    const QString now = nowIso();

    QSqlQuery exists(db_);
    exists.prepare(QStringLiteral("SELECT id FROM clips WHERE hash = :hash"));
    exists.bindValue(QStringLiteral(":hash"), hash);
    if (exists.exec() && exists.next()) {
        QSqlQuery update(db_);
        update.prepare(QStringLiteral("UPDATE clips SET updated_at = :updated_at WHERE hash = :hash"));
        update.bindValue(QStringLiteral(":updated_at"), now);
        update.bindValue(QStringLiteral(":hash"), hash);
        if (!update.exec()) {
            emit errorOccurred(QStringLiteral("Cannot update clipboard item: %1").arg(update.lastError().text()));
        }
        return;
    }

    QSqlQuery insert(db_);
    insert.prepare(QStringLiteral(R"SQL(
        INSERT INTO clips(type, text, pinned, created_at, updated_at, hash)
        VALUES('text', :text, 0, :created_at, :updated_at, :hash)
    )SQL"));
    insert.bindValue(QStringLiteral(":text"), text);
    insert.bindValue(QStringLiteral(":created_at"), now);
    insert.bindValue(QStringLiteral(":updated_at"), now);
    insert.bindValue(QStringLiteral(":hash"), hash);
    if (!insert.exec()) {
        emit errorOccurred(QStringLiteral("Cannot insert clipboard item: %1").arg(insert.lastError().text()));
    }
}

void ClipboardStore::enforceLimit(int maxItems) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(R"SQL(
        DELETE FROM clips
        WHERE id NOT IN (
            SELECT id FROM clips ORDER BY updated_at DESC LIMIT :limit
        )
    )SQL"));
    query.bindValue(QStringLiteral(":limit"), maxItems);
    if (!query.exec()) {
        emit errorOccurred(QStringLiteral("Cannot trim clipboard history: %1").arg(query.lastError().text()));
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
            SELECT id, type, text, pinned, created_at, updated_at, hash
            FROM clips
            WHERE text LIKE :search
            ORDER BY updated_at DESC
            LIMIT :limit
        )SQL"));
        query.bindValue(QStringLiteral(":search"), QStringLiteral("%") + search.trimmed() + QStringLiteral("%"));
    } else {
        query.prepare(QStringLiteral(R"SQL(
            SELECT id, type, text, pinned, created_at, updated_at, hash
            FROM clips
            ORDER BY updated_at DESC
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
        SELECT id, type, text, pinned, created_at, updated_at, hash
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
    item.type = query.value(1).toString();
    item.text = query.value(2).toString();
    item.pinned = query.value(3).toInt() != 0;
    item.createdAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs);
    item.updatedAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODateWithMs);
    item.hash = query.value(6).toString();
    return item;
}

bool ClipboardStore::copyToClipboard(const ClipItem &item) {
    if (!item.isValid() || item.text.isEmpty()) {
        return false;
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return false;
    }

    suppressCapture_ = true;
    lastCapturedHash_ = item.hash;

    clipboard->setMimeData(mimeDataFromItem(item), QClipboard::Clipboard);

    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QSqlQuery update(db_);
    update.prepare(QStringLiteral("UPDATE clips SET updated_at = :updated_at WHERE id = :id"));
    update.bindValue(QStringLiteral(":updated_at"), nowIso());
    update.bindValue(QStringLiteral(":id"), item.id);
    update.exec();

    QTimer::singleShot(50, this, [this]() {
        suppressCapture_ = false;
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
    if (query.exec(QStringLiteral("DELETE FROM clips"))) {
        emit changed();
    } else {
        emit errorOccurred(QStringLiteral("Cannot clear clipboard history: %1").arg(query.lastError().text()));
    }
}
