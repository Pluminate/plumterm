#pragma once
#include <QPointer>
#include <QProcess>
#include <QWidget>

class AskAiBar;
class PaneHeader;
class SearchBar;
class TaskBar;
class TaskOutputDialog;
class TerminalView;

class TerminalPane : public QWidget {
    Q_OBJECT
public:
    explicit TerminalPane(const QString &cwd = {}, QWidget *parent = nullptr);

    TerminalView *view() const { return m_view; }
    PaneHeader *header() const { return m_header; }
    SearchBar *searchBar() const { return m_searchBar; }
    AskAiBar *aiBar() const { return m_aiBar; }
    TaskBar *taskBar() const { return m_taskBar; }
    QString currentDirectory() const;

    void setActive(bool active);
    bool isActive() const { return m_isActive; }

    void runLastCommand();
    QString getProjectRunCommand(const QString &cwd = QString()) const;
    void showRunContextMenu(const QPoint &globalPos);
    void toggleSearch();
    void toggleAi();
    void toggleTask();
    void runBackgroundTask(const QString &cmd);
    void showTaskOutput();
    void stopBackgroundTask();
    void openCommitDialog();

signals:
    void activated(TerminalPane *pane);
    void splitRequested(TerminalPane *pane, bool right);
    void closeRequested(TerminalPane *pane);
    void newTabRequested();

protected:
    void mousePressEvent(QMouseEvent *e) override;

private:
    PaneHeader *m_header = nullptr;
    SearchBar *m_searchBar = nullptr;
    AskAiBar *m_aiBar = nullptr;
    TaskBar *m_taskBar = nullptr;
    TerminalView *m_view = nullptr;
    bool m_isActive = false;

    QProcess *m_bgProcess = nullptr;
    QString m_bgCommand;
    QString m_bgOutput;
    int m_bgExitCode = 0;
    bool m_bgRunning = false;
    QPointer<TaskOutputDialog> m_taskDialog;
};
