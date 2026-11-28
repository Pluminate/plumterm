#include "TabBar.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStackedLayout>
#include <QWindow>
#include "MenuStyle.h"

TabItem::TabItem(int id, const QString &title, QWidget *parent)
    : QFrame(parent), m_id(id), m_title(title) {
    setFixedHeight(34);
    setMinimumWidth(90);
    setMaximumWidth(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 8, 0);
    layout->setSpacing(8);

    m_label = new QLabel(title, this);
    m_label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(m_label, 1);

    m_edit = new QLineEdit(this);
    m_edit->setVisible(false);
    m_edit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: #111; color: #fff; border: 1px solid #38bdf8; border-radius: 2px; padding: 1px 4px; font-size: 11px; }"
    ));
    connect(m_edit, &QLineEdit::editingFinished, this, &TabItem::finishRename);
    layout->addWidget(m_edit, 1);

    m_closeBtn = new QPushButton(QStringLiteral("×"), this);
    m_closeBtn->setFixedSize(16, 16);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: none; color: #666666; font-size: 14px; font-weight: bold; line-height: 14px; }"
        "QPushButton:hover { color: #ffffff; background: #333333; border-radius: 8px; }"
    ));
    connect(m_closeBtn, &QPushButton::clicked, this, [this] {
        emit closeRequested(m_id);
    });
    layout->addWidget(m_closeBtn, 0);

    setActive(false);
}

void TabItem::setTitle(const QString &title) {
    m_title = title;
    m_label->setText(title);
}

void TabItem::setDark(bool dark) {
    m_dark = dark;
    m_edit->setStyleSheet(dark
        ? QStringLiteral("QLineEdit { background: #111; color: #fff; border: 1px solid #38bdf8; border-radius: 2px; padding: 1px 4px; font-size: 11px; }")
        : QStringLiteral("QLineEdit { background: #fff; color: #0f172a; border: 1px solid #0284c7; border-radius: 2px; padding: 1px 4px; font-size: 11px; }"));
    m_closeBtn->setStyleSheet(dark
        ? QStringLiteral(
            "QPushButton { background: transparent; border: none; color: #666666; font-size: 14px; font-weight: bold; line-height: 14px; }"
            "QPushButton:hover { color: #ffffff; background: #333333; border-radius: 8px; }")
        : QStringLiteral(
            "QPushButton { background: transparent; border: none; color: #64748b; font-size: 14px; font-weight: bold; line-height: 14px; }"
            "QPushButton:hover { color: #0f172a; background: #cbd5e1; border-radius: 8px; }"));
    setActive(m_active);
}

void TabItem::setActive(bool active) {
    m_active = active;
    if (m_dark) {
        if (active) {
            setStyleSheet(QStringLiteral(
                "TabItem { background-color: #000000; border-right: 1px solid #222222; border-bottom: 2px solid #38bdf8; }"
            ));
            m_label->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 11px; font-weight: 500;"));
        } else {
            setStyleSheet(QStringLiteral(
                "TabItem { background-color: #1a1a1a; border-right: 1px solid #222222; border-bottom: 2px solid #1a1a1a; }"
                "TabItem:hover { background-color: #141414; }"
            ));
            m_label->setStyleSheet(QStringLiteral("color: #666666; font-size: 11px; font-weight: 500;"));
        }
    } else {
        if (active) {
            setStyleSheet(QStringLiteral(
                "TabItem { background-color: #ffffff; border-right: 1px solid #e2e8f0; border-bottom: 2px solid #0284c7; }"
            ));
            m_label->setStyleSheet(QStringLiteral("color: #0f172a; font-size: 11px; font-weight: 500;"));
        } else {
            setStyleSheet(QStringLiteral(
                "TabItem { background-color: #e5e7eb; border-right: 1px solid #e2e8f0; border-bottom: 2px solid #e5e7eb; }"
                "TabItem:hover { background-color: #eef2f7; }"
            ));
            m_label->setStyleSheet(QStringLiteral("color: #64748b; font-size: 11px; font-weight: 500;"));
        }
    }
}

void TabItem::setCloseable(bool closeable) {
    m_closeBtn->setVisible(closeable);
}

void TabItem::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        emit selected(m_id);
        if (!m_edit || !m_edit->isVisible()) {
            m_pressing = true;
            m_dragging = false;
            m_dragStartPos = e->globalPosition().toPoint();
        }
        e->accept();
        return;
    } else if (e->button() == Qt::MiddleButton) {
        emit closeRequested(m_id);
        e->accept();
        return;
    }
    QFrame::mousePressEvent(e);
}

void TabItem::mouseMoveEvent(QMouseEvent *e) {
    if (!m_pressing || !(e->buttons() & Qt::LeftButton) || (m_edit && m_edit->isVisible())) {
        QFrame::mouseMoveEvent(e);
        return;
    }

    const QPoint globalPos = e->globalPosition().toPoint();
    if (!m_dragging) {
        if ((globalPos - m_dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
            m_dragging = true;
            setCursor(Qt::ClosedHandCursor);
        }
    }

    if (m_dragging) {
        emit dragMoved(m_id, globalPos);
        e->accept();
        return;
    }

    QFrame::mouseMoveEvent(e);
}

void TabItem::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        m_pressing = false;
        if (m_dragging) {
            m_dragging = false;
            setCursor(Qt::PointingHandCursor);
            e->accept();
            return;
        }
    }
    QFrame::mouseReleaseEvent(e);
}

void TabItem::contextMenuEvent(QContextMenuEvent *e) {
    emit selected(m_id);
    emit contextMenuRequested(m_id, e->globalPos());
    e->accept();
}

void TabItem::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        startRename();
        e->accept();
        return;
    }
    QFrame::mouseDoubleClickEvent(e);
}

void TabItem::startRename() {
    m_edit->setText(m_title);
    m_label->setVisible(false);
    m_edit->setVisible(true);
    m_edit->setFocus();
    m_edit->selectAll();
}

void TabItem::finishRename() {
    if (!m_edit->isVisible()) return;
    const QString newTitle = m_edit->text().trimmed();
    if (!newTitle.isEmpty()) {
        setTitle(newTitle);
        emit renamed(m_id, newTitle);
    }
    m_edit->setVisible(false);
    m_label->setVisible(true);
}

// ------------------------------------------------------------------------------------------------

TabBar::TabBar(QWidget *parent) : QFrame(parent) {
    setFixedHeight(34);
    setStyleSheet(QStringLiteral(
        "TabBar { background-color: #111111; border-bottom: 1px solid #222222; }"
    ));

    m_mainLayout = new QHBoxLayout(this);
#ifdef Q_OS_MACOS
    m_mainLayout->setContentsMargins(78, 0, 6, 0);
#else
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
#endif
    m_mainLayout->setSpacing(0);

    m_tabLayout = new QHBoxLayout();
    m_tabLayout->setContentsMargins(0, 0, 0, 0);
    m_tabLayout->setSpacing(0);
    m_mainLayout->addLayout(m_tabLayout, 0);

    m_addBtn = new QToolButton(this);
    m_addBtn->setText(QStringLiteral("+"));
    m_addBtn->setToolTip(QStringLiteral("New tab (Ctrl+T)"));
    m_addBtn->setAutoRaise(true);
    m_addBtn->setFixedSize(32, 34);
    m_addBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; color: #94a3b8; font-size: 17px; font-weight: 300; }"
        "QToolButton:hover { background: #222222; color: #ffffff; }"
    ));
    connect(m_addBtn, &QToolButton::clicked, this, &TabBar::newTabRequested);
    m_mainLayout->addWidget(m_addBtn, 0);

    m_menuBtn = new QToolButton(this);
    m_menuBtn->setText(QStringLiteral("▾"));
    m_menuBtn->setToolTip(QStringLiteral("Menu"));
    m_menuBtn->setAutoRaise(true);
    m_menuBtn->setFixedSize(28, 34);
    m_menuBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; color: #94a3b8; font-size: 12px; }"
        "QToolButton:hover { background: #222222; color: #ffffff; }"
    ));
    connect(m_menuBtn, &QToolButton::clicked, this, [this] {
        const QPoint pos = m_menuBtn->mapToGlobal(QPoint(0, m_menuBtn->height()));
        if (m_appMenu) {
            m_appMenu->popup(pos);
        } else {
            showTabMenu(m_activeId, pos);
        }
    });
    m_mainLayout->addWidget(m_menuBtn, 0);

    m_mainLayout->addStretch(1);

#ifdef Q_OS_WIN
    const QString winBtnStyle = QStringLiteral(
        "QToolButton { background: transparent; border: none; color: #cccccc; font-size: 12px; }"
        "QToolButton:hover { background: #2a2a2a; color: #ffffff; }"
    );
    m_minBtn = new QToolButton(this);
    m_minBtn->setText(QStringLiteral("─"));
    m_minBtn->setToolTip(QStringLiteral("Minimize"));
    m_minBtn->setFixedSize(46, 34);
    m_minBtn->setStyleSheet(winBtnStyle);
    connect(m_minBtn, &QToolButton::clicked, this, &TabBar::minimizeRequested);
    m_mainLayout->addWidget(m_minBtn, 0);

    m_maxBtn = new QToolButton(this);
    m_maxBtn->setText(QStringLiteral("□"));
    m_maxBtn->setToolTip(QStringLiteral("Maximize"));
    m_maxBtn->setFixedSize(46, 34);
    m_maxBtn->setStyleSheet(winBtnStyle);
    connect(m_maxBtn, &QToolButton::clicked, this, &TabBar::maximizeRestoreRequested);
    m_mainLayout->addWidget(m_maxBtn, 0);

    m_winCloseBtn = new QToolButton(this);
    m_winCloseBtn->setText(QStringLiteral("×"));
    m_winCloseBtn->setToolTip(QStringLiteral("Close"));
    m_winCloseBtn->setFixedSize(46, 34);
    m_winCloseBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; color: #cccccc; font-size: 16px; }"
        "QToolButton:hover { background: #e81123; color: #ffffff; }"
    ));
    connect(m_winCloseBtn, &QToolButton::clicked, this, &TabBar::windowCloseRequested);
    m_mainLayout->addWidget(m_winCloseBtn, 0);
#else
    m_settingsBtn = new QToolButton(this);
    m_settingsBtn->setText(QStringLiteral("⚙"));
    m_settingsBtn->setToolTip(QStringLiteral("Settings (Cmd+,)"));
    m_settingsBtn->setAutoRaise(true);
    m_settingsBtn->setFixedSize(28, 28);
    m_settingsBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; color: #64748b; font-size: 15px; }"
        "QToolButton:hover { color: #e2e8f0; }"
    ));
    connect(m_settingsBtn, &QToolButton::clicked, this, &TabBar::settingsRequested);
    m_mainLayout->addWidget(m_settingsBtn, 0);
#endif
}

int TabBar::addTab(const QString &title) {
    const int id = m_nextId++;
    auto *item = new TabItem(id, title, this);
    item->setDark(m_dark);
    m_tabs.append(item);
    m_tabLayout->addWidget(item, 1);

    connect(item, &TabItem::selected, this, &TabBar::setActiveTab);
    connect(item, &TabItem::closeRequested, this, &TabBar::tabCloseRequested);
    connect(item, &TabItem::contextMenuRequested, this, &TabBar::showTabMenu);
    connect(item, &TabItem::dragMoved, this, &TabBar::handleTabDrag);

    updateTabStyles();
    setActiveTab(id);
    return id;
}

void TabBar::setAppMenu(QMenu *menu) {
    m_appMenu = menu;
}

void TabBar::setDark(bool dark) {
    m_dark = dark;
    setStyleSheet(dark
        ? QStringLiteral("TabBar { background-color: #111111; border-bottom: 1px solid #222222; }")
        : QStringLiteral("TabBar { background-color: #f3f4f6; border-bottom: 1px solid #e2e8f0; }"));

    const QString actionStyle = dark
        ? QStringLiteral("QToolButton { background: transparent; border: none; color: #94a3b8; font-size: %1px; font-weight: %2; }"
                         "QToolButton:hover { background: #222222; color: #ffffff; }")
        : QStringLiteral("QToolButton { background: transparent; border: none; color: #475569; font-size: %1px; font-weight: %2; }"
                         "QToolButton:hover { background: #e2e8f0; color: #0f172a; }");
    if (m_addBtn) m_addBtn->setStyleSheet(actionStyle.arg(17).arg(300));
    if (m_menuBtn) m_menuBtn->setStyleSheet(actionStyle.arg(12).arg(400));

    if (m_settingsBtn) {
        m_settingsBtn->setStyleSheet(dark
            ? QStringLiteral("QToolButton { background: transparent; border: none; color: #64748b; font-size: 15px; } QToolButton:hover { color: #e2e8f0; }")
            : QStringLiteral("QToolButton { background: transparent; border: none; color: #475569; font-size: 15px; } QToolButton:hover { color: #0f172a; }"));
    }

    const QString winBtnStyle = dark
        ? QStringLiteral("QToolButton { background: transparent; border: none; color: #cccccc; font-size: 12px; } QToolButton:hover { background: #2a2a2a; color: #ffffff; }")
        : QStringLiteral("QToolButton { background: transparent; border: none; color: #334155; font-size: 12px; } QToolButton:hover { background: #e2e8f0; color: #0f172a; }");
    if (m_minBtn) m_minBtn->setStyleSheet(winBtnStyle);
    if (m_maxBtn) m_maxBtn->setStyleSheet(winBtnStyle);
    if (m_winCloseBtn) {
        m_winCloseBtn->setStyleSheet(dark
            ? QStringLiteral("QToolButton { background: transparent; border: none; color: #cccccc; font-size: 16px; } QToolButton:hover { background: #e81123; color: #ffffff; }")
            : QStringLiteral("QToolButton { background: transparent; border: none; color: #334155; font-size: 16px; } QToolButton:hover { background: #e81123; color: #ffffff; }"));
    }

    for (auto *item : m_tabs) {
        item->setDark(dark);
    }
}

void TabBar::setMaximizedState(bool maximized) {
    if (!m_maxBtn) return;
    m_maxBtn->setText(maximized ? QStringLiteral("❐") : QStringLiteral("□"));
    m_maxBtn->setToolTip(maximized ? QStringLiteral("Restore") : QStringLiteral("Maximize"));
}

void TabBar::setTitleBarInset(int leftPx) {
    if (!m_mainLayout) return;
    const QMargins m = m_mainLayout->contentsMargins();
    m_mainLayout->setContentsMargins(leftPx, m.top(), m.right(), m.bottom());
}

bool TabBar::isCaptionArea(const QPoint &globalPos) const {
    const QPoint localPos = mapFromGlobal(globalPos);
    if (!rect().contains(localPos)) return false;
    QWidget *child = childAt(localPos);
    return child == nullptr || child == this;
}

void TabBar::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && isCaptionArea(e->globalPosition().toPoint())) {
        if (QWidget *win = window()) {
            if (QWindow *wh = win->windowHandle()) {
                if (wh->startSystemMove()) {
                    e->accept();
                    return;
                }
            }
        }
    }
    QFrame::mousePressEvent(e);
}

void TabBar::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && isCaptionArea(e->globalPosition().toPoint())) {
        emit maximizeRestoreRequested();
        e->accept();
        return;
    }
    QFrame::mouseDoubleClickEvent(e);
}

void TabBar::removeTab(int id) {
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->id() == id) {
            auto *item = m_tabs.takeAt(i);
            m_tabLayout->removeWidget(item);
            item->deleteLater();
            break;
        }
    }
    updateTabStyles();
    if (m_activeId == id) {
        if (!m_tabs.isEmpty()) {
            setActiveTab(m_tabs.last()->id());
        } else {
            m_activeId = -1;
        }
    }
}

void TabBar::moveTab(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= m_tabs.size() ||
        toIndex < 0 || toIndex >= m_tabs.size() ||
        fromIndex == toIndex) {
        return;
    }

    TabItem *item = m_tabs.at(fromIndex);
    m_tabs.move(fromIndex, toIndex);
    m_tabLayout->removeWidget(item);
    m_tabLayout->insertWidget(toIndex, item, 1);
    m_tabLayout->activate();
    if (layout()) layout()->activate();

    updateTabStyles();
    emit tabMoved(fromIndex, toIndex);
}

int TabBar::tabIdAt(int index) const {
    if (index < 0 || index >= m_tabs.size()) return -1;
    return m_tabs.at(index)->id();
}

int TabBar::indexOfTab(int id) const {
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->id() == id) return i;
    }
    return -1;
}

void TabBar::handleTabDrag(int id, const QPoint &globalPos) {
    if (m_tabs.size() <= 1) return;
    const int fromIndex = indexOfTab(id);
    if (fromIndex < 0) return;

    const int x = mapFromGlobal(globalPos).x();
    const int fromCenter = m_tabs[fromIndex]->geometry().center().x();

    int toIndex = fromIndex;
    if (x > fromCenter) {
        for (int i = fromIndex + 1; i < m_tabs.size(); ++i) {
            if (x > m_tabs[i]->geometry().center().x()) {
                toIndex = i;
            }
        }
    } else if (x < fromCenter) {
        for (int i = fromIndex - 1; i >= 0; --i) {
            if (x < m_tabs[i]->geometry().center().x()) {
                toIndex = i;
            }
        }
    }

    if (toIndex != fromIndex) {
        moveTab(fromIndex, toIndex);
    }
}

void TabBar::setActiveTab(int id) {
    m_activeId = id;
    for (auto *t : m_tabs) {
        t->setActive(t->id() == id);
    }
    emit currentChanged(id);
}

QString TabBar::tabTitle(int id) const {
    for (auto *t : m_tabs) {
        if (t->id() == id) return t->title();
    }
    return QString();
}

void TabBar::setTabTitle(int id, const QString &title) {
    for (auto *t : m_tabs) {
        if (t->id() == id) {
            t->setTitle(title);
            break;
        }
    }
}

void TabBar::startRename(int id) {
    for (auto *t : m_tabs) {
        if (t->id() == id) {
            t->startRename();
            break;
        }
    }
}

void TabBar::contextMenuEvent(QContextMenuEvent *e) {
    showTabMenu(m_activeId, e->globalPos());
    e->accept();
}

void TabBar::showTabMenu(int id, const QPoint &globalPos) {
    QMenu menu(this);
    MenuStyle::apply(&menu, m_dark);

    auto *newTabAct = menu.addAction(MenuStyle::makeIcon(MenuIcons::kAdd, m_dark), tr("New Tab\t⌘T"), this, &TabBar::newTabRequested);
    Q_UNUSED(newTabAct);

    auto *newWinAct = menu.addAction(MenuStyle::makeIcon(MenuIcons::kWindow, m_dark), tr("New Window\t⌘N"), this, &TabBar::newWindowRequested);
    Q_UNUSED(newWinAct);

    if (id > 0) {
        menu.addSeparator();

        menu.addAction(MenuStyle::makeIcon(MenuIcons::kRename, m_dark), tr("Rename Tab…"), this, [this, id] {
            startRename(id);
        });

        menu.addAction(MenuStyle::makeIcon(MenuIcons::kCopy, m_dark), tr("Duplicate Tab"), this, [this, id] {
            emit duplicateTabRequested(id);
        });
    }

    menu.addSeparator();

    if (id > 0) {
        menu.addAction(MenuStyle::makeIcon(MenuIcons::kClose, m_dark), tr("Close Tab\t⌘W"), this, [this, id] {
            emit tabCloseRequested(id);
        });

        auto *closeOtherAct = menu.addAction(tr("Close Other Tabs"), this, [this, id] {
            emit closeOtherTabsRequested(id);
        });
        closeOtherAct->setEnabled(m_tabs.size() > 1);
    }

    menu.addAction(tr("Close All Tabs"), this, &TabBar::closeAllTabsRequested);

    menu.addSeparator();

    menu.addAction(MenuStyle::makeIcon(MenuIcons::kSplitRight, m_dark), tr("Split Right\t⌘⇧R"), this, [this] {
        emit splitRequested(true);
    });

    menu.addAction(MenuStyle::makeIcon(MenuIcons::kSplitDown, m_dark), tr("Split Down\t⌘⇧D"), this, [this] {
        emit splitRequested(false);
    });

    menu.exec(globalPos);
}

void TabBar::updateTabStyles() {
    const bool canClose = m_tabs.size() > 1;
    for (auto *t : m_tabs) {
        t->setCloseable(canClose);
    }
}
