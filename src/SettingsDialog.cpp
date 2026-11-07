#include "SettingsDialog.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonObject>
#include <QLabel>
#include <QVBoxLayout>
#include "CoreBridge.h"

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Settings"));
    setMinimumWidth(450);
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #1a1a1c; color: #e2e8f0; }"
        "QLabel { color: #cbd5e1; font-size: 12px; }"
        "QLineEdit { background-color: #0f1012; color: #f8fafc; border: 1px solid #334155; border-radius: 4px; padding: 6px 8px; font-size: 12px; }"
        "QLineEdit:focus { border-color: #38bdf8; }"
        "QSpinBox { background-color: #0f1012; color: #f8fafc; border: 1px solid #334155; border-radius: 4px; padding: 4px; }"
        "QCheckBox { color: #cbd5e1; font-size: 12px; spacing: 6px; }"
        "QGroupBox { color: #38bdf8; font-size: 13px; font-weight: bold; border: 1px solid #334155; border-radius: 6px; margin-top: 12px; padding-top: 12px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QPushButton { background-color: #2563eb; color: #ffffff; border: none; border-radius: 4px; padding: 6px 14px; font-weight: 500; }"
        "QPushButton:hover { background-color: #1d4ed8; }"
        "QPushButton[text=\"Cancel\"] { background-color: #334155; color: #cbd5e1; }"
        "QPushButton[text=\"Cancel\"]:hover { background-color: #475569; }"
    ));

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);

    const auto s = CoreBridge::settings().toObject();

    // Shortcuts Group
    auto *shortcutsBox = new QGroupBox(QStringLiteral("Keyboard Shortcuts"), this);
    auto *shortcutsLayout = new QFormLayout(shortcutsBox);

    m_hotkeyEdit = new QLineEdit(s.value(QStringLiteral("globalHotkey")).toString(), shortcutsBox);
    shortcutsLayout->addRow(QStringLiteral("Global Hotkey:"), m_hotkeyEdit);

    m_rerunEdit = new QLineEdit(s.value(QStringLiteral("rerunShortcut")).toString(), shortcutsBox);
    shortcutsLayout->addRow(QStringLiteral("Rerun Shortcut:"), m_rerunEdit);

    m_runEdit = new QLineEdit(s.value(QStringLiteral("runShortcut")).toString(), shortcutsBox);
    shortcutsLayout->addRow(QStringLiteral("Run Shortcut:"), m_runEdit);

    mainLayout->addWidget(shortcutsBox);

    // AI Group
    auto *aiBox = new QGroupBox(QStringLiteral("AI Assistance"), this);
    auto *aiLayout = new QFormLayout(aiBox);

    m_geminiEdit = new QLineEdit(s.value(QStringLiteral("geminiToken")).toString(), aiBox);
    m_geminiEdit->setEchoMode(QLineEdit::Password);
    m_geminiEdit->setPlaceholderText(QStringLiteral("Gemini API Key"));
    aiLayout->addRow(QStringLiteral("Gemini Token:"), m_geminiEdit);

    mainLayout->addWidget(aiBox);

    // General Group
    auto *generalBox = new QGroupBox(QStringLiteral("General"), this);
    auto *generalLayout = new QVBoxLayout(generalBox);

    m_confirmQuitCheck = new QCheckBox(QStringLiteral("Confirm on quit"), generalBox);
    m_confirmQuitCheck->setChecked(s.value(QStringLiteral("confirmOnQuit")).toBool(true));
    generalLayout->addWidget(m_confirmQuitCheck);

    m_excludeDangerousCheck = new QCheckBox(QStringLiteral("Exclude dangerous commands from auto-rerun (git, cd)"), generalBox);
    m_excludeDangerousCheck->setChecked(s.value(QStringLiteral("excludeDangerousRerun")).toBool(true));
    generalLayout->addWidget(m_excludeDangerousCheck);

    m_notifyEndCheck = new QCheckBox(QStringLiteral("Notify when long commands finish"), generalBox);
    m_notifyEndCheck->setChecked(s.value(QStringLiteral("notifyOnCommandEnd")).toBool(true));
    generalLayout->addWidget(m_notifyEndCheck);

    auto *threshLayout = new QHBoxLayout();
    threshLayout->addWidget(new QLabel(QStringLiteral("Notification threshold (seconds):"), generalBox));
    m_notifyThresholdSpin = new QSpinBox(generalBox);
    m_notifyThresholdSpin->setRange(1, 3600);
    m_notifyThresholdSpin->setValue(s.value(QStringLiteral("notificationThreshold")).toInt(120));
    threshLayout->addWidget(m_notifyThresholdSpin);
    threshLayout->addStretch();
    generalLayout->addLayout(threshLayout);

    m_restoreSessionCheck = new QCheckBox(QStringLiteral("Restore tabs and split panes on launch"), generalBox);
    m_restoreSessionCheck->setChecked(s.value(QStringLiteral("restoreSession")).toBool(true));
    generalLayout->addWidget(m_restoreSessionCheck);

    mainLayout->addWidget(generalBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

void SettingsDialog::save() {
    QJsonObject patch;
    patch[QStringLiteral("globalHotkey")] = m_hotkeyEdit->text().trimmed();
    patch[QStringLiteral("rerunShortcut")] = m_rerunEdit->text().trimmed();
    patch[QStringLiteral("runShortcut")] = m_runEdit->text().trimmed();
    patch[QStringLiteral("geminiToken")] = m_geminiEdit->text().trimmed();
    patch[QStringLiteral("confirmOnQuit")] = m_confirmQuitCheck->isChecked();
    patch[QStringLiteral("excludeDangerousRerun")] = m_excludeDangerousCheck->isChecked();
    patch[QStringLiteral("notifyOnCommandEnd")] = m_notifyEndCheck->isChecked();
    patch[QStringLiteral("notificationThreshold")] = m_notifyThresholdSpin->value();
    patch[QStringLiteral("restoreSession")] = m_restoreSessionCheck->isChecked();

    CoreBridge::setSettings(QString::fromUtf8(QJsonDocument(patch).toJson(QJsonDocument::Compact)));
}
