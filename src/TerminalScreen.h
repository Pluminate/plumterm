#pragma once

#include <QColor>
#include <QByteArray>
#include <QPoint>
#include <QString>
#include <QVector>
#include <cstdint>
#include <functional>

// Pure VT100/xterm screen state machine: primary + alternate screen buffers, 5,000-line scrollback,
// SGR (bold/dim/italic/underline/inverse/strikethrough/hidden, 16-color ANSI, 256-color indexed,
// 24-bit RGB truecolor), cursor save/restore, scroll regions (DECSTBM), application cursor keys,
// bracketed paste, and device status responses.
class TerminalScreen {
public:
    enum StyleFlag : uint8_t {
        Bold          = 1 << 0,
        Dim           = 1 << 1,
        Italic        = 1 << 2,
        Underline     = 1 << 3,
        Inverse       = 1 << 4,
        Hidden        = 1 << 5,
        Strikethrough = 1 << 6,
        Wide          = 1 << 7,   // leading cell of a 2-column glyph; trailing cell has ch == 0
    };

    enum class ColorKind : uint8_t {
        DefaultFg,
        DefaultBg,
        Indexed,   // 0..255
        Rgb,       // 24-bit r, g, b
    };

    struct ColorSpec {
        ColorKind kind = ColorKind::DefaultFg;
        uint8_t index = 0;
        uint8_t r = 0, g = 0, b = 0;

        bool operator==(const ColorSpec &) const = default;
    };

    struct Cell {
        char32_t ch = U' ';
        ColorSpec fg{ColorKind::DefaultFg, 0, 0, 0, 0};
        ColorSpec bg{ColorKind::DefaultBg, 0, 0, 0, 0};
        uint8_t flags = 0;

        bool operator==(const Cell &) const = default;
    };

    struct Line {
        QVector<Cell> cells;
        bool wrapped = false;
    };

    static constexpr int kMaxScrollback = 5000;

    explicit TerminalScreen(int cols = 80, int rows = 24);

    void resize(int cols, int rows);
    void reset();

    // Feeds raw UTF-8/VT bytes from the PTY into the screen state machine.
    void feed(const uint8_t *data, size_t len);
    void feed(const QByteArray &bytes) {
        feed(reinterpret_cast<const uint8_t *>(bytes.constData()), size_t(bytes.size()));
    }

    void setReplyCallback(std::function<void(const QByteArray &)> cb) { m_reply = std::move(cb); }
    // OSC 0/2 window title, OSC 7 working directory (decoded local path), and BEL.
    void setTitleCallback(std::function<void(const QString &)> cb) { m_onTitle = std::move(cb); }
    void setCwdCallback(std::function<void(const QString &)> cb) { m_onCwd = std::move(cb); }
    void setBellCallback(std::function<void()> cb) { m_onBell = std::move(cb); }

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }
    int cursorRow() const { return m_curRow; }
    int cursorCol() const { return m_curCol; }
    bool cursorVisible() const { return m_cursorVisible; }
    bool applicationCursorKeys() const { return m_appCursorKeys; }
    bool bracketedPaste() const { return m_bracketedPaste; }
    bool usingAlternateScreen() const { return m_altActive; }

    int scrollbackCount() const { return m_altActive ? 0 : int(m_scrollback.size()); }

    // Line access across scrollback + active grid:
    // index 0 .. scrollbackCount()-1 = scrollback lines (oldest to newest),
    // index scrollbackCount() .. totalLines()-1 = visible grid rows (0 .. rows()-1).
    int totalLines() const { return scrollbackCount() + m_rows; }
    const Line &lineAt(int historyAndGridRow) const;
    const Line &gridRow(int row) const { return activeGrid()[std::clamp(row, 0, m_rows - 1)]; }

    void clearScrollback();

    // Extracts plain text between two (lineIndex, col) coordinates in [0 .. totalLines()-1].
    QString extractText(QPoint start, QPoint end) const;

    // Resolves a ColorSpec to a concrete QColor for the given theme (dark or light).
    static QColor resolveColor(const ColorSpec &spec, bool dark, bool isForeground);
    static int charDisplayWidth(char32_t cp);

private:
    struct SavedCursor {
        int row = 0;
        int col = 0;
        ColorSpec fg{ColorKind::DefaultFg, 0, 0, 0, 0};
        ColorSpec bg{ColorKind::DefaultBg, 0, 0, 0, 0};
        uint8_t flags = 0;
        bool wrapPending = false;
    };

    enum class State : uint8_t {
        Ground,
        Escape,
        EscapeIntermediate,
        CsiEntry,
        OscString,
        OscEscape,
        IgnoreString,
        IgnoreEscape,
    };

    QVector<Line> &activeGrid() { return m_altActive ? m_altGrid : m_priGrid; }
    const QVector<Line> &activeGrid() const { return m_altActive ? m_altGrid : m_priGrid; }
    Cell blankCell() const;
    Line makeLine(int cols) const;

    void processCodepoint(char32_t cp);
    void putChar(char32_t cp);
    void newLine();
    void reverseIndex();
    void scrollUp(int top, int bottom, int count);
    void scrollDown(int top, int bottom, int count);
    void handleCsi(char finalByte);
    void handleSgr(const QByteArray &params);
    void handlePrivateMode(const QByteArray &params, bool set);
    void enterAlternateScreen(bool clearAlt);
    void leaveAlternateScreen();

    int m_cols = 80;
    int m_rows = 24;
    QVector<Line> m_priGrid;
    QVector<Line> m_altGrid;
    QVector<Line> m_scrollback;
    bool m_altActive = false;

    int m_curRow = 0;
    int m_curCol = 0;
    bool m_wrapPending = false;
    int m_scrollTop = 0;
    int m_scrollBottom = 23;

    ColorSpec m_fg{ColorKind::DefaultFg, 0, 0, 0, 0};
    ColorSpec m_bg{ColorKind::DefaultBg, 0, 0, 0, 0};
    uint8_t m_flags = 0;
    char32_t m_lastPrintedChar = U' ';

    SavedCursor m_savedPri;
    SavedCursor m_savedAlt;

    bool m_cursorVisible = true;
    bool m_appCursorKeys = false;
    bool m_autowrap = true;
    bool m_bracketedPaste = false;

    State m_state = State::Ground;
    QByteArray m_csiBuf;
    QByteArray m_oscBuf;

    // Streaming UTF-8 decoder state
    uint32_t m_utf8Acc = 0;
    int m_utf8Need = 0;

    std::function<void(const QByteArray &)> m_reply;
    std::function<void(const QString &)> m_onTitle;
    std::function<void(const QString &)> m_onCwd;
    std::function<void()> m_onBell;
    void dispatchOsc();
};
