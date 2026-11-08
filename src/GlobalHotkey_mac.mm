#include "GlobalHotkey.h"
#include <QMetaObject>
#include <QWidget>

#import <AppKit/AppKit.h>
#include <Carbon/Carbon.h>

namespace {

constexpr OSType kSignature = 'PlTm';
EventHotKeyRef g_hotKeyRef = nullptr;
EventHandlerRef g_handlerRef = nullptr;

OSStatus onHotKey(EventHandlerCallRef, EventRef event, void *) {
    EventHotKeyID id{};
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(id), nullptr, &id) == noErr &&
        id.signature == kSignature) {
        GlobalHotkey *hk = GlobalHotkey::instance();
        QMetaObject::invokeMethod(hk, [hk] { emit hk->activated(); }, Qt::QueuedConnection);
    }
    return noErr;
}

// Find the virtual keycode that produces `ch` in the user's current keyboard
// layout, so e.g. "Cmd+Z" means the key labelled Z on AZERTY too.
int keycodeForChar(QChar ch) {
    TISInputSourceRef src = TISCopyCurrentKeyboardLayoutInputSource();
    if (!src) return -1;
    int result = -1;
    if (auto data = static_cast<CFDataRef>(TISGetInputSourceProperty(src, kTISPropertyUnicodeKeyLayoutData))) {
        auto layout = reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(data));
        const QChar want = ch.toLower();
        for (UInt16 kc = 0; kc < 128 && result < 0; ++kc) {
            UInt32 dead = 0;
            UniChar buf[4];
            UniCharCount len = 0;
            if (UCKeyTranslate(layout, kc, kUCKeyActionDisplay, 0, LMGetKbdType(), kUCKeyTranslateNoDeadKeysMask,
                               &dead, 4, &len, buf) == noErr &&
                len == 1 && QChar(buf[0]).toLower() == want) {
                result = kc;
            }
        }
    }
    CFRelease(src);
    return result;
}

int keycodeFor(int key) {
    switch (key) {
    case Qt::Key_Space: return kVK_Space;
    case Qt::Key_Return:
    case Qt::Key_Enter: return kVK_Return;
    case Qt::Key_Tab: return kVK_Tab;
    case Qt::Key_Escape: return kVK_Escape;
    case Qt::Key_Backspace: return kVK_Delete;
    case Qt::Key_Delete: return kVK_ForwardDelete;
    case Qt::Key_Home: return kVK_Home;
    case Qt::Key_End: return kVK_End;
    case Qt::Key_PageUp: return kVK_PageUp;
    case Qt::Key_PageDown: return kVK_PageDown;
    case Qt::Key_Left: return kVK_LeftArrow;
    case Qt::Key_Right: return kVK_RightArrow;
    case Qt::Key_Up: return kVK_UpArrow;
    case Qt::Key_Down: return kVK_DownArrow;
    case Qt::Key_F1: return kVK_F1;
    case Qt::Key_F2: return kVK_F2;
    case Qt::Key_F3: return kVK_F3;
    case Qt::Key_F4: return kVK_F4;
    case Qt::Key_F5: return kVK_F5;
    case Qt::Key_F6: return kVK_F6;
    case Qt::Key_F7: return kVK_F7;
    case Qt::Key_F8: return kVK_F8;
    case Qt::Key_F9: return kVK_F9;
    case Qt::Key_F10: return kVK_F10;
    case Qt::Key_F11: return kVK_F11;
    case Qt::Key_F12: return kVK_F12;
    case Qt::Key_F13: return kVK_F13;
    case Qt::Key_F14: return kVK_F14;
    case Qt::Key_F15: return kVK_F15;
    case Qt::Key_F16: return kVK_F16;
    case Qt::Key_F17: return kVK_F17;
    case Qt::Key_F18: return kVK_F18;
    case Qt::Key_F19: return kVK_F19;
    case Qt::Key_F20: return kVK_F20;
    default: break;
    }
    if (key >= 0x20 && key <= 0x7e) return keycodeForChar(QChar(key));
    return -1;
}

} // namespace

namespace GlobalHotkeyNative {

bool registerHotkey(const GlobalHotkey::Spec &spec, QString *error) {
    const int keycode = keycodeFor(spec.key);
    if (keycode < 0) {
        if (error) *error = QStringLiteral("That key isn't available on the current keyboard layout.");
        return false;
    }

    if (!g_handlerRef) {
        EventTypeSpec type{kEventClassKeyboard, kEventHotKeyPressed};
        InstallApplicationEventHandler(&onHotKey, 1, &type, nullptr, &g_handlerRef);
    }

    UInt32 mods = 0;
    if (spec.cmd) mods |= cmdKey;
    if (spec.ctrl) mods |= controlKey;
    if (spec.alt) mods |= optionKey;
    if (spec.shift) mods |= shiftKey;

    const EventHotKeyID id{kSignature, 1};
    const OSStatus st = RegisterEventHotKey(UInt32(keycode), mods, id, GetApplicationEventTarget(), 0, &g_hotKeyRef);
    if (st != noErr) {
        g_hotKeyRef = nullptr;
        if (error) {
            *error = st == eventHotKeyExistsErr
                         ? QStringLiteral("That hotkey is already registered by another application.")
                         : QStringLiteral("macOS refused the hotkey (error %1).").arg(st);
        }
        return false;
    }
    return true;
}

void unregisterHotkey() {
    if (g_hotKeyRef) {
        UnregisterEventHotKey(g_hotKeyRef);
        g_hotKeyRef = nullptr;
    }
}

bool appIsFrontmost() {
    return [NSApp isActive] && ![NSApp isHidden];
}

void hideApp(const QList<QWidget *> &) {
    // Hiding the whole app (like Cmd+H) hands focus back to the previous app.
    [NSApp hide:nil];
}

void activateApp(const QList<QWidget *> &windows, QWidget *preferred) {
    [NSApp unhide:nil];
    for (QWidget *w : windows) {
        if (w->isMinimized()) w->showNormal();
        else if (!w->isVisible()) w->show();
    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
    if (preferred) {
        preferred->raise();
        preferred->activateWindow();
    }
}

} // namespace GlobalHotkeyNative
