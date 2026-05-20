#include "AppSettings.h"

#include <QSettings>

namespace {
constexpr auto kPersistentHistoryKey = "history/persistent";
constexpr auto kHistoryLimitKey = "history/limit";
constexpr auto kConfirmBeforeClearKey = "history/confirm_before_clear";
constexpr auto kGlobalShortcutKey = "shortcut/display";
constexpr auto kShowStartupDiagnosticsKey = "startup/show_diagnostics";
constexpr auto kStartupDiagnosticsShownKey = "startup/diagnostics_shown";

int boundedHistoryLimit(int value) {
    return qBound(20, value, 5000);
}
} // namespace

bool AppSettings::persistentHistory() {
    return QSettings().value(QString::fromLatin1(kPersistentHistoryKey), false).toBool();
}

void AppSettings::setPersistentHistory(bool enabled) {
    QSettings().setValue(QString::fromLatin1(kPersistentHistoryKey), enabled);
}

int AppSettings::historyLimit() {
    return boundedHistoryLimit(QSettings().value(QString::fromLatin1(kHistoryLimitKey), 120).toInt());
}

void AppSettings::setHistoryLimit(int value) {
    QSettings().setValue(QString::fromLatin1(kHistoryLimitKey), boundedHistoryLimit(value));
}

bool AppSettings::confirmBeforeClear() {
    return QSettings().value(QString::fromLatin1(kConfirmBeforeClearKey), true).toBool();
}

void AppSettings::setConfirmBeforeClear(bool enabled) {
    QSettings().setValue(QString::fromLatin1(kConfirmBeforeClearKey), enabled);
}

bool AppSettings::showStartupDiagnostics() {
    return QSettings().value(QString::fromLatin1(kShowStartupDiagnosticsKey), true).toBool();
}

void AppSettings::setShowStartupDiagnostics(bool enabled) {
    QSettings settings;
    settings.setValue(QString::fromLatin1(kShowStartupDiagnosticsKey), enabled);
    if (enabled) {
        settings.setValue(QString::fromLatin1(kStartupDiagnosticsShownKey), false);
    }
}

bool AppSettings::startupDiagnosticsShown() {
    return QSettings().value(QString::fromLatin1(kStartupDiagnosticsShownKey), false).toBool();
}

void AppSettings::setStartupDiagnosticsShown(bool shown) {
    QSettings().setValue(QString::fromLatin1(kStartupDiagnosticsShownKey), shown);
}

QString AppSettings::globalShortcut() {
    return QSettings().value(QString::fromLatin1(kGlobalShortcutKey), QStringLiteral("Ctrl+Alt+V")).toString();
}

void AppSettings::setGlobalShortcut(const QString &shortcutDisplay) {
    QSettings().setValue(QString::fromLatin1(kGlobalShortcutKey), shortcutDisplay.trimmed());
}
