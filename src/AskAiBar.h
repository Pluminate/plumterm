#pragma once
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>

class TerminalView;

class AskAiBar : public QFrame {
    Q_OBJECT
public:
    explicit AskAiBar(TerminalView *view, QWidget *parent = nullptr);

    void toggle();
    void openBar();
    void closeBar();

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    void submitPrompt();
    void setSuggestedCommand(const QString &cmd);

    TerminalView *m_view = nullptr;
    QLineEdit *m_input = nullptr;
    QLabel *m_statusLabel = nullptr;
    QFrame *m_suggestionBox = nullptr;
    QLabel *m_cmdLabel = nullptr;
    QPushButton *m_pasteBtn = nullptr;
    QPushButton *m_runBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;

    QString m_currentCmd;
    bool m_loading = false;
};
