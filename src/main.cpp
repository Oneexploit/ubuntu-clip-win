
#include "ClipboardStore.h"
#include "PasteController.h"
#include "PopupWindow.h"
#include "SingleInstance.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QTimer>

#include <memory>

namespace {
QIcon makeFallbackIcon() {
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(QColor(41, 128, 255));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(8, 8, 48, 48), 12, 12);
    painter.setBrush(QColor(255, 255, 255));
    painter.drawRoundedRect(QRectF(21, 17, 22, 7), 3, 3);
    painter.setPen(QPen(QColor(255, 255, 255), 4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(20, 31), QPointF(44, 31));
    painter.drawLine(QPointF(20, 41), QPointF(38, 41));
    return QIcon(pixmap);
}

QIcon appIcon() {
    QIcon icon(QStringLiteral(":/icons/ubuntu-clip-win.png"));
    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("ubuntu-clip-win"));
    }
    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("edit-paste"));
    }
    if (icon.isNull()) {
        icon = makeFallbackIcon();
    }
    return icon;
}

QString showMessageWithTargetWindow() {
    const QString activeWindowId = PasteController::activeWindowId();
    if (activeWindowId.isEmpty()) {
        return QStringLiteral("show");
    }
    return QStringLiteral("show|") + activeWindowId;
}
} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setApplicationName(QStringLiteral("Ubuntu Clip Win"));
    QApplication::setApplicationDisplayName(QStringLiteral("Clipboard History"));
    QApplication::setOrganizationName(QStringLiteral("AmirHosein"));
    QApplication::setWindowIcon(appIcon());

    const QStringList args = app.arguments();
    const bool showOnStart = args.contains(QStringLiteral("--show")) || args.contains(QStringLiteral("-s"));
    const bool backgroundOnly = args.contains(QStringLiteral("--background"));

    if (showOnStart && SingleInstance::sendMessage(showMessageWithTargetWindow())) {
        return 0;
    }
    if (!showOnStart && !backgroundOnly && SingleInstance::sendMessage(showMessageWithTargetWindow())) {
        return 0;
    }

    SingleInstance singleInstance;
    if (!singleInstance.listen()) {
        return 1;
    }

    ClipboardStore store;
    if (!store.open()) {
        QMessageBox::critical(nullptr,
                              QStringLiteral("Clipboard History"),
                              QStringLiteral("The clipboard database could not be opened."));
        return 1;
    }

    std::unique_ptr<PopupWindow> popup;
    auto ensurePopup = [&]() -> PopupWindow * {
        if (!popup) {
            popup = std::make_unique<PopupWindow>(&store);
        }
        return popup.get();
    };

    QObject::connect(&singleInstance, &SingleInstance::showRequested, &app, [&]() {
        ensurePopup()->showPopup();
    });
    QObject::connect(&singleInstance, &SingleInstance::showRequestedForWindow, &app, [&](const QString &targetWindowId) {
        ensurePopup()->showPopupForWindow(targetWindowId);
    });

    QClipboard *systemClipboard = QApplication::clipboard();
    if (systemClipboard) {
        QObject::connect(systemClipboard, &QClipboard::dataChanged, &store, &ClipboardStore::captureFromClipboard);
        QObject::connect(systemClipboard, &QClipboard::dataChanged, &store, &ClipboardStore::scheduleCaptureFromClipboard);
        QObject::connect(systemClipboard, &QClipboard::changed, &store, [&store](QClipboard::Mode mode) {
            if (mode == QClipboard::Clipboard) {
                store.captureFromClipboard();
                store.scheduleCaptureFromClipboard();
            }
        });
    }

    QSystemTrayIcon *tray = nullptr;
    QMenu *menu = nullptr;
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray = new QSystemTrayIcon(appIcon(), &app);
        menu = new QMenu();
        QObject::connect(&app, &QCoreApplication::aboutToQuit, menu, &QObject::deleteLater);

        QAction *showAction = menu->addAction(QStringLiteral("Show clipboard history"));
        QAction *clearAction = menu->addAction(QStringLiteral("Clear history"));
        menu->addSeparator();
        QAction *quitAction = menu->addAction(QStringLiteral("Quit"));

        QObject::connect(showAction, &QAction::triggered, &app, [&]() {
            ensurePopup()->showPopup();
        });
        QObject::connect(clearAction, &QAction::triggered, &store, &ClipboardStore::clearUnpinned);
        QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);
        QObject::connect(tray, &QSystemTrayIcon::activated, &app, [&](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                ensurePopup()->showPopup();
            }
        });
        QObject::connect(&store, &ClipboardStore::errorOccurred, tray, [tray](const QString &message) {
            if (tray && tray->isVisible()) {
                tray->showMessage(QStringLiteral("Clipboard History"), message, QSystemTrayIcon::Warning, 4000);
            }
        });

        tray->setToolTip(QStringLiteral("Clipboard History"));
        tray->setContextMenu(menu);
        tray->show();
    }

    QTimer clipboardPoller;
    clipboardPoller.setInterval(80);
    QObject::connect(&clipboardPoller, &QTimer::timeout, &store, &ClipboardStore::captureFromClipboard);
    clipboardPoller.start();

    QTimer::singleShot(200, &store, &ClipboardStore::scheduleCaptureFromClipboard);
    if (showOnStart || !backgroundOnly) {
        QTimer::singleShot(0, &app, [&]() {
            ensurePopup()->showPopup();
        });
    }

    return app.exec();
}
