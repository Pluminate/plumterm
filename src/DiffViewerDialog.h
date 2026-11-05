#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>

class DiffViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit DiffViewerDialog(const QString &cwd, const QString &file, QWidget *parent = nullptr);

signals:
    void fileDiscarded(const QString &file);

private:
    void loadDiff();
    void discardFile();

    QString m_cwd;
    QString m_file;
    QLabel *m_titleLabel = nullptr;
    QTextEdit *m_textEdit = nullptr;
    QPushButton *m_discardBtn = nullptr;
};
