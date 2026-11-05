#pragma once
#include <QCheckBox>
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void save();

private:
    QLineEdit *m_hotkeyEdit = nullptr;
    QLineEdit *m_rerunEdit = nullptr;
    QLineEdit *m_runEdit = nullptr;
    QLineEdit *m_geminiEdit = nullptr;
    QCheckBox *m_confirmQuitCheck = nullptr;
    QCheckBox *m_excludeDangerousCheck = nullptr;
    QCheckBox *m_notifyEndCheck = nullptr;
    QSpinBox *m_notifyThresholdSpin = nullptr;
    QCheckBox *m_restoreSessionCheck = nullptr;
};
