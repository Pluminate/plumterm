#include <QtTest/QtTest>
#include "TerminalScreen.h"

// Escape-sequence fixtures for TerminalScreen (first five ported from pluminate's terminal_test.cpp).
class TerminalScreenTest : public QObject {
    Q_OBJECT
private slots:
    void basicTextAndCursorMovement() {
        TerminalScreen s(20, 5);
        s.feed("Hello\r\nWorld!");
        QCOMPARE(s.cursorRow(), 1);
        QCOMPARE(s.cursorCol(), 6);
        QCOMPARE(s.extractText(QPoint(0, 0), QPoint(20, 1)), QStringLiteral("Hello\nWorld!"));

        // Cursor up 1, column 1 (1-based CHA), overwrite "Hello" with "Plum "
        s.feed("\x1b[A\x1b[1GPlum ");
        QCOMPARE(s.extractText(QPoint(0, 0), QPoint(20, 1)), QStringLiteral("Plum\nWorld!"));
    }

    void sgrColorsAndAttributes() {
        TerminalScreen s(20, 4);
        s.feed("\x1b[1;31;48;5;240mA\x1b[38;2;10;20;30;4;9mB\x1b[0mC");
        const auto &row = s.gridRow(0);

        QVERIFY(row.cells[0].flags & TerminalScreen::Bold);
        QCOMPARE(row.cells[0].fg.kind, TerminalScreen::ColorKind::Indexed);
        QCOMPARE(row.cells[0].fg.index, uint8_t(1));
        QCOMPARE(row.cells[0].bg.kind, TerminalScreen::ColorKind::Indexed);
        QCOMPARE(row.cells[0].bg.index, uint8_t(240));

        QCOMPARE(row.cells[1].fg.kind, TerminalScreen::ColorKind::Rgb);
        QCOMPARE(row.cells[1].fg.r, uint8_t(10));
        QCOMPARE(row.cells[1].fg.g, uint8_t(20));
        QCOMPARE(row.cells[1].fg.b, uint8_t(30));
        QVERIFY(row.cells[1].flags & TerminalScreen::Underline);
        QVERIFY(row.cells[1].flags & TerminalScreen::Strikethrough);

        QCOMPARE(row.cells[2].fg.kind, TerminalScreen::ColorKind::DefaultFg);
        QCOMPARE(row.cells[2].bg.kind, TerminalScreen::ColorKind::DefaultBg);
        QCOMPARE(row.cells[2].flags, uint8_t(0));
    }

    void scrollbackAndErase() {
        TerminalScreen s(10, 3);
        s.feed("line1\r\nline2\r\nline3\r\nline4\r\nline5");
        QCOMPARE(s.scrollbackCount(), 2);
        QCOMPARE(s.extractText(QPoint(0, 0), QPoint(10, 4)), QStringLiteral("line1\nline2\nline3\nline4\nline5"));

        // Erase in display mode 2 clears visible grid but keeps scrollback
        s.feed("\x1b[2J\x1b[Htop");
        QCOMPARE(s.scrollbackCount(), 2);
        QCOMPARE(s.extractText(QPoint(0, 2), QPoint(10, 2)), QStringLiteral("top"));

        // Erase mode 3 clears scrollback
        s.feed("\x1b[3J");
        QCOMPARE(s.scrollbackCount(), 0);
    }

    void alternateScreenRestoresPrimary() {
        TerminalScreen s(15, 4);
        s.feed("shell prompt$ ");
        const int savedCol = s.cursorCol();
        QVERIFY(!s.usingAlternateScreen());

        // Enter alternate screen (?1049h), draw full-screen app content, then exit (?1049l)
        s.feed("\x1b[?1049h\x1b[1;1H--- VIM ---");
        QVERIFY(s.usingAlternateScreen());
        QCOMPARE(s.extractText(QPoint(0, 0), QPoint(15, 0)), QStringLiteral("--- VIM ---"));

        s.feed("\x1b[?1049l");
        QVERIFY(!s.usingAlternateScreen());
        QCOMPARE(s.cursorCol(), savedCol);
        QCOMPARE(s.extractText(QPoint(0, 0), QPoint(15, 0)), QStringLiteral("shell prompt$"));
    }

    void splitUtf8AndDeviceStatusReport() {
        TerminalScreen s(20, 4);
        QByteArray reply;
        s.setReplyCallback([&](const QByteArray &r) { reply = r; });

        // Feed 3-byte UTF-8 '✓' (0xE2 0x9C 0x93) one byte at a time
        const uint8_t b1 = 0xe2, b2 = 0x9c, b3 = 0x93;
        s.feed(&b1, 1);
        s.feed(&b2, 1);
        s.feed(&b3, 1);
        QCOMPARE(s.gridRow(0).cells[0].ch, U'✓');
        QCOMPARE(s.cursorCol(), 1);

        // DSR cursor position query (\x1b[6n) -> row 1, col 2 (1-based)
        s.feed("\x1b[6n");
        QCOMPARE(reply, QByteArrayLiteral("\x1b[1;2R"));
    }

    void oscTitleAndCwd() {
        TerminalScreen s(20, 4);
        QStringList titles, cwds;
        s.setTitleCallback([&](const QString &t) { titles << t; });
        s.setCwdCallback([&](const QString &c) { cwds << c; });
        s.feed("\x1b]0;hello world\x07\x1b]2;second\x1b\\");               // BEL and ST terminators
        s.feed("\x1b]7;file://host/Users/me/My%20Dir\x07");
        s.feed("\x1b]1;ignored\x07\x1b]8;;http://x\x07");
        QCOMPARE(titles, (QStringList{"hello world", "second"}));
#ifdef Q_OS_WIN
        QCOMPARE(cwds, (QStringList{"//host/Users/me/My Dir"}));  // a host means a network path here
#else
        QCOMPARE(cwds, (QStringList{"/Users/me/My Dir"}));
#endif
        s.feed("ok");                                                            // parser recovered
        QCOMPARE(s.extractText(QPoint(0, 0), QPoint(20, 0)), QStringLiteral("ok"));
    }

    void bellOnlyOutsideOsc() {
        TerminalScreen s(20, 4);
        int bells = 0;
        s.setBellCallback([&] { ++bells; });
        s.feed("a\x07" "b");
        QCOMPARE(bells, 1);
        s.feed("\x1b]0;t\x07");  // BEL here terminates the OSC, it is not a bell
        QCOMPARE(bells, 1);
    }

    void wideCharsAndTrueColor() {
        TerminalScreen s(10, 2);
        s.feed("\xe4\xbd\xa0x");  // 你 (wide) then x
        QVERIFY(s.gridRow(0).cells[0].flags & TerminalScreen::Wide);
        QCOMPARE(s.cursorCol(), 3);
        QCOMPARE(s.gridRow(0).cells[2].ch, U'x');
    }
};

QTEST_MAIN(TerminalScreenTest)
#include "terminal_screen_test.moc"
