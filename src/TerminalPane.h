#pragma once
#include <QWidget>

class AskAiBar;
class PaneHeader;
class SearchBar;
class TerminalView;

class TerminalPane : public QWidget {
    Q_OBJECT
public:
    explicit TerminalPane(const QString &cwd = {}, QWidget *parent = nullptr);

    TerminalView *view() const { return m_view; }
    PaneHeader *header() const { return m_header; }
    SearchBar *searchBar() const { return m_searchBar; }
    AskAiBar *aiBar() const { return m_aiBar; }
    QString currentDirectory() const;

    void setActive(bool active);
    bool isActive() const { return m_isActive; }

    void runLastCommand();
    void toggleSearch();
    void toggleAi();
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
    TerminalView *m_view = nullptr;
    bool m_isActive = false;
};
