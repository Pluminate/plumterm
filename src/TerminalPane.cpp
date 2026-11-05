#include "TerminalPane.h"
#include <QVBoxLayout>
#include "AskAiBar.h"
#include "CommitDialog.h"
#include "CoreBridge.h"
#include "PaneHeader.h"
#include "SearchBar.h"
#include "TerminalView.h"

TerminalPane::TerminalPane(const QString &cwd, QWidget *parent)
    : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_view = new TerminalView(true, this);
    m_view->setBackground(QColor(0x00, 0x00, 0x00)); // pitch black background like TerminalPro

    m_header = new PaneHeader(m_view, this);
    layout->addWidget(m_header);

    m_searchBar = new SearchBar(m_view, this);
    layout->addWidget(m_searchBar);

    m_aiBar = new AskAiBar(m_view, this);
    layout->addWidget(m_aiBar);

    layout->addWidget(m_view);

    connect(m_view, &TerminalView::activated, this, [this] {
        emit activated(this);
    });

    connect(m_header, &PaneHeader::splitRequested, this, [this](bool right) {
        emit splitRequested(this, right);
    });

    connect(m_header, &PaneHeader::closeRequested, this, [this] {
        emit closeRequested(this);
    });

    connect(m_header, &PaneHeader::runRequested, this, &TerminalPane::runLastCommand);
    connect(m_header, &PaneHeader::aiRequested, this, &TerminalPane::toggleAi);

    m_view->start(cwd);
}

QString TerminalPane::currentDirectory() const {
    return m_view ? m_view->currentDirectory() : QString();
}

void TerminalPane::setActive(bool active) {
    m_isActive = active;
    if (m_header) m_header->setActive(active);
    if (active && m_view && !m_searchBar->isVisible() && !m_aiBar->isVisible()) {
        m_view->setFocus();
    }
}

void TerminalPane::runLastCommand() {
    if (!m_view) return;
    const QString cmd = CoreBridge::lastShellCommand();
    if (!cmd.isEmpty()) {
        m_view->sendInput(cmd.toUtf8() + "\r");
    }
}

void TerminalPane::toggleSearch() {
    if (m_searchBar) {
        if (m_searchBar->isVisible()) m_searchBar->closeSearch();
        else m_searchBar->openSearch();
    }
}

void TerminalPane::toggleAi() {
    if (m_aiBar) {
        m_aiBar->toggle();
    }
}

void TerminalPane::openCommitDialog() {
    CommitDialog dlg(currentDirectory(), this);
    dlg.exec();
    if (m_header) m_header->updateStatus();
}

void TerminalPane::mousePressEvent(QMouseEvent *e) {
    emit activated(this);
    QWidget::mousePressEvent(e);
}
