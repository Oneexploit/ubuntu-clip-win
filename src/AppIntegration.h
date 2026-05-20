#pragma once

#include <QString>
#include <QStringList>

class AppIntegration {
public:
    static QString environmentSummary();
    static QStringList diagnosticsMessages();

    static bool isShortcutConfigAvailable();
    static QString configuredShortcutDisplay();
    static QString normalizeShortcutDisplay(const QString &displayShortcut);
    static bool setConfiguredShortcutDisplay(const QString &displayShortcut, QString *errorMessage = nullptr);

    static bool isAutostartEnabled();
    static bool setAutostartEnabled(bool enabled, QString *errorMessage = nullptr);
};
