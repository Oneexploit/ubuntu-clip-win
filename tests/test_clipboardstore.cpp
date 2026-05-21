#include "../src/AppSettings.h"
#include "../src/ClipboardStore.h"

#include <QtTest/QTest>

#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

class ClipboardStoreTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void clearUnpinnedKeepsPinnedItems();
    void migratesLegacyRichSchemaToTextOnly();

private:
    QString databasePath_;
};

void ClipboardStoreTest::initTestCase() {
    AppSettings::setPersistentHistory(true);
}

void ClipboardStoreTest::init() {
    const QString tempDir = QDir::temp().filePath(QStringLiteral("ubuntu-clip-win-tests-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QVERIFY(QDir().mkpath(tempDir));
    databasePath_ = QDir(tempDir).filePath(QStringLiteral("clips.sqlite"));
    qputenv("UBUNTU_CLIP_WIN_DB_PATH", databasePath_.toUtf8());
}

void ClipboardStoreTest::clearUnpinnedKeepsPinnedItems() {
    ClipboardStore store;
    QVERIFY(store.open());

    auto *clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard != nullptr);

    clipboard->setText(QStringLiteral("alpha"));
    store.captureFromClipboard();
    clipboard->setText(QStringLiteral("beta"));
    store.captureFromClipboard();

    QList<ClipItem> items = store.recentItems();
    QCOMPARE(items.size(), 2);

    store.togglePinned(items.first().id);
    store.clearUnpinned();

    items = store.recentItems();
    QCOMPARE(items.size(), 1);
    QVERIFY(items.first().pinned);
}

void ClipboardStoreTest::migratesLegacyRichSchemaToTextOnly() {
    const QString seedConnectionName = QStringLiteral("seed-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase seedDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), seedConnectionName);
        seedDb.setDatabaseName(databasePath_);
        QVERIFY(seedDb.open());

        QSqlQuery query(seedDb);
        QVERIFY(query.exec(QStringLiteral(R"SQL(
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
        )SQL")));
        QVERIFY(query.exec(QStringLiteral(R"SQL(
            INSERT INTO clips(kind, text, html, pinned, created_at, updated_at, hash)
            VALUES('text', '', '<b>Hello</b><br/>World', 0, '2024-01-01T00:00:00.000Z', '2024-01-01T00:00:00.000Z', 'legacy-html')
        )SQL")));
        QVERIFY(query.exec(QStringLiteral(R"SQL(
            INSERT INTO clips(kind, text, html, pinned, created_at, updated_at, hash)
            VALUES('text', 'Direct text', '', 1, '2024-01-02T00:00:00.000Z', '2024-01-02T00:00:00.000Z', 'legacy-text')
        )SQL")));
        seedDb.close();
    }
    QSqlDatabase::removeDatabase(seedConnectionName);

    {
        ClipboardStore store;
        QVERIFY(store.open());

        const QList<ClipItem> items = store.recentItems();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.first().text, QStringLiteral("Direct text"));
        QVERIFY(items.first().pinned);
        QCOMPARE(items.last().text, QStringLiteral("Hello\nWorld"));
        QVERIFY(!items.last().pinned);
    }

    const QString inspectConnectionName = QStringLiteral("inspect-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase inspectDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), inspectConnectionName);
        inspectDb.setDatabaseName(databasePath_);
        QVERIFY(inspectDb.open());

        QSqlQuery query(inspectDb);
        QVERIFY(query.exec(QStringLiteral("PRAGMA table_info(clips)")));

        QStringList columns;
        while (query.next()) {
            columns << query.value(1).toString();
        }

        QCOMPARE(columns, (QStringList{
            QStringLiteral("id"),
            QStringLiteral("text"),
            QStringLiteral("pinned"),
            QStringLiteral("created_at"),
            QStringLiteral("updated_at"),
            QStringLiteral("hash")
        }));

        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name LIKE 'clips_legacy_%'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 0);
        inspectDb.close();
    }
    QSqlDatabase::removeDatabase(inspectConnectionName);
}

QTEST_MAIN(ClipboardStoreTest)

#include "test_clipboardstore.moc"
