#include <QApplication>
#include <QDir>
#include <QJsonObject>
#include <QPixmap>
#include <QTimer>
#include "CoreBridge.h"
#include "GlobalHotkey.h"
#include "MainWindow.h"
#include "MenuStyle.h"
#include "TerminalPane.h"
#include "TerminalView.h"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("PlumTerm");
    MenuStyle::installGlobalHook();

    QStringList args = app.arguments();
    const bool headless = !qEnvironmentVariableIsEmpty("TP_HEADLESS_EXIT");
    // `plumterm --toggle`: show/hide an already-running instance (bind this in
    // your desktop's keyboard settings where global hotkeys aren't possible,
    // e.g. Wayland). With no instance running, fall through and launch.
    if (args.removeAll(QStringLiteral("--toggle")) > 0 && GlobalHotkey::sendToggleToRunningInstance()) {
        return 0;
    }

    auto *w = new MainWindow();
    w->setAttribute(Qt::WA_DeleteOnClose);
    const QString initCwd = args.size() > 1 ? args.at(1) : QDir::currentPath();
    if (!w->restoreSession()) {
        w->newTab(initCwd);
    }

    if (!headless) {
        w->show();

        GlobalHotkey *hotkey = GlobalHotkey::instance();
        hotkey->startToggleServer();
        QString hotkeyError;
        const QString hotkeyText = CoreBridge::settings().toObject().value(QStringLiteral("globalHotkey")).toString();
        if (!hotkey->setShortcut(hotkeyText, &hotkeyError)) {
            qWarning("Global hotkey \"%s\" not registered: %s", qPrintable(hotkeyText), qPrintable(hotkeyError));
        }

        if (!qEnvironmentVariableIsEmpty("TP_SPLIT_2X2")) {
            QTimer::singleShot(150, w, [w] {
                w->splitActivePane(true);  // split right
                QTimer::singleShot(150, w, [w] {
                    w->splitActivePane(false); // split down right
                    QTimer::singleShot(150, w, [w] {
                        w->splitPaneAt(0, false); // split down left
                    });
                });
            });
        }

        if (!qEnvironmentVariableIsEmpty("TP_OPEN_SEARCH")) {
            QTimer::singleShot(600, w, [w] {
                if (w->activePane()) w->activePane()->toggleSearch();
            });
        }

        if (!qEnvironmentVariableIsEmpty("TP_OPEN_AI")) {
            QTimer::singleShot(600, w, [w] {
                if (w->activePane()) w->activePane()->toggleAi();
            });
        }

        if (const QByteArray shot = qgetenv("TP_SHOT"); !shot.isEmpty()) {
            if (const QByteArray keys = qgetenv("TP_KEYS"); !keys.isEmpty()) {
                QTimer::singleShot(1000, w, [w, keys] {
                    if (w->activePane() && w->activePane()->view()) {
                        w->activePane()->view()->sendInput(QByteArray::fromPercentEncoding(keys));
                    }
                });
            }
            const int waitMs = qEnvironmentVariableIntValue("TP_SHOT_MS") ? qEnvironmentVariableIntValue("TP_SHOT_MS") : 2500;
            QTimer::singleShot(waitMs, &app, [&app, w, shot] {
                w->grab().save(QString::fromLocal8Bit(shot));
                app.quit();
            });
        }
    } else {
        delete w;
        return 0;
    }

    return app.exec();
}
