#pragma once
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QPushButton>
#include <QToolButton>

class TabItem : public QFrame {
    Q_OBJECT
public:
    explicit TabItem(int id, const QString &title, QWidget *parent = nullptr);

    int id() const { return m_id; }
    QString title() const { return m_title; }
    void setTitle(const QString &title);
    void setActive(bool active);
    void setDark(bool dark);
    void setCloseable(bool closeable);
    void startRename();

signals:
    void selected(int id);
    void closeRequested(int id);
    void renamed(int id, const QString &newTitle);
    void contextMenuRequested(int id, const QPoint &globalPos);
    void dragMoved(int id, const QPoint &globalPos);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    void finishRename();

    int m_id = 0;
    QString m_title;
    bool m_active = false;
    bool m_dark = true;
    bool m_pressing = false;
    bool m_dragging = false;
    QPoint m_dragStartPos;
    QLabel *m_label = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QLineEdit *m_edit = nullptr;
};

class TabBar : public QFrame {
    Q_OBJECT
public:
    explicit TabBar(QWidget *parent = nullptr);

    int addTab(const QString &title);
    void removeTab(int id);
    void moveTab(int fromIndex, int toIndex);
    void setActiveTab(int id);
    int activeTabId() const { return m_activeId; }
    int count() const { return m_tabs.size(); }
    int tabIdAt(int index) const;
    int indexOfTab(int id) const;
    QString tabTitle(int id) const;
    void setTabTitle(int id, const QString &title);
    void startRename(int id);
    void showTabMenu(int id, const QPoint &globalPos);
    void setAppMenu(QMenu *menu);
    void setDark(bool dark);
    void setMaximizedState(bool maximized);
    void setTitleBarInset(int leftPx);
    bool isCaptionArea(const QPoint &globalPos) const;

signals:
    void currentChanged(int id);
    void tabMoved(int fromIndex, int toIndex);
    void tabCloseRequested(int id);
    void newTabRequested();
    void newWindowRequested();
    void duplicateTabRequested(int id);
    void closeOtherTabsRequested(int id);
    void closeAllTabsRequested();
    void splitRequested(bool right);
    void settingsRequested();
    void minimizeRequested();
    void maximizeRestoreRequested();
    void windowCloseRequested();

protected:
    void contextMenuEvent(QContextMenuEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    void updateTabStyles();
    void handleTabDrag(int id, const QPoint &globalPos);

    QHBoxLayout *m_mainLayout = nullptr;
    QHBoxLayout *m_tabLayout = nullptr;
    QList<TabItem *> m_tabs;
    QToolButton *m_addBtn = nullptr;
    QToolButton *m_menuBtn = nullptr;
    QToolButton *m_settingsBtn = nullptr;
    QToolButton *m_minBtn = nullptr;
    QToolButton *m_maxBtn = nullptr;
    QToolButton *m_winCloseBtn = nullptr;
    QMenu *m_appMenu = nullptr;
    bool m_dark = true;
    int m_nextId = 1;
    int m_activeId = -1;
};
