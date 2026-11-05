#include "PaneHeader.h"
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPointer>
#include <QtConcurrent>
#include "CoreBridge.h"
#include "TerminalView.h"

#include "CommitDialog.h"
#include "DiffViewerDialog.h"

PaneHeader::PaneHeader(TerminalView *view, QWidget *parent)
    : QFrame(parent), m_view(view) {
    setObjectName(QStringLiteral("paneHeader"));
    setFixedHeight(28);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(8);

    m_folderLabel = new QLabel(this);
    m_folderLabel->setCursor(Qt::PointingHandCursor);
    m_folderLabel->setStyleSheet(QStringLiteral("color: #aaaaaa; font-size: 11px; font-weight: 500;"));
    m_folderLabel->setText(QStringLiteral("📁 ..."));
    layout->addWidget(m_folderLabel);

    m_branchBtn = new QToolButton(this);
    m_branchBtn->setCursor(Qt::PointingHandCursor);
    m_branchBtn->setAutoRaise(true);
    m_branchBtn->setStyleSheet(QStringLiteral(
        "QToolButton { color: #2ecc71; font-size: 11px; font-weight: 500; border: none; padding: 2px 4px; }"
        "QToolButton:hover { color: #27ae60; background-color: #1a1a1a; border-radius: 3px; }"
    ));
    m_branchBtn->setVisible(false);
    connect(m_branchBtn, &QToolButton::clicked, this, [this] {
        showBranchMenu(m_branchBtn->mapToGlobal(QPoint(0, m_branchBtn->height())));
    });
    layout->addWidget(m_branchBtn);

    m_statsLabel = new QLabel(this);
    m_statsLabel->setCursor(Qt::PointingHandCursor);
    m_statsLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 500;"));
    m_statsLabel->setVisible(false);
    layout->addWidget(m_statsLabel);

    layout->addStretch();

    m_aiBtn = new QToolButton(this);
    m_aiBtn->setText(QStringLiteral("✨"));
    m_aiBtn->setToolTip(QStringLiteral("Ask AI (Cmd+G)"));
    m_aiBtn->setAutoRaise(true);
    m_aiBtn->setStyleSheet(QStringLiteral(
        "QToolButton { color: #888888; font-size: 12px; border: none; padding: 2px 4px; }"
        "QToolButton:hover { color: #f1c40f; background-color: #1a1a1a; border-radius: 3px; }"
    ));
    connect(m_aiBtn, &QToolButton::clicked, this, &PaneHeader::aiRequested);
    layout->addWidget(m_aiBtn);

    m_taskBtn = new QToolButton(this);
    m_taskBtn->setText(QStringLiteral("⚙"));
    m_taskBtn->setToolTip(QStringLiteral("Run Background Task (Cmd+B)"));
    m_taskBtn->setAutoRaise(true);
    m_taskBtn->setStyleSheet(QStringLiteral(
        "QToolButton { color: #888888; font-size: 12px; border: none; padding: 2px 4px; }"
        "QToolButton:hover { color: #3498db; background-color: #1a1a1a; border-radius: 3px; }"
    ));
    layout->addWidget(m_taskBtn);

    m_runBtn = new QToolButton(this);
    m_runBtn->setText(QStringLiteral("▶"));
    m_runBtn->setToolTip(QStringLiteral("Run / Rerun (Cmd+R)"));
    m_runBtn->setAutoRaise(true);
    m_runBtn->setStyleSheet(QStringLiteral(
        "QToolButton { color: #2ecc71; font-size: 12px; border: none; padding: 2px 4px; }"
        "QToolButton:hover { color: #4ade80; background-color: #1a1a1a; border-radius: 3px; }"
    ));
    connect(m_runBtn, &QToolButton::clicked, this, &PaneHeader::runRequested);
    layout->addWidget(m_runBtn);

    setActive(true);

    connect(&m_pollTimer, &QTimer::timeout, this, &PaneHeader::updateStatus);
    m_pollTimer.start(3000);

    QTimer::singleShot(200, this, &PaneHeader::updateStatus);
}

void PaneHeader::setActive(bool active) {
    m_isActive = active;
    if (active) {
        setStyleSheet(QStringLiteral(
            "#paneHeader { background-color: #111111; border-bottom: 1px solid #222222; }"
        ));
        m_folderLabel->setStyleSheet(QStringLiteral("color: #cccccc; font-size: 11px; font-weight: 500;"));
    } else {
        setStyleSheet(QStringLiteral(
            "#paneHeader { background-color: #161618; border-bottom: 1px solid #222222; }"
        ));
        m_folderLabel->setStyleSheet(QStringLiteral("color: #777777; font-size: 11px; font-weight: 500;"));
    }
}

void PaneHeader::updateStatus() {
    if (!m_view || m_polling) return;

    QString cwd = m_view->currentDirectory();
    if (cwd.isEmpty()) cwd = m_lastCwd;
    if (cwd.isEmpty()) cwd = QDir::currentPath();
    m_lastCwd = cwd;

    const QString folderName = QDir(cwd).dirName();
    m_folderLabel->setText(QStringLiteral("📁 %1").arg(folderName.isEmpty() ? cwd : folderName));
    m_folderLabel->setToolTip(cwd);

    m_polling = true;
    QPointer<PaneHeader> self = this;
    (void)QtConcurrent::run([self, cwd] {
        const auto statusVal = CoreBridge::git::status(cwd);
        QMetaObject::invokeMethod(qApp, [self, statusVal] {
            if (!self) return;
            self->m_polling = false;

            if (statusVal.isObject()) {
                const auto obj = statusVal.toObject();
                const QString branch = obj.value(QStringLiteral("branch")).toString();
                self->m_lastBranch = branch;
                if (!branch.isEmpty()) {
                    self->m_branchBtn->setText(QStringLiteral("⎇ %1").arg(branch));
                    self->m_branchBtn->setVisible(true);
                } else {
                    self->m_branchBtn->setVisible(false);
                }

                const auto statsVal = obj.value(QStringLiteral("gitStats"));
                if (statsVal.isObject()) {
                    const auto statsObj = statsVal.toObject();
                    const int ins = statsObj.value(QStringLiteral("insertions")).toInt();
                    const int del = statsObj.value(QStringLiteral("deletions")).toInt();
                    if (ins > 0 || del > 0) {
                        self->m_statsLabel->setText(QStringLiteral(
                            "<span style=\"color: #2ecc71;\">+%1</span> <span style=\"color: #e74c3c;\">-%2</span>"
                        ).arg(ins).arg(del));
                        self->m_statsLabel->setVisible(true);
                    } else {
                        self->m_statsLabel->setVisible(false);
                    }
                } else {
                    self->m_statsLabel->setVisible(false);
                }
            } else {
                self->m_branchBtn->setVisible(false);
                self->m_statsLabel->setVisible(false);
            }
        });
    });
}

void PaneHeader::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        if (m_folderLabel->geometry().contains(e->pos())) {
            CoreBridge::openIn(QStringLiteral("finder"), m_lastCwd);
            e->accept();
            return;
        }
        if (m_statsLabel->isVisible() && m_statsLabel->geometry().contains(e->pos())) {
            QMenu menu(this);
            menu.setStyleSheet(QStringLiteral(
                "QMenu { background-color: #1e1e20; color: #cccccc; border: 1px solid #333333; padding: 4px; }"
                "QMenu::item { padding: 4px 16px; border-radius: 3px; font-size: 12px; }"
                "QMenu::item:selected { background-color: #3574f0; color: #ffffff; }"
                "QMenu::separator { height: 1px; background-color: #333333; margin: 4px 0; }"
            ));

            auto *commitAct = menu.addAction(QStringLiteral("✨ Commit Changes..."));
            connect(commitAct, &QAction::triggered, this, [this] {
                CommitDialog dlg(m_lastCwd, this);
                dlg.exec();
                updateStatus();
            });

            menu.addSeparator();

            const auto filesVal = CoreBridge::git::changedFiles(m_lastCwd);
            if (filesVal.isArray()) {
                for (const auto &item : filesVal.toArray()) {
                    const auto obj = item.toObject();
                    const QString st = obj.value(QStringLiteral("status")).toString();
                    const QString f = obj.value(QStringLiteral("file")).toString();

                    auto *fileAct = menu.addAction(QStringLiteral("[%1] %2").arg(st, f));
                    connect(fileAct, &QAction::triggered, this, [this, f] {
                        DiffViewerDialog dlg(m_lastCwd, f, this);
                        dlg.exec();
                        updateStatus();
                    });
                }
            }

            menu.exec(mapToGlobal(e->pos()));
            e->accept();
            return;
        }
    }
    QFrame::mousePressEvent(e);
}

void PaneHeader::contextMenuEvent(QContextMenuEvent *e) {
    if (m_folderLabel->geometry().contains(e->pos())) {
        showFolderMenu(e->globalPos());
        e->accept();
        return;
    }

    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background-color: #1e1e20; color: #cccccc; border: 1px solid #333333; padding: 4px; }"
        "QMenu::item { padding: 4px 20px; border-radius: 3px; font-size: 12px; }"
        "QMenu::item:selected { background-color: #3574f0; color: #ffffff; }"
        "QMenu::separator { height: 1px; background-color: #333333; margin: 4px 0; }"
    ));

    auto *splitRightAct = menu.addAction(QStringLiteral("Split Right (Cmd+Shift+R)"));
    connect(splitRightAct, &QAction::triggered, this, [this] { emit splitRequested(true); });

    auto *splitDownAct = menu.addAction(QStringLiteral("Split Down (Cmd+Shift+D)"));
    connect(splitDownAct, &QAction::triggered, this, [this] { emit splitRequested(false); });

    menu.addSeparator();

    auto *closeAct = menu.addAction(QStringLiteral("Close Pane (Cmd+W)"));
    connect(closeAct, &QAction::triggered, this, &PaneHeader::closeRequested);

    menu.exec(e->globalPos());
}

void PaneHeader::showFolderMenu(const QPoint &globalPos) {
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background-color: #1e1e20; color: #cccccc; border: 1px solid #333333; padding: 4px; }"
        "QMenu::item { padding: 4px 20px; border-radius: 3px; font-size: 12px; }"
        "QMenu::item:selected { background-color: #3574f0; color: #ffffff; }"
        "QMenu::separator { height: 1px; background-color: #333333; margin: 4px 0; }"
    ));

    auto *copyPath = menu.addAction(QStringLiteral("Copy Path"));
    connect(copyPath, &QAction::triggered, this, [this] {
        QApplication::clipboard()->setText(m_lastCwd);
    });

    menu.addSeparator();

    auto *openFinder = menu.addAction(QStringLiteral("Open in Finder"));
    connect(openFinder, &QAction::triggered, this, [this] {
        CoreBridge::openIn(QStringLiteral("finder"), m_lastCwd);
    });

    menu.addSeparator();

    auto *openVSCode = menu.addAction(QStringLiteral("Open in VS Code"));
    connect(openVSCode, &QAction::triggered, this, [this] {
        CoreBridge::openIn(QStringLiteral("vscode"), m_lastCwd);
    });

    auto *openIntelliJ = menu.addAction(QStringLiteral("Open in IntelliJ IDEA"));
    connect(openIntelliJ, &QAction::triggered, this, [this] {
        CoreBridge::openIn(QStringLiteral("intellij"), m_lastCwd);
    });

    auto *openGithubDesktop = menu.addAction(QStringLiteral("Open in GitHub Desktop"));
    connect(openGithubDesktop, &QAction::triggered, this, [this] {
        CoreBridge::openIn(QStringLiteral("githubdesktop"), m_lastCwd);
    });

    menu.exec(globalPos);
}

void PaneHeader::showBranchMenu(const QPoint &globalPos) {
    const auto branchesVal = CoreBridge::git::branches(m_lastCwd);
    if (!branchesVal.isArray()) return;

    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background-color: #1e1e20; color: #cccccc; border: 1px solid #333333; padding: 4px; max-height: 300px; }"
        "QMenu::item { padding: 4px 20px; border-radius: 3px; font-size: 12px; }"
        "QMenu::item:selected { background-color: #3574f0; color: #ffffff; }"
    ));

    const auto branches = branchesVal.toArray();
    for (const auto &bVal : branches) {
        const QString b = bVal.toString();
        auto *act = menu.addAction(b);
        if (b == m_lastBranch) {
            act->setIcon(QIcon());
            QFont f = act->font();
            f.setBold(true);
            act->setFont(f);
        }
        connect(act, &QAction::triggered, this, [this, b] {
            if (m_view) {
                m_view->sendInput(QStringLiteral("git checkout %1\r").arg(b).toUtf8());
            }
        });
    }

    menu.exec(globalPos);
}
