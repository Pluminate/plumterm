#include "PaneHeader.h"
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QtConcurrent>
#include "CoreBridge.h"
#include "TerminalView.h"

#include "CommitDialog.h"
#include "DiffViewerDialog.h"
#include "MenuStyle.h"
#include "Platform.h"

namespace {

QIcon createTerminalConsoleIcon() {
    auto makePix = [](const QColor &color) {
        QPixmap pix(48, 48);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.scale(2.0, 2.0); // 24x24 logical viewport

        // Window border
        p.setPen(QPen(color, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(2.5, 3.5, 19, 17), 2.5, 2.5);

        // Header divider line
        p.drawLine(QPointF(2.5, 8.5), QPointF(21.5, 8.5));

        // Prompt '>'
        QPainterPath prompt;
        prompt.moveTo(6.0, 11.5);
        prompt.lineTo(9.5, 14.0);
        prompt.lineTo(6.0, 16.5);
        p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(prompt);

        // Cursor '_'
        p.drawLine(QPointF(11.5, 16.5), QPointF(16.5, 16.5));
        p.end();
        return pix;
    };

    QIcon icon;
    icon.addPixmap(makePix(QColor(0x94, 0xa3, 0xb8)), QIcon::Normal);
    icon.addPixmap(makePix(QColor(0x38, 0xbd, 0xf8)), QIcon::Active);
    icon.addPixmap(makePix(QColor(0x38, 0xbd, 0xf8)), QIcon::Selected);
    return icon;
}

} // namespace

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
    m_aiBtn->setToolTip(Platform::hint(QStringLiteral("Ask AI (Cmd+G)")));
    m_aiBtn->setAutoRaise(true);
    m_aiBtn->setStyleSheet(QStringLiteral(
        "QToolButton { color: #888888; font-size: 12px; border: none; padding: 2px 4px; }"
        "QToolButton:hover { color: #f1c40f; background-color: #1a1a1a; border-radius: 3px; }"
    ));
    connect(m_aiBtn, &QToolButton::clicked, this, &PaneHeader::aiRequested);
    layout->addWidget(m_aiBtn);

    m_taskBtn = new QToolButton(this);
    m_taskBtn->setIcon(createTerminalConsoleIcon());
    m_taskBtn->setIconSize(QSize(18, 18));
    m_taskBtn->setToolTip(Platform::hint(QStringLiteral("Run Background Task (Cmd+B)")));
    m_taskBtn->setAutoRaise(true);
    m_taskBtn->setCursor(Qt::PointingHandCursor);
    m_taskBtn->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; padding: 2px 4px; border-radius: 3px; }"
        "QToolButton:hover { background-color: #1a1a1a; }"
    ));
    connect(m_taskBtn, &QToolButton::clicked, this, &PaneHeader::taskRequested);
    layout->addWidget(m_taskBtn);

    m_taskIndicatorBtn = new QToolButton(this);
    m_taskIndicatorBtn->setAutoRaise(true);
    m_taskIndicatorBtn->setCursor(Qt::PointingHandCursor);
    m_taskIndicatorBtn->setVisible(false);
    connect(m_taskIndicatorBtn, &QToolButton::clicked, this, &PaneHeader::taskIndicatorClicked);
    layout->addWidget(m_taskIndicatorBtn);

    m_runBtn = new QToolButton(this);
    m_runBtn->setText(QStringLiteral("▶"));
    m_runBtn->setToolTip(Platform::hint(QStringLiteral("Run (Cmd+R) · Right-click to configure")));
    m_runBtn->setAutoRaise(true);
    m_runBtn->setCursor(Qt::PointingHandCursor);
    m_runBtn->setStyleSheet(QStringLiteral(
        "QToolButton { color: #2ecc71; font-size: 13px; font-weight: bold; border: none; padding: 2px 4px; }"
        "QToolButton:hover { color: #4ade80; background-color: #1a1a1a; border-radius: 3px; }"
    ));
    connect(m_runBtn, &QToolButton::clicked, this, &PaneHeader::runRequested);
    m_runBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_runBtn, &QToolButton::customContextMenuRequested, this, [this](const QPoint &p) {
        emit runContextMenuRequested(m_runBtn->mapToGlobal(p));
    });
    layout->addWidget(m_runBtn);

    setActive(true);

    connect(&m_pollTimer, &QTimer::timeout, this, &PaneHeader::updateStatus);
    m_pollTimer.start(3000);

    QTimer::singleShot(200, this, &PaneHeader::updateStatus);
}

void PaneHeader::setTaskStatus(bool running, const QString &command, int exitCode) {
    if (command.isEmpty()) {
        m_taskIndicatorBtn->setVisible(false);
        return;
    }

    const QString shortCmd = command.length() > 25 ? (command.left(22) + QStringLiteral("…")) : command;

    if (running) {
        m_taskIndicatorBtn->setText(QStringLiteral("● %1").arg(shortCmd));
        m_taskIndicatorBtn->setToolTip(QStringLiteral("Running: %1\nClick to view output").arg(command));
        m_taskIndicatorBtn->setStyleSheet(QStringLiteral(
            "QToolButton { color: #f59e0b; font-size: 11px; font-weight: 600; padding: 1px 6px; background-color: #271c08; border: 1px solid #78350f; border-radius: 3px; }"
            "QToolButton:hover { background-color: #3b280b; color: #fbbf24; }"
        ));
        m_taskIndicatorBtn->setVisible(true);
    } else {
        if (exitCode == 0) {
            m_taskIndicatorBtn->setText(QStringLiteral("✓ %1").arg(shortCmd));
            m_taskIndicatorBtn->setToolTip(QStringLiteral("Finished successfully: %1\nClick to view output").arg(command));
            m_taskIndicatorBtn->setStyleSheet(QStringLiteral(
                "QToolButton { color: #22c55e; font-size: 11px; font-weight: 600; padding: 1px 6px; background-color: #0d2b1a; border: 1px solid #166534; border-radius: 3px; }"
                "QToolButton:hover { background-color: #143d26; color: #4ade80; }"
            ));
        } else {
            m_taskIndicatorBtn->setText(QStringLiteral("✗ %1 (%2)").arg(shortCmd).arg(exitCode));
            m_taskIndicatorBtn->setToolTip(QStringLiteral("Failed with exit code %1: %2\nClick to view output").arg(exitCode).arg(command));
            m_taskIndicatorBtn->setStyleSheet(QStringLiteral(
                "QToolButton { color: #ef4444; font-size: 11px; font-weight: 600; padding: 1px 6px; background-color: #2b0d0d; border: 1px solid #991b1b; border-radius: 3px; }"
                "QToolButton:hover { background-color: #3d1414; color: #f87171; }"
            ));
        }
        m_taskIndicatorBtn->setVisible(true);
    }
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
            MenuStyle::apply(&menu, true);

            auto *commitAct = menu.addAction(MenuStyle::makeIcon(MenuIcons::kGit, true), QStringLiteral("Commit Changes…"));
            connect(commitAct, &QAction::triggered, this, [this] {
                CommitDialog dlg(m_lastCwd, this);
                dlg.exec();
                updateStatus();
            });

            const auto filesVal = CoreBridge::git::changedFiles(m_lastCwd);
            if (filesVal.isArray() && !filesVal.toArray().isEmpty()) {
                menu.addSeparator();
                for (const auto &item : filesVal.toArray()) {
                    const auto obj = item.toObject();
                    const QString st = obj.value(QStringLiteral("status")).toString();
                    const QString f = obj.value(QStringLiteral("file")).toString();

                    auto *fileAct = menu.addAction(MenuStyle::makeIcon(MenuIcons::kDiff, true), QStringLiteral("[%1] %2").arg(st, f));
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
    MenuStyle::apply(&menu, true);

    auto *splitRightAct = menu.addAction(MenuStyle::makeIcon(MenuIcons::kSplitRight, true), Platform::menuText(QStringLiteral("Split Right\t⌘⇧R")));
    connect(splitRightAct, &QAction::triggered, this, [this] { emit splitRequested(true); });

    auto *splitDownAct = menu.addAction(MenuStyle::makeIcon(MenuIcons::kSplitDown, true), Platform::menuText(QStringLiteral("Split Down\t⌘⇧D")));
    connect(splitDownAct, &QAction::triggered, this, [this] { emit splitRequested(false); });

    menu.addSeparator();

    auto *closeAct = menu.addAction(MenuStyle::makeIcon(MenuIcons::kClose, true), Platform::menuText(QStringLiteral("Close Pane\t⌘W")));
    connect(closeAct, &QAction::triggered, this, &PaneHeader::closeRequested);

    menu.exec(e->globalPos());
}

void PaneHeader::showFolderMenu(const QPoint &globalPos) {
    QMenu menu(this);
    MenuStyle::apply(&menu, true);

    auto *copyPath = menu.addAction(MenuStyle::makeIcon(MenuIcons::kCopy, true), QStringLiteral("Copy Path"));
    connect(copyPath, &QAction::triggered, this, [this] {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(m_lastCwd));
    });

    menu.addSeparator();

    auto *openFinder = menu.addAction(MenuStyle::makeIcon(MenuIcons::kFinder, true), Platform::openInFileManagerLabel());
    connect(openFinder, &QAction::triggered, this, [this] {
        CoreBridge::openIn(QStringLiteral("finder"), m_lastCwd);
    });

    menu.addSeparator();

    auto *openVSCode = menu.addAction(MenuStyle::makeIcon(MenuIcons::kCode, true), QStringLiteral("Open in VS Code"));
    connect(openVSCode, &QAction::triggered, this, [this] {
        CoreBridge::openIn(QStringLiteral("vscode"), m_lastCwd);
    });

    auto *openIntelliJ = menu.addAction(MenuStyle::makeIcon(MenuIcons::kCode, true), QStringLiteral("Open in IntelliJ IDEA"));
    connect(openIntelliJ, &QAction::triggered, this, [this] {
        CoreBridge::openIn(QStringLiteral("intellij"), m_lastCwd);
    });

    auto *openGithubDesktop = menu.addAction(MenuStyle::makeIcon(MenuIcons::kGit, true), QStringLiteral("Open in GitHub Desktop"));
    connect(openGithubDesktop, &QAction::triggered, this, [this] {
        CoreBridge::openIn(QStringLiteral("githubdesktop"), m_lastCwd);
    });

    menu.exec(globalPos);
}

void PaneHeader::showBranchMenu(const QPoint &globalPos) {
    const auto branchesVal = CoreBridge::git::branches(m_lastCwd);
    if (!branchesVal.isArray()) return;

    QMenu menu(this);
    MenuStyle::apply(&menu, true);

    const auto branches = branchesVal.toArray();
    for (const auto &bVal : branches) {
        const QString b = bVal.toString();
        auto *act = menu.addAction(b);
        act->setCheckable(true);
        if (b == m_lastBranch) {
            act->setChecked(true);
        }
        connect(act, &QAction::triggered, this, [this, b] {
            if (m_view) {
                m_view->sendInput(QStringLiteral("git checkout %1\r").arg(b).toUtf8());
            }
        });
    }

    menu.exec(globalPos);
}
