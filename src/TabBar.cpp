#include "TabBar.h"
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStackedLayout>
#include "MenuStyle.h"
#include "Platform.h"

TabItem::TabItem(int id, const QString &title, QWidget *parent)
    : QFrame(parent), m_id(id), m_title(title) {
    setFixedHeight(32);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 8, 0);
    layout->setSpacing(8);

    m_label = new QLabel(title, this);
    m_label->setAlignment(Qt::AlignCenter);
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

void TabItem::setActive(bool active) {
    m_active = active;
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
}

void TabItem::setCloseable(bool closeable) {
    m_closeBtn->setVisible(closeable);
}

void TabItem::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        emit selected(m_id);
    } else if (e->button() == Qt::MiddleButton) {
        emit closeRequested(m_id);
    }
    QFrame::mousePressEvent(e);
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

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 8, 0);
    mainLayout->setSpacing(0);

    m_tabLayout = new QHBoxLayout();
    m_tabLayout->setContentsMargins(0, 0, 0, 0);
    m_tabLayout->setSpacing(0);
    mainLayout->addLayout(m_tabLayout, 1);

    m_addBtn = new QToolButton(this);
    m_addBtn->setText(QStringLiteral("+"));
    m_addBtn->setToolTip(Platform::hint(QStringLiteral("New tab (Cmd+T)")));
    m_addBtn->setAutoRaise(true);
    m_addBtn->setFixedSize(28, 28);
    m_addBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; color: #64748b; font-size: 18px; font-weight: 300; }"
        "QToolButton:hover { color: #e2e8f0; }"
    ));
    connect(m_addBtn, &QToolButton::clicked, this, &TabBar::newTabRequested);
    mainLayout->addWidget(m_addBtn, 0);

    m_menuBtn = new QToolButton(this);
    m_menuBtn->setText(QStringLiteral("⋯"));
    m_menuBtn->setToolTip(QStringLiteral("Tab menu"));
    m_menuBtn->setAutoRaise(true);
    m_menuBtn->setFixedSize(28, 28);
    m_menuBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; color: #64748b; font-size: 16px; font-weight: bold; }"
        "QToolButton:hover { color: #e2e8f0; }"
    ));
    connect(m_menuBtn, &QToolButton::clicked, this, [this] {
        showTabMenu(m_activeId, m_menuBtn->mapToGlobal(QPoint(0, m_menuBtn->height() + 2)));
    });
    mainLayout->addWidget(m_menuBtn, 0);

    m_settingsBtn = new QToolButton(this);
    m_settingsBtn->setText(QStringLiteral("⚙"));
    m_settingsBtn->setToolTip(Platform::hint(QStringLiteral("Settings (Cmd+,)")));
    m_settingsBtn->setAutoRaise(true);
    m_settingsBtn->setFixedSize(28, 28);
    m_settingsBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; color: #64748b; font-size: 15px; }"
        "QToolButton:hover { color: #e2e8f0; }"
    ));
    connect(m_settingsBtn, &QToolButton::clicked, this, &TabBar::settingsRequested);
    mainLayout->addWidget(m_settingsBtn, 0);
}

int TabBar::addTab(const QString &title) {
    const int id = m_nextId++;
    auto *item = new TabItem(id, title, this);
    m_tabs.append(item);
    m_tabLayout->addWidget(item, 1);

    connect(item, &TabItem::selected, this, &TabBar::setActiveTab);
    connect(item, &TabItem::closeRequested, this, &TabBar::tabCloseRequested);
    connect(item, &TabItem::contextMenuRequested, this, &TabBar::showTabMenu);

    updateTabStyles();
    setActiveTab(id);
    return id;
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
    MenuStyle::apply(&menu, true);

    auto *newTabAct = menu.addAction(MenuStyle::makeIcon(MenuIcons::kAdd, true), Platform::menuText(tr("New Tab\t⌘T")), this, &TabBar::newTabRequested);
    Q_UNUSED(newTabAct);

    auto *newWinAct = menu.addAction(MenuStyle::makeIcon(MenuIcons::kWindow, true), Platform::menuText(tr("New Window\t⌘N")), this, &TabBar::newWindowRequested);
    Q_UNUSED(newWinAct);

    if (id > 0) {
        menu.addSeparator();

        menu.addAction(MenuStyle::makeIcon(MenuIcons::kRename, true), tr("Rename Tab…"), this, [this, id] {
            startRename(id);
        });

        menu.addAction(MenuStyle::makeIcon(MenuIcons::kCopy, true), tr("Duplicate Tab"), this, [this, id] {
            emit duplicateTabRequested(id);
        });
    }

    menu.addSeparator();

    if (id > 0) {
        menu.addAction(MenuStyle::makeIcon(MenuIcons::kClose, true), Platform::menuText(tr("Close Tab\t⌘W")), this, [this, id] {
            emit tabCloseRequested(id);
        });

        auto *closeOtherAct = menu.addAction(tr("Close Other Tabs"), this, [this, id] {
            emit closeOtherTabsRequested(id);
        });
        closeOtherAct->setEnabled(m_tabs.size() > 1);
    }

    menu.addAction(tr("Close All Tabs"), this, &TabBar::closeAllTabsRequested);

    menu.addSeparator();

    menu.addAction(MenuStyle::makeIcon(MenuIcons::kSplitRight, true), Platform::menuText(tr("Split Right\t⌘⇧R")), this, [this] {
        emit splitRequested(true);
    });

    menu.addAction(MenuStyle::makeIcon(MenuIcons::kSplitDown, true), Platform::menuText(tr("Split Down\t⌘⇧D")), this, [this] {
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
