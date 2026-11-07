#pragma once
#include <QFont>
#include <QPoint>
#include <QTimer>
#include <QWidget>

#include "TerminalScreen.h"

// One terminal: a TerminalScreen painted by QPainter, wired to a Rust PTY session through CoreBridge.
// PTY callbacks arrive on a reader thread and are re-posted to the GUI thread.
class TerminalView : public QWidget {
    Q_OBJECT
public:
    explicit TerminalView(bool dark = true, QWidget *parent = nullptr);
    ~TerminalView() override;

    // Spawn the shell in `cwd` (empty = home). The PTY is created as soon as the widget has a usable size.
    void start(const QString &cwd = {});
    bool isRunning() const { return m_pty != 0; }

    TerminalScreen &screen() { return m_screen; }
    const TerminalScreen &screen() const { return m_screen; }
    int cols() const { return m_screen.cols(); }
    int rows() const { return m_screen.rows(); }

    void feedOutput(const QByteArray &bytes);  // PTY output (also usable without a PTY, e.g. tests)
    void sendInput(const QByteArray &bytes);   // bytes typed/pasted by the user -> PTY
    QByteArray encodeKey(QKeyEvent *e) const;

    void setDark(bool dark);
    void setTermFont(const QFont &font);
    void setBeepOnBell(bool on) { m_beepOnBell = on; }
    void setBackground(const QColor &bg) { m_bgOverride = bg; update(); }  // invalid = theme default
    QString currentDirectory() const;  // shell's cwd (from the OS process, or its own report on Windows), empty if unknown
    void copySelection();
    void pasteClipboard();
    void selectAllText();
    void clearTerminal();
    bool hasSelection() const { return m_hasSelection; }
    QString selectedText() const;
    int scrollOffset() const { return m_scrollOffset; }

    // Search scrollback + screen; selects and scrolls to the match. Wraps around once.
    bool find(const QString &needle, bool backward = false, Qt::CaseSensitivity cs = Qt::CaseInsensitive);

    // Plain text of one line (history + grid index). colOfUnit (optional) maps each UTF-16 unit to its column.
    QString lineText(int lineIdx, QList<int> *colOfUnit = nullptr) const;
    // URL covering `col` in `line`, or empty.
    static QString urlAt(const QString &line, int col, int *startUnit = nullptr, int *endUnit = nullptr);

signals:
    void titleChanged(const QString &title);
    void cwdChanged(const QString &cwd);
    void bell();
    void exited();
    void output(const QByteArray &bytes);  // raw PTY output, for mirroring
    void activated();
    void splitRequested(bool right);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void inputMethodEvent(QInputMethodEvent *e) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    bool focusNextPrevChild(bool next) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    struct Ctx;  // heap state handed to the PTY callbacks
    static void onData(void *user, quint64 id, const quint8 *data, size_t len);
    static void onExit(void *user, quint64 id);

    void updateMetrics();
    void recalculateGridSize();
    void spawnIfReady();
    QPoint pixelToCell(const QPointF &pos) const;
    bool isCellSelected(int lineIdx, int col) const;
    QString urlUnder(const QPointF &pos) const;
    void scrollToLine(int lineIdx);

    TerminalScreen m_screen;
    QFont m_font;
    qreal m_cellW = 8.0, m_cellH = 16.0, m_ascent = 12.0;

    bool m_dark = true;
    bool m_beepOnBell = true;
    QColor m_bgOverride;
    bool m_cursorBlinkOn = true;
    QTimer m_blinkTimer;

    QString m_pendingCwd;
    QString m_reportedCwd;  // latest OSC 7 from the shell
    bool m_startRequested = false;
    quint64 m_pty = 0;
    Ctx *m_ctx = nullptr;

    int m_scrollOffset = 0;  // 0 = live; >0 = lines scrolled back
    qreal m_wheelCarry = 0.0;

    bool m_selecting = false;
    bool m_hasSelection = false;
    QPoint m_selAnchor, m_selActive;  // x = col, y = line index in [0, totalLines)
};
