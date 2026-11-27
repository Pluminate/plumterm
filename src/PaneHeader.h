#pragma once
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QPoint>
#include <QSplitter>
#include <QList>
#include <QVector>
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
    void setDark(bool dark);
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
    void mouseMoveEvent(QMouseEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void updateResizeCursor();
    void showFolderMenu(const QPoint &globalPos);
    void showBranchMenu(const QPoint &globalPos);

    // Header-drag resizing: moves the splitter handles adjacent to this pane.
    struct DragHandle { QSplitter *splitter; int index; QList<int> startSizes; };
    QVector<DragHandle> m_dragHandles;
    QPoint m_dragStart;

    TerminalView *m_view = nullptr;
    QLabel *m_folderLabel = nullptr;
    QToolButton *m_branchBtn = nullptr;
    QLabel *m_statsLabel = nullptr;
    QToolButton *m_aiBtn = nullptr;
    QToolButton *m_taskBtn = nullptr;
    QToolButton *m_taskIndicatorBtn = nullptr;
    QToolButton *m_runBtn = nullptr;

    QTimer m_pollTimer;
    QTimer m_taskDismissTimer;   // auto-hides a finished task indicator
    bool m_taskFinished = false;
    QString m_lastCwd;
    QString m_lastBranch;
    bool m_isActive = false;
    bool m_dark = true;
    bool m_polling = false;
};
