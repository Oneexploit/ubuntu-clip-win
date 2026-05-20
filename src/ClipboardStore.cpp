#include "ClipboardStore.h"

#include "AppSettings.h"
#include "ClipMime.h"

#include <QApplication>
#include <QByteArrayView>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUuid>
#include <QVariant>

namespace {
constexpr int kMaxSearchableTextChars = 256 * 1024;

QString nonNullString(const QString &value) {
    return value.isNull() ? QStringLiteral("") : value;
}

QString normalizedStoredText(QString text) {
    return ClipMime::normalizedText(nonNullString(text)).left(kMaxSearchableTextChars);
}

QString textFromMimeBundle(const QByteArray &bundle) {
    if (bundle.isEmpty()) {
        return {};
    }

    const QMap<QString, QByteArray> formats = ClipMime::deserializeMimeBundle(bundle);
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

    return normalizedStoredText(textFromMimeBundle(mimeBundle));
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
    const QString path = databasePath();
    const QFileInfo fileInfo(path);
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
    if (!normalizeExistingRowsToTextOnly()) {
        return false;
    }

    applyStartupRetentionPolicy();
    enforceLimit();
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
            kind TEXT NOT NULL DEFAULT 'text',
            text TEXT NOT NULL DEFAULT '',
            html TEXT NOT NULL DEFAULT '',
            pinned INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            hash TEXT NOT NULL UNIQUE,
            mime_bundle BLOB NOT NULL DEFAULT X'',
            image_png BLOB NOT NULL DEFAULT X''
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

    QSqlQuery tableInfo(db_);
    if (!tableInfo.exec(QStringLiteral("PRAGMA table_info(clips)"))) {
        emit errorOccurred(QStringLiteral("Cannot inspect the clipboard schema: %1").arg(tableInfo.lastError().text()));
        return false;
    }

    QStringList columns;
    while (tableInfo.next()) {
        columns << tableInfo.value(1).toString();
    }
    if (columns.contains(QStringLiteral("mime_bundle")) && columns.contains(QStringLiteral("kind"))) {
        return true;
    }

    QSqlQuery rename(db_);
    if (!rename.exec(QStringLiteral("ALTER TABLE clips RENAME TO clips_legacy"))) {
        emit errorOccurred(QStringLiteral("Cannot migrate the old clipboard table: %1").arg(rename.lastError().text()));
        return false;
    }

    QSqlQuery create(db_);
    if (!create.exec(QStringLiteral(R"SQL(
        CREATE TABLE clips (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            kind TEXT NOT NULL DEFAULT 'text',
            text TEXT NOT NULL DEFAULT '',
            html TEXT NOT NULL DEFAULT '',
            pinned INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            hash TEXT NOT NULL UNIQUE,
            mime_bundle BLOB NOT NULL DEFAULT X'',
            image_png BLOB NOT NULL DEFAULT X''
        )
    )SQL"))) {
        emit errorOccurred(QStringLiteral("Cannot create the upgraded clipboard table: %1").arg(create.lastError().text()));
        return false;
    }

    QSqlQuery readLegacy(db_);
    if (!readLegacy.exec(QStringLiteral("SELECT text, pinned, created_at, updated_at FROM clips_legacy ORDER BY id ASC"))) {
        emit errorOccurred(QStringLiteral("Cannot read legacy clipboard items: %1").arg(readLegacy.lastError().text()));
        return false;
    }

    while (readLegacy.next()) {
        ClipItem item;
        item.kind = QStringLiteral("text");
        item.text = readLegacy.value(0).toString();
        item.pinned = readLegacy.value(1).toInt() != 0;
        item.createdAt = QDateTime::fromString(readLegacy.value(2).toString(), Qt::ISODateWithMs);
        item.updatedAt = QDateTime::fromString(readLegacy.value(3).toString(), Qt::ISODateWithMs);
        item.hash = hashFor(item);

        QSqlQuery insert(db_);
        insert.prepare(QStringLiteral(R"SQL(
            INSERT INTO clips(kind, text, pinned, created_at, updated_at, hash)
            VALUES(:kind, :text, :pinned, :created_at, :updated_at, :hash)
        )SQL"));
        insert.bindValue(QStringLiteral(":kind"), item.kind);
        insert.bindValue(QStringLiteral(":text"), item.text);
        insert.bindValue(QStringLiteral(":pinned"), item.pinned ? 1 : 0);
        insert.bindValue(QStringLiteral(":created_at"), item.createdAt.isValid() ? item.createdAt.toString(Qt::ISODateWithMs) : nowIso());
        insert.bindValue(QStringLiteral(":updated_at"), item.updatedAt.isValid() ? item.updatedAt.toString(Qt::ISODateWithMs) : nowIso());
        insert.bindValue(QStringLiteral(":hash"), item.hash);
        if (!insert.exec()) {
            emit errorOccurred(QStringLiteral("Cannot migrate a clipboard item: %1").arg(insert.lastError().text()));
        }
    }

    QSqlQuery drop(db_);
    drop.exec(QStringLiteral("DROP TABLE IF EXISTS clips_legacy"));
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

QString ClipboardStore::nowIso() const {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString ClipboardStore::hashFor(const ClipItem &item) const {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(nonNullString(item.kind).toUtf8());
    hash.addData(QByteArrayView("\0", 1));
    hash.addData(nonNullString(item.text).toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

void ClipboardStore::scheduleCaptureFromClipboard() {
    if (!isOpen() || suppressCapture_) {
        return;
    }

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

    const auto payload = ClipMime::payloadFromMimeData(mime);
    if (!payload.has_value()) {
        return false;
    }

    ClipItem item;
    item.kind = QStringLiteral("text");
    item.text = normalizedStoredText(payload->text);
    if (item.text.trimmed().isEmpty()) {
        return false;
    }
    item.hash = hashFor(item);

    if (item.hash == lastCapturedHash_) {
        return false;
    }

    lastCapturedHash_ = item.hash;
    touchExistingOrInsert(item);
    enforceLimit();
    emit changed();
    return true;
}

void ClipboardStore::touchExistingOrInsert(const ClipItem &item) {
    const QString now = nowIso();

    QSqlQuery exists(db_);
    exists.prepare(QStringLiteral("SELECT id FROM clips WHERE hash = :hash"));
    exists.bindValue(QStringLiteral(":hash"), item.hash);
    if (exists.exec() && exists.next()) {
        QSqlQuery update(db_);
        update.prepare(QStringLiteral(R"SQL(
            UPDATE clips
            SET kind = :kind,
                text = :text,
                updated_at = :updated_at
            WHERE hash = :hash
        )SQL"));
        update.bindValue(QStringLiteral(":kind"), item.kind);
        update.bindValue(QStringLiteral(":text"), item.text);
        update.bindValue(QStringLiteral(":updated_at"), now);
        update.bindValue(QStringLiteral(":hash"), item.hash);
        if (!update.exec()) {
            emit errorOccurred(QStringLiteral("Cannot update clipboard item: %1").arg(update.lastError().text()));
        }
        return;
    }

    QSqlQuery insert(db_);
    insert.prepare(QStringLiteral(R"SQL(
        INSERT INTO clips(kind, text, pinned, created_at, updated_at, hash)
        VALUES(:kind, :text, 0, :created_at, :updated_at, :hash)
    )SQL"));
    insert.bindValue(QStringLiteral(":kind"), item.kind);
    insert.bindValue(QStringLiteral(":text"), item.text);
    insert.bindValue(QStringLiteral(":created_at"), now);
    insert.bindValue(QStringLiteral(":updated_at"), now);
    insert.bindValue(QStringLiteral(":hash"), item.hash);
    if (!insert.exec()) {
        emit errorOccurred(QStringLiteral("Cannot insert clipboard item: %1").arg(insert.lastError().text()));
    }
}

bool ClipboardStore::normalizeExistingRowsToTextOnly() {
    QSqlQuery needsRewrite(db_);
    if (!needsRewrite.exec(QStringLiteral(R"SQL(
        SELECT COUNT(*)
        FROM clips
        WHERE kind IS NULL
           OR lower(kind) <> 'text'
           OR text IS NULL
           OR html IS NULL
           OR COALESCE(html, '') <> ''
           OR mime_bundle IS NULL
           OR length(mime_bundle) > 0
           OR image_png IS NULL
           OR length(image_png) > 0
    )SQL"))) {
        emit errorOccurred(QStringLiteral("Cannot inspect clipboard items: %1").arg(needsRewrite.lastError().text()));
        return false;
    }

    if (!needsRewrite.next() || needsRewrite.value(0).toInt() == 0) {
        return true;
    }

    QSqlQuery read(db_);
    if (!read.exec(QStringLiteral(R"SQL(
        SELECT id, kind, text, html, pinned, created_at, updated_at, hash, mime_bundle, image_png
        FROM clips
        ORDER BY updated_at ASC, id ASC
    )SQL"))) {
        emit errorOccurred(QStringLiteral("Cannot normalize clipboard history: %1").arg(read.lastError().text()));
        return false;
    }

    QList<ClipItem> normalizedItems;
    QHash<QString, int> indexByHash;
    while (read.next()) {
        ClipItem item;
        item.text = textOnlyValue(read.value(2).toString(),
                                  nonNullString(read.value(3).toString()),
                                  read.value(8).toByteArray());
        if (item.text.trimmed().isEmpty()) {
            continue;
        }

        item.kind = QStringLiteral("text");
        item.pinned = read.value(4).toInt() != 0;
        item.createdAt = QDateTime::fromString(read.value(5).toString(), Qt::ISODateWithMs);
        item.updatedAt = QDateTime::fromString(read.value(6).toString(), Qt::ISODateWithMs);
        item.hash = hashFor(item);

        const auto existingIt = indexByHash.constFind(item.hash);
        if (existingIt != indexByHash.cend()) {
            ClipItem &existing = normalizedItems[existingIt.value()];
            existing.pinned = existing.pinned || item.pinned;
            if (item.createdAt.isValid() && (!existing.createdAt.isValid() || item.createdAt < existing.createdAt)) {
                existing.createdAt = item.createdAt;
            }
            if (item.updatedAt.isValid() && (!existing.updatedAt.isValid() || item.updatedAt > existing.updatedAt)) {
                existing.updatedAt = item.updatedAt;
            }
            continue;
        }

        indexByHash.insert(item.hash, normalizedItems.size());
        normalizedItems << item;
    }

    if (!db_.transaction()) {
        emit errorOccurred(QStringLiteral("Cannot start clipboard cleanup transaction: %1").arg(db_.lastError().text()));
        return false;
    }

    QSqlQuery clear(db_);
    if (!clear.exec(QStringLiteral("DELETE FROM clips"))) {
        db_.rollback();
        emit errorOccurred(QStringLiteral("Cannot rewrite clipboard history: %1").arg(clear.lastError().text()));
        return false;
    }

    QSqlQuery insert(db_);
    insert.prepare(QStringLiteral(R"SQL(
        INSERT INTO clips(kind, text, pinned, created_at, updated_at, hash)
        VALUES(:kind, :text, :pinned, :created_at, :updated_at, :hash)
    )SQL"));

    for (const ClipItem &item : normalizedItems) {
        insert.bindValue(QStringLiteral(":kind"), item.kind);
        insert.bindValue(QStringLiteral(":text"), item.text);
        insert.bindValue(QStringLiteral(":pinned"), item.pinned ? 1 : 0);
        insert.bindValue(QStringLiteral(":created_at"), item.createdAt.isValid() ? item.createdAt.toString(Qt::ISODateWithMs) : nowIso());
        insert.bindValue(QStringLiteral(":updated_at"), item.updatedAt.isValid() ? item.updatedAt.toString(Qt::ISODateWithMs) : nowIso());
        insert.bindValue(QStringLiteral(":hash"), item.hash);
        if (!insert.exec()) {
            db_.rollback();
            emit errorOccurred(QStringLiteral("Cannot save normalized clipboard item: %1").arg(insert.lastError().text()));
            return false;
        }
    }

    if (!db_.commit()) {
        db_.rollback();
        emit errorOccurred(QStringLiteral("Cannot finish clipboard cleanup: %1").arg(db_.lastError().text()));
        return false;
    }

    lastCapturedHash_.clear();
    return true;
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
            ORDER BY updated_at DESC
            LIMIT :limit
        )
    )SQL"));
    query.bindValue(QStringLiteral(":limit"), AppSettings::historyLimit());
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
            SELECT id, kind, text, html, pinned, created_at, updated_at, hash, mime_bundle, image_png
            FROM clips
            WHERE text LIKE :search
            ORDER BY pinned DESC, updated_at DESC
            LIMIT :limit
        )SQL"));
        query.bindValue(QStringLiteral(":search"), QStringLiteral("%") + search.trimmed() + QStringLiteral("%"));
    } else {
        query.prepare(QStringLiteral(R"SQL(
            SELECT id, kind, text, html, pinned, created_at, updated_at, hash, mime_bundle, image_png
            FROM clips
            ORDER BY pinned DESC, updated_at DESC
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
        SELECT id, kind, text, html, pinned, created_at, updated_at, hash, mime_bundle, image_png
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
    item.kind = nonNullString(query.value(1).toString());
    if (item.kind.trimmed().isEmpty()) {
        item.kind = QStringLiteral("text");
    }
    item.text = normalizedStoredText(query.value(2).toString());
    item.pinned = query.value(4).toInt() != 0;
    item.createdAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODateWithMs);
    item.updatedAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs);
    item.hash = query.value(7).toString();
    return item;
}

bool ClipboardStore::copyToClipboard(const ClipItem &item) {
    if (!item.isValid() || item.text.trimmed().isEmpty()) {
        return false;
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return false;
    }

    suppressCapture_ = true;
    lastCapturedHash_ = item.hash;

    clipboard->setText(item.text, QClipboard::Clipboard);

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
        lastCapturedHash_.clear();
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
