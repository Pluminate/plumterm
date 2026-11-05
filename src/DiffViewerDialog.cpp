#include "DiffViewerDialog.h"
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>
#include "CoreBridge.h"

DiffViewerDialog::DiffViewerDialog(const QString &cwd, const QString &file, QWidget *parent)
    : QDialog(parent), m_cwd(cwd), m_file(file) {
    setWindowTitle(QStringLiteral("Diff: %1").arg(file));
    resize(750, 500);
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #0d1117; color: #c9d1d9; }"
        "QTextEdit { background-color: #0d1117; color: #c9d1d9; border: 1px solid #30363d; font-family: Menlo, Monaco, monospace; font-size: 12px; }"
        "QPushButton { background-color: #21262d; color: #c9d1d9; border: 1px solid #30363d; border-radius: 4px; padding: 5px 12px; font-size: 12px; }"
        "QPushButton:hover { background-color: #30363d; }"
        "QPushButton#discardBtn { background-color: #8b0000; color: #ffffff; border: 1px solid #da3633; }"
        "QPushButton#discardBtn:hover { background-color: #b30000; }"
    ));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 10, 12, 10);
    rootLayout->setSpacing(10);

    auto *headerLayout = new QHBoxLayout();
    m_titleLabel = new QLabel(QStringLiteral("📄 %1").arg(file), this);
    m_titleLabel->setStyleSheet(QStringLiteral("color: #58a6ff; font-weight: bold; font-size: 13px;"));
    headerLayout->addWidget(m_titleLabel, 1);

    m_discardBtn = new QPushButton(QStringLiteral("Discard Changes"), this);
    m_discardBtn->setObjectName(QStringLiteral("discardBtn"));
    connect(m_discardBtn, &QPushButton::clicked, this, &DiffViewerDialog::discardFile);
    headerLayout->addWidget(m_discardBtn);

    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    headerLayout->addWidget(closeBtn);

    rootLayout->addLayout(headerLayout);

    m_textEdit = new QTextEdit(this);
    m_textEdit->setReadOnly(true);
    rootLayout->addWidget(m_textEdit, 1);

    loadDiff();
}

void DiffViewerDialog::loadDiff() {
    const QString raw = CoreBridge::git::fileDiff(m_cwd, m_file);
    if (raw.trimmed().isEmpty()) {
        m_textEdit->setPlainText(QStringLiteral("(No changes or untracked file)"));
        return;
    }

    QString html = QStringLiteral("<pre style=\"font-family: Menlo, Monaco, monospace; margin: 0;\">");
    const QStringList lines = raw.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString esc = line.toHtmlEscaped();
        if (line.startsWith(QLatin1String("+++")) || line.startsWith(QLatin1String("---"))) {
            html += QStringLiteral("<div style=\"color: #8b949e; font-weight: bold;\">%1</div>").arg(esc);
        } else if (line.startsWith(QLatin1Char('+'))) {
            html += QStringLiteral("<div style=\"background-color: #143521; color: #7ee787;\">%1</div>").arg(esc);
        } else if (line.startsWith(QLatin1Char('-'))) {
            html += QStringLiteral("<div style=\"background-color: #3b181e; color: #ffa198;\">%1</div>").arg(esc);
        } else if (line.startsWith(QLatin1String("@@"))) {
            html += QStringLiteral("<div style=\"background-color: #162032; color: #79c0ff; font-weight: bold;\">%1</div>").arg(esc);
        } else {
            html += QStringLiteral("<div style=\"color: #c9d1d9;\">%1</div>").arg(esc);
        }
    }
    html += QStringLiteral("</pre>");
    m_textEdit->setHtml(html);
}

void DiffViewerDialog::discardFile() {
    const auto btn = QMessageBox::warning(
        this,
        QStringLiteral("Discard Changes"),
        QStringLiteral("Are you sure you want to discard all changes in '%1'? This cannot be undone.").arg(m_file),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    if (btn == QMessageBox::Yes) {
        CoreBridge::git::discard(m_cwd, m_file);
        emit fileDiscarded(m_file);
        accept();
    }
}
