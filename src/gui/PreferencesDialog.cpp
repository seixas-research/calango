#include "gui/PreferencesDialog.hpp"

#include "gui/EnvFile.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace calango::gui {

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent)
    , envPathEdit_(new QLineEdit(this))
    , statusLabel_(new QLabel(this))
{
    setWindowTitle(tr("Preferences"));
    resize(560, 220);

    auto* envGroup = new QGroupBox(tr("Environment File (.env)"), this);
    auto* envLayout = new QVBoxLayout(envGroup);

    auto* note = new QLabel(
        tr("At launch Calango reads this file and exports MP_API_KEY (the "
           "Materials Project API key) into the environment. A key already "
           "set in the shell takes precedence at startup; \"Reload Now\" "
           "applies the file unconditionally."),
        envGroup);
    note->setWordWrap(true);
    envLayout->addWidget(note);

    auto* pathRow = new QHBoxLayout;
    envPathEdit_->setText(envFilePath());
    auto* browseButton = new QPushButton(tr("Browse…"), envGroup);
    auto* reloadButton = new QPushButton(tr("Reload Now"), envGroup);
    pathRow->addWidget(envPathEdit_, 1);
    pathRow->addWidget(browseButton);
    pathRow->addWidget(reloadButton);
    envLayout->addLayout(pathRow);

    statusLabel_->setWordWrap(true);
    envLayout->addWidget(statusLabel_);

    connect(envPathEdit_, &QLineEdit::textChanged, this, [this](const QString& path) {
        setEnvFilePath(path.trimmed());
        updateStatus();
    });
    connect(browseButton, &QPushButton::clicked,
            this, &PreferencesDialog::browseEnvFile);
    connect(reloadButton, &QPushButton::clicked,
            this, &PreferencesDialog::reloadEnvFile);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(envGroup);
    layout->addStretch(1);
    layout->addWidget(buttons);

    updateStatus();
}

void PreferencesDialog::browseEnvFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select Environment File"), envFilePath(),
        tr("Environment files (*.env .env);;All files (*)"));
    if (!path.isEmpty())
        envPathEdit_->setText(path);
}

void PreferencesDialog::reloadEnvFile()
{
    loadEnvironmentFile(/*overrideExisting=*/true);
    updateStatus();
}

void PreferencesDialog::updateStatus()
{
    const QString path = envFilePath();
    if (!QFileInfo::exists(path)) {
        statusLabel_->setStyleSheet(QStringLiteral("color: #b07d2a;"));
        statusLabel_->setText(tr("File not found: %1").arg(path));
        return;
    }
    const auto values = parseEnvFile(path);
    if (values.contains(QStringLiteral("MP_API_KEY"))
        && !values.value(QStringLiteral("MP_API_KEY")).isEmpty()) {
        statusLabel_->setStyleSheet(QString());
        statusLabel_->setText(tr("MP_API_KEY found (%n entries in file).", nullptr,
                                 static_cast<int>(values.size())));
    } else {
        statusLabel_->setStyleSheet(QStringLiteral("color: #b07d2a;"));
        statusLabel_->setText(tr("File read, but it defines no MP_API_KEY "
                                 "(%n entries).", nullptr,
                                 static_cast<int>(values.size())));
    }
}

} // namespace calango::gui
