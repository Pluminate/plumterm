#include "TerminalPane.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QTimer>
#include <QVBoxLayout>
#include "AskAiBar.h"
#include "CommitDialog.h"
#include "CoreBridge.h"
#include "MenuStyle.h"
#include "PaneHeader.h"
#include "SearchBar.h"
#include "TaskBar.h"
#include "TaskOutputDialog.h"
#include "TerminalView.h"

TerminalPane::TerminalPane(const QString &cwd, QWidget *parent)
    : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_view = new TerminalView(true, this);
    m_view->setBackground(QColor(0x00, 0x00, 0x00)); // pitch black background like PlumTerm

    m_header = new PaneHeader(m_view, this);
    layout->addWidget(m_header);

    m_searchBar = new SearchBar(m_view, this);
    layout->addWidget(m_searchBar);

    m_aiBar = new AskAiBar(m_view, this);
    layout->addWidget(m_aiBar);

    m_taskBar = new TaskBar(m_view, this);
    layout->addWidget(m_taskBar);

    layout->addWidget(m_view, 1);

    connect(m_view, &TerminalView::activated, this, [this] {
        emit activated(this);
    });

    connect(m_header, &PaneHeader::splitRequested, this, [this](bool right) {
        emit splitRequested(this, right);
    });

    connect(m_view, &TerminalView::splitRequested, this, [this](bool right) {
        emit splitRequested(this, right);
    });

    connect(m_header, &PaneHeader::closeRequested, this, [this] {
        emit closeRequested(this);
    });

    connect(m_header, &PaneHeader::runRequested, this, &TerminalPane::runLastCommand);
    connect(m_header, &PaneHeader::runContextMenuRequested, this, &TerminalPane::showRunContextMenu);
    connect(m_header, &PaneHeader::aiRequested, this, &TerminalPane::toggleAi);
    connect(m_header, &PaneHeader::taskRequested, this, &TerminalPane::toggleTask);
    connect(m_header, &PaneHeader::taskIndicatorClicked, this, &TerminalPane::showTaskOutput);
    connect(m_taskBar, &TaskBar::runTaskRequested, this, &TerminalPane::runBackgroundTask);

    m_view->start(cwd);
}

QString TerminalPane::currentDirectory() const {
    return m_view ? m_view->currentDirectory() : QString();
}

void TerminalPane::setActive(bool active) {
    m_isActive = active;
    if (m_header) m_header->setActive(active);
    if (active && m_view && !m_searchBar->isVisible() && !m_aiBar->isVisible() && (!m_taskBar || !m_taskBar->isVisible())) {
        m_view->setFocus();
    }
}

QString TerminalPane::getProjectRunCommand(const QString &cwd) const {
    const QString targetCwd = cwd.isEmpty() ? currentDirectory() : cwd;
    if (targetCwd.isEmpty()) return QStringLiteral("./start.sh");

    // 1. settings.json runCommands[cwd]
    const auto s = CoreBridge::settings().toObject();
    const auto runCmds = s.value(QStringLiteral("runCommands")).toObject();
    if (runCmds.contains(targetCwd)) {
        const QString custom = runCmds.value(targetCwd).toString().trimmed();
        if (!custom.isEmpty()) return custom;
    }

    // 2. ./start.sh
    if (QFile::exists(targetCwd + QStringLiteral("/start.sh"))) {
        return QStringLiteral("./start.sh");
    }

    // 3. package.json with scripts.start or scripts.dev
    const QString pkgJsonPath = targetCwd + QStringLiteral("/package.json");
    if (QFile::exists(pkgJsonPath)) {
        QFile f(pkgJsonPath);
        if (f.open(QIODevice::ReadOnly)) {
            const auto doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) {
                const auto scripts = doc.object().value(QStringLiteral("scripts")).toObject();
                if (scripts.contains(QStringLiteral("start"))) {
                    return QStringLiteral("npm start");
                }
                if (scripts.contains(QStringLiteral("dev"))) {
                    return QStringLiteral("npm run dev");
                }
            }
        }
    }

    // 4. Cargo.toml
    if (QFile::exists(targetCwd + QStringLiteral("/Cargo.toml"))) {
        return QStringLiteral("cargo run");
    }

    // 5. Makefile
    if (QFile::exists(targetCwd + QStringLiteral("/Makefile"))) {
        return QStringLiteral("make");
    }

    // 6. Last safe shell command from history
    const QString lastCmd = CoreBridge::lastShellCommand().trimmed();
    if (!lastCmd.isEmpty() && !lastCmd.contains(QStringLiteral("exit"))) {
        return lastCmd;
    }

    return QStringLiteral("./start.sh");
}

void TerminalPane::showRunContextMenu(const QPoint &globalPos) {
    const QString cwd = currentDirectory();
    const QString cmd = getProjectRunCommand(cwd);

    QMenu menu(this);
    MenuStyle::apply(&menu, true);

    auto *runAct = menu.addAction(MenuStyle::makeIcon(MenuIcons::kPlay, true), QStringLiteral("Run in Terminal: %1").arg(cmd));
    connect(runAct, &QAction::triggered, this, &TerminalPane::runLastCommand);

    auto *bgAct = menu.addAction(MenuStyle::makeIcon(MenuIcons::kFlash, true), QStringLiteral("Run in Background: %1").arg(cmd));
    connect(bgAct, &QAction::triggered, this, [this, cmd] {
        runBackgroundTask(cmd);
    });

    menu.addSeparator();

    auto *editAct = menu.addAction(MenuStyle::makeIcon(MenuIcons::kSettings, true), QStringLiteral("Configure Run Command for this folder…"));
    connect(editAct, &QAction::triggered, this, [this, cwd, cmd] {
        bool ok = false;
        const QString newCmd = QInputDialog::getText(
            this,
            QStringLiteral("Configure Run Command"),
            QStringLiteral("Enter run command for %1:").arg(QDir(cwd).dirName()),
            QLineEdit::Normal,
            cmd,
            &ok
        );
        if (ok && !newCmd.trimmed().isEmpty()) {
            auto s = CoreBridge::settings().toObject();
            auto runCmds = s.value(QStringLiteral("runCommands")).toObject();
            runCmds[cwd] = newCmd.trimmed();
            s[QStringLiteral("runCommands")] = runCmds;
            CoreBridge::setSettings(QString::fromUtf8(QJsonDocument(s).toJson(QJsonDocument::Compact)));
        }
    });

    menu.exec(globalPos);
}

void TerminalPane::runLastCommand() {
    if (!m_view) return;

    if (!m_view->isRunning()) {
        m_view->start(currentDirectory());
    }

    QString cmd = getProjectRunCommand(currentDirectory()).trimmed();

    if (cmd.endsWith(QStringLiteral("; exit"))) cmd.chop(7);
    if (cmd.endsWith(QStringLiteral("&& exit"))) cmd.chop(8);
    cmd = cmd.trimmed();

    if (!cmd.isEmpty()) {
        m_view->sendInput(cmd.toUtf8() + "\r");
    }
}

void TerminalPane::toggleSearch() {
    if (m_searchBar) {
        if (m_searchBar->isVisible()) {
            m_searchBar->closeSearch();
        } else {
            if (m_aiBar) m_aiBar->closeBar();
            if (m_taskBar) m_taskBar->closeBar();
            m_searchBar->openSearch();
        }
    }
}

void TerminalPane::toggleAi() {
    if (m_aiBar) {
        if (m_aiBar->isVisible()) {
            m_aiBar->closeBar();
        } else {
            if (m_searchBar) m_searchBar->closeSearch();
            if (m_taskBar) m_taskBar->closeBar();
            m_aiBar->openBar();
        }
    }
}

void TerminalPane::toggleTask() {
    if (m_taskBar) {
        if (m_taskBar->isVisible()) {
            m_taskBar->closeBar();
        } else {
            if (m_searchBar) m_searchBar->closeSearch();
            if (m_aiBar) m_aiBar->closeBar();
            m_taskBar->openBar();
        }
    }
}

void TerminalPane::runBackgroundTask(const QString &cmd) {
    if (cmd.trimmed().isEmpty()) return;

    if (m_bgProcess && m_bgProcess->state() != QProcess::NotRunning) {
        m_bgProcess->terminate();
        m_bgProcess->waitForFinished(500);
    }

    m_bgCommand = cmd.trimmed();
    m_bgOutput.clear();
    m_bgRunning = true;
    m_bgExitCode = 0;

    m_header->setTaskStatus(true, m_bgCommand);

    if (!m_bgProcess) {
        m_bgProcess = new QProcess(this);
    } else {
        m_bgProcess->disconnect();
    }

    QString cwd = currentDirectory();
    if (cwd.isEmpty()) cwd = QDir::currentPath();
    m_bgProcess->setWorkingDirectory(cwd);

    connect(m_bgProcess, &QProcess::readyReadStandardOutput, this, [this] {
        const QString chunk = QString::fromUtf8(m_bgProcess->readAllStandardOutput());
        m_bgOutput += chunk;
        if (m_taskDialog) m_taskDialog->appendOutput(chunk);
    });

    connect(m_bgProcess, &QProcess::readyReadStandardError, this, [this] {
        const QString chunk = QString::fromUtf8(m_bgProcess->readAllStandardError());
        m_bgOutput += chunk;
        if (m_taskDialog) m_taskDialog->appendOutput(chunk);
    });

    connect(m_bgProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        m_bgRunning = false;
        m_bgExitCode = exitCode;
        m_header->setTaskStatus(false, m_bgCommand, exitCode);
        if (m_taskDialog) m_taskDialog->setRunning(false, exitCode);
        if (exitCode != 0) {
            QApplication::beep();
        }
    });

#ifdef Q_OS_MACOS
    m_bgProcess->start(QStringLiteral("/bin/zsh"), {QStringLiteral("-l"), QStringLiteral("-c"), m_bgCommand});
#else
    m_bgProcess->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), m_bgCommand});
#endif
}

void TerminalPane::showTaskOutput() {
    if (m_bgCommand.isEmpty()) return;

    if (!m_taskDialog) {
        m_taskDialog = new TaskOutputDialog(m_bgCommand, this);
        connect(m_taskDialog, &TaskOutputDialog::stopRequested, this, &TerminalPane::stopBackgroundTask);
        connect(m_taskDialog, &TaskOutputDialog::rerunRequested, this, [this] {
            runBackgroundTask(m_bgCommand);
        });
    }

    m_taskDialog->appendOutput(m_bgOutput);
    m_taskDialog->setRunning(m_bgRunning, m_bgExitCode);
    m_taskDialog->show();
    m_taskDialog->raise();
    m_taskDialog->activateWindow();
}

void TerminalPane::stopBackgroundTask() {
    if (m_bgProcess && m_bgProcess->state() != QProcess::NotRunning) {
        m_bgProcess->terminate();
        QTimer::singleShot(1000, this, [this] {
            if (m_bgProcess && m_bgProcess->state() != QProcess::NotRunning) {
                m_bgProcess->kill();
            }
        });
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
