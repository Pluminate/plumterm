#include "TaskBar.h"
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include "TerminalView.h"

TaskBar::TaskBar(TerminalView *view, QWidget *parent)
    : QFrame(parent), m_view(view) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    setStyleSheet(QStringLiteral(
        "TaskBar { background-color: #111111; border-bottom: 1px solid #222222; }"
        "QLineEdit { background-color: #1e293b; color: #f1f5f9; border: 1px solid #334155; border-radius: 4px; padding: 4px 10px; font-size: 12px; }"
        "QLineEdit:focus { border-color: #38bdf8; }"
        "QPushButton { background-color: #2563eb; color: #ffffff; border: none; border-radius: 4px; padding: 4px 12px; font-size: 11px; font-weight: 500; }"
        "QPushButton:hover { background-color: #1d4ed8; }"
        "QToolButton#taskCloseBtn { background: transparent; border: none; color: #64748b; font-size: 16px; font-weight: bold; border-radius: 3px; }"
        "QToolButton#taskCloseBtn:hover { color: #f8fafc; background-color: #222222; }"
    ));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(10);

    auto *badge = new QLabel(QStringLiteral("⚡"), this);
    badge->setStyleSheet(QStringLiteral("font-size: 14px; color: #38bdf8;"));
    layout->addWidget(badge);

    m_input = new QLineEdit(this);
    m_input->setFixedHeight(30);
    m_input->setPlaceholderText(QStringLiteral("Run manual command in background (e.g. cargo build, npm test)…"));
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
