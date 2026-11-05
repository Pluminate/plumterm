#pragma once
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QSplitter>
#include <QStackedWidget>

class TabBar;
class TerminalPane;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    TerminalPane *activePane() const { return m_activePane; }
    void newTab(const QString &cwd = {});
    void splitActivePane(bool right);
    void splitPaneAt(int index, bool right);
    void closeActivePane();
    void rerunLastCommand();
    void openSettings();
    bool restoreSession();
    void saveSession();

    TerminalPane *createPane(const QString &cwd, QWidget *parent);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private:
    struct TabInfo {
        int id;
        QWidget *container = nullptr;
        QList<QPointer<TerminalPane>> panes;
    };

    void setActivePane(TerminalPane *pane);
    void removePane(TerminalPane *pane);
    TabInfo *currentTabInfo();
    TabInfo *tabInfoForId(int id);
    void setupShortcuts();

    TabBar *m_tabBar = nullptr;
    QStackedWidget *m_stack = nullptr;
    QList<TabInfo> m_tabs;
    QPointer<TerminalPane> m_activePane;
};
