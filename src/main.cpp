#include <QApplication>
#include <QDir>
#include <QPixmap>
#include <QTimer>
#include "CoreBridge.h"
#include "MainWindow.h"
#include "MenuStyle.h"
#include "TerminalPane.h"
#include "TerminalView.h"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("PlumTerm");
    MenuStyle::installGlobalHook();

    auto *w = new MainWindow();
    w->setAttribute(Qt::WA_DeleteOnClose);
    const QStringList args = app.arguments();
    const QString initCwd = args.size() > 1 ? args.at(1) : QDir::currentPath();
    if (!w->restoreSession()) {
        w->newTab(initCwd);
    }

    if (qEnvironmentVariableIsEmpty("TP_HEADLESS_EXIT")) {
        w->show();

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
