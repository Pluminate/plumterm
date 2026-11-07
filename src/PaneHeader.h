#pragma once
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QToolButton>

class TerminalView;

// Header status bar above each terminal pane:
// [📁 folder] [⎇ branch] [+ins -del]  ---spacer---  [✨ AI] [⚙ Task] [▶ Run]
class PaneHeader : public QFrame {
    Q_OBJECT
public:
    explicit PaneHeader(TerminalView *view, QWidget *parent = nullptr);

    void setActive(bool active);
    void updateStatus();
    void setTaskStatus(bool running, const QString &command, int exitCode = 0);

signals:
    void splitRequested(bool right);
    void closeRequested();
    void runRequested();
    void runContextMenuRequested(const QPoint &globalPos);
    void aiRequested();
    void taskRequested();
    void taskIndicatorClicked();
    void searchRequested();
    void commitRequested();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    void showFolderMenu(const QPoint &globalPos);
    void showBranchMenu(const QPoint &globalPos);

    TerminalView *m_view = nullptr;
    QLabel *m_folderLabel = nullptr;
    QToolButton *m_branchBtn = nullptr;
    QLabel *m_statsLabel = nullptr;
    QToolButton *m_aiBtn = nullptr;
    QToolButton *m_taskBtn = nullptr;
    QToolButton *m_taskIndicatorBtn = nullptr;
    QToolButton *m_runBtn = nullptr;

    QTimer m_pollTimer;
    QString m_lastCwd;
    QString m_lastBranch;
    bool m_isActive = false;
    bool m_polling = false;
};
