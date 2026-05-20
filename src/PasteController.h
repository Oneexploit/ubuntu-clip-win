#pragma once

#include <QPoint>
#include <QString>

class PasteController {
public:
    static QString activeWindowId();
    static bool canAutoPaste();
    static bool isX11Session();
    static bool isWaylandSession();
    static bool tryPasteToWindow(const QString &windowId,
                                 const QPoint &screenPoint = QPoint(-1, -1),
                                 bool requireFocusedPointTarget = false);
    static bool tryPasteToActiveApplication();

private:
    static QString xdotoolPath();
};
