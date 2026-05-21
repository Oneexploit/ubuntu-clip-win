#include "LinuxHotkeyManager.h"

#include "AppIntegration.h"
#include "PasteController.h"
#include "RuntimeLog.h"

#include <QGuiApplication>
#include <QKeySequence>

#if defined(Q_OS_LINUX)
#include <QtGui/qguiapplication_platform.h>

#include <X11/X.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <xcb/xcb.h>
#endif

namespace {
#if defined(Q_OS_LINUX)
int gX11GrabErrorCode = Success;

Display *nativeDisplay(void *displayHandle) {
    return static_cast<Display *>(displayHandle);
}

int x11GrabErrorHandler(Display *, XErrorEvent *event) {
    gX11GrabErrorCode = event ? event->error_code : BadAccess;
    return 0;
}

KeySym keySymForQtKey(int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return XK_a + (key - Qt::Key_A);
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return XK_0 + (key - Qt::Key_0);
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35) {
        return XK_F1 + (key - Qt::Key_F1);
    }

    switch (key) {
    case Qt::Key_Space: return XK_space;
    case Qt::Key_Tab: return XK_Tab;
    case Qt::Key_Backtab: return XK_ISO_Left_Tab;
    case Qt::Key_Return:
    case Qt::Key_Enter: return XK_Return;
    case Qt::Key_Escape: return XK_Escape;
    case Qt::Key_Backspace: return XK_BackSpace;
    case Qt::Key_Delete: return XK_Delete;
    case Qt::Key_Insert: return XK_Insert;
    case Qt::Key_Home: return XK_Home;
    case Qt::Key_End: return XK_End;
    case Qt::Key_PageUp: return XK_Page_Up;
    case Qt::Key_PageDown: return XK_Page_Down;
    case Qt::Key_Left: return XK_Left;
    case Qt::Key_Right: return XK_Right;
    case Qt::Key_Up: return XK_Up;
    case Qt::Key_Down: return XK_Down;
    case Qt::Key_Comma: return XK_comma;
    case Qt::Key_Period: return XK_period;
    case Qt::Key_Slash: return XK_slash;
    case Qt::Key_Backslash: return XK_backslash;
    case Qt::Key_Semicolon: return XK_semicolon;
    case Qt::Key_Apostrophe: return XK_apostrophe;
    case Qt::Key_QuoteLeft: return XK_grave;
    case Qt::Key_Minus: return XK_minus;
    case Qt::Key_Equal: return XK_equal;
    case Qt::Key_BracketLeft: return XK_bracketleft;
    case Qt::Key_BracketRight: return XK_bracketright;
    default: return NoSymbol;
    }
}

unsigned int modifierMaskFor(Qt::KeyboardModifiers modifiers) {
    unsigned int mask = 0;
    if (modifiers.testFlag(Qt::ControlModifier)) {
        mask |= ControlMask;
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        mask |= Mod1Mask;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        mask |= ShiftMask;
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        mask |= Mod4Mask;
    }
    return mask;
}

unsigned int numLockMaskFor(Display *display) {
    if (!display) {
        return 0;
    }

    unsigned int mask = 0;
    XModifierKeymap *modifierMap = XGetModifierMapping(display);
    if (!modifierMap) {
        return 0;
    }

    const KeyCode numLockKeycode = XKeysymToKeycode(display, XK_Num_Lock);
    for (int modifier = 0; modifier < 8; ++modifier) {
        for (int index = 0; index < modifierMap->max_keypermod; ++index) {
            const int mapIndex = modifier * modifierMap->max_keypermod + index;
            if (modifierMap->modifiermap[mapIndex] == numLockKeycode) {
                mask |= (1u << modifier);
            }
        }
    }

    XFreeModifiermap(modifierMap);
    return mask;
}
#endif
} // namespace

LinuxHotkeyManager::LinuxHotkeyManager(QObject *parent)
    : QObject(parent) {
    RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"), QStringLiteral("constructed"));
}

LinuxHotkeyManager::~LinuxHotkeyManager() {
    RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"), QStringLiteral("destructing"));
    unregisterShortcut();
    if (nativeFilterInstalled_ && qGuiApp) {
        qGuiApp->removeNativeEventFilter(this);
    }
}

bool LinuxHotkeyManager::start(QString *errorMessage) {
#if !defined(Q_OS_LINUX)
    if (errorMessage) {
        *errorMessage = QStringLiteral("The built-in Linux shortcut handler is unavailable on this platform.");
    }
    RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"), QStringLiteral("start failed reason=unsupported-platform"));
    return false;
#else
    if (!PasteController::isX11Session()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The built-in Linux shortcut handler is only available on X11 sessions.");
        }
        RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"), QStringLiteral("start failed reason=not-x11"));
        return false;
    }

    if (!nativeFilterInstalled_ && qGuiApp) {
        qGuiApp->installNativeEventFilter(this);
        nativeFilterInstalled_ = true;
    }

    if (!display_) {
        auto *native = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
        if (!native || !native->display()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("The X11 display connection is not available.");
            }
            RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"), QStringLiteral("start failed reason=no-display"));
            return false;
        }
        display_ = native->display();
        rootWindow_ = DefaultRootWindow(nativeDisplay(display_));
        numLockMask_ = numLockMaskFor(nativeDisplay(display_));
    }

    const QString shortcut = AppIntegration::configuredShortcutDisplay();
    RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"), QStringLiteral("start registering shortcut=%1").arg(shortcut));
    return registerShortcut(shortcut, errorMessage);
#endif
}

bool LinuxHotkeyManager::reloadShortcut(QString *errorMessage) {
#if !defined(Q_OS_LINUX)
    Q_UNUSED(errorMessage);
    RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"), QStringLiteral("reload-shortcut failed reason=unsupported-platform"));
    return false;
#else
    if (!display_) {
        RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"), QStringLiteral("reload-shortcut delegates-to-start"));
        return start(errorMessage);
    }
    const QString shortcut = AppIntegration::configuredShortcutDisplay();
    RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"), QStringLiteral("reload-shortcut shortcut=%1").arg(shortcut));
    return registerShortcut(shortcut, errorMessage);
#endif
}

bool LinuxHotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
    Q_UNUSED(result);

#if !defined(Q_OS_LINUX)
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    return false;
#else
    if (registeredKeycode_ == 0 || registeredModifiers_ == 0 || eventType != QByteArrayLiteral("xcb_generic_event_t")) {
        return false;
    }

    auto *event = static_cast<xcb_generic_event_t *>(message);
    if (!event || (event->response_type & ~0x80) != XCB_KEY_PRESS) {
        return false;
    }

    auto *keyEvent = reinterpret_cast<xcb_key_press_event_t *>(event);
    if (keyEvent->detail != registeredKeycode_) {
        return false;
    }

    const unsigned int cleanedState = keyEvent->state & ~(LockMask | numLockMask_);
    if (cleanedState != registeredModifiers_) {
        return false;
    }

    RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"),
                      QStringLiteral("activated keycode=%1 modifiers=%2").arg(registeredKeycode_).arg(registeredModifiers_));
    emit activated();
    return true;
#endif
}

void LinuxHotkeyManager::unregisterShortcut() {
#if defined(Q_OS_LINUX)
    if (!display_ || !rootWindow_ || registeredKeycode_ == 0) {
        registeredKeycode_ = 0;
        registeredModifiers_ = 0;
        return;
    }

    const unsigned int masks[] = {
        registeredModifiers_,
        registeredModifiers_ | LockMask,
        registeredModifiers_ | numLockMask_,
        registeredModifiers_ | LockMask | numLockMask_
    };

    for (const unsigned int mask : masks) {
        XUngrabKey(nativeDisplay(display_), registeredKeycode_, mask, rootWindow_);
    }
    XSync(nativeDisplay(display_), False);
    RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"),
                      QStringLiteral("unregister-shortcut keycode=%1 modifiers=%2").arg(registeredKeycode_).arg(registeredModifiers_));

    registeredKeycode_ = 0;
    registeredModifiers_ = 0;
#endif
}

bool LinuxHotkeyManager::registerShortcut(const QString &shortcutDisplay, QString *errorMessage) {
#if !defined(Q_OS_LINUX)
    Q_UNUSED(shortcutDisplay);
    if (errorMessage) {
        *errorMessage = QStringLiteral("The built-in Linux shortcut handler is unavailable on this platform.");
    }
    RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"), QStringLiteral("register-shortcut failed reason=unsupported-platform"));
    return false;
#else
    unregisterShortcut();

    const QString normalizedShortcut = AppIntegration::normalizeShortcutDisplay(shortcutDisplay);
    RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"),
                      QStringLiteral("register-shortcut begin requested=%1 normalized=%2").arg(shortcutDisplay).arg(normalizedShortcut));
    const QKeySequence sequence = QKeySequence::fromString(normalizedShortcut, QKeySequence::PortableText);
    if (sequence.count() < 1) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The shortcut could not be parsed.");
        }
        RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"), QStringLiteral("register-shortcut failed reason=parse"));
        return false;
    }

    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    const int key = combination.key();
    const KeySym keysym = keySymForQtKey(key);
    if (keysym == NoSymbol) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("This shortcut key is not supported yet on X11.");
        }
        RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"), QStringLiteral("register-shortcut failed reason=unsupported-key"));
        return false;
    }

    registeredKeycode_ = XKeysymToKeycode(nativeDisplay(display_), keysym);
    registeredModifiers_ = modifierMaskFor(modifiers);
    if (registeredKeycode_ == 0 || registeredModifiers_ == 0) {
        registeredKeycode_ = 0;
        registeredModifiers_ = 0;
        if (errorMessage) {
            *errorMessage = QStringLiteral("The shortcut is missing a supported modifier or key.");
        }
        RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"), QStringLiteral("register-shortcut failed reason=missing-modifier-or-key"));
        return false;
    }

    const unsigned int masks[] = {
        registeredModifiers_,
        registeredModifiers_ | LockMask,
        registeredModifiers_ | numLockMask_,
        registeredModifiers_ | LockMask | numLockMask_
    };

    gX11GrabErrorCode = Success;
    const auto previousErrorHandler = XSetErrorHandler(x11GrabErrorHandler);
    for (const unsigned int mask : masks) {
        XGrabKey(nativeDisplay(display_),
                 registeredKeycode_,
                 mask,
                 rootWindow_,
                 True,
                 GrabModeAsync,
                 GrabModeAsync);
    }
    XSync(nativeDisplay(display_), False);
    XSetErrorHandler(previousErrorHandler);

    if (gX11GrabErrorCode == BadAccess) {
        unregisterShortcut();
        if (errorMessage) {
            *errorMessage = QStringLiteral("This shortcut is already being used by Linux or another application.");
        }
        RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"), QStringLiteral("register-shortcut failed reason=bad-access"));
        return false;
    }
    RuntimeLog::write(QStringLiteral("LinuxHotkeyManager"),
                      QStringLiteral("register-shortcut success keycode=%1 modifiers=%2 numLockMask=%3")
                          .arg(registeredKeycode_)
                          .arg(registeredModifiers_)
                          .arg(numLockMask_));
    return true;
#endif
}
