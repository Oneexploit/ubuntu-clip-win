#pragma once

class AppSettings {
public:
    static bool persistentHistory();
    static void setPersistentHistory(bool enabled);

    static int historyLimit();
    static void setHistoryLimit(int value);

    static bool confirmBeforeClear();
    static void setConfirmBeforeClear(bool enabled);

    static bool showStartupDiagnostics();
    static void setShowStartupDiagnostics(bool enabled);

    static bool startupDiagnosticsShown();
    static void setStartupDiagnosticsShown(bool shown);
};
