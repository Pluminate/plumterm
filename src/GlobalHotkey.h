#pragma once
#include <QList>
#include <QObject>
#include <QString>

class QLocalServer;
class QWidget;

// A single system-wide hotkey that toggles PlumTerm's windows, plus a local
// IPC channel so `plumterm --toggle` can do the same (for Wayland, where apps
// can't grab keys globally and the user binds the command in their desktop).
//
// Backends: Carbon RegisterEventHotKey on macOS, XGrabKey on X11, none elsewhere.
class GlobalHotkey : public QObject {
    Q_OBJECT
public:
    struct Spec {
        bool cmd = false;   // macOS Command / Linux Super
        bool ctrl = false;
        bool alt = false;
        bool shift = false;
        int key = 0;        // Qt::Key
    };

    static GlobalHotkey *instance();

    // Accepts Electron-style ("CommandOrControl+Shift+T", "Cmd+3", "Alt+Space")
    // or Qt-style text. Empty string unregisters. On failure the previous
    // hotkey is left unregistered and *error explains why.
    bool setShortcut(const QString &text, QString *error = nullptr);
    QString shortcut() const { return m_text; }

    static bool parse(const QString &text, Spec *out, QString *error);

    // Show+focus PlumTerm if it's hidden or in the background, otherwise hide it.
    static void toggleWindows();

    // IPC for `plumterm --toggle`.
    static bool sendToggleToRunningInstance();
    void startToggleServer();

signals:
    void activated();

private:
    explicit GlobalHotkey(QObject *parent = nullptr);
    QString m_text;
    bool m_registered = false;
    QLocalServer *m_server = nullptr;
};

// Implemented per platform (GlobalHotkey_mac.mm / _x11.cpp / _none.cpp).
namespace GlobalHotkeyNative {
bool registerHotkey(const GlobalHotkey::Spec &spec, QString *error);
void unregisterHotkey();
bool appIsFrontmost();
void hideApp(const QList<QWidget *> &windows);
void activateApp(const QList<QWidget *> &windows, QWidget *preferred);
}
