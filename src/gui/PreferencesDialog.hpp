#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>

namespace calango::gui {

/// Application preferences: currently the environment-file (.env) location
/// used to auto-load the Materials Project API key (MP_API_KEY) at launch,
/// with an immediate reload action and status feedback.
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

private Q_SLOTS:
    void browseEnvFile();
    void reloadEnvFile();

private:
    void updateStatus();

    QLineEdit* envPathEdit_;
    QLabel* statusLabel_;
};

} // namespace calango::gui
