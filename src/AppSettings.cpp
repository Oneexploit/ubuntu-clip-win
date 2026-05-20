#include "AppSettings.h"

#include <QSettings>

namespace {
constexpr auto kPersistentHistoryKey = "history/persistent";
constexpr auto kHistoryLimitKey = "history/limit";
constexpr auto kConfirmBeforeClearKey = "history/confirm_before_clear";
constexpr auto kShowStartupDiagnosticsKey = "startup/show_diagnostics";
constexpr auto kStartupDiagnosticsShownKey = "startup/diagnostics_shown";

int boundedHistoryLimit(int value) {
    return qBound(20, value, 5000);
}
} // namespace

bool AppSettings::persistentHistory() {
    return QSettings().value(QStringLiteral(kPersistentHistoryKey), false).toBool();
}

void AppSettings::setPersistentHistory(bool enabled) {
    QSettings().setValue(QStringLiteral(kPersistentHistoryKey), enabled);
}

int AppSettings::historyLimit() {
    return boundedHistoryLimit(QSettings().value(QStringLiteral(kHistoryLimitKey), 120).toInt());
}

void AppSettings::setHistoryLimit(int value) {
    QSettings().setValue(QStringLiteral(kHistoryLimitKey), boundedHistoryLimit(value));
}

bool AppSettings::confirmBeforeClear() {
    return QSettings().value(QStringLiteral(kConfirmBeforeClearKey), true).toBool();
}

void AppSettings::setConfirmBeforeClear(bool enabled) {
    QSettings().setValue(QStringLiteral(kConfirmBeforeClearKey), enabled);
}

bool AppSettings::showStartupDiagnostics() {
    return QSettings().value(QStringLiteral(kShowStartupDiagnosticsKey), true).toBool();
}

void AppSettings::setShowStartupDiagnostics(bool enabled) {
    QSettings settings;
    settings.setValue(QStringLiteral(kShowStartupDiagnosticsKey), enabled);
    if (enabled) {
        settings.setValue(QStringLiteral(kStartupDiagnosticsShownKey), false);
    }
}

bool AppSettings::startupDiagnosticsShown() {
    return QSettings().value(QStringLiteral(kStartupDiagnosticsShownKey), false).toBool();
}

void AppSettings::setStartupDiagnosticsShown(bool shown) {
    QSettings().setValue(QStringLiteral(kStartupDiagnosticsShownKey), shown);
}
