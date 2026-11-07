#include "TaskOutputDialog.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QHBoxLayout>
#include <QVBoxLayout>

TaskOutputDialog::TaskOutputDialog(const QString &command, QWidget *parent)
    : QDialog(parent), m_command(command) {
    setWindowTitle(QStringLiteral("Background Task: %1").arg(command));
    resize(720, 500);

    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #0f172a; color: #f8fafc; }"
        "QPlainTextEdit { background-color: #000000; color: #f1f5f9; font-family: Menlo, Monaco, monospace; font-size: 12px; border: 1px solid #334155; border-radius: 4px; padding: 6px; }"
        "QPushButton { background-color: #1e293b; color: #f8fafc; border: 1px solid #334155; border-radius: 4px; padding: 6px 14px; font-size: 12px; font-weight: 500; }"
        "QPushButton:hover { background-color: #334155; }"
        "QPushButton#stopBtn { background-color: #ef4444; border: none; color: #ffffff; }"
        "QPushButton#stopBtn:hover { background-color: #dc2626; }"
        "QPushButton#rerunBtn { background-color: #2563eb; border: none; color: #ffffff; }"
        "QPushButton#rerunBtn:hover { background-color: #1d4ed8; }"
    ));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(12);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(10);

    auto *titleLabel = new QLabel(QStringLiteral("⚡ <b>%1</b>").arg(command.toHtmlEscaped()), this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 14px; color: #38bdf8;"));
    headerLayout->addWidget(titleLabel, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: bold;"));
    headerLayout->addWidget(m_statusLabel);

    root->addLayout(headerLayout);

    m_outputView = new QPlainTextEdit(this);
    m_outputView->setReadOnly(true);
    root->addWidget(m_outputView, 1);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);

    m_stopBtn = new QPushButton(QStringLiteral("■ Stop Task"), this);
    m_stopBtn->setObjectName(QStringLiteral("stopBtn"));
    m_stopBtn->setCursor(Qt::PointingHandCursor);
    connect(m_stopBtn, &QPushButton::clicked, this, &TaskOutputDialog::stopRequested);
    btnLayout->addWidget(m_stopBtn);

    m_rerunBtn = new QPushButton(QStringLiteral("↻ Re-run"), this);
    m_rerunBtn->setObjectName(QStringLiteral("rerunBtn"));
    m_rerunBtn->setCursor(Qt::PointingHandCursor);
    connect(m_rerunBtn, &QPushButton::clicked, this, &TaskOutputDialog::rerunRequested);
    btnLayout->addWidget(m_rerunBtn);

    btnLayout->addStretch(1);

    m_copyBtn = new QPushButton(QStringLiteral("Copy Output"), this);
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    connect(m_copyBtn, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_outputView->toPlainText());
    });
    btnLayout->addWidget(m_copyBtn);

    m_closeBtn = new QPushButton(QStringLiteral("Close"), this);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(m_closeBtn);

    root->addLayout(btnLayout);

    m_startTime = QDateTime::currentMSecsSinceEpoch();
    setRunning(true);
}

void TaskOutputDialog::appendOutput(const QString &text) {
    if (m_outputView) {
        m_outputView->moveCursor(QTextCursor::End);
        m_outputView->insertPlainText(text);
        m_outputView->moveCursor(QTextCursor::End);
    }
}

void TaskOutputDialog::setRunning(bool running, int exitCode) {
    if (running) {
        m_statusLabel->setText(QStringLiteral("<span style=\"color: #f59e0b;\">● Running…</span>"));
        m_stopBtn->setVisible(true);
        m_rerunBtn->setVisible(false);
    } else {
        if (exitCode == 0) {
            m_statusLabel->setText(QStringLiteral("<span style=\"color: #22c55e;\">✓ Finished successfully</span>"));
        } else {
            m_statusLabel->setText(QStringLiteral("<span style=\"color: #ef4444;\">✗ Failed (exit code %1)</span>").arg(exitCode));
        }
        m_stopBtn->setVisible(false);
        m_rerunBtn->setVisible(true);
    }
}
