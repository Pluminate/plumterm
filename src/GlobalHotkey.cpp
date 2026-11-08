#include "GlobalHotkey.h"
#include <QApplication>
#include <QKeySequence>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QWidget>

namespace {

QString serverName() {
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty()) user = qEnvironmentVariable("USERNAME");
    return QStringLiteral("plumterm-toggle-") + user;
}

QPointer<QWidget> s_lastActive;

QList<QWidget *> mainWindows() {
    QList<QWidget *> out;
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (w->inherits("MainWindow")) out.append(w);
    }
    return out;
}

} // namespace

GlobalHotkey::GlobalHotkey(QObject *parent) : QObject(parent) {
    connect(this, &GlobalHotkey::activated, this, &GlobalHotkey::toggleWindows);
}

GlobalHotkey *GlobalHotkey::instance() {
    static GlobalHotkey *inst = new GlobalHotkey(qApp);
    return inst;
}

bool GlobalHotkey::parse(const QString &text, Spec *out, QString *error) {
    Spec spec;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (error) *error = QStringLiteral("Hotkey is empty.");
        return false;
    }

    // Split on '+', but let a trailing "++" mean the Plus key.
    QStringList parts = trimmed.split(QLatin1Char('+'));
    if (trimmed.endsWith(QLatin1String("++"))) {
        parts.removeLast();
        parts.last() = QStringLiteral("+");
    }

    QString keyToken;
    for (const QString &raw : parts) {
        const QString tok = raw.trimmed();
        const QString t = tok.toLower();
        if (t.isEmpty()) {
            if (error) *error = QStringLiteral("Malformed hotkey \"%1\".").arg(text);
            return false;
        }
        if (t == QLatin1String("cmd") || t == QLatin1String("command") || t == QLatin1String("meta") ||
            t == QLatin1String("super") || t == QLatin1String("win")) {
            spec.cmd = true;
        } else if (t == QLatin1String("commandorcontrol") || t == QLatin1String("cmdorctrl")) {
#ifdef Q_OS_MACOS
            spec.cmd = true;
#else
            spec.ctrl = true;
#endif
        } else if (t == QLatin1String("ctrl") || t == QLatin1String("control")) {
            spec.ctrl = true;
        } else if (t == QLatin1String("alt") || t == QLatin1String("option") || t == QLatin1String("opt")) {
            spec.alt = true;
        } else if (t == QLatin1String("shift")) {
            spec.shift = true;
        } else {
            if (!keyToken.isEmpty()) {
                if (error) *error = QStringLiteral("Hotkey \"%1\" has more than one key.").arg(text);
                return false;
            }
            keyToken = tok;
        }
    }

    if (keyToken.isEmpty()) {
        if (error) *error = QStringLiteral("Hotkey \"%1\" has no key, only modifiers.").arg(text);
        return false;
    }

    if (keyToken.compare(QLatin1String("plus"), Qt::CaseInsensitive) == 0) keyToken = QStringLiteral("+");
    if (keyToken.compare(QLatin1String("enter"), Qt::CaseInsensitive) == 0) keyToken = QStringLiteral("Return");
    const QKeySequence seq = QKeySequence::fromString(keyToken, QKeySequence::PortableText);
    if (seq.count() != 1 || seq[0].keyboardModifiers() != Qt::NoModifier || seq[0].key() == Qt::Key_unknown) {
        if (error) *error = QStringLiteral("Unknown key \"%1\".").arg(keyToken);
        return false;
    }
    spec.key = seq[0].key();

    const bool isFKey = spec.key >= Qt::Key_F1 && spec.key <= Qt::Key_F35;
    if (!isFKey && !spec.cmd && !spec.ctrl && !spec.alt && !spec.shift) {
        if (error) *error = QStringLiteral("A global hotkey needs at least one modifier (Cmd, Ctrl, Alt or Shift).");
        return false;
    }

    if (out) *out = spec;
    return true;
}

bool GlobalHotkey::setShortcut(const QString &text, QString *error) {
    if (m_registered) {
        GlobalHotkeyNative::unregisterHotkey();
        m_registered = false;
    }
    m_text = text.trimmed();
    if (m_text.isEmpty()) return true;

    Spec spec;
    if (!parse(m_text, &spec, error)) return false;
    m_registered = GlobalHotkeyNative::registerHotkey(spec, error);
    return m_registered;
}

void GlobalHotkey::toggleWindows() {
    const QList<QWidget *> wins = mainWindows();
    if (wins.isEmpty()) return;

    bool anyShown = false;
    for (QWidget *w : wins) {
        if (w->isVisible() && !w->isMinimized()) anyShown = true;
    }

    if (anyShown && GlobalHotkeyNative::appIsFrontmost()) {
        QWidget *active = QApplication::activeWindow();
        if (active && active->inherits("MainWindow")) s_lastActive = active;
        GlobalHotkeyNative::hideApp(wins);
    } else {
        QWidget *preferred = s_lastActive && wins.contains(s_lastActive.data()) ? s_lastActive.data() : wins.first();
        GlobalHotkeyNative::activateApp(wins, preferred);
    }
}

bool GlobalHotkey::sendToggleToRunningInstance() {
    QLocalSocket sock;
    sock.connectToServer(serverName());
    if (!sock.waitForConnected(500)) return false;
    sock.write("toggle\n");
    sock.flush();
    sock.waitForBytesWritten(500);
    sock.disconnectFromServer();
    return true;
}

void GlobalHotkey::startToggleServer() {
    if (m_server) return;
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(serverName())) {
        // Another live instance owns the name: leave it alone. Otherwise the
        // socket file is stale (crash), so reclaim it.
        QLocalSocket probe;
        probe.connectToServer(serverName());
        if (probe.waitForConnected(200)) {
            probe.disconnectFromServer();
            delete m_server;
            m_server = nullptr;
            return;
        }
        QLocalServer::removeServer(serverName());
        if (!m_server->listen(serverName())) {
            qWarning("GlobalHotkey: could not start toggle server: %s", qPrintable(m_server->errorString()));
            delete m_server;
            m_server = nullptr;
            return;
        }
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *c = m_server->nextPendingConnection()) {
            connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
            connect(c, &QLocalSocket::readyRead, this, [c] {
                while (c->canReadLine()) {
                    if (c->readLine().trimmed() == "toggle") toggleWindows();
                }
            });
        }
    });
}
