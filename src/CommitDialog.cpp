#include "CommitDialog.h"
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QtConcurrent>
#include <QVBoxLayout>
#include "CoreBridge.h"
#include "DiffViewerDialog.h"

CommitDialog::CommitDialog(const QString &cwd, QWidget *parent)
    : QDialog(parent), m_cwd(cwd) {
    setWindowTitle(QStringLiteral("Git Commit"));
    resize(650, 480);
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #0d1117; color: #c9d1d9; }"
        "QLabel { color: #8b949e; font-size: 12px; }"
        "QListWidget { background-color: #161b22; color: #c9d1d9; border: 1px solid #30363d; border-radius: 4px; padding: 4px; }"
        "QPlainTextEdit { background-color: #161b22; color: #c9d1d9; border: 1px solid #30363d; border-radius: 4px; padding: 6px; font-size: 13px; }"
        "QPlainTextEdit:focus { border-color: #58a6ff; }"
        "QCheckBox { color: #c9d1d9; font-size: 12px; spacing: 6px; }"
        "QPushButton { background-color: #21262d; color: #c9d1d9; border: 1px solid #30363d; border-radius: 4px; padding: 6px 14px; font-size: 12px; font-weight: 500; }"
        "QPushButton:hover { background-color: #30363d; }"
        "QPushButton#commitBtn { background-color: #238636; color: #ffffff; border: 1px solid #2ea043; font-weight: bold; }"
        "QPushButton#commitBtn:hover { background-color: #2ea043; }"
        "QPushButton#aiBtn { color: #f1c40f; border-color: #856404; }"
    ));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 14, 16, 14);
    rootLayout->setSpacing(10);

    auto *fileHeader = new QHBoxLayout();
    fileHeader->addWidget(new QLabel(QStringLiteral("Select files to commit:"), this));
    fileHeader->addStretch();

    auto *selectAllBtn = new QPushButton(QStringLiteral("Select All"), this);
    connect(selectAllBtn, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_fileList->count(); ++i) {
            m_fileList->item(i)->setCheckState(Qt::Checked);
        }
    });
    fileHeader->addWidget(selectAllBtn);
    rootLayout->addLayout(fileHeader);

    m_fileList = new QListWidget(this);
    connect(m_fileList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const QString f = item->data(Qt::UserRole).toString();
        if (!f.isEmpty()) {
            DiffViewerDialog dlg(m_cwd, f, this);
            dlg.exec();
            refreshFiles();
        }
    });
    rootLayout->addWidget(m_fileList, 1);

    auto *msgHeader = new QHBoxLayout();
    msgHeader->addWidget(new QLabel(QStringLiteral("Commit Message:"), this));
    msgHeader->addStretch();

    m_aiBtn = new QPushButton(QStringLiteral("✨ Generate with AI"), this);
    m_aiBtn->setObjectName(QStringLiteral("aiBtn"));
    connect(m_aiBtn, &QPushButton::clicked, this, &CommitDialog::generateAiMessage);
    msgHeader->addWidget(m_aiBtn);
    rootLayout->addLayout(msgHeader);

    m_msgEdit = new QPlainTextEdit(this);
    m_msgEdit->setPlaceholderText(QStringLiteral("Write a clear, concise commit message (or use AI)…"));
    m_msgEdit->setFixedHeight(70);
    rootLayout->addWidget(m_msgEdit);

    auto *optRow = new QHBoxLayout();
    optRow->setSpacing(24);  // keep "Pull first" from butting against the previous label
    m_pushCheck = new QCheckBox(QStringLiteral("Push after commit"), this);
    m_pushCheck->setChecked(true);
    optRow->addWidget(m_pushCheck);

    m_pullFirstCheck = new QCheckBox(QStringLiteral("Pull first"), this);
    optRow->addWidget(m_pullFirstCheck);

    optRow->addStretch();

    m_commitBtn = new QPushButton(QStringLiteral("Commit & Push"), this);
    m_commitBtn->setObjectName(QStringLiteral("commitBtn"));
    connect(m_commitBtn, &QPushButton::clicked, this, &CommitDialog::doCommit);
    optRow->addWidget(m_commitBtn);

    auto *cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    optRow->addWidget(cancelBtn);

    rootLayout->addLayout(optRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setVisible(false);
    rootLayout->addWidget(m_statusLabel);

    connect(m_pushCheck, &QCheckBox::toggled, this, [this](bool push) {
        m_commitBtn->setText(push ? QStringLiteral("Commit & Push") : QStringLiteral("Commit"));
    });

    refreshFiles();
}

void CommitDialog::refreshFiles() {
    m_fileList->clear();
    const auto filesVal = CoreBridge::git::changedFiles(m_cwd);
    if (!filesVal.isArray()) return;

    for (const auto &item : filesVal.toArray()) {
        const auto obj = item.toObject();
        const QString status = obj.value(QStringLiteral("status")).toString();
        const QString file = obj.value(QStringLiteral("file")).toString();

        auto *listItem = new QListWidgetItem(QStringLiteral("[%1]  %2").arg(status, file), m_fileList);
        listItem->setFlags(listItem->flags() | Qt::ItemIsUserCheckable);
        listItem->setCheckState(Qt::Checked);
        listItem->setData(Qt::UserRole, file);

        if (status == QStringLiteral("M")) {
            listItem->setForeground(QColor(0xf3, 0x9c, 0x12));
        } else if (status == QStringLiteral("A")) {
            listItem->setForeground(QColor(0x2e, 0xcc, 0x71));
        } else if (status == QStringLiteral("D")) {
            listItem->setForeground(QColor(0xe7, 0x4c, 0x3c));
        } else {
            listItem->setForeground(QColor(0x94, 0xa3, 0xb8));
        }
    }
}

void CommitDialog::generateAiMessage() {
    QStringList files;
    for (int i = 0; i < m_fileList->count(); ++i) {
        auto *item = m_fileList->item(i);
        if (item->checkState() == Qt::Checked) {
            files << item->data(Qt::UserRole).toString();
        }
    }
    if (files.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("AI Commit"), QStringLiteral("Please select at least one file."));
        return;
    }

    const auto s = CoreBridge::settings().toObject();
    const QString token = s.value(QStringLiteral("geminiToken")).toString().trimmed();
    if (token.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("AI Commit"), QStringLiteral("Gemini API key is not configured in Settings (Cmd+,)."));
        return;
    }

    m_aiBtn->setEnabled(false);
    m_aiBtn->setText(QStringLiteral("✨ Generating…"));

    const QString cwd = m_cwd;
    const QString filesJson = QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(files)).toJson(QJsonDocument::Compact));

    QPointer<CommitDialog> self = this;
    (void)QtConcurrent::run([self, token, cwd, filesJson] {
        const auto res = CoreBridge::generateCommitMessage(token, cwd, filesJson).toObject();
        QMetaObject::invokeMethod(qApp, [self, res] {
            if (!self) return;
            self->m_aiBtn->setEnabled(true);
            self->m_aiBtn->setText(QStringLiteral("✨ Generate with AI"));
            if (res.value(QStringLiteral("success")).toBool()) {
                self->m_msgEdit->setPlainText(res.value(QStringLiteral("message")).toString());
            } else {
                QMessageBox::warning(self, QStringLiteral("AI Commit"), res.value(QStringLiteral("error")).toString());
            }
        });
    });
}

void CommitDialog::doCommit() {
    QStringList files;
    for (int i = 0; i < m_fileList->count(); ++i) {
        auto *item = m_fileList->item(i);
        if (item->checkState() == Qt::Checked) {
            files << item->data(Qt::UserRole).toString();
        }
    }
    if (files.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Commit"), QStringLiteral("Please select at least one file to commit."));
        return;
    }

    const QString msg = m_msgEdit->toPlainText().trimmed();
    if (msg.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Commit"), QStringLiteral("Please enter a commit message."));
        return;
    }

    m_commitBtn->setEnabled(false);
    m_commitBtn->setText(QStringLiteral("Committing…"));

    const QString cwd = m_cwd;
    const QString filesJson = QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(files)).toJson(QJsonDocument::Compact));
    const bool push = m_pushCheck->isChecked();
    const bool pullFirst = m_pullFirstCheck->isChecked();

    QPointer<CommitDialog> self = this;
    (void)QtConcurrent::run([self, cwd, filesJson, msg, push, pullFirst] {
        const auto res = CoreBridge::git::commit(cwd, filesJson, msg, push, pullFirst).toObject();
        QMetaObject::invokeMethod(qApp, [self, res] {
            if (!self) return;
            self->m_commitBtn->setEnabled(true);
            self->m_commitBtn->setText(self->m_pushCheck->isChecked() ? QStringLiteral("Commit & Push") : QStringLiteral("Commit"));

            if (res.value(QStringLiteral("success")).toBool()) {
                if (res.contains(QStringLiteral("pushError"))) {
                    QMessageBox::warning(self, QStringLiteral("Commit Succeeded, Push Failed"), res.value(QStringLiteral("pushError")).toString());
                }
                emit self->committed();
                self->accept();
            } else {
                QMessageBox::critical(self, QStringLiteral("Commit Error"), res.value(QStringLiteral("error")).toString());
            }
        });
    });
}
