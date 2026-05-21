#include "../src/AppSettings.h"
#include "../src/ClipboardStore.h"

#include <QtTest/QTest>

#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMimeData>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

#include <utility>

class DelayedTextMimeData : public QMimeData {
public:
    DelayedTextMimeData(QString text, int attemptsBeforeReady)
        : text_(std::move(text)),
          attemptsBeforeReady_(attemptsBeforeReady) {}

protected:
    QStringList formats() const override {
        return {QStringLiteral("text/plain")};
    }

    QVariant retrieveData(const QString &mimeType, QMetaType type) const override {
        if (mimeType == QStringLiteral("text/plain") || mimeType == QStringLiteral("text/plain;charset=utf-8")) {
            ++attemptCount_;
            if (attemptCount_ > attemptsBeforeReady_) {
                return text_;
            }
            return {};
        }

        return QMimeData::retrieveData(mimeType, type);
    }

private:
    QString text_;
    int attemptsBeforeReady_ = 0;
    mutable int attemptCount_ = 0;
};

class ClipboardStoreTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void clearUnpinnedKeepsPinnedItems();
    void capturesDelayedClipboardPayloadsWithoutManualPause();
    void preservesFullTextWithoutTruncation();
    void schedulesClipboardRetriesAsynchronously();
    void migratesLegacyRichSchemaToTextOnly();
    void ordersNewestItemsFirstWhenTimestampsMatch();

private:
    QString databasePath_;
};

void ClipboardStoreTest::initTestCase() {
    AppSettings::setPersistentHistory(true);
    AppSettings::setHistoryLimit(5000);
}

void ClipboardStoreTest::init() {
    const QString tempDir = QDir::temp().filePath(QStringLiteral("ubuntu-clip-win-tests-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QVERIFY(QDir().mkpath(tempDir));
    databasePath_ = QDir(tempDir).filePath(QStringLiteral("clips.sqlite"));
    qputenv("UBUNTU_CLIP_WIN_DB_PATH", databasePath_.toUtf8());
    AppSettings::setPersistentHistory(true);
    AppSettings::setHistoryLimit(5000);
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

void ClipboardStoreTest::capturesDelayedClipboardPayloadsWithoutManualPause() {
    ClipboardStore store;
    QVERIFY(store.open());

    auto *clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard != nullptr);

    clipboard->setMimeData(new DelayedTextMimeData(QStringLiteral("alpha"), 3));
    store.captureFromClipboard();

    clipboard->setMimeData(new DelayedTextMimeData(QStringLiteral("beta"), 3));
    store.captureFromClipboard();

    const QList<ClipItem> items = store.recentItems();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.first().text, QStringLiteral("beta"));
    QCOMPARE(items.last().text, QStringLiteral("alpha"));
}

void ClipboardStoreTest::preservesFullTextWithoutTruncation() {
    ClipboardStore store;
    QVERIFY(store.open());

    auto *clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard != nullptr);

    const QString text = QStringLiteral("BEGIN\n")
        + QString(320 * 1024, QLatin1Char('x'))
        + QStringLiteral("\nEND");

    clipboard->setText(text);
    store.captureFromClipboard();

    const QList<ClipItem> items = store.recentItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().text, text);

    QVERIFY(store.copyToClipboard(items.first()));
    QCOMPARE(clipboard->text(), text);
}

void ClipboardStoreTest::schedulesClipboardRetriesAsynchronously() {
    ClipboardStore store;
    QVERIFY(store.open());

    auto *clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard != nullptr);

    clipboard->setMimeData(new DelayedTextMimeData(QStringLiteral("alpha"), 3));

    QElapsedTimer timer;
    timer.start();
    store.scheduleCaptureFromClipboard();

    QVERIFY2(timer.elapsed() < 20, "scheduleCaptureFromClipboard should not block the UI thread");
    QTRY_COMPARE_WITH_TIMEOUT(store.recentItems().size(), 1, 300);
    QCOMPARE(store.recentItems().first().text, QStringLiteral("alpha"));
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

void ClipboardStoreTest::ordersNewestItemsFirstWhenTimestampsMatch() {
    const QString seedConnectionName = QStringLiteral("seed-order-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase seedDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), seedConnectionName);
        seedDb.setDatabaseName(databasePath_);
        QVERIFY(seedDb.open());

        QSqlQuery query(seedDb);
        QVERIFY(query.exec(QStringLiteral(R"SQL(
            CREATE TABLE clips (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                text TEXT NOT NULL DEFAULT '',
                pinned INTEGER NOT NULL DEFAULT 0,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL,
                hash TEXT NOT NULL UNIQUE
            )
        )SQL")));
        QVERIFY(query.exec(QStringLiteral(R"SQL(
            INSERT INTO clips(text, pinned, created_at, updated_at, hash)
            VALUES('first', 0, '2024-01-01T00:00:00.000Z', '2024-01-03T00:00:00.000Z', 'first-hash')
        )SQL")));
        QVERIFY(query.exec(QStringLiteral(R"SQL(
            INSERT INTO clips(text, pinned, created_at, updated_at, hash)
            VALUES('second', 0, '2024-01-02T00:00:00.000Z', '2024-01-03T00:00:00.000Z', 'second-hash')
        )SQL")));
        seedDb.close();
    }
    QSqlDatabase::removeDatabase(seedConnectionName);

    ClipboardStore store;
    QVERIFY(store.open());

    const QList<ClipItem> items = store.recentItems();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.first().text, QStringLiteral("second"));
    QCOMPARE(items.last().text, QStringLiteral("first"));
}

QTEST_MAIN(ClipboardStoreTest)

#include "test_clipboardstore.moc"
