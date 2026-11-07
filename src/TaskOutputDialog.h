#pragma once
#include <QDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>

class TaskOutputDialog : public QDialog {
    Q_OBJECT
public:
    explicit TaskOutputDialog(const QString &command, QWidget *parent = nullptr);

    void appendOutput(const QString &text);
    void setRunning(bool running, int exitCode = 0);

signals:
    void stopRequested();
    void rerunRequested();

private:
    QString m_command;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    QPlainTextEdit *m_outputView = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_rerunBtn = nullptr;
    QPushButton *m_copyBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    qint64 m_startTime = 0;
};
