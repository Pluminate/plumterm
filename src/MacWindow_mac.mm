#include "MacWindow.h"
#include <QGuiApplication>
#include <QWidget>

#import <AppKit/AppKit.h>

void applyMacTitleBar(QWidget *window, bool dark) {
    if (!window) return;
    // Skip when running under headless/offscreen QPA platforms (e.g. unit/smoke tests).
    if (QGuiApplication::platformName() != QLatin1String("cocoa")) return;

    NSView *view = reinterpret_cast<NSView *>(window->winId());
    if (!view || ![view isKindOfClass:[NSView class]]) return;

    NSWindow *nsWin = [view window];
    if (!nsWin) return;

    nsWin.styleMask |= NSWindowStyleMaskFullSizeContentView;
    nsWin.titlebarAppearsTransparent = YES;
    nsWin.titleVisibility = NSWindowTitleHidden;
    nsWin.movableByWindowBackground = NO;
    nsWin.appearance = [NSAppearance appearanceNamed:(dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua)];
}
