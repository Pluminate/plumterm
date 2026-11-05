#pragma once
#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>

class CommitDialog : public QDialog {
    Q_OBJECT
public:
    explicit CommitDialog(const QString &cwd, QWidget *parent = nullptr);

signals:
    void committed();

private:
    void refreshFiles();
    void generateAiMessage();
    void doCommit();

    QString m_cwd;
    QListWidget *m_fileList = nullptr;
    QPlainTextEdit *m_msgEdit = nullptr;
    QPushButton *m_aiBtn = nullptr;
    QCheckBox *m_pushCheck = nullptr;
    QCheckBox *m_pullFirstCheck = nullptr;
    QPushButton *m_commitBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
};
