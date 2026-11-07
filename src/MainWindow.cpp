#include "MainWindow.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>
#include "CoreBridge.h"
#include "MenuStyle.h"
#include "SettingsDialog.h"
#include "TabBar.h"
#include "TerminalPane.h"
#include "TerminalView.h"

namespace {

QJsonObject serializeNode(QWidget *w) {
    if (auto *pane = qobject_cast<TerminalPane *>(w)) {
        QJsonObject o;
        o[QStringLiteral("type")] = QStringLiteral("pane");
        o[QStringLiteral("savedCwd")] = pane->currentDirectory();
        return o;
    }
    if (auto *splitter = qobject_cast<QSplitter *>(w)) {
        QJsonObject o;
        o[QStringLiteral("type")] = QStringLiteral("split");
        o[QStringLiteral("direction")] = (splitter->orientation() == Qt::Horizontal ? QStringLiteral("vertical") : QStringLiteral("horizontal"));
        QJsonArray children;
        for (int i = 0; i < splitter->count(); ++i) {
            children.append(serializeNode(splitter->widget(i)));
        }
        o[QStringLiteral("children")] = children;
        QJsonArray sizes;
        for (int sz : splitter->sizes()) sizes.append(sz);
        o[QStringLiteral("sizes")] = sizes;
        return o;
    }
    return QJsonObject();
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Terminal"));
    resize(1000, 650);

    // Dark window styling
    setStyleSheet(QStringLiteral(
        "QMainWindow { background-color: #000000; }"
        "QSplitter::handle { background-color: #222222; }"
        "QSplitter::handle:hover { background-color: #38bdf8; }"
    ));

    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_tabBar = new TabBar(central);
    rootLayout->addWidget(m_tabBar);

    m_stack = new QStackedWidget(central);
    rootLayout->addWidget(m_stack);

    setCentralWidget(central);

    connect(m_tabBar, &TabBar::currentChanged, this, [this](int id) {
        if (auto *t = tabInfoForId(id)) {
            m_stack->setCurrentWidget(t->container);
            if (!t->panes.isEmpty() && t->panes.first()) {
                setActivePane(t->panes.first());
            }
        }
    });

    connect(m_tabBar, &TabBar::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabBar, &TabBar::newTabRequested, this, [this] { newTab(); });
    connect(m_tabBar, &TabBar::newWindowRequested, this, [this] { newWindow(); });
    connect(m_tabBar, &TabBar::duplicateTabRequested, this, &MainWindow::duplicateTab);
    connect(m_tabBar, &TabBar::closeOtherTabsRequested, this, &MainWindow::closeOtherTabs);
    connect(m_tabBar, &TabBar::closeAllTabsRequested, this, &MainWindow::closeAllTabs);
    connect(m_tabBar, &TabBar::splitRequested, this, &MainWindow::splitActivePane);
    connect(m_tabBar, &TabBar::settingsRequested, this, &MainWindow::openSettings);

    setupMenuBar();
    setupShortcuts();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupMenuBar() {
    auto *bar = menuBar();

    // 1. Shell menu
    auto *shellMenu = bar->addMenu(tr("&Shell"));

    auto *newTabAct = shellMenu->addAction(MenuStyle::makeIcon(MenuIcons::kAdd, true), tr("New Tab\t⌘T"), this, [this] { newTab(); });
    newTabAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));

    auto *newWinAct = shellMenu->addAction(MenuStyle::makeIcon(MenuIcons::kWindow, true), tr("New Window\t⌘N"), this, [this] { newWindow(); });
    newWinAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));

    shellMenu->addAction(MenuStyle::makeIcon(MenuIcons::kCopy, true), tr("Duplicate Tab"), this, [this] {
        if (m_tabBar) duplicateTab(m_tabBar->activeTabId());
    });

    shellMenu->addSeparator();

    auto *splitRightAct = shellMenu->addAction(MenuStyle::makeIcon(MenuIcons::kSplitRight, true), tr("Split Right\t⌘⇧R"), this, [this] { splitActivePane(true); });
    splitRightAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));

    auto *splitDownAct = shellMenu->addAction(MenuStyle::makeIcon(MenuIcons::kSplitDown, true), tr("Split Down\t⌘⇧D"), this, [this] { splitActivePane(false); });
    splitDownAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));

    shellMenu->addSeparator();

    auto *closePaneAct = shellMenu->addAction(MenuStyle::makeIcon(MenuIcons::kClose, true), tr("Close Pane\t⌘W"), this, &MainWindow::closeActivePane);
    closePaneAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));

    auto *closeTabAct = shellMenu->addAction(tr("Close Tab\t⌘⇧W"), this, &MainWindow::closeActiveTab);
    closeTabAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+W")));

    shellMenu->addAction(tr("Close Other Tabs"), this, [this] {
        if (m_tabBar) closeOtherTabs(m_tabBar->activeTabId());
    });

    shellMenu->addAction(tr("Close All Tabs"), this, &MainWindow::closeAllTabs);

    shellMenu->addSeparator();

    auto *settingsAct = shellMenu->addAction(MenuStyle::makeIcon(MenuIcons::kSettings, true), tr("Settings…\t⌘,"), this, &MainWindow::openSettings);
    settingsAct->setShortcut(QKeySequence::Preferences);
    settingsAct->setMenuRole(QAction::PreferencesRole);

    // 2. Edit menu
    auto *editMenu = bar->addMenu(tr("&Edit"));

    auto *copyAct = editMenu->addAction(MenuStyle::makeIcon(MenuIcons::kCopy, true), tr("Copy\t⌘C"), this, [this] {
        if (m_activePane && m_activePane->view()) m_activePane->view()->copySelection();
    });
    copyAct->setShortcut(QKeySequence::Copy);

    auto *pasteAct = editMenu->addAction(MenuStyle::makeIcon(MenuIcons::kPaste, true), tr("Paste\t⌘V"), this, [this] {
        if (m_activePane && m_activePane->view()) m_activePane->view()->pasteClipboard();
    });
    pasteAct->setShortcut(QKeySequence::Paste);

    auto *selectAllAct = editMenu->addAction(MenuStyle::makeIcon(MenuIcons::kSelectAll, true), tr("Select All\t⌘A"), this, [this] {
        if (m_activePane && m_activePane->view()) m_activePane->view()->selectAllText();
    });
    selectAllAct->setShortcut(QKeySequence::SelectAll);

    editMenu->addSeparator();

    auto *clearAct = editMenu->addAction(MenuStyle::makeIcon(MenuIcons::kClear, true), tr("Clear Terminal\t⌘K"), this, [this] {
        if (m_activePane && m_activePane->view()) m_activePane->view()->clearTerminal();
    });
    clearAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));

    editMenu->addSeparator();

    auto *findAct = editMenu->addAction(tr("Find…\t⌘F"), this, [this] {
        if (m_activePane) m_activePane->toggleSearch();
    });
    findAct->setShortcut(QKeySequence::Find);

    // 3. View menu
    auto *viewMenu = bar->addMenu(tr("&View"));

    auto *aiAct = viewMenu->addAction(tr("Ask AI…\t⌘G"), this, [this] {
        if (m_activePane) m_activePane->toggleAi();
    });
    aiAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));

    auto *taskAct = viewMenu->addAction(MenuStyle::makeIcon(MenuIcons::kFlash, true), tr("Run Background Task…\t⌘B"), this, [this] {
        if (m_activePane) m_activePane->toggleTask();
    });
    taskAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));

    viewMenu->addSeparator();

    viewMenu->addAction(tr("Next Tab\t⌃Tab"), this, [this] {
        if (m_tabs.size() <= 1) return;
        const int curIdx = m_stack->currentIndex();
        const int nextIdx = (curIdx + 1) % m_tabs.size();
        m_tabBar->setActiveTab(m_tabs[nextIdx].id);
    });

    viewMenu->addAction(tr("Previous Tab\t⌃⇧Tab"), this, [this] {
        if (m_tabs.size() <= 1) return;
        const int curIdx = m_stack->currentIndex();
        const int prevIdx = (curIdx - 1 + m_tabs.size()) % m_tabs.size();
        m_tabBar->setActiveTab(m_tabs[prevIdx].id);
    });

    viewMenu->addSeparator();

    auto *fsAct = viewMenu->addAction(tr("Toggle Full Screen\t⌃⌘F"), this, [this] {
        if (isFullScreen()) showNormal(); else showFullScreen();
    });
    fsAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Meta+F")));

    // 4. Window menu
    auto *winMenu = bar->addMenu(tr("&Window"));
    auto *minAct = winMenu->addAction(tr("Minimize\t⌘M"), this, &QWidget::showMinimized);
    minAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));

    winMenu->addAction(tr("Zoom"), this, [this] {
        if (isMaximized()) showNormal(); else showMaximized();
    });

    winMenu->addSeparator();
    winMenu->addAction(tr("Bring All to Front"), this, &QWidget::raise);
}

void MainWindow::newWindow(const QString &cwd) {
    auto *win = new MainWindow();
    win->setAttribute(Qt::WA_DeleteOnClose);
    const QString targetCwd = cwd.isEmpty() ? (m_activePane ? m_activePane->currentDirectory() : QDir::currentPath()) : cwd;
    win->newTab(targetCwd);
    win->resize(size());
    win->move(pos() + QPoint(30, 30));
    win->show();
    win->raise();
    win->activateWindow();
}

void MainWindow::duplicateTab(int id) {
    auto *t = tabInfoForId(id);
    if (!t) return;
    QString cwd;
    if (!t->panes.isEmpty() && t->panes.first()) {
        cwd = t->panes.first()->currentDirectory();
    }
    newTab(cwd);
}

void MainWindow::closeTab(int id) {
    if (m_tabs.size() == 1 && tabInfoForId(id)) {
        close();
        return;
    }
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].id == id) {
            auto *container = m_tabs[i].container;
            m_tabs.removeAt(i);
            m_tabBar->removeTab(id);
            if (container) {
                m_stack->removeWidget(container);
                container->deleteLater();
            }
            break;
        }
    }
    if (m_tabs.isEmpty()) {
        close();
    } else {
        auto *t = currentTabInfo();
        if (t && !t->panes.isEmpty() && t->panes.first()) {
            setActivePane(t->panes.first());
        }
    }
}

void MainWindow::closeActiveTab() {
    if (m_tabBar) closeTab(m_tabBar->activeTabId());
}

void MainWindow::closeOtherTabs(int keepId) {
    QList<int> toClose;
    for (const auto &t : m_tabs) {
        if (t.id != keepId) toClose.append(t.id);
    }
    for (int id : toClose) {
        closeTab(id);
    }
}

void MainWindow::closeAllTabs() {
    close();
}

void MainWindow::setupShortcuts() {
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")), this, [this] {
        if (isMinimized() || !isVisible()) {
            showNormal();
            raise();
            activateWindow();
        } else {
            hide();
        }
    });

    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), this, [this] {
        if (m_tabs.size() <= 1) return;
        const int curIdx = m_stack->currentIndex();
        const int nextIdx = (curIdx + 1) % m_tabs.size();
        m_tabBar->setActiveTab(m_tabs[nextIdx].id);
    });

    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), this, [this] {
        if (m_tabs.size() <= 1) return;
        const int curIdx = m_stack->currentIndex();
        const int prevIdx = (curIdx - 1 + m_tabs.size()) % m_tabs.size();
        m_tabBar->setActiveTab(m_tabs[prevIdx].id);
    });

    // Cmd+1 to Cmd+9
    for (int i = 1; i <= 9; ++i) {
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i)), this, [this, i] {
            if (i <= m_tabs.size()) {
                m_tabBar->setActiveTab(m_tabs[i - 1].id);
            }
        });
    }
}

TerminalPane *MainWindow::createPane(const QString &cwd, QWidget *parent) {
    auto *pane = new TerminalPane(cwd, parent);
    connect(pane, &TerminalPane::activated, this, &MainWindow::setActivePane);
    connect(pane, &TerminalPane::splitRequested, this, [this](TerminalPane *p, bool right) {
        if (p) setActivePane(p);
        splitActivePane(right);
    });
    connect(pane, &TerminalPane::closeRequested, this, [this](TerminalPane *p) {
        removePane(p);
    });
    return pane;
}

void MainWindow::setActivePane(TerminalPane *pane) {
    if (m_activePane == pane) return;
    if (m_activePane) m_activePane->setActive(false);
    m_activePane = pane;
    if (m_activePane) m_activePane->setActive(true);
}

void MainWindow::newTab(const QString &cwd) {
    auto *t = currentTabInfo();
    QString dir = cwd;
    if (dir.isEmpty() && m_activePane) {
        dir = m_activePane->currentDirectory();
    }
    if (dir.isEmpty()) dir = QDir::currentPath();

    auto *container = new QWidget(m_stack);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *pane = createPane(dir, container);
    layout->addWidget(pane);

    const int id = m_tabBar->addTab(QStringLiteral("Tab %1").arg(m_tabs.size() + 1));
    m_stack->addWidget(container);

    TabInfo info;
    info.id = id;
    info.container = container;
    info.panes.append(pane);
    m_tabs.append(info);

    m_tabBar->setActiveTab(id);
    setActivePane(pane);
}

void MainWindow::splitPaneAt(int index, bool right) {
    auto *t = currentTabInfo();
    if (!t || index < 0 || index >= t->panes.size() || !t->panes[index]) return;
    setActivePane(t->panes[index]);
    splitActivePane(right);
}

void MainWindow::splitActivePane(bool right) {
    auto *t = currentTabInfo();
    if (!t || !m_activePane) return;

    TerminalPane *targetPane = m_activePane;
    const QString originCwd = targetPane->currentDirectory();

    auto *splitter = new QSplitter(right ? Qt::Horizontal : Qt::Vertical);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(2);

    auto *newPane = createPane(originCwd, splitter);
    t->panes.append(newPane);

    QWidget *parentWidget = targetPane->parentWidget();
    if (auto *parentSplit = qobject_cast<QSplitter *>(parentWidget)) {
        const int idx = parentSplit->indexOf(targetPane);
        const QList<int> oldSizes = parentSplit->sizes();
        parentSplit->replaceWidget(idx, splitter);
        splitter->addWidget(targetPane);
        splitter->addWidget(newPane);
        parentSplit->setSizes(oldSizes);
    } else if (parentWidget && parentWidget->layout()) {
        parentWidget->layout()->replaceWidget(targetPane, splitter);
        splitter->addWidget(targetPane);
        splitter->addWidget(newPane);
    }

    const int half = (right ? splitter->width() : splitter->height()) / 2;
    splitter->setSizes({std::max(100, half), std::max(100, half)});

    setActivePane(newPane);
}

void MainWindow::removePane(TerminalPane *pane) {
    auto *t = currentTabInfo();
    if (!t || !pane) return;

    if (m_tabs.size() == 1 && t->panes.size() == 1 && t->panes.contains(pane)) {
        close();
        return;
    }

    t->panes.removeAll(pane);

    if (t->panes.isEmpty()) {
        closeTab(t->id);
        return;
    }

    QWidget *parentWidget = pane->parentWidget();
    pane->setParent(nullptr);
    pane->deleteLater();

    if (auto *parentSplit = qobject_cast<QSplitter *>(parentWidget)) {
        if (parentSplit->count() == 1) {
            QWidget *survivor = parentSplit->widget(0);
            QWidget *grandParent = parentSplit->parentWidget();
            if (auto *grandSplit = qobject_cast<QSplitter *>(grandParent)) {
                const int idx = grandSplit->indexOf(parentSplit);
                const QList<int> oldSizes = grandSplit->sizes();
                grandSplit->replaceWidget(idx, survivor);
                grandSplit->setSizes(oldSizes);
                parentSplit->deleteLater();
            } else if (grandParent && grandParent->layout()) {
                grandParent->layout()->replaceWidget(parentSplit, survivor);
                parentSplit->deleteLater();
            }
        }
    }

    // Set focus to another remaining pane
    TerminalPane *nextPane = nullptr;
    for (auto &p : t->panes) {
        if (p) { nextPane = p; break; }
    }
    if (nextPane) setActivePane(nextPane);
}

void MainWindow::closeActivePane() {
    if (m_activePane) {
        removePane(m_activePane);
    }
}

void MainWindow::rerunLastCommand() {
    if (m_activePane) {
        m_activePane->runLastCommand();
    }
}

void MainWindow::openSettings() {
    SettingsDialog dlg(this);
    dlg.exec();
}

MainWindow::TabInfo *MainWindow::currentTabInfo() {
    return tabInfoForId(m_tabBar->activeTabId());
}

MainWindow::TabInfo *MainWindow::tabInfoForId(int id) {
    for (auto &t : m_tabs) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

bool MainWindow::confirmClose() {
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Confirm Close"));
    box.setText(tr("Are you sure you want to close this window?"));
    box.setInformativeText(tr("All active terminal sessions in this window will be terminated."));
    auto *closeBtn = box.addButton(tr("Close Window"), QMessageBox::AcceptRole);
    auto *cancelBtn = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(closeBtn);
    box.setEscapeButton(cancelBtn);
    box.exec();
    return box.clickedButton() == closeBtn;
}

void MainWindow::closeEvent(QCloseEvent *e) {
    const auto s = CoreBridge::settings().toObject();
    if (s.value(QStringLiteral("confirmOnQuit")).toBool(true) && qEnvironmentVariableIsEmpty("TP_SHOT") && qEnvironmentVariableIsEmpty("TP_HEADLESS_EXIT")) {
        if (!confirmClose()) {
            e->ignore();
            return;
        }
    }
    saveSession();
    QMainWindow::closeEvent(e);
}

void MainWindow::saveSession() {
    const auto s = CoreBridge::settings().toObject();
    if (!s.value(QStringLiteral("restoreSession")).toBool(true)) return;

    QJsonArray tabArr;
    for (const auto &t : m_tabs) {
        if (!t.container) continue;
        QJsonObject tabObj;
        tabObj[QStringLiteral("id")] = t.id;
        tabObj[QStringLiteral("title")] = m_tabBar->tabTitle(t.id);
        if (t.container->layout() && t.container->layout()->count() > 0) {
            tabObj[QStringLiteral("layout")] = serializeNode(t.container->layout()->itemAt(0)->widget());
        }
        tabArr.append(tabObj);
    }

    CoreBridge::saveSession(QString::fromUtf8(QJsonDocument(tabArr).toJson(QJsonDocument::Compact)));
}

namespace {
QWidget *deserializeNode(const QJsonObject &obj, QWidget *parent, QList<QPointer<TerminalPane>> &panesList, MainWindow *mw) {
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("pane")) {
        const QString cwd = obj.value(QStringLiteral("savedCwd")).toString();
        auto *pane = mw->createPane(cwd, parent);
        panesList.append(pane);
        return pane;
    }
    if (type == QStringLiteral("split")) {
        const QString dir = obj.value(QStringLiteral("direction")).toString();
        auto *splitter = new QSplitter(dir == QStringLiteral("vertical") ? Qt::Horizontal : Qt::Vertical, parent);
        splitter->setChildrenCollapsible(false);
        splitter->setHandleWidth(2);
        const auto children = obj.value(QStringLiteral("children")).toArray();
        for (const auto &cVal : children) {
            auto *childW = deserializeNode(cVal.toObject(), splitter, panesList, mw);
            if (childW) splitter->addWidget(childW);
        }
        const auto sizes = obj.value(QStringLiteral("sizes")).toArray();
        if (!sizes.isEmpty()) {
            QList<int> szList;
            for (const auto &sz : sizes) szList.append(sz.toInt());
            splitter->setSizes(szList);
        }
        return splitter;
    }
    return nullptr;
}
}

bool MainWindow::restoreSession() {
    const auto s = CoreBridge::settings().toObject();
    if (!s.value(QStringLiteral("restoreSession")).toBool(true)) return false;

    const QString raw = CoreBridge::loadSession();
    if (raw.trimmed().isEmpty()) return false;

    const auto doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isArray() || doc.array().isEmpty()) return false;

    const auto tabArr = doc.array();
    for (const auto &tVal : tabArr) {
        const auto tabObj = tVal.toObject();
        const QString title = tabObj.value(QStringLiteral("title")).toString(QStringLiteral("Terminal"));
        const auto layoutObj = tabObj.value(QStringLiteral("layout")).toObject();

        auto *container = new QWidget(m_stack);
        auto *rootLayout = new QVBoxLayout(container);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->setSpacing(0);

        QList<QPointer<TerminalPane>> panesList;
        auto *rootChild = deserializeNode(layoutObj, container, panesList, this);
        if (!rootChild) {
            rootChild = createPane(QString(), container);
            panesList.append(qobject_cast<TerminalPane *>(rootChild));
        }
        rootLayout->addWidget(rootChild);

        const int id = m_tabBar->addTab(title);
        m_stack->addWidget(container);

        TabInfo info;
        info.id = id;
        info.container = container;
        info.panes = panesList;
        m_tabs.append(info);
    }

    if (!m_tabs.isEmpty()) {
        m_tabBar->setActiveTab(m_tabs.first().id);
        if (!m_tabs.first().panes.isEmpty() && m_tabs.first().panes.first()) {
            setActivePane(m_tabs.first().panes.first());
        }
        return true;
    }
    return false;
}

void MainWindow::keyPressEvent(QKeyEvent *e) {
    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool cmd = (mods & Qt::ControlModifier) != 0;
    const bool shift = (mods & Qt::ShiftModifier) != 0;

    if (cmd) {
        if (shift) {
            if (e->key() == Qt::Key_R) {
                splitActivePane(true); // Cmd+Shift+R: Split Right
                e->accept();
                return;
            }
            if (e->key() == Qt::Key_D) {
                splitActivePane(false); // Cmd+Shift+D: Split Down
                e->accept();
                return;
            }
            if (e->key() == Qt::Key_C) {
                if (m_activePane) m_activePane->openCommitDialog(); // Cmd+Shift+C: Commit Dialog
                e->accept();
                return;
            }
        } else {
            if (e->key() == Qt::Key_F) {
                if (m_activePane) m_activePane->toggleSearch(); // Cmd+F: Find
                e->accept();
                return;
            }
            if (e->key() == Qt::Key_G) {
                if (m_activePane) m_activePane->toggleAi(); // Cmd+G: Ask AI
                e->accept();
                return;
            }
            if (e->key() == Qt::Key_B) {
                if (m_activePane) m_activePane->toggleTask(); // Cmd+B: Run Background Task
                e->accept();
                return;
            }
            if (e->key() == Qt::Key_T) {
                newTab();
                e->accept();
                return;
            }
            if (e->key() == Qt::Key_W) {
                closeActivePane();
                e->accept();
                return;
            }
            if (e->key() == Qt::Key_R) {
                rerunLastCommand();
                e->accept();
                return;
            }
            if (e->key() == Qt::Key_Comma) {
                openSettings();
                e->accept();
                return;
            }
        }
    }

    QMainWindow::keyPressEvent(e);
}
