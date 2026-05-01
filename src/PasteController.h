#pragma once

#include <QString>

class PasteController {
public:
    // X11 helper. Wayland deliberately returns empty/false because normal client
    // applications cannot safely inject keyboard input into other applications.
    static QString activeWindowId();
    static bool tryPasteToWindow(const QString &windowId);
    static bool tryPasteToActiveApplication();

private:
    static QString xdotoolPath();
    static bool isX11();
};
