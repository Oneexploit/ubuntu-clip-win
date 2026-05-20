#pragma once

#include <QString>

class PasteController {
public:
    static QString activeWindowId();
    static bool canAutoPaste();
    static bool isX11Session();
    static bool isWaylandSession();
    static bool tryPasteToWindow(const QString &windowId);
    static bool tryPasteToActiveApplication();

private:
    static QString xdotoolPath();
};
