#pragma once
#include <QFrame>
#include <QLineEdit>
#include <QToolButton>

class TerminalView;

class SearchBar : public QFrame {
    Q_OBJECT
public:
    explicit SearchBar(TerminalView *view, QWidget *parent = nullptr);

    void openSearch();
    void closeSearch();

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    void findNext();
    void findPrev();

    TerminalView *m_view = nullptr;
    QLineEdit *m_input = nullptr;
    QToolButton *m_prevBtn = nullptr;
    QToolButton *m_nextBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;
};
