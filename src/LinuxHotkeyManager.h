#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>

class LinuxHotkeyManager : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit LinuxHotkeyManager(QObject *parent = nullptr);
    ~LinuxHotkeyManager() override;

    bool start(QString *errorMessage = nullptr);
    bool reloadShortcut(QString *errorMessage = nullptr);
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void activated();

private:
    void unregisterShortcut();
    bool registerShortcut(const QString &shortcutDisplay, QString *errorMessage = nullptr);

#if defined(Q_OS_LINUX)
    void *display_ = nullptr;
    unsigned long rootWindow_ = 0;
    int registeredKeycode_ = 0;
    unsigned int registeredModifiers_ = 0;
    unsigned int numLockMask_ = 0;
#endif
    bool nativeFilterInstalled_ = false;
};
