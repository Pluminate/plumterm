#include "TerminalView.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFontMetricsF>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>
#include <QWheelEvent>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>

#include "CoreBridge.h"

namespace {
constexpr int kPadLeft = 8;
constexpr int kPadTop = 4;

bool isWordChar(char32_t ch) {
    if (ch <= U' ') return false;
    return QChar::isLetterOrNumber(ch) || ch == U'_' || ch == U'-' || ch == U'.' || ch == U'/';
}

#ifdef Q_OS_MACOS
constexpr Qt::KeyboardModifier kLinkModifier = Qt::ControlModifier;  // Cmd
#else
constexpr Qt::KeyboardModifier kLinkModifier = Qt::ControlModifier;
#endif
}  // namespace

// Lives from spawn until the PTY's exit callback. The view detaches by nulling `view`; the exit callback frees it.
struct TerminalView::Ctx {
    QPointer<TerminalView> view;
};

TerminalView::TerminalView(bool dark, QWidget *parent) : QWidget(parent), m_dark(dark) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMouseTracking(true);
    setCursor(Qt::IBeamCursor);

    m_font = QFont(QStringLiteral("Menlo"));
    m_font.setStyleHint(QFont::Monospace);
    m_font.setFixedPitch(true);
    m_font.setPixelSize(13);
    updateMetrics();

    m_screen.setReplyCallback([this](const QByteArray &reply) { sendInput(reply); });
    m_screen.setTitleCallback([this](const QString &t) { emit titleChanged(t); });
    m_screen.setCwdCallback([this](const QString &c) { emit cwdChanged(c); });
    m_screen.setBellCallback([this] {
        emit bell();
        if (m_beepOnBell) QApplication::beep();
    });

    m_blinkTimer.setInterval(530);
    connect(&m_blinkTimer, &QTimer::timeout, this, [this] {
        if (!hasFocus() || m_scrollOffset != 0) return;
        m_cursorBlinkOn = !m_cursorBlinkOn;
        const int cr = m_screen.cursorRow(), cc = m_screen.cursorCol();
        const QRect curRect(int(kPadLeft + cc * m_cellW) - 1, int(kPadTop + cr * m_cellH) - 1, int(m_cellW * 2) + 2, int(m_cellH) + 2);
        update(curRect);
    });
}

TerminalView::~TerminalView() {
    if (m_ctx) m_ctx->view = nullptr;  // exit callback frees ctx
    if (m_pty) tp_pty_kill(m_pty);
}

// ---- PTY wiring --------------------------------------------------------------------------------------------------------

void TerminalView::start(const QString &cwd) {
    m_pendingCwd = cwd.isEmpty() ? QDir::homePath() : cwd;
    m_startRequested = true;
    recalculateGridSize();
}

void TerminalView::spawnIfReady() {
    if (!m_startRequested || m_pty != 0) return;
    m_startRequested = false;
    m_ctx = new Ctx{QPointer<TerminalView>(this)};
    m_pty = tp_pty_spawn(m_pendingCwd.toUtf8().constData(), quint16(m_screen.cols()), quint16(m_screen.rows()),
                         &TerminalView::onData, &TerminalView::onExit, m_ctx);
    if (!m_pty) {
        delete m_ctx;
        m_ctx = nullptr;
        emit exited();
    }
}

// Reader thread -> GUI thread.
void TerminalView::onData(void *user, quint64, const quint8 *data, size_t len) {
    auto *ctx = static_cast<Ctx *>(user);
    QPointer<TerminalView> v = ctx->view;
    if (!v) return;
    const QByteArray bytes(reinterpret_cast<const char *>(data), qsizetype(len));
    QMetaObject::invokeMethod(v.data(), [v, bytes] { if (v) v->feedOutput(bytes); }, Qt::QueuedConnection);
}

void TerminalView::onExit(void *user, quint64) {
    auto *ctx = static_cast<Ctx *>(user);
    QPointer<TerminalView> v = ctx->view;
    delete ctx;
    if (!v) return;
    QMetaObject::invokeMethod(v.data(), [v] {
        if (!v) return;
        v->m_pty = 0;
        v->m_ctx = nullptr;
        emit v->exited();
    }, Qt::QueuedConnection);
}

void TerminalView::sendInput(const QByteArray &bytes) {
    if (m_pty && !bytes.isEmpty()) tp_pty_write(m_pty, reinterpret_cast<const uint8_t *>(bytes.constData()), size_t(bytes.size()));
}

QString TerminalView::currentDirectory() const { return m_pty ? CoreBridge::ptyCwd(m_pty) : QString(); }

void TerminalView::feedOutput(const QByteArray &bytes) {
    const int oldSb = m_screen.scrollbackCount();
    m_screen.feed(bytes);
    const int newSb = m_screen.scrollbackCount();
    if (m_scrollOffset > 0) m_scrollOffset = std::clamp(m_scrollOffset + (newSb - oldSb), 0, newSb);
    emit output(bytes);
    update();
}

// ---- Geometry / metrics -----------------------------------------------------------------------------------------------

void TerminalView::updateMetrics() {
    QFontMetricsF fm(m_font);
    m_cellW = std::max(4.0, fm.horizontalAdvance(QLatin1Char('M')));
    m_cellH = std::max(8.0, std::ceil(fm.height() + 2.0));
    m_ascent = std::ceil(fm.ascent()) + 1.0;
}

void TerminalView::setTermFont(const QFont &font) {
    m_font = font;
    m_font.setFixedPitch(true);
    updateMetrics();
    recalculateGridSize();
    update();
}

void TerminalView::setDark(bool dark) {
    if (m_dark == dark) return;
    m_dark = dark;
    update();
}

void TerminalView::recalculateGridSize() {
    if (width() < 20 || height() < 20) return;
    const int availW = std::max(16, width() - kPadLeft - 4);
    const int availH = std::max(16, height() - kPadTop - 2);
    const int c = std::max(2, int(std::floor(availW / m_cellW)));
    const int r = std::max(1, int(std::floor(availH / m_cellH)));
    if (c != m_screen.cols() || r != m_screen.rows()) {
        m_screen.resize(c, r);
        m_scrollOffset = std::clamp(m_scrollOffset, 0, m_screen.scrollbackCount());
        if (m_pty) tp_pty_resize(m_pty, quint16(c), quint16(r));
    }
    spawnIfReady();
}

void TerminalView::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    recalculateGridSize();
}

QPoint TerminalView::pixelToCell(const QPointF &pos) const {
    const int col = std::clamp(int(std::floor((pos.x() - kPadLeft) / m_cellW)), 0, m_screen.cols());
    const int visRow = std::clamp(int(std::floor((pos.y() - kPadTop) / m_cellH)), 0, m_screen.rows() - 1);
    const int lineIdx = std::clamp(m_screen.scrollbackCount() - m_scrollOffset + visRow, 0, m_screen.totalLines() - 1);
    return QPoint(col, lineIdx);
}

bool TerminalView::isCellSelected(int lineIdx, int col) const {
    if (!m_hasSelection) return false;
    QPoint s = m_selAnchor, e = m_selActive;
    if (s.y() > e.y() || (s.y() == e.y() && s.x() > e.x())) std::swap(s, e);
    if (lineIdx < s.y() || lineIdx > e.y()) return false;
    if (s.y() == e.y()) return col >= s.x() && col < e.x();
    if (lineIdx == s.y()) return col >= s.x();
    if (lineIdx == e.y()) return col < e.x();
    return true;
}

// ---- Painting ---------------------------------------------------------------------------------------------------------

void TerminalView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const QColor themeBg = m_dark ? QColor(0x1e, 0x1f, 0x22) : QColor(0xff, 0xff, 0xff);
    const QColor defaultBg = m_bgOverride.isValid() ? m_bgOverride : themeBg;
    const QColor defaultFg = m_dark ? QColor(0xdf, 0xe1, 0xe5) : QColor(0x1f, 0x23, 0x28);
    const QColor selBg = m_dark ? QColor(0x2e, 0x43, 0x6e) : QColor(0xad, 0xd6, 0xff);

    p.fillRect(rect(), defaultBg);
    p.setFont(m_font);

    const int rows = m_screen.rows();
    const int cols = m_screen.cols();
    const int baseLine = m_screen.scrollbackCount() - m_scrollOffset;

    QFont normalFont = m_font, boldFont = m_font, italicFont = m_font, boldItalicFont = m_font;
    boldFont.setBold(true);
    italicFont.setItalic(true);
    boldItalicFont.setBold(true);
    boldItalicFont.setItalic(true);

    auto bgOf = [&](const TerminalScreen::Cell &cell, int lineIdx, int c) {
        const bool inv = (cell.flags & TerminalScreen::Inverse) != 0;
        return isCellSelected(lineIdx, c) ? selBg
               : inv ? TerminalScreen::resolveColor(cell.fg, m_dark, true)
                     : TerminalScreen::resolveColor(cell.bg, m_dark, false);
    };

    for (int r = 0; r < rows; ++r) {
        const int lineIdx = baseLine + r;
        if (lineIdx < 0 || lineIdx >= m_screen.totalLines()) continue;
        const TerminalScreen::Line &ln = m_screen.lineAt(lineIdx);
        const qreal y = kPadTop + r * m_cellH;

        // 1. backgrounds + selection, merged into runs
        for (int c = 0; c < cols && c < ln.cells.size(); ++c) {
            const QColor bg = bgOf(ln.cells[c], lineIdx, c);
            if (bg == themeBg) continue;  // cells with the default background show the widget fill
            int span = 1;
            while (c + span < cols && c + span < ln.cells.size() && bgOf(ln.cells[c + span], lineIdx, c + span) == bg) ++span;
            p.fillRect(QRectF(kPadLeft + c * m_cellW, y, span * m_cellW, m_cellH), bg);
            c += span - 1;
        }

        // 2. glyphs + decorations batched into runs
        uint8_t curFontStyle = 0xff;
        for (int c = 0; c < cols && c < ln.cells.size();) {
            const TerminalScreen::Cell &cell = ln.cells[c];
            if (cell.ch == 0 || cell.ch == U' ' || (cell.flags & TerminalScreen::Hidden)) {
                if ((cell.flags & (TerminalScreen::Underline | TerminalScreen::Strikethrough)) && cell.ch == U' ') {
                    const bool inv = (cell.flags & TerminalScreen::Inverse) != 0;
                    p.setPen(inv ? TerminalScreen::resolveColor(cell.bg, m_dark, false)
                                 : TerminalScreen::resolveColor(cell.fg, m_dark, true));
                    const qreal x = kPadLeft + c * m_cellW;
                    if (cell.flags & TerminalScreen::Underline)
                        p.drawLine(QPointF(x, y + m_ascent + 2.0), QPointF(x + m_cellW, y + m_ascent + 2.0));
                    if (cell.flags & TerminalScreen::Strikethrough)
                        p.drawLine(QPointF(x, y + m_cellH * 0.5), QPointF(x + m_cellW, y + m_cellH * 0.5));
                }
                ++c;
                continue;
            }

            // Wide character (takes 2 cells, e.g. emoji or CJK)
            if (cell.flags & TerminalScreen::Wide) {
                const uint8_t fstyle = cell.flags & (TerminalScreen::Bold | TerminalScreen::Italic);
                if (fstyle != curFontStyle) {
                    curFontStyle = fstyle;
                    p.setFont(fstyle == (TerminalScreen::Bold | TerminalScreen::Italic) ? boldItalicFont
                              : fstyle == TerminalScreen::Bold ? boldFont
                              : fstyle == TerminalScreen::Italic ? italicFont : normalFont);
                }
                const bool inv = (cell.flags & TerminalScreen::Inverse) != 0;
                QColor fg = inv ? TerminalScreen::resolveColor(cell.bg, m_dark, false)
                                : TerminalScreen::resolveColor(cell.fg, m_dark, true);
                if ((cell.flags & TerminalScreen::Bold) && cell.fg.kind == TerminalScreen::ColorKind::Indexed && cell.fg.index < 8 && !inv) {
                    TerminalScreen::ColorSpec bright = cell.fg;
                    bright.index += 8;
                    fg = TerminalScreen::resolveColor(bright, m_dark, true);
                }
                if (cell.flags & TerminalScreen::Dim) fg.setAlpha(155);
                p.setPen(fg);

                const qreal x = kPadLeft + c * m_cellW;
                p.drawText(QPointF(x, y + m_ascent), QString::fromUcs4(&cell.ch, 1));
                if (cell.flags & TerminalScreen::Underline)
                    p.drawLine(QPointF(x, y + m_ascent + 2.0), QPointF(x + 2 * m_cellW, y + m_ascent + 2.0));
                if (cell.flags & TerminalScreen::Strikethrough)
                    p.drawLine(QPointF(x, y + m_cellH * 0.5), QPointF(x + 2 * m_cellW, y + m_cellH * 0.5));
                c += 2;
                continue;
            }

            // Standard 1-cell glyph: gather run of consecutive cells with matching style
            const uint8_t fstyle = cell.flags & (TerminalScreen::Bold | TerminalScreen::Italic);
            if (fstyle != curFontStyle) {
                curFontStyle = fstyle;
                p.setFont(fstyle == (TerminalScreen::Bold | TerminalScreen::Italic) ? boldItalicFont
                          : fstyle == TerminalScreen::Bold ? boldFont
                          : fstyle == TerminalScreen::Italic ? italicFont : normalFont);
            }

            const bool inv = (cell.flags & TerminalScreen::Inverse) != 0;
            QColor fg = inv ? TerminalScreen::resolveColor(cell.bg, m_dark, false)
                            : TerminalScreen::resolveColor(cell.fg, m_dark, true);
            if ((cell.flags & TerminalScreen::Bold) && cell.fg.kind == TerminalScreen::ColorKind::Indexed && cell.fg.index < 8 && !inv) {
                TerminalScreen::ColorSpec bright = cell.fg;
                bright.index += 8;
                fg = TerminalScreen::resolveColor(bright, m_dark, true);
            }
            if (cell.flags & TerminalScreen::Dim) fg.setAlpha(155);
            p.setPen(fg);

            const int startC = c;
            QVector<char32_t> runChars;
            bool hasUnderline = false;
            bool hasStrikethrough = false;

            while (c < cols && c < ln.cells.size()) {
                const auto &nextCell = ln.cells[c];
                if (nextCell.ch == 0 || nextCell.ch == U' ' || (nextCell.flags & (TerminalScreen::Wide | TerminalScreen::Hidden))) break;
                const uint8_t nextFstyle = nextCell.flags & (TerminalScreen::Bold | TerminalScreen::Italic);
                if (nextFstyle != fstyle) break;

                const bool nextInv = (nextCell.flags & TerminalScreen::Inverse) != 0;
                QColor nextFg = nextInv ? TerminalScreen::resolveColor(nextCell.bg, m_dark, false)
                                        : TerminalScreen::resolveColor(nextCell.fg, m_dark, true);
                if ((nextCell.flags & TerminalScreen::Bold) && nextCell.fg.kind == TerminalScreen::ColorKind::Indexed && nextCell.fg.index < 8 && !nextInv) {
                    TerminalScreen::ColorSpec b = nextCell.fg;
                    b.index += 8;
                    nextFg = TerminalScreen::resolveColor(b, m_dark, true);
                }
                if (nextCell.flags & TerminalScreen::Dim) nextFg.setAlpha(155);
                if (nextFg != fg) break;

                if (nextCell.flags & TerminalScreen::Underline) hasUnderline = true;
                if (nextCell.flags & TerminalScreen::Strikethrough) hasStrikethrough = true;

                runChars.append(nextCell.ch);
                ++c;
            }

            const qreal runX = kPadLeft + startC * m_cellW;
            const QString runStr = QString::fromUcs4(runChars.constData(), runChars.size());
            p.drawText(QPointF(runX, y + m_ascent), runStr);

            if (hasUnderline) {
                for (int i = 0; i < runChars.size(); ++i) {
                    if (ln.cells[startC + i].flags & TerminalScreen::Underline) {
                        const qreal ux = kPadLeft + (startC + i) * m_cellW;
                        p.drawLine(QPointF(ux, y + m_ascent + 2.0), QPointF(ux + m_cellW, y + m_ascent + 2.0));
                    }
                }
            }
            if (hasStrikethrough) {
                for (int i = 0; i < runChars.size(); ++i) {
                    if (ln.cells[startC + i].flags & TerminalScreen::Strikethrough) {
                        const qreal sx = kPadLeft + (startC + i) * m_cellW;
                        p.drawLine(QPointF(sx, y + m_cellH * 0.5), QPointF(sx + m_cellW, y + m_cellH * 0.5));
                    }
                }
            }
        }
    }

    // 3. cursor
    if (m_scrollOffset == 0 && m_screen.cursorVisible()) {
        const int cr = m_screen.cursorRow(), cc = m_screen.cursorCol();
        if (cr >= 0 && cr < rows && cc >= 0 && cc < cols) {
            const TerminalScreen::Line &ln = m_screen.gridRow(cr);
            const TerminalScreen::Cell cell = (cc < ln.cells.size()) ? ln.cells[cc] : TerminalScreen::Cell{};
            const int cw = (cell.flags & TerminalScreen::Wide) ? 2 : 1;
            const QRectF cur(kPadLeft + cc * m_cellW, kPadTop + cr * m_cellH, cw * m_cellW, m_cellH);
            if (hasFocus()) {
                if (m_cursorBlinkOn) {
                    p.fillRect(cur, defaultFg);
                    if (cell.ch != 0 && cell.ch != U' ') {
                        p.setFont(normalFont);
                        p.setPen(defaultBg);
                        p.drawText(QPointF(cur.left(), cur.top() + m_ascent), QString::fromUcs4(&cell.ch, 1));
                    }
                }
            } else {
                p.setPen(QPen(defaultFg, 1.0));
                p.drawRect(cur.adjusted(0.5, 0.5, -0.5, -0.5));
            }
        }
    }

    // 4. scrollback thumb
    const int sb = m_screen.scrollbackCount();
    if (sb > 0 && m_scrollOffset > 0) {
        const qreal total = sb + rows;
        const qreal thumbH = std::max(20.0, (rows / total) * height());
        const qreal frac = qreal(sb - m_scrollOffset) / qreal(sb);
        p.fillRect(QRectF(width() - 4.0, frac * (height() - thumbH), 3.0, thumbH), QColor(128, 134, 144, 140));
    }
}

// ---- Focus ------------------------------------------------------------------------------------------------------------

bool TerminalView::focusNextPrevChild(bool) { return false; }  // keep Tab for shell completion

void TerminalView::focusInEvent(QFocusEvent *e) {
    QWidget::focusInEvent(e);
    m_cursorBlinkOn = true;
    m_blinkTimer.start();
    emit activated();
    update();
}

void TerminalView::focusOutEvent(QFocusEvent *e) {
    QWidget::focusOutEvent(e);
    m_blinkTimer.stop();
    m_cursorBlinkOn = true;
    update();
}

// ---- Clipboard / selection --------------------------------------------------------------------------------------------

QString TerminalView::selectedText() const {
    return m_hasSelection ? m_screen.extractText(m_selAnchor, m_selActive) : QString();
}

void TerminalView::copySelection() {
    const QString text = selectedText();
    if (!text.isEmpty()) QApplication::clipboard()->setText(text);
}

void TerminalView::pasteClipboard() {
    QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) return;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
    text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    QByteArray payload = text.toUtf8();
    if (m_screen.bracketedPaste()) {
        payload.prepend("\x1b[200~");
        payload.append("\x1b[201~");
    }
    m_scrollOffset = 0;
    sendInput(payload);
}

void TerminalView::selectAllText() {
    m_hasSelection = true;
    m_selAnchor = QPoint(0, 0);
    m_selActive = QPoint(m_screen.cols(), std::max(0, m_screen.totalLines() - 1));
    update();
}

void TerminalView::clearTerminal() {
    m_screen.clearScrollback();
    m_scrollOffset = 0;
    m_hasSelection = false;
    sendInput(QByteArrayLiteral("\x0c"));
    update();
}

// ---- Keyboard ---------------------------------------------------------------------------------------------------------

QByteArray TerminalView::encodeKey(QKeyEvent *e) const {
    const int key = e->key();
    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool shift = mods & Qt::ShiftModifier;
    const bool alt = mods & Qt::AltModifier;
#ifdef Q_OS_MACOS
    const bool ctrl = (mods & Qt::MetaModifier) != 0;  // physical Control is Meta on macOS in Qt Cocoa
#else
    const bool ctrl = (mods & Qt::ControlModifier) != 0;
#endif
    const int xtermMod = 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0);
    auto csiCursor = [&](char f) -> QByteArray {
        if (xtermMod > 1) return QStringLiteral("\x1b[1;%1%2").arg(xtermMod).arg(QLatin1Char(f)).toLatin1();
        if (m_screen.applicationCursorKeys()) return QByteArray("\x1bO") + f;
        return QByteArray("\x1b[") + f;
    };
    auto csiTilde = [&](int code) -> QByteArray {
        if (xtermMod > 1) return QStringLiteral("\x1b[%1;%2~").arg(code).arg(xtermMod).toLatin1();
        return QStringLiteral("\x1b[%1~").arg(code).toLatin1();
    };
    auto fKey = [&](char f, const char *plain) -> QByteArray {
        return xtermMod > 1 ? QStringLiteral("\x1b[1;%1%2").arg(xtermMod).arg(QLatin1Char(f)).toLatin1() : QByteArray(plain);
    };

    switch (key) {
    case Qt::Key_Up:        return csiCursor('A');
    case Qt::Key_Down:      return csiCursor('B');
    case Qt::Key_Right:     return csiCursor('C');
    case Qt::Key_Left:      return csiCursor('D');
    case Qt::Key_Home:      return csiCursor('H');
    case Qt::Key_End:       return csiCursor('F');
    case Qt::Key_Insert:    return csiTilde(2);
    case Qt::Key_Delete:    return csiTilde(3);
    case Qt::Key_PageUp:    return csiTilde(5);
    case Qt::Key_PageDown:  return csiTilde(6);
    case Qt::Key_F1:        return fKey('P', "\x1bOP");
    case Qt::Key_F2:        return fKey('Q', "\x1bOQ");
    case Qt::Key_F3:        return fKey('R', "\x1bOR");
    case Qt::Key_F4:        return fKey('S', "\x1bOS");
    case Qt::Key_F5:        return csiTilde(15);
    case Qt::Key_F6:        return csiTilde(17);
    case Qt::Key_F7:        return csiTilde(18);
    case Qt::Key_F8:        return csiTilde(19);
    case Qt::Key_F9:        return csiTilde(20);
    case Qt::Key_F10:       return csiTilde(21);
    case Qt::Key_F11:       return csiTilde(23);
    case Qt::Key_F12:       return csiTilde(24);
    case Qt::Key_Tab:       return shift ? QByteArrayLiteral("\x1b[Z") : QByteArrayLiteral("\t");
    case Qt::Key_Backtab:   return QByteArrayLiteral("\x1b[Z");
    case Qt::Key_Return:
    case Qt::Key_Enter:     return shift ? QByteArrayLiteral("\n") : alt ? QByteArrayLiteral("\x1b\r") : QByteArrayLiteral("\r");
    case Qt::Key_Backspace: return alt ? QByteArrayLiteral("\x1b\x7f") : QByteArrayLiteral("\x7f");
    case Qt::Key_Escape:    return QByteArrayLiteral("\x1b");
    default: break;
    }

    if (ctrl) {
        if (key >= Qt::Key_A && key <= Qt::Key_Z) {
            const char ch = char(key - Qt::Key_A + 1);
            return alt ? (QByteArray("\x1b") + ch) : QByteArray(1, ch);
        }
        switch (key) {
        case Qt::Key_Space:
        case Qt::Key_2:            return QByteArray(1, '\x00');
        case Qt::Key_BracketLeft:  return QByteArray(1, '\x1b');
        case Qt::Key_Backslash:    return QByteArray(1, '\x1c');
        case Qt::Key_BracketRight: return QByteArray(1, '\x1d');
        case Qt::Key_6:
        case Qt::Key_AsciiCircum:  return QByteArray(1, '\x1e');
        case Qt::Key_Minus:
        case Qt::Key_Underscore:   return QByteArray(1, '\x1f');
        default: break;
        }
    }

    const QString text = e->text();
    if (!text.isEmpty()) {
        if (alt && text.size() == 1 && text[0].unicode() >= 0x20 && text[0].unicode() < 0x7f)
            return QByteArray("\x1b") + text.toUtf8();
        return text.toUtf8();
    }
    return {};
}

void TerminalView::keyPressEvent(QKeyEvent *e) {
    const Qt::KeyboardModifiers mods = e->modifiers();
#ifdef Q_OS_MACOS
    const bool cmd = (mods & Qt::ControlModifier) != 0;
#else
    const bool cmd = (mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier);
#endif
    if (cmd) {
        if (e->key() == Qt::Key_C && !(mods & Qt::ShiftModifier)) {
            if (m_hasSelection) { copySelection(); e->accept(); return; }
        } else if (e->key() == Qt::Key_C) {
            copySelection(); e->accept(); return;
        } else if (e->key() == Qt::Key_V) {
            pasteClipboard(); e->accept(); return;
        } else if (e->key() == Qt::Key_A) {
            selectAllText(); e->accept(); return;
        } else if (e->key() == Qt::Key_K) {
            clearTerminal(); e->accept(); return;
        }
#ifdef Q_OS_MACOS
        // Cmd+Left/Right/Backspace/Enter edit the shell line like the Electron app did.
        if (!(mods & (Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier))) {
            QByteArray seq;
            switch (e->key()) {
            case Qt::Key_Left:      seq = "\x01"; break;  // beginning of line
            case Qt::Key_Right:     seq = "\x05"; break;  // end of line
            case Qt::Key_Backspace: seq = "\x15"; break;  // clear to line start
            case Qt::Key_Return:
            case Qt::Key_Enter:     seq = "\n"; break;
            case Qt::Key_End:       m_scrollOffset = 0; update(); e->accept(); return;
            default: break;
            }
            if (!seq.isEmpty()) { m_scrollOffset = 0; sendInput(seq); e->accept(); return; }
        }
        QWidget::keyPressEvent(e);  // other Cmd+key bubble up to window shortcuts
        return;
#endif
    }
#ifdef Q_OS_MACOS
    // Option+Left/Right/Delete move by word (ESC b / ESC f / ESC d); Option+Backspace is ESC DEL (encodeKey).
    if ((mods & Qt::AltModifier) && !(mods & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier))) {
        QByteArray seq;
        if (e->key() == Qt::Key_Left) seq = "\x1b" "b";
        else if (e->key() == Qt::Key_Right) seq = "\x1b" "f";
        else if (e->key() == Qt::Key_Delete) seq = "\x1b" "d";
        if (!seq.isEmpty()) { m_scrollOffset = 0; sendInput(seq); e->accept(); return; }
    }
#endif

    if ((mods & Qt::ShiftModifier) && !m_screen.usingAlternateScreen()) {
        const int page = std::max(1, m_screen.rows() - 1);
        if (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown) {
            const int d = e->key() == Qt::Key_PageUp ? page : -page;
            m_scrollOffset = std::clamp(m_scrollOffset + d, 0, m_screen.scrollbackCount());
            update();
            e->accept();
            return;
        }
    }

    const QByteArray bytes = encodeKey(e);
    if (!bytes.isEmpty()) {
        m_scrollOffset = 0;
        m_hasSelection = false;
        sendInput(bytes);
        update();
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

void TerminalView::inputMethodEvent(QInputMethodEvent *e) {
    const QString commit = e->commitString();
    if (!commit.isEmpty()) {
        m_scrollOffset = 0;
        m_hasSelection = false;
        sendInput(commit.toUtf8());
        update();
    }
    e->accept();
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImCursorRectangle)
        return QRect(int(kPadLeft + m_screen.cursorCol() * m_cellW), int(kPadTop + m_screen.cursorRow() * m_cellH),
                     int(m_cellW), int(m_cellH));
    return QWidget::inputMethodQuery(query);
}

// ---- Mouse ------------------------------------------------------------------------------------------------------------

QString TerminalView::urlUnder(const QPointF &pos) const {
    const QPoint cell = pixelToCell(pos);
    QList<int> map;
    const QString line = lineText(cell.y(), &map);
    int unit = 0;
    while (unit < map.size() && map[unit] < cell.x()) ++unit;
    if (unit >= map.size() || map[unit] != cell.x()) return {};
    return urlAt(line, unit);
}

void TerminalView::mousePressEvent(QMouseEvent *e) {
    setFocus(Qt::MouseFocusReason);
    emit activated();
    if (e->button() == Qt::LeftButton) {
        if (e->modifiers() & kLinkModifier) {
            const QString url = urlUnder(e->position());
            if (!url.isEmpty()) {
                QDesktopServices::openUrl(QUrl(url));
                e->accept();
                return;
            }
        }
        m_selecting = true;
        m_hasSelection = false;
        m_selAnchor = pixelToCell(e->position());
        m_selActive = m_selAnchor;
        update();
    }
    e->accept();
}

void TerminalView::mouseMoveEvent(QMouseEvent *e) {
    if (m_selecting && (e->buttons() & Qt::LeftButton)) {
        const QPoint pt = pixelToCell(e->position());
        if (pt != m_selActive) {
            m_selActive = pt;
            m_hasSelection = (m_selAnchor != m_selActive);
            update();
        }
        return;
    }
    const bool overLink = (e->modifiers() & kLinkModifier) && !urlUnder(e->position()).isEmpty();
    setCursor(overLink ? Qt::PointingHandCursor : Qt::IBeamCursor);
}

void TerminalView::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) m_selecting = false;
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    const QPoint cell = pixelToCell(e->position());
    const TerminalScreen::Line &ln = m_screen.lineAt(cell.y());
    if (ln.cells.isEmpty()) return;
    const int col = std::clamp(cell.x(), 0, int(ln.cells.size()) - 1);
    int left = col, right = col;
    if (isWordChar(ln.cells[col].ch)) {
        while (left > 0 && isWordChar(ln.cells[left - 1].ch)) --left;
        while (right + 1 < ln.cells.size() && isWordChar(ln.cells[right + 1].ch)) ++right;
    }
    ++right;
    m_selAnchor = QPoint(left, cell.y());
    m_selActive = QPoint(right, cell.y());
    m_hasSelection = (m_selAnchor != m_selActive);
    update();
}

void TerminalView::wheelEvent(QWheelEvent *e) {
    const qreal dy = !e->pixelDelta().isNull() ? e->pixelDelta().y() : (e->angleDelta().y() / 8.0);
    m_wheelCarry += dy;
    const int stepLines = int(m_wheelCarry / m_cellH);
    if (stepLines == 0) { e->accept(); return; }
    m_wheelCarry -= stepLines * m_cellH;

    if (m_screen.usingAlternateScreen()) {  // full-screen apps (less, vim): wheel = arrow keys
        const bool app = m_screen.applicationCursorKeys();
        const QByteArray seq = stepLines > 0 ? (app ? QByteArrayLiteral("\x1bOA") : QByteArrayLiteral("\x1b[A"))
                                             : (app ? QByteArrayLiteral("\x1bOB") : QByteArrayLiteral("\x1b[B"));
        const int count = std::min(10, std::abs(stepLines));
        for (int i = 0; i < count; ++i) sendInput(seq);
    } else {
        m_scrollOffset = std::clamp(m_scrollOffset + stepLines, 0, m_screen.scrollbackCount());
        update();
    }
    e->accept();
}

void TerminalView::contextMenuEvent(QContextMenuEvent *e) {
    QMenu menu(this);
    menu.addAction(tr("Copy"), this, &TerminalView::copySelection)->setEnabled(m_hasSelection);
    menu.addAction(tr("Paste"), this, &TerminalView::pasteClipboard);
    menu.addSeparator();
    menu.addAction(tr("Select All"), this, &TerminalView::selectAllText);
    menu.addAction(tr("Clear Terminal"), this, &TerminalView::clearTerminal);
    menu.exec(e->globalPos());
}

// ---- Search / text helpers --------------------------------------------------------------------------------------------

QString TerminalView::lineText(int lineIdx, QList<int> *colOfUnit) const {
    if (colOfUnit) colOfUnit->clear();
    if (lineIdx < 0 || lineIdx >= m_screen.totalLines()) return {};
    const TerminalScreen::Line &ln = m_screen.lineAt(lineIdx);
    QString out;
    bool prevWide = false;
    for (int c = 0; c < ln.cells.size() && c < m_screen.cols(); ++c) {
        const auto &cell = ln.cells[c];
        if (cell.ch == 0 && prevWide) { prevWide = false; continue; }  // continuation of a wide glyph
        prevWide = (cell.flags & TerminalScreen::Wide) != 0;
        const QString s = cell.ch == 0 ? QStringLiteral(" ") : QString::fromUcs4(&cell.ch, 1);
        out += s;
        if (colOfUnit) for (int i = 0; i < s.size(); ++i) colOfUnit->append(c);
    }
    return out;
}

QString TerminalView::urlAt(const QString &line, int col, int *startUnit, int *endUnit) {
    static const QRegularExpression re(QStringLiteral(R"((?:https?|ftp|file)://[^\s<>"'`]+)"));
    auto it = re.globalMatch(line);
    while (it.hasNext()) {
        const auto m = it.next();
        int end = int(m.capturedEnd());
        while (end > m.capturedStart() && QStringLiteral(".,;:!?)]}").contains(line[end - 1])) --end;  // trailing punctuation
        if (col >= m.capturedStart() && col < end) {
            if (startUnit) *startUnit = int(m.capturedStart());
            if (endUnit) *endUnit = end;
            return line.mid(int(m.capturedStart()), end - int(m.capturedStart()));
        }
    }
    return {};
}

void TerminalView::scrollToLine(int lineIdx) {
    const int sb = m_screen.scrollbackCount();
    const int base = sb - m_scrollOffset;
    if (lineIdx < base) m_scrollOffset = sb - lineIdx;
    else if (lineIdx >= base + m_screen.rows()) m_scrollOffset = sb - (lineIdx - m_screen.rows() + 1);
    m_scrollOffset = std::clamp(m_scrollOffset, 0, sb);
}

bool TerminalView::find(const QString &needle, bool backward, Qt::CaseSensitivity cs) {
    if (needle.isEmpty()) return false;
    const int total = m_screen.totalLines();
    QPoint s = m_selAnchor, e = m_selActive;
    if (s.y() > e.y() || (s.y() == e.y() && s.x() > e.x())) std::swap(s, e);

    // search position as (line, unit index) in that line's text
    int line = backward ? total - 1 : 0;
    int fromCol = backward ? INT_MAX : 0;
    if (m_hasSelection) {
        line = backward ? s.y() : e.y();
        fromCol = backward ? s.x() : e.x();
    }

    for (int n = 0; n <= total; ++n) {  // one extra pass so the starting line is rescanned after wrapping
        QList<int> map;
        const QString text = lineText(line, &map);
        int hit = -1;
        if (!backward) {
            int unit = 0;
            while (unit < map.size() && map[unit] < fromCol) ++unit;
            hit = text.indexOf(needle, unit, cs);
        } else {
            int limit = text.size();  // matches must start before the previous selection
            if (fromCol != INT_MAX) {
                limit = 0;
                while (limit < map.size() && map[limit] < fromCol) ++limit;
            }
            hit = limit > 0 ? text.lastIndexOf(needle, limit - 1, cs) : -1;
        }
        if (hit >= 0) {
            const int lastUnit = hit + int(needle.size()) - 1;
            const int endCol = map[lastUnit] + 1 + (m_screen.lineAt(line).cells[map[lastUnit]].flags & TerminalScreen::Wide ? 1 : 0);
            m_selAnchor = QPoint(map[hit], line);
            m_selActive = QPoint(endCol, line);
            m_hasSelection = true;
            scrollToLine(line);
            update();
            return true;
        }
        line += backward ? -1 : 1;
        if (line < 0) line = total - 1;
        if (line >= total) line = 0;
        fromCol = backward ? INT_MAX : 0;
    }
    return false;
}
