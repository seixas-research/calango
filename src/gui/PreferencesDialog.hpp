#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>

class QComboBox;
class QSpinBox;

namespace calango::gui {

/// Application preferences: the environment-file (.env) location used to
/// auto-load the Materials Project API key (MP_API_KEY), plus Appearance
/// (theme) and Computation (OMP thread count, Conda environments directory)
/// settings. Values persist through QSettings, which SettingsManager mirrors
/// to ~/.calango/settings.json; the theme + thread readout apply live when the
/// dialog closes (MainWindow::showPreferences).
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

private Q_SLOTS:
    void browseEnvFile();
    void reloadEnvFile();
    void browseCondaDir();

private:
    void updateStatus();
    void updateCondaStatus();

    QLineEdit* envPathEdit_;
    QLabel* statusLabel_;
    QComboBox* themeCombo_ = nullptr;
    QSpinBox* threadsSpin_ = nullptr;
    QLineEdit* condaDirEdit_ = nullptr;
    QLabel* condaStatusLabel_ = nullptr;
};

} // namespace calango::gui
