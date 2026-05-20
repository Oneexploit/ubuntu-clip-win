#include "AppIntegration.h"
#include "AppSettings.h"
#include "ClipboardStore.h"
#include "LinuxHotkeyManager.h"
#include "PasteController.h"
#include "PopupWindow.h"
#include "SettingsDialog.h"
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
    const bool openSettingsOnStart = args.contains(QStringLiteral("--settings"));

    if (openSettingsOnStart && SingleInstance::sendMessage(QStringLiteral("settings"))) {
        return 0;
    }
    if (showOnStart && SingleInstance::sendMessage(showMessageWithTargetWindow())) {
        return 0;
    }
    if (!showOnStart && !backgroundOnly && !openSettingsOnStart && SingleInstance::sendMessage(showMessageWithTargetWindow())) {
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

    QSystemTrayIcon *tray = nullptr;
    QMenu *menu = nullptr;

    auto notify = [&](const QString &title, const QString &message, bool warning) {
        if (message.trimmed().isEmpty()) {
            return;
        }

        if (tray && tray->isVisible()) {
            tray->showMessage(title,
                              message,
                              warning ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information,
                              5000);
            return;
        }

        if (!backgroundOnly) {
            if (warning) {
                QMessageBox::warning(nullptr, title, message);
            } else {
                QMessageBox::information(nullptr, title, message);
            }
        }
    };

    std::unique_ptr<PopupWindow> popup;
    std::unique_ptr<LinuxHotkeyManager> hotkeyManager;
    auto openSettings = [&]() {
        SettingsDialog dialog(popup ? popup.get() : nullptr);
        bool settingsApplied = false;
        QObject::connect(&dialog, &SettingsDialog::settingsApplied, &app, [&]() {
            settingsApplied = true;
            store.reloadSettings();
            if (hotkeyManager) {
                QString shortcutError;
                if (!hotkeyManager->reloadShortcut(&shortcutError) && !shortcutError.trimmed().isEmpty()) {
                    notify(QStringLiteral("Clipboard History"), shortcutError, true);
                }
            }
            if (popup) {
                popup->refreshItems();
            }
        });
        dialog.exec();
        if (!settingsApplied) {
            store.reloadSettings();
        }
        if (popup) {
            popup->refreshItems();
        }
    };

    auto ensurePopup = [&]() -> PopupWindow * {
        if (!popup) {
            popup = std::make_unique<PopupWindow>(&store);
            QObject::connect(popup.get(), &PopupWindow::settingsRequested, &app, openSettings);
            QObject::connect(popup.get(),
                             &PopupWindow::notificationRequested,
                             &app,
                             [&](const QString &title, const QString &message, bool warning) {
                                 notify(title, message, warning);
                             });
        }
        return popup.get();
    };

    QObject::connect(&singleInstance, &SingleInstance::showRequested, &app, [&]() {
        ensurePopup()->showPopup();
    });
    QObject::connect(&singleInstance, &SingleInstance::showRequestedForWindow, &app, [&](const QString &targetWindowId) {
        ensurePopup()->showPopupForWindow(targetWindowId);
    });
    QObject::connect(&singleInstance, &SingleInstance::settingsRequested, &app, [&]() {
        ensurePopup()->showPopup();
        openSettings();
    });

    QClipboard *systemClipboard = QApplication::clipboard();
    if (systemClipboard) {
        QObject::connect(systemClipboard, &QClipboard::changed, &store, [&store](QClipboard::Mode mode) {
            if (mode == QClipboard::Clipboard) {
                store.scheduleCaptureFromClipboard();
            }
        });
    }

    QObject::connect(&store, &ClipboardStore::errorOccurred, &app, [&](const QString &message) {
        notify(QStringLiteral("Clipboard History"), message, true);
    });

    if (PasteController::isX11Session()) {
        hotkeyManager = std::make_unique<LinuxHotkeyManager>(&app);
        QObject::connect(hotkeyManager.get(), &LinuxHotkeyManager::activated, &app, [&]() {
            ensurePopup()->showPopup();
        });

        QString hotkeyError;
        if (!hotkeyManager->start(&hotkeyError) && !hotkeyError.trimmed().isEmpty()) {
            QTimer::singleShot(1000, &app, [&, hotkeyError]() {
                notify(QStringLiteral("Clipboard History"), hotkeyError, true);
            });
        }
    }

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray = new QSystemTrayIcon(appIcon(), &app);
        tray->setToolTip(QStringLiteral("Clipboard History"));

        menu = new QMenu();
        QObject::connect(&app, &QCoreApplication::aboutToQuit, menu, &QObject::deleteLater);

        QAction *showAction = menu->addAction(QStringLiteral("Show clipboard history"));
        QAction *settingsAction = menu->addAction(QStringLiteral("Settings"));
        QAction *clearAction = menu->addAction(QStringLiteral("Clear unpinned history"));
        menu->addSeparator();
        QAction *quitAction = menu->addAction(QStringLiteral("Quit"));

        QObject::connect(showAction, &QAction::triggered, &app, [&]() {
            ensurePopup()->showPopup();
        });
        QObject::connect(settingsAction, &QAction::triggered, &app, openSettings);
        QObject::connect(clearAction, &QAction::triggered, &app, [&]() {
            ensurePopup()->clearHistoryWithConfirmation();
        });
        QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);
        QObject::connect(tray, &QSystemTrayIcon::activated, &app, [&](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                ensurePopup()->showPopup();
            }
        });

        tray->setContextMenu(menu);
        tray->show();
    }

    QTimer::singleShot(200, &store, &ClipboardStore::scheduleCaptureFromClipboard);

    if (AppSettings::showStartupDiagnostics() && !AppSettings::startupDiagnosticsShown()) {
        const QStringList diagnostics = AppIntegration::diagnosticsMessages();
        if (!diagnostics.isEmpty()) {
            QTimer::singleShot(900, &app, [&, diagnostics]() {
                notify(QStringLiteral("Clipboard History"),
                       diagnostics.join(QStringLiteral("\n\n")),
                       !PasteController::canAutoPaste());
                AppSettings::setStartupDiagnosticsShown(true);
            });
        }
    }

    if (showOnStart || openSettingsOnStart || !backgroundOnly) {
        QTimer::singleShot(0, &app, [&]() {
            ensurePopup()->showPopup();
            if (openSettingsOnStart) {
                openSettings();
            }
        });
    }

    return app.exec();
}
