#include "../src/AppSettings.h"
#include "../src/ClipboardStore.h"

#include <QtTest/QTest>

#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QUuid>

class ClipboardStoreTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void clearUnpinnedKeepsPinnedItems();
};

void ClipboardStoreTest::initTestCase() {
    const QString tempDir = QDir::temp().filePath(QStringLiteral("ubuntu-clip-win-tests-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QVERIFY(QDir().mkpath(tempDir));
    qputenv("UBUNTU_CLIP_WIN_DB_PATH", QDir(tempDir).filePath(QStringLiteral("clips.sqlite")).toUtf8());
    AppSettings::setPersistentHistory(true);
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

QTEST_MAIN(ClipboardStoreTest)

#include "test_clipboardstore.moc"
