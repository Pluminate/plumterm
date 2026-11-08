// Fallback for platforms without a native backend (Windows, Linux built
// without libX11). `plumterm --toggle` still works through the IPC server.
#include "GlobalHotkey.h"
#include <QGuiApplication>
#include <QWidget>

namespace GlobalHotkeyNative {

bool registerHotkey(const GlobalHotkey::Spec &, QString *error) {
    if (error) {
        *error = QStringLiteral("Global hotkeys aren't supported in this build. Bind a system shortcut to:  plumterm --toggle");
    }
    return false;
}

void unregisterHotkey() {}

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
