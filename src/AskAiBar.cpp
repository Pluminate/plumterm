#include "AskAiBar.h"
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeyEvent>
#include <QtConcurrent>
#include <QVBoxLayout>
#include "CoreBridge.h"
#include "TerminalView.h"
#include "Platform.h"

AskAiBar::AskAiBar(TerminalView *view, QWidget *parent)
    : QFrame(parent), m_view(view) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    setStyleSheet(QStringLiteral(
        "AskAiBar { background-color: #111111; border-bottom: 1px solid #222222; }"
        "QLineEdit { background-color: #1e293b; color: #f1f5f9; border: 1px solid #334155; border-radius: 4px; padding: 4px 10px; font-size: 12px; }"
        "QLineEdit:focus { border-color: #38bdf8; }"
        "QPushButton { background-color: #2563eb; color: #ffffff; border: none; border-radius: 4px; padding: 4px 12px; font-size: 11px; font-weight: 500; }"
        "QPushButton:hover { background-color: #1d4ed8; }"
        "QToolButton#aiCloseBtn { background: transparent; border: none; color: #64748b; font-size: 16px; font-weight: bold; border-radius: 3px; }"
        "QToolButton#aiCloseBtn:hover { color: #f8fafc; background-color: #222222; }"
    ));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 6, 10, 6);
    rootLayout->setSpacing(6);

    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(10);

    auto *sparkle = new QLabel(QStringLiteral("✨"), this);
    sparkle->setStyleSheet(QStringLiteral("font-size: 14px;"));
    topRow->addWidget(sparkle);

    m_input = new QLineEdit(this);
    m_input->setFixedHeight(30);
    m_input->setPlaceholderText(QStringLiteral("Ask AI for a command (e.g. find large files, undo last commit)…"));
    connect(m_input, &QLineEdit::returnPressed, this, &AskAiBar::submitPrompt);
    topRow->addWidget(m_input, 1);

    m_closeBtn = new QToolButton(this);
    m_closeBtn->setObjectName(QStringLiteral("aiCloseBtn"));
    m_closeBtn->setText(QStringLiteral("×"));
    m_closeBtn->setFixedSize(26, 26);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setToolTip(QStringLiteral("Close (Escape)"));
    connect(m_closeBtn, &QToolButton::clicked, this, &AskAiBar::closeBar);
    topRow->addWidget(m_closeBtn);

    rootLayout->addLayout(topRow);

    // Suggestion box
    m_suggestionBox = new QFrame(this);
    m_suggestionBox->setStyleSheet(QStringLiteral(
        "QFrame { background-color: #1e293b; border: 1px solid #334155; border-radius: 4px; padding: 6px; }"
    ));
    m_suggestionBox->setVisible(false);

    auto *sugLayout = new QVBoxLayout(m_suggestionBox);
    sugLayout->setContentsMargins(6, 4, 6, 4);
    sugLayout->setSpacing(6);

    m_cmdLabel = new QLabel(m_suggestionBox);
    m_cmdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_cmdLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #38bdf8; font-family: monospace; font-size: 12px; font-weight: bold; background: #0f172a; padding: 4px 8px; border-radius: 3px; }"
    ));
    sugLayout->addWidget(m_cmdLabel);

    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(8);

    m_statusLabel = new QLabel(m_suggestionBox);
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));
    actionRow->addWidget(m_statusLabel, 1);

    m_pasteBtn = new QPushButton(QStringLiteral("Paste"), m_suggestionBox);
    m_pasteBtn->setStyleSheet(QStringLiteral("background-color: #334155; color: #f8fafc;"));
    connect(m_pasteBtn, &QPushButton::clicked, this, [this] {
        if (m_view && !m_currentCmd.isEmpty()) {
            m_view->sendInput(m_currentCmd.toUtf8());
            closeBar();
        }
    });
    actionRow->addWidget(m_pasteBtn);

    m_runBtn = new QPushButton(QStringLiteral("▶ Run"), m_suggestionBox);
    m_runBtn->setStyleSheet(QStringLiteral("background-color: #16a34a; color: #ffffff;"));
    connect(m_runBtn, &QPushButton::clicked, this, [this] {
        if (m_view && !m_currentCmd.isEmpty()) {
            m_view->sendInput(m_currentCmd.toUtf8() + "\r");
            closeBar();
        }
    });
    actionRow->addWidget(m_runBtn);

    sugLayout->addLayout(actionRow);
    rootLayout->addWidget(m_suggestionBox);

    setVisible(false);
}

void AskAiBar::toggle() {
    if (isVisible()) closeBar();
    else openBar();
}

void AskAiBar::openBar() {
    setVisible(true);
    m_input->setFocus();
    m_input->selectAll();
}

void AskAiBar::closeBar() {
    setVisible(false);
    m_suggestionBox->setVisible(false);
    m_currentCmd.clear();
    if (m_view) m_view->setFocus();
}

void AskAiBar::submitPrompt() {
    const QString prompt = m_input->text().trimmed();
    if (prompt.isEmpty() || m_loading) return;

    const auto s = CoreBridge::settings().toObject();
    const QString token = s.value(QStringLiteral("geminiToken")).toString().trimmed();
    if (token.isEmpty()) {
        m_suggestionBox->setVisible(true);
        m_cmdLabel->setText(Platform::hint(QStringLiteral("Error: Gemini Token is not set in Settings (Cmd+,).")));
        m_statusLabel->setText(QStringLiteral("<span style=\"color: #ef4444;\">Please add your Gemini API key in Settings.</span>"));
        m_pasteBtn->setEnabled(false);
        m_runBtn->setEnabled(false);
        return;
    }

    m_loading = true;
    m_suggestionBox->setVisible(true);
    m_cmdLabel->setText(QStringLiteral("Asking Gemini AI…"));
    m_statusLabel->setText(QStringLiteral("<span style=\"color: #94a3b8;\">Thinking…</span>"));
    m_pasteBtn->setEnabled(false);
    m_runBtn->setEnabled(false);

    QPointer<AskAiBar> self = this;
    (void)QtConcurrent::run([self, token, prompt] {
        const auto res = CoreBridge::askAI(token, prompt).toObject();
        QMetaObject::invokeMethod(qApp, [self, res] {
            if (!self) return;
            self->m_loading = false;
            if (res.value(QStringLiteral("success")).toBool()) {
                const QString cmd = res.value(QStringLiteral("command")).toString();
                self->setSuggestedCommand(cmd);
            } else {
                const QString err = res.value(QStringLiteral("error")).toString();
                self->m_cmdLabel->setText(QStringLiteral("Error: %1").arg(err));
                self->m_statusLabel->setText(QStringLiteral("<span style=\"color: #ef4444;\">Gemini request failed</span>"));
                self->m_pasteBtn->setEnabled(false);
                self->m_runBtn->setEnabled(false);
            }
        });
    });
}

void AskAiBar::setSuggestedCommand(const QString &cmd) {
    m_currentCmd = cmd;
    m_cmdLabel->setText(cmd);

    const bool valid = CoreBridge::verifyCommand(cmd);
    const bool dangerous = CoreBridge::isDangerous(cmd);

    if (!valid) {
        m_statusLabel->setText(QStringLiteral("<span style=\"color: #ef4444;\">⚠ Syntax error</span>"));
    } else if (dangerous) {
        m_statusLabel->setText(QStringLiteral("<span style=\"color: #f59e0b;\">⚠ Valid but potentially dangerous (git/cd)</span>"));
    } else {
        m_statusLabel->setText(QStringLiteral("<span style=\"color: #22c55e;\">✓ Syntax valid</span>"));
    }

    m_pasteBtn->setEnabled(true);
    m_runBtn->setEnabled(true);
}

void AskAiBar::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        closeBar();
        e->accept();
        return;
    }
    QFrame::keyPressEvent(e);
}
