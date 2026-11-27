#include "SettingsDialog.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>
#include "CoreBridge.h"
#include "GlobalHotkey.h"
#include "MenuStyle.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {

class ShortcutRecorder : public QObject {
public:
    explicit ShortcutRecorder(QLineEdit *edit) : QObject(edit), m_edit(edit) {}

protected:
    bool eventFilter(QObject *obj, QEvent *e) override {
        if (obj != m_edit || e->type() != QEvent::KeyPress) return false;
        auto *ke = static_cast<QKeyEvent *>(e);
        const int key = ke->key();
        if (key == Qt::Key_Tab || key == Qt::Key_Backtab || key == Qt::Key_Escape) return false;
        if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
            key == Qt::Key_Meta || key == Qt::Key_AltGr) {
            return true;
        }
        const Qt::KeyboardModifiers mods = ke->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
        if ((key == Qt::Key_Backspace || key == Qt::Key_Delete) && mods == Qt::NoModifier) {
            m_edit->clear();
            return true;
        }
        if (mods == Qt::NoModifier) return true; // require at least one modifier for a shortcut

        QStringList parts;
#ifdef Q_OS_MACOS
        if (mods & Qt::ControlModifier) parts << QStringLiteral("CommandOrControl");
        if (mods & Qt::MetaModifier) parts << QStringLiteral("Control");
#else
        if (mods & Qt::ControlModifier) parts << QStringLiteral("CommandOrControl");
        if (mods & Qt::MetaModifier) parts << QStringLiteral("Super");
#endif
        if (mods & Qt::AltModifier) parts << QStringLiteral("Alt");
        if (mods & Qt::ShiftModifier) parts << QStringLiteral("Shift");

        const QString keyStr = QKeySequence(key).toString(QKeySequence::PortableText);
        if (keyStr.isEmpty()) return true;
        parts << keyStr;
        m_edit->setText(parts.join(QLatin1Char('+')));
        return true;
    }

private:
    QLineEdit *m_edit = nullptr;
};

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Settings"));
    setMinimumWidth(450);
    const bool dark = MenuStyle::isDark(this);
#ifdef Q_OS_WIN
    if (HWND hwnd = reinterpret_cast<HWND>(winId())) {
        BOOL val = dark ? TRUE : FALSE;
        if (FAILED(DwmSetWindowAttribute(hwnd, 20, &val, sizeof(val)))) {
            DwmSetWindowAttribute(hwnd, 19, &val, sizeof(val));
        }
    }
#endif
    setStyleSheet(dark
        ? QStringLiteral(
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
            "QPushButton[text=\"Cancel\"]:hover { background-color: #475569; }")
        : QStringLiteral(
            "QDialog { background-color: #ffffff; color: #0f172a; }"
            "QLabel { color: #334155; font-size: 12px; }"
            "QLineEdit { background-color: #f8fafc; color: #0f172a; border: 1px solid #cbd5e1; border-radius: 4px; padding: 6px 8px; font-size: 12px; }"
            "QLineEdit:focus { border-color: #0284c7; }"
            "QSpinBox { background-color: #f8fafc; color: #0f172a; border: 1px solid #cbd5e1; border-radius: 4px; padding: 4px; }"
            "QCheckBox { color: #334155; font-size: 12px; spacing: 6px; }"
            "QGroupBox { color: #0284c7; font-size: 13px; font-weight: bold; border: 1px solid #cbd5e1; border-radius: 6px; margin-top: 12px; padding-top: 12px; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
            "QPushButton { background-color: #2563eb; color: #ffffff; border: none; border-radius: 4px; padding: 6px 14px; font-weight: 500; }"
            "QPushButton:hover { background-color: #1d4ed8; }"
            "QPushButton[text=\"Cancel\"] { background-color: #e2e8f0; color: #334155; }"
            "QPushButton[text=\"Cancel\"]:hover { background-color: #cbd5e1; }"));

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);

    const auto s = CoreBridge::settings().toObject();

    // Shortcuts Group
    auto *shortcutsBox = new QGroupBox(QStringLiteral("Keyboard Shortcuts"), this);
    auto *shortcutsLayout = new QFormLayout(shortcutsBox);

    m_hotkeyEdit = new QLineEdit(s.value(QStringLiteral("globalHotkey")).toString(), shortcutsBox);
    m_hotkeyEdit->setPlaceholderText(QStringLiteral("Press shortcut keys… (Backspace to clear)"));
    m_hotkeyEdit->installEventFilter(new ShortcutRecorder(m_hotkeyEdit));
    shortcutsLayout->addRow(QStringLiteral("Global Hotkey:"), m_hotkeyEdit);
    auto *hotkeyHint = new QLabel(QStringLiteral(
        "Shows/hides PlumTerm from any app. Takes priority over in-app shortcuts "
        "(e.g. Cmd+3 would stop switching to tab 3). Leave empty to disable."), shortcutsBox);
    hotkeyHint->setWordWrap(true);
    hotkeyHint->setStyleSheet(QStringLiteral("color: #64748b; font-size: 11px;"));
    shortcutsLayout->addRow(QString(), hotkeyHint);

    m_rerunEdit = new QLineEdit(s.value(QStringLiteral("rerunShortcut")).toString(), shortcutsBox);
    m_rerunEdit->setPlaceholderText(QStringLiteral("Press shortcut keys… (Backspace to clear)"));
    m_rerunEdit->installEventFilter(new ShortcutRecorder(m_rerunEdit));
    shortcutsLayout->addRow(QStringLiteral("Rerun Shortcut:"), m_rerunEdit);

    m_runEdit = new QLineEdit(s.value(QStringLiteral("runShortcut")).toString(), shortcutsBox);
    m_runEdit->setPlaceholderText(QStringLiteral("Press shortcut keys… (Backspace to clear)"));
    m_runEdit->installEventFilter(new ShortcutRecorder(m_runEdit));
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
        const QString hotkey = m_hotkeyEdit->text().trimmed();
        QString error;
        if (!hotkey.isEmpty() && !GlobalHotkey::parse(hotkey, nullptr, &error)) {
            QMessageBox::warning(this, QStringLiteral("Global Hotkey"), error);
            m_hotkeyEdit->setFocus();
            return;
        }
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
