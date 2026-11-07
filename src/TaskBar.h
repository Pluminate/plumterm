#pragma once
#include <QFrame>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>

class TerminalView;

class TaskBar : public QFrame {
    Q_OBJECT
public:
    explicit TaskBar(TerminalView *view, QWidget *parent = nullptr);

    void toggle();
    void openBar();
    void closeBar();

signals:
    void runTaskRequested(const QString &command);

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    void submit();

    TerminalView *m_view = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_runBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;
};
