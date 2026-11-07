#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QtTest/QtTest>
#include "MenuStyle.h"
#include "Platform.h"
#include "TerminalView.h"

class TerminalViewTest : public QObject {
    Q_OBJECT

    static QString screenText(const TerminalView &v) {
        return v.screen().extractText(QPoint(0, 0), QPoint(v.cols(), v.screen().totalLines() - 1));
    }
    // Resize events are only delivered to shown widgets.
    static void sized(TerminalView &v, int w, int h) {
        v.show();
        v.resize(w, h);
    }
    static QByteArray keys(TerminalView &v, int key, Qt::KeyboardModifiers m = Qt::NoModifier, const QString &text = {}) {
        QKeyEvent e(QEvent::KeyPress, key, m, text);
        return v.encodeKey(&e);
    }

private slots:
    void keyEncoding() {
        TerminalView v;
        QCOMPARE(keys(v, Qt::Key_Up), QByteArrayLiteral("\x1b[A"));
        QCOMPARE(keys(v, Qt::Key_Left, Qt::ShiftModifier), QByteArrayLiteral("\x1b[1;2D"));
        QCOMPARE(keys(v, Qt::Key_Delete), QByteArrayLiteral("\x1b[3~"));
        QCOMPARE(keys(v, Qt::Key_F1), QByteArrayLiteral("\x1bOP"));
        QCOMPARE(keys(v, Qt::Key_F5), QByteArrayLiteral("\x1b[15~"));
        QCOMPARE(keys(v, Qt::Key_F12, Qt::ShiftModifier), QByteArrayLiteral("\x1b[24;2~"));
        QCOMPARE(keys(v, Qt::Key_Return), QByteArrayLiteral("\r"));
        QCOMPARE(keys(v, Qt::Key_Backspace), QByteArrayLiteral("\x7f"));
        QCOMPARE(keys(v, Qt::Key_Tab, Qt::ShiftModifier), QByteArrayLiteral("\x1b[Z"));
        QCOMPARE(keys(v, Qt::Key_A, Qt::NoModifier, "a"), QByteArrayLiteral("a"));
        QCOMPARE(keys(v, Qt::Key_E, Qt::AltModifier, "e"), QByteArrayLiteral("\x1b" "e"));
        QCOMPARE(keys(v, Qt::Key_Q, Qt::NoModifier, QString::fromUtf8("é")), QString::fromUtf8("é").toUtf8());
#ifdef Q_OS_MACOS
        const Qt::KeyboardModifier ctrl = Qt::MetaModifier;  // physical Control on macOS
#else
        const Qt::KeyboardModifier ctrl = Qt::ControlModifier;
#endif
        QCOMPARE(keys(v, Qt::Key_C, ctrl), QByteArray(1, '\x03'));
        QCOMPARE(keys(v, Qt::Key_Space, ctrl), QByteArray(1, '\0'));
    }

#ifndef Q_OS_MACOS
    void appShortcutsLeaveControlKeysToTheShell() {
        for (const char *shellKey : {"Ctrl+C", "Ctrl+V", "Ctrl+A", "Ctrl+K", "Ctrl+W", "Ctrl+T", "Ctrl+N", "Ctrl+B", "Ctrl+G", "Ctrl+F", "Ctrl+R", "Ctrl+M"}) {
            QVERIFY2(Platform::key(shellKey) != QKeySequence(QString::fromLatin1(shellKey)), shellKey);
        }
        QCOMPARE(Platform::key("Ctrl+C"), QKeySequence(QStringLiteral("Ctrl+Shift+C")));
        QVERIFY(Platform::key("Ctrl+M").isEmpty());
        QCOMPARE(Platform::menuText(QString::fromUtf8("Copy\t⌘C")), QStringLiteral("Copy\tCtrl+Shift+C"));
        QCOMPARE(Platform::menuText(QString::fromUtf8("Minimize\t⌘M")), QStringLiteral("Minimize"));
        QCOMPARE(Platform::hint(QStringLiteral("Ask AI (Cmd+G)")), QStringLiteral("Ask AI (Ctrl+Shift+I)"));
    }
#endif

#ifdef Q_OS_WIN
    void altGrTypesItsCharacter() {
        TerminalView v;
        const Qt::KeyboardModifiers altGr = Qt::ControlModifier | Qt::AltModifier;
        QCOMPARE(keys(v, Qt::Key_At, altGr, "@"), QByteArrayLiteral("@"));
        QCOMPARE(keys(v, Qt::Key_E, altGr, QString::fromUtf8("€")), QString::fromUtf8("€").toUtf8());
        QCOMPARE(keys(v, Qt::Key_C, Qt::ControlModifier), QByteArray(1, '\x03'));  // real Ctrl is untouched
    }

    void ctrlCCopiesASelectionThenInterrupts() {
        TerminalView v;
        sized(v, 400, 200);
        v.feedOutput("copy me\r\n");
        v.selectAllText();
        QApplication::clipboard()->clear();
        QKeyEvent press(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
        QApplication::sendEvent(&v, &press);
        QVERIFY(QApplication::clipboard()->text().contains("copy me"));
        QVERIFY(!v.hasSelection());  // the next Ctrl+C goes to the shell as an interrupt
    }

    void osc7WindowsPathHasNoLeadingSlash() {
        TerminalView v;
        QSignalSpy cwd(&v, &TerminalView::cwdChanged);
        v.feedOutput("\x1b]7;file:///C:/Users/a%20b\x07");
        QCOMPARE(cwd.value(0).value(0).toString(), QStringLiteral("C:/Users/a b"));
        QCOMPARE(v.currentDirectory(), QStringLiteral("C:/Users/a b"));
    }
#endif

    void applicationCursorKeys() {
        TerminalView v;
        v.feedOutput("\x1b[?1h");
        QCOMPARE(keys(v, Qt::Key_Up), QByteArrayLiteral("\x1bOA"));
    }

    void mouseTrackingAndSgrModes() {
        TerminalView v;
        QVERIFY(!v.screen().mouseTracking());
        QVERIFY(!v.screen().mouseSgr());
        QVERIFY(!v.screen().alternateScroll());

        v.feedOutput("\x1b[?1049h\x1b[?1000h\x1b[?1006h");
        QVERIFY(v.screen().usingAlternateScreen());
        QVERIFY(v.screen().mouseTracking());
        QVERIFY(v.screen().mouseSgr());
        QVERIFY(!v.screen().alternateScroll());

        v.feedOutput("\x1b[?1006l\x1b[?1000l\x1b[?1049l");
        QVERIFY(!v.screen().usingAlternateScreen());
        QVERIFY(!v.screen().mouseTracking());
        QVERIFY(!v.screen().mouseSgr());
    }

    void resizeChangesGrid() {
        TerminalView v;
        sized(v, 400, 200);
        const int c1 = v.cols(), r1 = v.rows();
        sized(v, 800, 400);
        QVERIFY(v.cols() > c1);
        QVERIFY(v.rows() > r1);
    }

    void urlDetection() {
        const QString line = "see https://example.com/a?b=1, and (http://x.org/y). done";
        QCOMPARE(TerminalView::urlAt(line, 6), QString("https://example.com/a?b=1"));
        QCOMPARE(TerminalView::urlAt(line, line.indexOf("http://") + 3), QString("http://x.org/y"));
        QCOMPARE(TerminalView::urlAt(line, 0), QString());
        QCOMPARE(TerminalView::urlAt(line, line.indexOf("and")), QString());
    }

    void signalsFromOutput() {
        TerminalView v;
        v.setBeepOnBell(false);
        QSignalSpy title(&v, &TerminalView::titleChanged), cwd(&v, &TerminalView::cwdChanged), bell(&v, &TerminalView::bell),
            out(&v, &TerminalView::output);
        v.feedOutput("\x1b]0;my title\x07\x1b]7;file:///tmp/x\x07\x07");
        QCOMPARE(title.value(0).value(0).toString(), QString("my title"));
        QCOMPARE(cwd.value(0).value(0).toString(), QString("/tmp/x"));
        QCOMPARE(bell.count(), 1);
        QCOMPARE(out.count(), 1);
    }

    void lineTextAndWideColumns() {
        TerminalView v;
        sized(v, 400, 200);
        v.feedOutput(QString::fromUtf8("a你b").toUtf8());
        QList<int> map;
        const QString t = v.lineText(v.screen().scrollbackCount(), &map).trimmed();
        QCOMPARE(t, QString::fromUtf8("a你b"));
        QCOMPARE(map.mid(0, 3), (QList<int>{0, 1, 3}));  // wide glyph occupies columns 1-2
    }

    void searchSelectsAndScrolls() {
        TerminalView v;
        sized(v, 400, 120);  // few rows, so history builds up
        for (int i = 0; i < 60; ++i) v.feedOutput(QStringLiteral("line %1 apple\r\n").arg(i).toUtf8());
        v.feedOutput("the needle is here\r\nand another Needle\r\n");
        QVERIFY(v.screen().scrollbackCount() > 0);

        QVERIFY(v.find("needle", true));          // backward from bottom: last match
        QCOMPARE(v.selectedText(), QString("Needle"));
        QVERIFY(v.find("needle", true));          // previous match
        QCOMPARE(v.selectedText(), QString("needle"));
        QVERIFY(v.find("needle", false));         // forward again
        QCOMPARE(v.selectedText(), QString("Needle"));
        QVERIFY(v.find("needle", false));         // wraps to the first
        QCOMPARE(v.selectedText(), QString("needle"));
        QVERIFY(!v.find("nothere"));
        QVERIFY(!v.find("needle", false, Qt::CaseSensitive) || v.selectedText() == "needle");

        QVERIFY(v.find("line 0 ", false));        // oldest line is in scrollback: view must scroll up
        QVERIFY(v.scrollOffset() > 0);
    }

    void selectionCopyAndPaste() {
        TerminalView v;
        sized(v, 400, 200);
        v.feedOutput("hello world\r\n");
        v.selectAllText();
        QVERIFY(v.hasSelection());
        v.copySelection();
        QVERIFY(QApplication::clipboard()->text().contains("hello world"));
        v.clearTerminal();
        QVERIFY(!v.hasSelection());
    }

    void wheelScrollsHistory() {
        TerminalView v;
        sized(v, 400, 120);
        for (int i = 0; i < 50; ++i) v.feedOutput(QStringLiteral("row %1\r\n").arg(i).toUtf8());
        QCOMPARE(v.scrollOffset(), 0);
        QWheelEvent up(QPointF(10, 10), QPointF(10, 10), QPoint(0, 0), QPoint(0, 240), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(&v, &up);
        QVERIFY(v.scrollOffset() > 0);
        const int off = v.scrollOffset();
        v.feedOutput("more\r\n");  // stays anchored while scrolled back
        QVERIFY(v.scrollOffset() >= off);
    }

    void realShellEchoAndExit() {
        TerminalView v;
        sized(v, 640, 320);
        QSignalSpy exited(&v, &TerminalView::exited);
        v.start(QDir::tempPath());
        QVERIFY(v.isRunning());
#ifdef Q_OS_WIN
        v.sendInput("echo tp-$(20+22)-done\r");
#else
        v.sendInput("echo tp-$((20+22))-done\r");
#endif
        QTRY_VERIFY_WITH_TIMEOUT(screenText(v).contains("tp-42-done"), 15000);
        v.sendInput("exit\r");
        QTRY_VERIFY_WITH_TIMEOUT(exited.count() == 1, 15000);
        QVERIFY(!v.isRunning());
    }

    void fullScreenProgramUsesAlternateScreen() {
#ifdef Q_OS_WIN
        QSKIP("needs `less`, which Windows does not ship");
#endif
        QTemporaryDir dir;
        QFile f(dir.filePath("pager.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        for (int i = 0; i < 200; ++i) f.write(QStringLiteral("pager-line-%1\n").arg(i).toUtf8());
        f.close();

        TerminalView v;
        sized(v, 640, 320);
        v.start(dir.path());
        v.sendInput("less pager.txt\r");
        QTRY_VERIFY_WITH_TIMEOUT(v.screen().usingAlternateScreen(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(screenText(v).contains("pager-line-0"), 15000);
        v.sendInput("G");  // jump to end
        QTRY_VERIFY_WITH_TIMEOUT(screenText(v).contains("pager-line-199"), 15000);
        v.sendInput("q");
        QTRY_VERIFY_WITH_TIMEOUT(!v.screen().usingAlternateScreen(), 15000);
    }

    void startBeforeSizedWaitsForResize() {
        TerminalView v;
        sized(v, 10, 10);  // too small for a grid: no PTY until it has a usable size
        v.start(QDir::tempPath());
        QVERIFY(!v.isRunning());
        v.resize(640, 320);
        QVERIFY(v.isRunning());
        // shell must see the grid size
#ifdef Q_OS_WIN
        v.sendInput("\"$($Host.UI.RawUI.WindowSize.Height) $($Host.UI.RawUI.WindowSize.Width)\"\r");
#else
        v.sendInput("stty size\r");
#endif
        QTRY_VERIFY_WITH_TIMEOUT(screenText(v).contains(QStringLiteral("%1 %2").arg(v.rows()).arg(v.cols())), 15000);
    }

    void destroyingRunningViewIsSafe() {
        for (int i = 0; i < 3; ++i) {
            auto *v = new TerminalView;
            v->show();
            v->resize(400, 200);
            v->start(QDir::tempPath());
            QVERIFY(v->isRunning());
            delete v;
        }
        QTest::qWait(300);  // let exit callbacks run against destroyed views
    }

    void menuStyleAndSplitSignals() {
        const auto &dark = MenuStyle::colors(true);
        QCOMPARE(dark.panel, QColor("#2b2d30"));
        QCOMPARE(dark.selFill, QColor("#2e436e"));
        QCOMPARE(dark.selBorder, QColor("#3574f0"));

        QIcon icon = MenuStyle::makeIcon(MenuIcons::kCopy, true);
        QVERIFY(!icon.isNull());
        QVERIFY(!MenuStyle::makeIcon(MenuIcons::kSplitRight, true).isNull());
        QVERIFY(!MenuStyle::makeIcon(MenuIcons::kSplitDown, true).isNull());

        TerminalView v;
        sized(v, 400, 200);

        QSignalSpy spySplit(&v, &TerminalView::splitRequested);
        emit v.splitRequested(true);
        QCOMPARE(spySplit.count(), 1);
        QCOMPARE(spySplit.at(0).at(0).toBool(), true);

        emit v.splitRequested(false);
        QCOMPARE(spySplit.count(), 2);
        QCOMPARE(spySplit.at(1).at(0).toBool(), false);

        QMenu menu;
        MenuStyle::apply(&menu, true);
        auto *copyAct = menu.addAction(MenuStyle::makeIcon(MenuIcons::kCopy, true), QStringLiteral("Copy\t⌘C"));
        copyAct->setEnabled(false);
        menu.addAction(MenuStyle::makeIcon(MenuIcons::kPaste, true), QStringLiteral("Paste\t⌘V"));
        menu.addSeparator();
        menu.addAction(MenuStyle::makeIcon(MenuIcons::kSelectAll, true), QStringLiteral("Select All\t⌘A"));
        menu.addAction(MenuStyle::makeIcon(MenuIcons::kClear, true), QStringLiteral("Clear Terminal\t⌘K"));
        menu.addSeparator();
        auto *splitRightAct = menu.addAction(MenuStyle::makeIcon(MenuIcons::kSplitRight, true), QStringLiteral("Split Right\t⌘⇧R"));
        menu.addAction(MenuStyle::makeIcon(MenuIcons::kSplitDown, true), QStringLiteral("Split Down\t⌘⇧D"));

        menu.ensurePolished();
        menu.adjustSize();
        const QSize s = menu.sizeHint();
        QVERIFY(s.width() >= 180);
        QVERIFY(s.height() > 100);

        QImage img(s, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        menu.render(&img);
        QVERIFY(!img.isNull());
        img.save(QDir::temp().filePath(QStringLiteral("menu_render.png")));

        menu.setActiveAction(splitRightAct);
        img.fill(Qt::transparent);
        menu.render(&img);
        img.save(QDir::temp().filePath(QStringLiteral("menu_render_selected.png")));

        // Render Tab Menu
        QMenu tabMenu;
        MenuStyle::apply(&tabMenu, true);
        tabMenu.addAction(MenuStyle::makeIcon(MenuIcons::kAdd, true), QStringLiteral("New Tab\t⌘T"));
        tabMenu.addAction(MenuStyle::makeIcon(MenuIcons::kWindow, true), QStringLiteral("New Window\t⌘N"));
        tabMenu.addSeparator();
        tabMenu.addAction(MenuStyle::makeIcon(MenuIcons::kRename, true), QStringLiteral("Rename Tab…"));
        tabMenu.addAction(MenuStyle::makeIcon(MenuIcons::kCopy, true), QStringLiteral("Duplicate Tab"));
        tabMenu.addSeparator();
        tabMenu.addAction(MenuStyle::makeIcon(MenuIcons::kClose, true), QStringLiteral("Close Tab\t⌘W"));
        tabMenu.addAction(QStringLiteral("Close Other Tabs"));
        tabMenu.addAction(QStringLiteral("Close All Tabs"));
        tabMenu.addSeparator();
        tabMenu.addAction(MenuStyle::makeIcon(MenuIcons::kSplitRight, true), QStringLiteral("Split Right\t⌘⇧R"));
        tabMenu.addAction(MenuStyle::makeIcon(MenuIcons::kSplitDown, true), QStringLiteral("Split Down\t⌘⇧D"));

        tabMenu.ensurePolished();
        tabMenu.adjustSize();
        const QSize ts = tabMenu.sizeHint();
        QImage tImg(ts, QImage::Format_ARGB32_Premultiplied);
        tImg.fill(Qt::transparent);
        tabMenu.render(&tImg);
        tImg.save(QDir::temp().filePath(QStringLiteral("tab_menu_render.png")));
    }
};

QTEST_MAIN(TerminalViewTest)
#include "terminalview_test.moc"
