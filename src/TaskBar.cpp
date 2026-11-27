#include "TaskBar.h"
#include <QHBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QToolTip>
#include <algorithm>
#include <QLabel>
#include "MenuStyle.h"
#include "TerminalView.h"

TaskBar::TaskBar(TerminalView *view, QWidget *parent)
    : QFrame(parent), m_view(view) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    const bool dark = MenuStyle::isDark(this);
    setStyleSheet(dark
        ? QStringLiteral(
            "TaskBar { background-color: #111111; border-bottom: 1px solid #222222; }"
            "QLineEdit { background-color: #1e293b; color: #f1f5f9; border: 1px solid #334155; border-radius: 4px; padding: 4px 10px; font-size: 12px; }"
            "QLineEdit:focus { border-color: #38bdf8; }"
            "QPushButton { background-color: #2563eb; color: #ffffff; border: none; border-radius: 4px; padding: 4px 12px; font-size: 11px; font-weight: 500; }"
            "QPushButton:hover { background-color: #1d4ed8; }"
            "QToolButton#taskCloseBtn { background: transparent; border: none; color: #64748b; font-size: 16px; font-weight: bold; border-radius: 3px; }"
            "QToolButton#taskCloseBtn:hover { color: #f8fafc; background-color: #222222; }")
        : QStringLiteral(
            "TaskBar { background-color: #f8fafc; border-bottom: 1px solid #e2e8f0; }"
            "QLineEdit { background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; border-radius: 4px; padding: 4px 10px; font-size: 12px; }"
            "QLineEdit:focus { border-color: #0284c7; }"
            "QPushButton { background-color: #2563eb; color: #ffffff; border: none; border-radius: 4px; padding: 4px 12px; font-size: 11px; font-weight: 500; }"
            "QPushButton:hover { background-color: #1d4ed8; }"
            "QToolButton#taskCloseBtn { background: transparent; border: none; color: #64748b; font-size: 16px; font-weight: bold; border-radius: 3px; }"
            "QToolButton#taskCloseBtn:hover { color: #0f172a; background-color: #e2e8f0; }"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(10);

    auto *badge = new QLabel(QStringLiteral("⚡"), this);
    badge->setStyleSheet(QStringLiteral("font-size: 14px; color: #38bdf8;"));
    layout->addWidget(badge);

    m_input = new QLineEdit(this);
    m_input->setFixedHeight(30);
    m_input->setPlaceholderText(QStringLiteral("Run manual command in background (e.g. cargo build, npm test)…"));
    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::returnPressed, this, &TaskBar::submit);
    layout->addWidget(m_input, 1);

    m_runBtn = new QPushButton(QStringLiteral("▶ Run"), this);
    m_runBtn->setFixedHeight(30);
    m_runBtn->setCursor(Qt::PointingHandCursor);
    connect(m_runBtn, &QPushButton::clicked, this, &TaskBar::submit);
    layout->addWidget(m_runBtn);

    m_closeBtn = new QToolButton(this);
    m_closeBtn->setObjectName(QStringLiteral("taskCloseBtn"));
    m_closeBtn->setText(QStringLiteral("×"));
    m_closeBtn->setFixedSize(26, 26);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setToolTip(QStringLiteral("Close (Escape)"));
    connect(m_closeBtn, &QToolButton::clicked, this, &TaskBar::closeBar);
    layout->addWidget(m_closeBtn);

    setVisible(false);
}

void TaskBar::toggle() {
    if (isVisible()) closeBar();
    else openBar();
}

void TaskBar::openBar() {
    setVisible(true);
    m_input->setFocus();
    m_input->selectAll();
}

void TaskBar::closeBar() {
    setVisible(false);
    if (m_view) m_view->setFocus();
}

void TaskBar::submit() {
    const QString cmd = m_input->text().trimmed();
    if (cmd.isEmpty()) return;
    emit runTaskRequested(cmd);
    m_input->clear();
    closeBar();
}

void TaskBar::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        closeBar();
        e->accept();
        return;
    }
    QFrame::keyPressEvent(e);
}

bool TaskBar::eventFilter(QObject *obj, QEvent *ev) {
    if (obj == m_input && ev->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(ev);
        if (ke->key() == Qt::Key_Tab && !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
            completeWord();
            return true;
        }
    }
    return QFrame::eventFilter(obj, ev);
}

// Shell-like path completion for the word before the cursor, relative to the
// pane's current directory.
void TaskBar::completeWord() {
    const QString text = m_input->text();
    const int cur = m_input->cursorPosition();
    int start = cur;
    while (start > 0 && !(text[start - 1] == QLatin1Char(' ') && (start < 2 || text[start - 2] != QLatin1Char('\\')))) --start;

    QString word = text.mid(start, cur - start);
    word.replace(QStringLiteral("\\ "), QStringLiteral(" "));

    QString cwd = m_view ? m_view->currentDirectory() : QString();
    if (cwd.isEmpty()) cwd = QDir::currentPath();

    const int slash = word.lastIndexOf(QLatin1Char('/'));
    const QString dirPart = slash >= 0 ? word.left(slash + 1) : QString();
    const QString namePart = word.mid(slash + 1);

    QString lookup = dirPart;
    if (lookup.startsWith(QLatin1Char('~'))) lookup = QDir::homePath() + lookup.mid(1);
    const QDir dir(lookup.isEmpty() ? cwd : (QDir::isAbsolutePath(lookup) ? lookup : QDir(cwd).filePath(lookup)));
    if (!dir.exists()) return;

    QDir::Filters f = QDir::AllEntries | QDir::NoDotAndDotDot;
    if (namePart.startsWith(QLatin1Char('.'))) f |= QDir::Hidden;
    QStringList matches;
    for (const QFileInfo &fi : dir.entryInfoList(f, QDir::Name | QDir::DirsFirst)) {
        if (fi.fileName().startsWith(namePart)) matches << fi.fileName() + (fi.isDir() ? QStringLiteral("/") : QString());
    }
    if (matches.isEmpty()) return;

    QString common = matches.first();
    for (const QString &m : std::as_const(matches)) {
        int i = 0;
        while (i < common.size() && i < m.size() && common[i] == m[i]) ++i;
        common.truncate(i);
    }
    if (common.size() < namePart.size()) common = namePart;

    QString completed = dirPart + common;
    if (matches.size() == 1 && !completed.endsWith(QLatin1Char('/'))) completed += QLatin1Char(' ');
    completed.replace(QLatin1Char(' '), QStringLiteral("\\ "));
    if (matches.size() == 1 && completed.endsWith(QStringLiteral("\\ "))) completed.chop(2), completed += QLatin1Char(' ');

    m_input->setText(text.left(start) + completed + text.mid(cur));
    m_input->setCursorPosition(start + completed.size());

    if (matches.size() > 1 && common == namePart) {
        QToolTip::showText(m_input->mapToGlobal(QPoint(0, m_input->height())), matches.mid(0, 30).join(QStringLiteral("   ")), m_input);
    }
}
