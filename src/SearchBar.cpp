#include "SearchBar.h"
#include <QHBoxLayout>
#include <QKeyEvent>
#include "TerminalView.h"

SearchBar::SearchBar(TerminalView *view, QWidget *parent)
    : QFrame(parent), m_view(view) {
    setFixedHeight(30);
    setStyleSheet(QStringLiteral(
        "SearchBar { background-color: #111111; border-bottom: 1px solid #222222; padding: 2px 8px; }"
        "QLineEdit { background-color: #1e293b; color: #f1f5f9; border: 1px solid #334155; border-radius: 4px; padding: 2px 8px; font-size: 11px; }"
        "QLineEdit:focus { border-color: #38bdf8; }"
        "QToolButton { background: transparent; border: none; color: #888888; font-size: 13px; padding: 0 4px; }"
        "QToolButton:hover { color: #ffffff; }"
    ));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(6);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("Search terminal…"));
    connect(m_input, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_view && !text.isEmpty()) {
            m_view->find(text, false);
        }
    });
    connect(m_input, &QLineEdit::returnPressed, this, &SearchBar::findNext);
    layout->addWidget(m_input, 1);

    m_prevBtn = new QToolButton(this);
    m_prevBtn->setText(QStringLiteral("↑"));
    m_prevBtn->setToolTip(QStringLiteral("Previous match (Shift+Enter)"));
    connect(m_prevBtn, &QToolButton::clicked, this, &SearchBar::findPrev);
    layout->addWidget(m_prevBtn);

    m_nextBtn = new QToolButton(this);
    m_nextBtn->setText(QStringLiteral("↓"));
    m_nextBtn->setToolTip(QStringLiteral("Next match (Enter)"));
    connect(m_nextBtn, &QToolButton::clicked, this, &SearchBar::findNext);
    layout->addWidget(m_nextBtn);

    m_closeBtn = new QToolButton(this);
    m_closeBtn->setText(QStringLiteral("×"));
    m_closeBtn->setToolTip(QStringLiteral("Close (Escape)"));
    connect(m_closeBtn, &QToolButton::clicked, this, &SearchBar::closeSearch);
    layout->addWidget(m_closeBtn);

    setVisible(false);
}

void SearchBar::openSearch() {
    setVisible(true);
    m_input->setFocus();
    m_input->selectAll();
    if (!m_input->text().isEmpty() && m_view) {
        m_view->find(m_input->text(), false);
    }
}

void SearchBar::closeSearch() {
    setVisible(false);
    if (m_view) m_view->setFocus();
}

void SearchBar::findNext() {
    if (m_view && !m_input->text().isEmpty()) {
        m_view->find(m_input->text(), false);
    }
}

void SearchBar::findPrev() {
    if (m_view && !m_input->text().isEmpty()) {
        m_view->find(m_input->text(), true);
    }
}

void SearchBar::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        closeSearch();
        e->accept();
        return;
    }
    QFrame::keyPressEvent(e);
}
