#include "GlobalHotkey.h"
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QGuiApplication>
#include <QPointer>
#include <QWidget>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

constexpr int kHotKeyId = 0x504C; // 'PL'

UINT toVirtualKey(int qtKey) {
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) return static_cast<UINT>(qtKey);
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) return static_cast<UINT>(qtKey);
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24) return static_cast<UINT>(VK_F1 + (qtKey - Qt::Key_F1));
    switch (qtKey) {
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Tab: return VK_TAB;
    case Qt::Key_Return:
    case Qt::Key_Enter: return VK_RETURN;
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_Minus: return VK_OEM_MINUS;
    case Qt::Key_Equal:
    case Qt::Key_Plus: return VK_OEM_PLUS;
    case Qt::Key_BracketLeft: return VK_OEM_4;
    case Qt::Key_Backslash: return VK_OEM_5;
    case Qt::Key_BracketRight: return VK_OEM_6;
    case Qt::Key_Semicolon: return VK_OEM_1;
    case Qt::Key_Apostrophe: return VK_OEM_7;
    case Qt::Key_Comma: return VK_OEM_COMMA;
    case Qt::Key_Period: return VK_OEM_PERIOD;
    case Qt::Key_Slash: return VK_OEM_2;
    case Qt::Key_QuoteLeft:
    case Qt::Key_AsciiTilde: return VK_OEM_3;
    default: return 0;
    }
}

class WinHotkeyFilter final : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) override {
        if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
            const auto *msg = static_cast<const MSG *>(message);
            if (msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == kHotKeyId) {
                emit GlobalHotkey::instance()->activated();
                return true;
            }
        }
        return false;
    }
};

WinHotkeyFilter *s_filter = nullptr;
bool s_registered = false;
QList<QPointer<QWidget>> s_hiddenDialogs;

} // namespace

namespace GlobalHotkeyNative {

bool registerHotkey(const GlobalHotkey::Spec &spec, QString *error) {
    unregisterHotkey();
    const UINT vk = toVirtualKey(spec.key);
    if (vk == 0) {
        if (error) *error = QStringLiteral("Unsupported key for Windows global hotkey.");
        return false;
    }
    UINT mods = MOD_NOREPEAT;
    if (spec.ctrl) mods |= MOD_CONTROL;
    if (spec.alt) mods |= MOD_ALT;
    if (spec.shift) mods |= MOD_SHIFT;
    if (spec.cmd) mods |= MOD_WIN;

    if (!s_filter && qApp) {
        s_filter = new WinHotkeyFilter();
        qApp->installNativeEventFilter(s_filter);
    }

    if (!RegisterHotKey(nullptr, kHotKeyId, mods, vk)) {
        if (error) {
            *error = QStringLiteral("Another app may already be using this shortcut (Win32 error %1).").arg(GetLastError());
        }
        return false;
    }
    s_registered = true;
    return true;
}

void unregisterHotkey() {
    if (s_registered) {
        UnregisterHotKey(nullptr, kHotKeyId);
        s_registered = false;
    }
}

bool appIsFrontmost() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

void hideApp(const QList<QWidget *> &windows) {
    s_hiddenDialogs.clear();
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<QDialog *>(w) && w->isVisible()) {
            s_hiddenDialogs.append(w);
            w->hide();
        }
    }
    for (QWidget *w : windows) w->hide();
}

void activateApp(const QList<QWidget *> &windows, QWidget *preferred) {
    for (QWidget *w : windows) {
        if (w->isMinimized()) w->showNormal();
        else if (!w->isVisible()) w->show();
    }
    QWidget *front = preferred;
    for (const QPointer<QWidget> &dlg : s_hiddenDialogs) {
        if (dlg) {
            dlg->show();
            front = dlg.data();
        }
    }
    s_hiddenDialogs.clear();
    if (front) {
        front->raise();
        front->activateWindow();
        if (HWND hwnd = reinterpret_cast<HWND>(front->winId())) {
            SetForegroundWindow(hwnd);
        }
    }
}

} // namespace GlobalHotkeyNative
