// X11 backend. Uses its own Xlib connection (independent of Qt's xcb one) with
// a QSocketNotifier, so it only needs libX11 and no Qt private APIs.
#include "GlobalHotkey.h"
#include <QGuiApplication>
#include <QMetaObject>
#include <QSocketNotifier>
#include <QWidget>

// X11 headers last: they #define None, KeyPress, Bool, ... which clash with Qt.
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

namespace {

Display *g_dpy = nullptr;
QSocketNotifier *g_notifier = nullptr;
KeyCode g_keycode = 0;
unsigned int g_mods = 0;
bool g_grabbed = false;
bool g_keyDown = false;  // suppress auto-repeat toggling while held
int g_xerror = 0;

constexpr unsigned int kRelevantMods = ShiftMask | ControlMask | Mod1Mask | Mod4Mask;
// NumLock (Mod2) and CapsLock must not stop the hotkey from matching.
constexpr unsigned int kIgnoredCombos[] = {0, Mod2Mask, LockMask, Mod2Mask | LockMask};

int onXError(Display *, XErrorEvent *e) {
    g_xerror = e->error_code;
    return 0;
}

KeySym keysymFor(int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return XK_a + (key - Qt::Key_A);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35) return XK_F1 + (key - Qt::Key_F1);
    switch (key) {
    case Qt::Key_Space: return XK_space;
    case Qt::Key_Return:
    case Qt::Key_Enter: return XK_Return;
    case Qt::Key_Tab: return XK_Tab;
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
    default: break;
    }
    // Digits and ASCII punctuation: Qt::Key values equal the Latin-1 keysyms.
    if (key >= 0x20 && key <= 0x7e) return KeySym(key);
    return NoSymbol;
}

void ungrab() {
    if (!g_dpy || !g_grabbed) return;
    const Window root = DefaultRootWindow(g_dpy);
    for (unsigned int extra : kIgnoredCombos) XUngrabKey(g_dpy, g_keycode, g_mods | extra, root);
    XFlush(g_dpy);
    g_grabbed = false;
}

void drainEvents() {
    while (g_dpy && XPending(g_dpy)) {
        XEvent ev;
        XNextEvent(g_dpy, &ev);
        if (!g_grabbed || (ev.type != KeyPress && ev.type != KeyRelease) || ev.xkey.keycode != g_keycode) continue;
        if (ev.type == KeyRelease) {
            g_keyDown = false;
        } else if (ev.type == KeyPress && (ev.xkey.state & kRelevantMods) == g_mods && !g_keyDown) {
            g_keyDown = true;
            GlobalHotkey *hk = GlobalHotkey::instance();
            QMetaObject::invokeMethod(hk, [hk] { emit hk->activated(); }, Qt::QueuedConnection);
        }
    }
}

bool ensureDisplay(QString *error) {
    if (g_dpy) return true;
    g_dpy = XOpenDisplay(nullptr);
    if (!g_dpy) {
        if (error) *error = QStringLiteral("Could not connect to the X server.");
        return false;
    }
    // Repeats then arrive as press-press-press instead of press-release pairs,
    // so g_keyDown can filter them out.
    XkbSetDetectableAutoRepeat(g_dpy, True, nullptr);
    g_notifier = new QSocketNotifier(ConnectionNumber(g_dpy), QSocketNotifier::Read, qApp);
    QObject::connect(g_notifier, &QSocketNotifier::activated, qApp, [] { drainEvents(); });
    return true;
}

} // namespace

namespace GlobalHotkeyNative {

bool registerHotkey(const GlobalHotkey::Spec &spec, QString *error) {
    if (QGuiApplication::platformName() != QLatin1String("xcb")) {
        if (error) {
            *error = QStringLiteral(
                "Wayland doesn't let apps register global hotkeys. Add a custom shortcut in your "
                "desktop's keyboard settings that runs:  plumterm --toggle");
        }
        return false;
    }
    if (!ensureDisplay(error)) return false;

    const KeySym sym = keysymFor(spec.key);
    const KeyCode code = sym == NoSymbol ? 0 : XKeysymToKeycode(g_dpy, sym);
    if (!code) {
        if (error) *error = QStringLiteral("That key isn't available on the current keyboard layout.");
        return false;
    }

    unsigned int mods = 0;
    if (spec.cmd) mods |= Mod4Mask;
    if (spec.ctrl) mods |= ControlMask;
    if (spec.alt) mods |= Mod1Mask;
    if (spec.shift) mods |= ShiftMask;

    const Window root = DefaultRootWindow(g_dpy);
    XSync(g_dpy, False);
    g_xerror = 0;
    auto prevHandler = XSetErrorHandler(onXError);
    for (unsigned int extra : kIgnoredCombos) {
        XGrabKey(g_dpy, code, mods | extra, root, True, GrabModeAsync, GrabModeAsync);
    }
    XSync(g_dpy, False);
    XSetErrorHandler(prevHandler);

    g_keycode = code;
    g_mods = mods;
    g_grabbed = true;
    g_keyDown = false;

    if (g_xerror) {
        ungrab();
        if (error) {
            *error = g_xerror == BadAccess ? QStringLiteral("That hotkey is already grabbed by another application.")
                                           : QStringLiteral("The X server refused the hotkey (error %1).").arg(g_xerror);
        }
        return false;
    }
    return true;
}

void unregisterHotkey() {
    ungrab();
}

bool appIsFrontmost() {
    return QGuiApplication::applicationState() == Qt::ApplicationActive;
}

void hideApp(const QList<QWidget *> &windows) {
    for (QWidget *w : windows) w->hide();
}

void activateApp(const QList<QWidget *> &windows, QWidget *preferred) {
    for (QWidget *w : windows) {
        if (w->isMinimized()) w->showNormal();
        else if (!w->isVisible()) w->show();
    }
    if (preferred) {
        preferred->raise();
        preferred->activateWindow();
    }
}

} // namespace GlobalHotkeyNative
