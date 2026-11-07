#include "TerminalScreen.h"

#include <QChar>
#include <QUrl>
#include <algorithm>

namespace {

// Standard 16-color ANSI palettes (dark matches xterm/OneDark used in Pluminate; light is high-contrast).
constexpr QRgb kAnsiDark[16] = {
    0xff1e1f22, 0xffe06c75, 0xff98c379, 0xffe5c07b, 0xff61afef, 0xffc678dd, 0xff56b6c2, 0xffabb2bf,
    0xff5c6370, 0xffff7b86, 0xffb5e890, 0xffffd88a, 0xff7ec0ff, 0xffe08bff, 0xff6fd5e0, 0xffffffff,
};

constexpr QRgb kAnsiLight[16] = {
    0xff1f2328, 0xffcf222e, 0xff1a7f37, 0xff9a6700, 0xff0969da, 0xff8250df, 0xff1b7c83, 0xff6e7781,
    0xff57606a, 0xffa40e26, 0xff116329, 0xff7d4e00, 0xff0550ae, 0xff6639ba, 0xff136067, 0xff24292f,
};

QRgb indexedColor256(uint8_t idx, bool dark) {
    if (idx < 16) return dark ? kAnsiDark[idx] : kAnsiLight[idx];
    if (idx < 232) {
        const int n = idx - 16;
        const int r = (n / 36) % 6;
        const int g = (n / 6) % 6;
        const int b = n % 6;
        auto comp = [](int v) { return v == 0 ? 0 : 55 + v * 40; };
        return qRgb(comp(r), comp(g), comp(b));
    }
    const int gray = 8 + (idx - 232) * 10;
    return qRgb(gray, gray, gray);
}

QVector<int> parseCsiParams(const QByteArray &raw) {
    QVector<int> out;
    int i = 0;
    if (i < raw.size() && (raw[i] == '?' || raw[i] == '>' || raw[i] == '=' || raw[i] == '!')) ++i;
    int cur = -1;
    for (; i < raw.size(); ++i) {
        const char c = raw[i];
        if (c >= '0' && c <= '9') {
            cur = (cur < 0 ? 0 : cur * 10) + (c - '0');
        } else if (c == ';' || c == ':') {
            out.append(cur < 0 ? 0 : cur);
            cur = -1;
        }
    }
    if (cur >= 0 || !raw.isEmpty()) out.append(cur < 0 ? 0 : cur);
    return out;
}

}  // namespace

TerminalScreen::TerminalScreen(int cols, int rows) {
    m_cols = std::max(2, cols);
    m_rows = std::max(1, rows);
    reset();
}

TerminalScreen::Cell TerminalScreen::blankCell() const {
    Cell c;
    c.ch = U' ';
    c.fg = {ColorKind::DefaultFg, 0, 0, 0, 0};
    c.bg = m_bg;
    c.flags = 0;
    return c;
}

TerminalScreen::Line TerminalScreen::makeLine(int cols) const {
    Line l;
    l.cells.fill(blankCell(), cols);
    l.wrapped = false;
    return l;
}

void TerminalScreen::reset() {
    m_fg = {ColorKind::DefaultFg, 0, 0, 0, 0};
    m_bg = {ColorKind::DefaultBg, 0, 0, 0, 0};
    m_flags = 0;
    m_altActive = false;
    m_priGrid.clear();
    m_altGrid.clear();
    for (int r = 0; r < m_rows; ++r) {
        m_priGrid.append(makeLine(m_cols));
        m_altGrid.append(makeLine(m_cols));
    }
    m_scrollback.clear();
    m_curRow = 0;
    m_curCol = 0;
    m_wrapPending = false;
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    m_cursorVisible = true;
    m_appCursorKeys = false;
    m_autowrap = true;
    m_bracketedPaste = false;
    m_mouseTracking = 0;
    m_mouseSgr = false;
    m_alternateScroll = false;
    m_savedPri = {};
    m_savedAlt = {};
    m_state = State::Ground;
    m_csiBuf.clear();
    m_oscBuf.clear();
    m_utf8Acc = 0;
    m_utf8Need = 0;
}

void TerminalScreen::resize(int cols, int rows) {
    cols = std::max(2, cols);
    rows = std::max(1, rows);
    if (cols == m_cols && rows == m_rows) return;

    auto resizeGrid = [&](QVector<Line> &grid, bool isPrimary) {
        for (Line &l : grid) {
            if (l.cells.size() < cols) {
                const int add = cols - int(l.cells.size());
                Cell blank;
                for (int i = 0; i < add; ++i) l.cells.append(blank);
            } else if (l.cells.size() > cols) {
                l.cells.resize(cols);
            }
        }
        if (rows < grid.size()) {
            const int drop = int(grid.size()) - rows;
            // On primary screen, push top lines into scrollback if cursor is below them
            if (isPrimary && !m_altActive) {
                const int pushToHistory = std::clamp(m_curRow - (rows - 1), 0, drop);
                for (int i = 0; i < pushToHistory; ++i) {
                    m_scrollback.append(grid.takeFirst());
                    if (m_scrollback.size() > kMaxScrollback) m_scrollback.removeFirst();
                }
                m_curRow = std::max(0, m_curRow - pushToHistory);
            }
            while (grid.size() > rows) grid.removeLast();
        } else if (rows > grid.size()) {
            while (grid.size() < rows) grid.append(makeLine(cols));
        }
    };

    resizeGrid(m_priGrid, true);
    resizeGrid(m_altGrid, false);
    m_cols = cols;
    m_rows = rows;
    m_curRow = std::clamp(m_curRow, 0, m_rows - 1);
    m_curCol = std::clamp(m_curCol, 0, m_cols - 1);
    m_wrapPending = false;
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
}

const TerminalScreen::Line &TerminalScreen::lineAt(int historyAndGridRow) const {
    const int sb = scrollbackCount();
    if (historyAndGridRow < sb) return m_scrollback[std::max(0, historyAndGridRow)];
    const int gr = std::clamp(historyAndGridRow - sb, 0, m_rows - 1);
    return activeGrid()[gr];
}

void TerminalScreen::clearScrollback() {
    m_scrollback.clear();
}

int TerminalScreen::charDisplayWidth(char32_t cp) {
    if (cp < 0x20 || (cp >= 0x7f && cp < 0xa0)) return 0;
    // Combining marks (zero width)
    if ((cp >= 0x0300 && cp <= 0x036f) || (cp >= 0x1ab0 && cp <= 0x1aff) ||
        (cp >= 0x1dc0 && cp <= 0x1dff) || (cp >= 0x20d0 && cp <= 0x20ff) ||
        (cp >= 0xfe00 && cp <= 0xfe0f) || (cp >= 0xfe20 && cp <= 0xfe2f))
        return 0;
    // East Asian Wide / Fullwidth & common emoji blocks
    if (cp >= 0x1100 &&
        (cp <= 0x115f || cp == 0x2329 || cp == 0x232a ||
         (cp >= 0x2e80 && cp <= 0xa4cf && cp != 0x303f) ||
         (cp >= 0xac00 && cp <= 0xd7a3) ||
         (cp >= 0xf900 && cp <= 0xfaff) ||
         (cp >= 0xfe10 && cp <= 0xfe19) ||
         (cp >= 0xfe30 && cp <= 0xfe6f) ||
         (cp >= 0xff00 && cp <= 0xff60) ||
         (cp >= 0xffe0 && cp <= 0xffe6) ||
         (cp >= 0x1f300 && cp <= 0x1faff) ||
         (cp >= 0x20000 && cp <= 0x3fffd)))
        return 2;
    return 1;
}

QColor TerminalScreen::resolveColor(const ColorSpec &spec, bool dark, bool isForeground) {
    switch (spec.kind) {
    case ColorKind::DefaultFg:
        return dark ? QColor(0xdf, 0xe1, 0xe5) : QColor(0x1f, 0x23, 0x28);
    case ColorKind::DefaultBg:
        return isForeground
                   ? (dark ? QColor(0xdf, 0xe1, 0xe5) : QColor(0x1f, 0x23, 0x28))
                   : (dark ? QColor(0x1e, 0x1f, 0x22) : QColor(0xff, 0xff, 0xff));
    case ColorKind::Indexed:
        return QColor::fromRgb(indexedColor256(spec.index, dark));
    case ColorKind::Rgb:
        return QColor(spec.r, spec.g, spec.b);
    }
    return QColor(0xdf, 0xe1, 0xe5);
}

QString TerminalScreen::extractText(QPoint start, QPoint end) const {
    if (start.y() > end.y() || (start.y() == end.y() && start.x() > end.x())) std::swap(start, end);
    const int total = totalLines();
    if (total <= 0) return {};
    start.setY(std::clamp(start.y(), 0, total - 1));
    end.setY(std::clamp(end.y(), 0, total - 1));
    start.setX(std::clamp(start.x(), 0, m_cols));
    end.setX(std::clamp(end.x(), 0, m_cols));

    QString out;
    for (int y = start.y(); y <= end.y(); ++y) {
        const Line &ln = lineAt(y);
        const int c0 = (y == start.y()) ? start.x() : 0;
        const int c1 = (y == end.y()) ? end.x() : int(ln.cells.size());
        QString rowStr;
        for (int x = c0; x < c1 && x < ln.cells.size(); ++x) {
            const char32_t ch = ln.cells[x].ch;
            if (ch == 0) continue;   // trailing half of wide character
            rowStr += QString::fromUcs4(&ch, 1);
        }
        if (y < end.y() || !ln.wrapped) {
            while (rowStr.endsWith(QLatin1Char(' '))) rowStr.chop(1);
        }
        out += rowStr;
        if (y < end.y() && !ln.wrapped) out += QLatin1Char('\n');
    }
    return out;
}

void TerminalScreen::scrollUp(int top, int bottom, int count) {
    top = std::clamp(top, 0, m_rows - 1);
    bottom = std::clamp(bottom, top, m_rows - 1);
    count = std::clamp(count, 1, bottom - top + 1);
    auto &grid = activeGrid();
    for (int i = 0; i < count; ++i) {
        Line popped = grid.takeAt(top);
        if (!m_altActive && top == 0) {
            m_scrollback.append(std::move(popped));
            if (m_scrollback.size() > kMaxScrollback) m_scrollback.removeFirst();
        }
        grid.insert(bottom, makeLine(m_cols));
    }
}

void TerminalScreen::scrollDown(int top, int bottom, int count) {
    top = std::clamp(top, 0, m_rows - 1);
    bottom = std::clamp(bottom, top, m_rows - 1);
    count = std::clamp(count, 1, bottom - top + 1);
    auto &grid = activeGrid();
    for (int i = 0; i < count; ++i) {
        grid.removeAt(bottom);
        grid.insert(top, makeLine(m_cols));
    }
}

void TerminalScreen::newLine() {
    m_wrapPending = false;
    if (m_curRow == m_scrollBottom) {
        scrollUp(m_scrollTop, m_scrollBottom, 1);
    } else if (m_curRow < m_rows - 1) {
        ++m_curRow;
    }
}

void TerminalScreen::reverseIndex() {
    m_wrapPending = false;
    if (m_curRow == m_scrollTop) {
        scrollDown(m_scrollTop, m_scrollBottom, 1);
    } else if (m_curRow > 0) {
        --m_curRow;
    }
}

void TerminalScreen::putChar(char32_t cp) {
    const int w = charDisplayWidth(cp);
    if (w <= 0) return;

    auto &grid = activeGrid();
    if (m_wrapPending && m_autowrap) {
        grid[m_curRow].wrapped = true;
        m_curCol = 0;
        newLine();
    }
    if (w == 2 && m_curCol == m_cols - 1) {
        if (m_autowrap) {
            grid[m_curRow].cells[m_curCol] = blankCell();
            grid[m_curRow].wrapped = true;
            m_curCol = 0;
            newLine();
        } else {
            m_curCol = std::max(0, m_cols - 2);
        }
    }

    Line &line = grid[m_curRow];
    // Clear halves if overwriting an existing wide character
    if (line.cells[m_curCol].ch == 0 && m_curCol > 0) {
        line.cells[m_curCol - 1] = blankCell();
    }
    if ((line.cells[m_curCol].flags & Wide) && m_curCol + 1 < m_cols) {
        line.cells[m_curCol + 1] = blankCell();
    }

    Cell c;
    c.ch = cp;
    c.fg = m_fg;
    c.bg = m_bg;
    c.flags = m_flags | (w == 2 ? Wide : 0);
    line.cells[m_curCol] = c;
    m_lastPrintedChar = cp;

    if (w == 2 && m_curCol + 1 < m_cols) {
        if ((line.cells[m_curCol + 1].flags & Wide) && m_curCol + 2 < m_cols) {
            line.cells[m_curCol + 2] = blankCell();
        }
        Cell trail = c;
        trail.ch = 0;
        trail.flags = m_flags;
        line.cells[m_curCol + 1] = trail;
        if (m_curCol + 2 >= m_cols) {
            m_curCol = m_cols - 1;
            m_wrapPending = m_autowrap;
        } else {
            m_curCol += 2;
            m_wrapPending = false;
        }
    } else {
        if (m_curCol + 1 >= m_cols) {
            m_wrapPending = m_autowrap;
        } else {
            ++m_curCol;
            m_wrapPending = false;
        }
    }
}

void TerminalScreen::enterAlternateScreen(bool clearAlt) {
    if (m_altActive) return;
    m_savedPri = {m_curRow, m_curCol, m_fg, m_bg, m_flags, m_wrapPending};
    m_altActive = true;
    if (clearAlt) {
        for (int r = 0; r < m_rows; ++r) m_altGrid[r] = makeLine(m_cols);
        m_curRow = 0;
        m_curCol = 0;
        m_wrapPending = false;
    }
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
}

void TerminalScreen::leaveAlternateScreen() {
    if (!m_altActive) return;
    m_altActive = false;
    m_curRow = std::clamp(m_savedPri.row, 0, m_rows - 1);
    m_curCol = std::clamp(m_savedPri.col, 0, m_cols - 1);
    m_fg = m_savedPri.fg;
    m_bg = m_savedPri.bg;
    m_flags = m_savedPri.flags;
    m_wrapPending = m_savedPri.wrapPending;
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
}

void TerminalScreen::handlePrivateMode(const QByteArray &raw, bool set) {
    const QVector<int> params = parseCsiParams(raw);
    for (int p : params) {
        switch (p) {
        case 1: m_appCursorKeys = set; break;
        case 7: m_autowrap = set; break;
        case 25: m_cursorVisible = set; break;
        case 47:
        case 1000:
            if (set) m_mouseTracking |= (1 << 0);
            else m_mouseTracking &= ~(1 << 0);
            break;
        case 1002:
            if (set) m_mouseTracking |= (1 << 1);
            else m_mouseTracking &= ~(1 << 1);
            break;
        case 1003:
            if (set) m_mouseTracking |= (1 << 2);
            else m_mouseTracking &= ~(1 << 2);
            break;
        case 1006:
            m_mouseSgr = set;
            break;
        case 1007:
            m_alternateScroll = set;
            break;
        case 1047:
            if (set) enterAlternateScreen(false);
            else leaveAlternateScreen();
            break;
        case 1048:
            if (set) {
                (m_altActive ? m_savedAlt : m_savedPri) = {m_curRow, m_curCol, m_fg, m_bg, m_flags, m_wrapPending};
            } else {
                const SavedCursor &s = m_altActive ? m_savedAlt : m_savedPri;
                m_curRow = std::clamp(s.row, 0, m_rows - 1);
                m_curCol = std::clamp(s.col, 0, m_cols - 1);
                m_fg = s.fg;
                m_bg = s.bg;
                m_flags = s.flags;
                m_wrapPending = s.wrapPending;
            }
            break;
        case 1049:
            if (set) enterAlternateScreen(true);
            else leaveAlternateScreen();
            break;
        case 2004:
            m_bracketedPaste = set;
            break;
        default:
            break;
        }
    }
}

void TerminalScreen::handleSgr(const QByteArray &raw) {
    // Support both ';' and ':' subparameters (e.g. 38;2;r;g;b and 38:2::r:g:b)
    if (raw.isEmpty()) {
        m_fg = {ColorKind::DefaultFg, 0, 0, 0, 0};
        m_bg = {ColorKind::DefaultBg, 0, 0, 0, 0};
        m_flags = 0;
        return;
    }
    const QList<QByteArray> groups = raw.split(';');
    for (int i = 0; i < groups.size(); ++i) {
        const QList<QByteArray> sub = groups[i].split(':');
        const int code = sub[0].isEmpty() ? 0 : sub[0].toInt();
        switch (code) {
        case 0:
            m_fg = {ColorKind::DefaultFg, 0, 0, 0, 0};
            m_bg = {ColorKind::DefaultBg, 0, 0, 0, 0};
            m_flags = 0;
            break;
        case 1: m_flags |= Bold; break;
        case 2: m_flags |= Dim; break;
        case 3: m_flags |= Italic; break;
        case 4:
            if (sub.size() > 1 && sub[1].toInt() == 0) m_flags &= ~Underline;
            else m_flags |= Underline;
            break;
        case 7: m_flags |= Inverse; break;
        case 8: m_flags |= Hidden; break;
        case 9: m_flags |= Strikethrough; break;
        case 21:
        case 22: m_flags &= ~(Bold | Dim); break;
        case 23: m_flags &= ~Italic; break;
        case 24: m_flags &= ~Underline; break;
        case 27: m_flags &= ~Inverse; break;
        case 28: m_flags &= ~Hidden; break;
        case 29: m_flags &= ~Strikethrough; break;
        case 30: case 31: case 32: case 33: case 34: case 35: case 36: case 37:
            m_fg = {ColorKind::Indexed, uint8_t(code - 30), 0, 0, 0};
            break;
        case 39:
            m_fg = {ColorKind::DefaultFg, 0, 0, 0, 0};
            break;
        case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47:
            m_bg = {ColorKind::Indexed, uint8_t(code - 40), 0, 0, 0};
            break;
        case 49:
            m_bg = {ColorKind::DefaultBg, 0, 0, 0, 0};
            break;
        case 90: case 91: case 92: case 93: case 94: case 95: case 96: case 97:
            m_fg = {ColorKind::Indexed, uint8_t(code - 90 + 8), 0, 0, 0};
            break;
        case 100: case 101: case 102: case 103: case 104: case 105: case 106: case 107:
            m_bg = {ColorKind::Indexed, uint8_t(code - 100 + 8), 0, 0, 0};
            break;
        case 38:
        case 48: {
            ColorSpec &target = (code == 38) ? m_fg : m_bg;
            if (sub.size() >= 3 && sub[1].toInt() == 5) {
                target = {ColorKind::Indexed, uint8_t(std::clamp(sub[2].toInt(), 0, 255)), 0, 0, 0};
            } else if (sub.size() >= 5 && sub[1].toInt() == 2) {
                const int off = (sub.size() >= 6) ? 3 : 2;
                target = {ColorKind::Rgb, 0,
                          uint8_t(std::clamp(sub[off].toInt(), 0, 255)),
                          uint8_t(std::clamp(sub[off + 1].toInt(), 0, 255)),
                          uint8_t(std::clamp(sub[off + 2].toInt(), 0, 255))};
            } else if (i + 1 < groups.size()) {
                const int mode = groups[i + 1].toInt();
                if (mode == 5 && i + 2 < groups.size()) {
                    target = {ColorKind::Indexed, uint8_t(std::clamp(groups[i + 2].toInt(), 0, 255)), 0, 0, 0};
                    i += 2;
                } else if (mode == 2 && i + 4 < groups.size()) {
                    target = {ColorKind::Rgb, 0,
                              uint8_t(std::clamp(groups[i + 2].toInt(), 0, 255)),
                              uint8_t(std::clamp(groups[i + 3].toInt(), 0, 255)),
                              uint8_t(std::clamp(groups[i + 4].toInt(), 0, 255))};
                    i += 4;
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

void TerminalScreen::handleCsi(char finalByte) {
    if (!m_csiBuf.isEmpty() && m_csiBuf[0] == '?') {
        if (finalByte == 'h' || finalByte == 'l') handlePrivateMode(m_csiBuf, finalByte == 'h');
        return;
    }
    if (!m_csiBuf.isEmpty() && (m_csiBuf[0] == '>' || m_csiBuf[0] == '=' || m_csiBuf[0] == '!')) {
        if (finalByte == 'c' && m_csiBuf[0] == '>' && m_reply) m_reply(QByteArrayLiteral("\x1b[>0;10;1c"));
        return;
    }

    const QVector<int> p = parseCsiParams(m_csiBuf);
    auto arg = [&](int idx, int def) {
        return (idx < p.size() && p[idx] > 0) ? p[idx] : def;
    };
    auto rawArg = [&](int idx, int def) {
        return (idx < p.size()) ? p[idx] : def;
    };

    auto &grid = activeGrid();
    switch (finalByte) {
    case 'A':   // CUU - cursor up
        m_wrapPending = false;
        m_curRow = std::max(m_curRow >= m_scrollTop ? m_scrollTop : 0, m_curRow - arg(0, 1));
        break;
    case 'B':   // CUD - cursor down
    case 'e':
        m_wrapPending = false;
        m_curRow = std::min(m_curRow <= m_scrollBottom ? m_scrollBottom : m_rows - 1, m_curRow + arg(0, 1));
        break;
    case 'C':   // CUF - cursor forward
    case 'a':
        m_wrapPending = false;
        m_curCol = std::min(m_cols - 1, m_curCol + arg(0, 1));
        break;
    case 'D':   // CUB - cursor back
        m_wrapPending = false;
        m_curCol = std::max(0, m_curCol - arg(0, 1));
        break;
    case 'E':   // CNL - cursor next line
        m_wrapPending = false;
        m_curCol = 0;
        m_curRow = std::min(m_rows - 1, m_curRow + arg(0, 1));
        break;
    case 'F':   // CPL - cursor preceding line
        m_wrapPending = false;
        m_curCol = 0;
        m_curRow = std::max(0, m_curRow - arg(0, 1));
        break;
    case 'G':   // CHA - cursor horizontal absolute (1-based)
    case '`':
        m_wrapPending = false;
        m_curCol = std::clamp(arg(0, 1) - 1, 0, m_cols - 1);
        break;
    case 'H':   // CUP / HVP - cursor position (1-based row;col)
    case 'f':
        m_wrapPending = false;
        m_curRow = std::clamp(arg(0, 1) - 1, 0, m_rows - 1);
        m_curCol = std::clamp(arg(1, 1) - 1, 0, m_cols - 1);
        break;
    case 'd':   // VPA - vertical position absolute (1-based)
        m_wrapPending = false;
        m_curRow = std::clamp(arg(0, 1) - 1, 0, m_rows - 1);
        break;
    case 'J': { // ED - erase in display
        const int mode = rawArg(0, 0);
        const Cell blank = blankCell();
        if (mode == 0) {
            for (int c = m_curCol; c < m_cols; ++c) grid[m_curRow].cells[c] = blank;
            grid[m_curRow].wrapped = false;
            for (int r = m_curRow + 1; r < m_rows; ++r) grid[r] = makeLine(m_cols);
        } else if (mode == 1) {
            for (int r = 0; r < m_curRow; ++r) grid[r] = makeLine(m_cols);
            for (int c = 0; c <= m_curCol && c < m_cols; ++c) grid[m_curRow].cells[c] = blank;
        } else if (mode == 2) {
            for (int r = 0; r < m_rows; ++r) grid[r] = makeLine(m_cols);
        } else if (mode == 3) {
            m_scrollback.clear();
        }
        break;
    }
    case 'K': { // EL - erase in line
        const int mode = rawArg(0, 0);
        const Cell blank = blankCell();
        if (mode == 0) {
            for (int c = m_curCol; c < m_cols; ++c) grid[m_curRow].cells[c] = blank;
            grid[m_curRow].wrapped = false;
        } else if (mode == 1) {
            for (int c = 0; c <= m_curCol && c < m_cols; ++c) grid[m_curRow].cells[c] = blank;
        } else if (mode == 2) {
            grid[m_curRow] = makeLine(m_cols);
        }
        break;
    }
    case 'X': { // ECH - erase characters
        const int count = arg(0, 1);
        const Cell blank = blankCell();
        for (int c = m_curCol; c < std::min(m_cols, m_curCol + count); ++c)
            grid[m_curRow].cells[c] = blank;
        break;
    }
    case 'L': { // IL - insert lines
        if (m_curRow >= m_scrollTop && m_curRow <= m_scrollBottom)
            scrollDown(m_curRow, m_scrollBottom, arg(0, 1));
        break;
    }
    case 'M': { // DL - delete lines
        if (m_curRow >= m_scrollTop && m_curRow <= m_scrollBottom)
            scrollUp(m_curRow, m_scrollBottom, arg(0, 1));
        break;
    }
    case '@': { // ICH - insert blank characters
        const int count = std::min(arg(0, 1), m_cols - m_curCol);
        Line &ln = grid[m_curRow];
        for (int i = 0; i < count; ++i) {
            ln.cells.removeLast();
            ln.cells.insert(m_curCol, blankCell());
        }
        break;
    }
    case 'P': { // DCH - delete characters
        const int count = std::min(arg(0, 1), m_cols - m_curCol);
        Line &ln = grid[m_curRow];
        for (int i = 0; i < count; ++i) {
            ln.cells.removeAt(m_curCol);
            ln.cells.append(blankCell());
        }
        break;
    }
    case 'S':   // SU - scroll up
        scrollUp(m_scrollTop, m_scrollBottom, arg(0, 1));
        break;
    case 'T':   // SD - scroll down
        scrollDown(m_scrollTop, m_scrollBottom, arg(0, 1));
        break;
    case 'b': { // REP - repeat last graphic character
        const int count = std::min(arg(0, 1), m_cols * m_rows);
        for (int i = 0; i < count; ++i) putChar(m_lastPrintedChar);
        break;
    }
    case 'r': { // DECSTBM - set top and bottom margins
        const int top = arg(0, 1) - 1;
        const int bot = (p.size() > 1 && p[1] > 0) ? (p[1] - 1) : (m_rows - 1);
        if (top >= 0 && bot < m_rows && top < bot) {
            m_scrollTop = top;
            m_scrollBottom = bot;
            m_curRow = 0;
            m_curCol = 0;
            m_wrapPending = false;
        }
        break;
    }
    case 's':   // SCOSC - save cursor
        (m_altActive ? m_savedAlt : m_savedPri) = {m_curRow, m_curCol, m_fg, m_bg, m_flags, m_wrapPending};
        break;
    case 'u': { // SCORC - restore cursor
        const SavedCursor &s = m_altActive ? m_savedAlt : m_savedPri;
        m_curRow = std::clamp(s.row, 0, m_rows - 1);
        m_curCol = std::clamp(s.col, 0, m_cols - 1);
        m_fg = s.fg;
        m_bg = s.bg;
        m_flags = s.flags;
        m_wrapPending = s.wrapPending;
        break;
    }
    case 'm':
        handleSgr(m_csiBuf);
        break;
    case 'n':   // DSR - device status report
        if (m_reply) {
            const int code = rawArg(0, 0);
            if (code == 5) m_reply(QByteArrayLiteral("\x1b[0n"));
            else if (code == 6) m_reply(QStringLiteral("\x1b[%1;%2R").arg(m_curRow + 1).arg(m_curCol + 1).toUtf8());
        }
        break;
    case 'c':   // DA - primary device attributes
        if (m_reply) m_reply(QByteArrayLiteral("\x1b[?1;2c"));
        break;
    default:
        break;
    }
}

void TerminalScreen::processCodepoint(char32_t cp) {
    // C0 controls are executed even inside escape/CSI sequences (standard VT behavior),
    // except inside OSC/Ignore strings where 0x07 (BEL) terminates OSC.
    if (m_state == State::OscString) {
        if (cp == 0x07 || cp == 0x9c) {
            m_state = State::Ground;
            dispatchOsc();
        } else if (cp == 0x1b) {
            m_state = State::OscEscape;
        } else if (m_oscBuf.size() < 4096) {
            m_oscBuf.append(QString::fromUcs4(&cp, 1).toUtf8());
        }
        return;
    }
    if (m_state == State::OscEscape) {
        m_state = State::Ground;
        if (cp == '\\') dispatchOsc();
        else processCodepoint(cp);
        return;
    }
    if (m_state == State::IgnoreString) {
        if (cp == 0x07 || cp == 0x9c) m_state = State::Ground;
        else if (cp == 0x1b) m_state = State::IgnoreEscape;
        return;
    }
    if (m_state == State::IgnoreEscape) {
        m_state = (cp == '\\') ? State::Ground : State::IgnoreString;
        return;
    }

    if (cp == 0x1b) {
        m_state = State::Escape;
        return;
    }
    if (cp < 0x20) {
        switch (cp) {
        case '\r':
            m_curCol = 0;
            m_wrapPending = false;
            break;
        case '\n':
        case '\v':
        case '\f':
            newLine();
            break;
        case '\b':
            m_wrapPending = false;
            if (m_curCol > 0) --m_curCol;
            break;
        case 0x07:
            if (m_onBell) m_onBell();
            break;
        case '\t':
            m_wrapPending = false;
            m_curCol = std::min(m_cols - 1, ((m_curCol / 8) + 1) * 8);
            break;
        default:
            break;
        }
        return;
    }

    switch (m_state) {
    case State::Ground:
        putChar(cp);
        break;

    case State::Escape:
        switch (cp) {
        case '[':
            m_csiBuf.clear();
            m_state = State::CsiEntry;
            break;
        case ']':
            m_oscBuf.clear();
            m_state = State::OscString;
            break;
        case 'P':   // DCS
        case '^':   // PM
        case '_':   // APC
        case 'X':   // SOS
            m_state = State::IgnoreString;
            break;
        case '(':
        case ')':
        case '*':
        case '+':
        case '-':
        case '.':
        case '/':
        case '#':
            m_state = State::EscapeIntermediate;
            break;
        case '7':   // DECSC
            (m_altActive ? m_savedAlt : m_savedPri) = {m_curRow, m_curCol, m_fg, m_bg, m_flags, m_wrapPending};
            m_state = State::Ground;
            break;
        case '8': { // DECRC
            const SavedCursor &s = m_altActive ? m_savedAlt : m_savedPri;
            m_curRow = std::clamp(s.row, 0, m_rows - 1);
            m_curCol = std::clamp(s.col, 0, m_cols - 1);
            m_fg = s.fg;
            m_bg = s.bg;
            m_flags = s.flags;
            m_wrapPending = s.wrapPending;
            m_state = State::Ground;
            break;
        }
        case 'D':   // IND
            newLine();
            m_state = State::Ground;
            break;
        case 'E':   // NEL
            m_curCol = 0;
            newLine();
            m_state = State::Ground;
            break;
        case 'M':   // RI
            reverseIndex();
            m_state = State::Ground;
            break;
        case 'c':   // RIS
            reset();
            break;
        default:
            m_state = State::Ground;
            break;
        }
        break;

    case State::EscapeIntermediate:
        if (cp >= 0x20 && cp <= 0x2f) {
            // stay in intermediate
        } else {
            m_state = State::Ground;
        }
        break;

    case State::CsiEntry:
        if (cp >= 0x40 && cp <= 0x7e) {
            handleCsi(char(cp));
            m_state = State::Ground;
        } else if (m_csiBuf.size() < 256) {
            m_csiBuf.append(char(cp));
        } else {
            m_state = State::Ground;
        }
        break;

    default:
        m_state = State::Ground;
        break;
    }
}

void TerminalScreen::feed(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = data[i];
        if (m_utf8Need > 0) {
            if ((b & 0xc0) == 0x80) {
                m_utf8Acc = (m_utf8Acc << 6) | (b & 0x3f);
                if (--m_utf8Need == 0) processCodepoint(m_utf8Acc);
                continue;
            } else {
                // Invalid continuation byte: reset decoder and re-process this byte
                m_utf8Need = 0;
                m_utf8Acc = 0;
            }
        }
        if (b < 0x80) {
            processCodepoint(b);
        } else if ((b & 0xe0) == 0xc0) {
            m_utf8Acc = b & 0x1f;
            m_utf8Need = 1;
        } else if ((b & 0xf0) == 0xe0) {
            m_utf8Acc = b & 0x0f;
            m_utf8Need = 2;
        } else if ((b & 0xf8) == 0xf0) {
            m_utf8Acc = b & 0x07;
            m_utf8Need = 3;
        }
    }
}

void TerminalScreen::dispatchOsc() {
    const int semi = m_oscBuf.indexOf(';');
    if (semi < 0) return;
    const QByteArray code = m_oscBuf.left(semi);
    const QString payload = QString::fromUtf8(m_oscBuf.mid(semi + 1));
    if (code == "0" || code == "2") {
        if (m_onTitle) m_onTitle(payload);
    } else if (code == "7") {
        // file://<host>/<path>: the host is ignored, the path is percent-decoded.
        const QUrl url(payload);
#ifdef Q_OS_WIN
        const QString path = url.toLocalFile();  // "C:/x" rather than "/C:/x", and keeps UNC hosts
#else
        const QString path = url.path();
#endif
        if (url.isValid() && url.scheme() == QLatin1String("file") && !path.isEmpty() && m_onCwd) m_onCwd(path);
    }
}
