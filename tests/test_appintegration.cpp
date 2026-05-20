#include "../src/AppIntegration.h"

#include <QtTest/QTest>

class AppIntegrationTest : public QObject {
    Q_OBJECT

private slots:
    void normalizesShortcutDisplay();
    void rejectsModifierOnlyShortcut();
};

void AppIntegrationTest::normalizesShortcutDisplay() {
    QCOMPARE(AppIntegration::normalizeShortcutDisplay(QStringLiteral("Meta+Ctrl+v")),
             QStringLiteral("Ctrl+Super+V"));
    QCOMPARE(AppIntegration::normalizeShortcutDisplay(QStringLiteral("['<Primary><Super>v']")),
             QStringLiteral("Ctrl+Super+V"));
    QCOMPARE(AppIntegration::normalizeShortcutDisplay(QStringLiteral("alt+shift+delete")),
             QStringLiteral("Alt+Shift+Delete"));
}

void AppIntegrationTest::rejectsModifierOnlyShortcut() {
    QVERIFY(AppIntegration::normalizeShortcutDisplay(QStringLiteral("Ctrl+Shift")).isEmpty());
}

QTEST_APPLESS_MAIN(AppIntegrationTest)

#include "test_appintegration.moc"
