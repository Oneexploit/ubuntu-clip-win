#include "AppIntegration.h"
#include "AppSettings.h"
#include "ClipboardStore.h"
#include "LinuxHotkeyManager.h"
#include "PasteController.h"
#include "PopupWindow.h"
#include "RuntimeLog.h"
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

QString clipboardModeName(QClipboard::Mode mode) {
    switch (mode) {
    case QClipboard::Clipboard:
        return QStringLiteral("Clipboard");
    case QClipboard::Selection:
        return QStringLiteral("Selection");
    case QClipboard::FindBuffer:
        return QStringLiteral("FindBuffer");
    }

    return QStringLiteral("Unknown");
}
} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setApplicationName(QStringLiteral("Ubuntu Clip Win"));
    QApplication::setApplicationDisplayName(QStringLiteral("Clipboard History"));
    QApplication::setOrganizationName(QStringLiteral("AmirHosein"));
    QApplication::setWindowIcon(appIcon());
    RuntimeLog::initialize();
    RuntimeLog::installQtMessageHandler();

    const QStringList args = app.arguments();
    const bool showOnStart = args.contains(QStringLiteral("--show")) || args.contains(QStringLiteral("-s"));
    const bool backgroundOnly = args.contains(QStringLiteral("--background"));
    const bool openSettingsOnStart = args.contains(QStringLiteral("--settings"));
    RuntimeLog::write(QStringLiteral("main"),
                      QStringLiteral("startup pid=%1 args=%2 showOnStart=%3 backgroundOnly=%4 openSettingsOnStart=%5 session=%6 autoPaste=%7 log=%8")
                          .arg(QCoreApplication::applicationPid())
                          .arg(args.join(QStringLiteral(" | ")))
                          .arg(showOnStart ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(backgroundOnly ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(openSettingsOnStart ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(QString::fromLocal8Bit(qgetenv("XDG_SESSION_TYPE")))
                          .arg(PasteController::canAutoPaste() ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(RuntimeLog::logFilePath()));

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, []() {
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("about-to-quit"));
    });
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &app, [](Qt::ApplicationState state) {
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("application-state-changed state=%1").arg(static_cast<int>(state)));
    });

    if (openSettingsOnStart && SingleInstance::sendMessage(QStringLiteral("settings"))) {
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("forwarded-to-existing-instance message=settings"));
        return 0;
    }
    if (showOnStart && SingleInstance::sendMessage(showMessageWithTargetWindow())) {
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("forwarded-to-existing-instance message=show"));
        return 0;
    }
    if (!showOnStart && !backgroundOnly && !openSettingsOnStart && SingleInstance::sendMessage(showMessageWithTargetWindow())) {
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("forwarded-to-existing-instance message=default-show"));
        return 0;
    }

    SingleInstance singleInstance;
    if (!singleInstance.listen()) {
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("single-instance-listen-failed"));
        return 1;
    }
    RuntimeLog::write(QStringLiteral("main"), QStringLiteral("single-instance-listening socket=%1").arg(SingleInstance::socketName()));

    ClipboardStore store;
    if (!store.open()) {
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("clipboard-store-open-failed"));
        QMessageBox::critical(nullptr,
                              QStringLiteral("Clipboard History"),
                              QStringLiteral("The clipboard database could not be opened."));
        return 1;
    }
    RuntimeLog::write(QStringLiteral("main"), QStringLiteral("clipboard-store-opened"));

    QSystemTrayIcon *tray = nullptr;
    QMenu *menu = nullptr;

    auto notify = [&](const QString &title, const QString &message, bool warning) {
        if (message.trimmed().isEmpty()) {
            return;
        }
        RuntimeLog::write(QStringLiteral("main"),
                          QStringLiteral("notify title=%1 warning=%2 message=%3")
                              .arg(title)
                              .arg(warning ? QStringLiteral("true") : QStringLiteral("false"))
                              .arg(message));

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
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("open-settings-dialog"));
        SettingsDialog dialog(popup ? popup.get() : nullptr);
        bool settingsApplied = false;
        QObject::connect(&dialog, &SettingsDialog::settingsApplied, &app, [&]() {
            settingsApplied = true;
            RuntimeLog::write(QStringLiteral("main"), QStringLiteral("settings-applied"));
            store.reloadSettings();
            if (hotkeyManager) {
                QString shortcutError;
                if (!hotkeyManager->reloadShortcut(&shortcutError) && !shortcutError.trimmed().isEmpty()) {
                    RuntimeLog::write(QStringLiteral("main"), QStringLiteral("hotkey-reload-failed error=%1").arg(shortcutError));
                    notify(QStringLiteral("Clipboard History"), shortcutError, true);
                }
            }
            if (popup) {
                popup->refreshItems();
            }
        });
        dialog.exec();
        if (!settingsApplied) {
            RuntimeLog::write(QStringLiteral("main"), QStringLiteral("settings-dialog-closed-without-apply"));
            store.reloadSettings();
        }
        if (popup) {
            popup->refreshItems();
        }
    };

    auto ensurePopup = [&]() -> PopupWindow * {
        if (!popup) {
            popup = std::make_unique<PopupWindow>(&store);
            RuntimeLog::write(QStringLiteral("main"), QStringLiteral("popup-created"));
            QObject::connect(popup.get(), &PopupWindow::settingsRequested, &app, openSettings);
            QObject::connect(popup.get(),
                             &PopupWindow::notificationRequested,
                             &app,
                             [&](const QString &title, const QString &message, bool warning) {
                                 RuntimeLog::write(QStringLiteral("main"),
                                                   QStringLiteral("popup-notification-requested title=%1 warning=%2")
                                                       .arg(title)
                                                       .arg(warning ? QStringLiteral("true") : QStringLiteral("false")));
                                 notify(title, message, warning);
                             });
        }
        return popup.get();
    };

    QObject::connect(&singleInstance, &SingleInstance::showRequested, &app, [&]() {
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("single-instance-signal showRequested"));
        ensurePopup()->showPopup();
    });
    QObject::connect(&singleInstance, &SingleInstance::showRequestedForWindow, &app, [&](const QString &targetWindowId) {
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("single-instance-signal showRequestedForWindow targetWindowId=%1").arg(targetWindowId));
        ensurePopup()->showPopupForWindow(targetWindowId);
    });
    QObject::connect(&singleInstance, &SingleInstance::settingsRequested, &app, [&]() {
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("single-instance-signal settingsRequested"));
        ensurePopup()->showPopup();
        openSettings();
    });
    QObject::connect(&singleInstance, &SingleInstance::messageReceived, &app, [&](const QString &message) {
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("single-instance-message-received message=%1").arg(message));
    });

    QClipboard *systemClipboard = QApplication::clipboard();
    if (systemClipboard) {
        QObject::connect(systemClipboard, &QClipboard::changed, &store, [&store](QClipboard::Mode mode) {
            RuntimeLog::write(QStringLiteral("main"), QStringLiteral("clipboard-changed mode=%1").arg(clipboardModeName(mode)));
            if (mode == QClipboard::Clipboard) {
                store.scheduleCaptureFromClipboard();
            }
        });
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("system-clipboard-connected"));
    }

    QObject::connect(&store, &ClipboardStore::errorOccurred, &app, [&](const QString &message) {
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("clipboard-store-error message=%1").arg(message));
        notify(QStringLiteral("Clipboard History"), message, true);
    });
    QObject::connect(&store, &ClipboardStore::changed, &app, [&]() {
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("clipboard-store-changed"));
    });

    if (PasteController::isX11Session()) {
        hotkeyManager = std::make_unique<LinuxHotkeyManager>(&app);
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("hotkey-manager-created"));
        QObject::connect(hotkeyManager.get(), &LinuxHotkeyManager::activated, &app, [&]() {
            RuntimeLog::write(QStringLiteral("main"), QStringLiteral("hotkey-activated"));
            ensurePopup()->showPopup();
        });

        QString hotkeyError;
        if (!hotkeyManager->start(&hotkeyError) && !hotkeyError.trimmed().isEmpty()) {
            RuntimeLog::write(QStringLiteral("main"), QStringLiteral("hotkey-start-failed error=%1").arg(hotkeyError));
            QTimer::singleShot(1000, &app, [&, hotkeyError]() {
                notify(QStringLiteral("Clipboard History"), hotkeyError, true);
            });
        } else {
            RuntimeLog::write(QStringLiteral("main"), QStringLiteral("hotkey-started"));
        }
    }

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray = new QSystemTrayIcon(appIcon(), &app);
        tray->setToolTip(QStringLiteral("Clipboard History"));
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("system-tray-created"));

        menu = new QMenu();
        QObject::connect(&app, &QCoreApplication::aboutToQuit, menu, &QObject::deleteLater);

        QAction *showAction = menu->addAction(QStringLiteral("Show clipboard history"));
        QAction *settingsAction = menu->addAction(QStringLiteral("Settings"));
        QAction *clearAction = menu->addAction(QStringLiteral("Clear unpinned history"));
        menu->addSeparator();
        QAction *quitAction = menu->addAction(QStringLiteral("Quit"));

        QObject::connect(showAction, &QAction::triggered, &app, [&]() {
            RuntimeLog::write(QStringLiteral("main"), QStringLiteral("tray-action show-history"));
            ensurePopup()->showPopup();
        });
        QObject::connect(settingsAction, &QAction::triggered, &app, [&]() {
            RuntimeLog::write(QStringLiteral("main"), QStringLiteral("tray-action settings"));
            openSettings();
        });
        QObject::connect(clearAction, &QAction::triggered, &app, [&]() {
            RuntimeLog::write(QStringLiteral("main"), QStringLiteral("tray-action clear-history"));
            ensurePopup()->clearHistoryWithConfirmation();
        });
        QObject::connect(quitAction, &QAction::triggered, &app, [&]() {
            RuntimeLog::write(QStringLiteral("main"), QStringLiteral("tray-action quit"));
            QApplication::quit();
        });
        QObject::connect(tray, &QSystemTrayIcon::activated, &app, [&](QSystemTrayIcon::ActivationReason reason) {
            RuntimeLog::write(QStringLiteral("main"), QStringLiteral("tray-activated reason=%1").arg(static_cast<int>(reason)));
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                ensurePopup()->showPopup();
            }
        });

        tray->setContextMenu(menu);
        tray->show();
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("system-tray-shown"));
    }

    QTimer::singleShot(200, &app, [&store]() {
        RuntimeLog::write(QStringLiteral("main"), QStringLiteral("startup-timer schedule-capture-from-clipboard"));
        store.scheduleCaptureFromClipboard();
    });

    if (AppSettings::showStartupDiagnostics() && !AppSettings::startupDiagnosticsShown()) {
        const QStringList diagnostics = AppIntegration::diagnosticsMessages();
        if (!diagnostics.isEmpty()) {
            QTimer::singleShot(900, &app, [&, diagnostics]() {
                RuntimeLog::write(QStringLiteral("main"), QStringLiteral("show-startup-diagnostics count=%1").arg(diagnostics.size()));
                notify(QStringLiteral("Clipboard History"),
                       diagnostics.join(QStringLiteral("\n\n")),
                       !PasteController::canAutoPaste());
                AppSettings::setStartupDiagnosticsShown(true);
            });
        }
    }

    if (showOnStart || openSettingsOnStart || !backgroundOnly) {
        QTimer::singleShot(0, &app, [&]() {
            RuntimeLog::write(QStringLiteral("main"),
                              QStringLiteral("initial-ui-flow showPopup=%1 openSettings=%2 backgroundOnly=%3")
                                  .arg((showOnStart || openSettingsOnStart || !backgroundOnly) ? QStringLiteral("true") : QStringLiteral("false"))
                                  .arg(openSettingsOnStart ? QStringLiteral("true") : QStringLiteral("false"))
                                  .arg(backgroundOnly ? QStringLiteral("true") : QStringLiteral("false")));
            ensurePopup()->showPopup();
            if (openSettingsOnStart) {
                openSettings();
            }
        });
    }

    return app.exec();
}
