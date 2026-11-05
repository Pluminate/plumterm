#pragma once
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
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
    void setCloseable(bool closeable);

signals:
    void selected(int id);
    void closeRequested(int id);
    void renamed(int id, const QString &newTitle);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    void startRename();
    void finishRename();

    int m_id = 0;
    QString m_title;
    bool m_active = false;
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
    void setActiveTab(int id);
    int activeTabId() const { return m_activeId; }
    int count() const { return m_tabs.size(); }
    QString tabTitle(int id) const;
    void setTabTitle(int id, const QString &title);

signals:
    void currentChanged(int id);
    void tabCloseRequested(int id);
    void newTabRequested();
    void settingsRequested();

private:
    void updateTabStyles();

    QHBoxLayout *m_tabLayout = nullptr;
    QList<TabItem *> m_tabs;
    QToolButton *m_addBtn = nullptr;
    QToolButton *m_settingsBtn = nullptr;
    int m_nextId = 1;
    int m_activeId = -1;
};
