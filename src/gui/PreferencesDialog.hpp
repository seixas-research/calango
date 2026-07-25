#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>

class QComboBox;
class QSpinBox;
class QTableWidget;
class QWidget;

namespace calango::gui {

/// Application preferences, organized into tabs:
///   • "General" — the environment-file (.env) location used to auto-load the
///     Materials Project API key (MP_API_KEY), Appearance (theme), and
///     Computation (OMP thread count, Conda environments directory).
///   • "Python & Environments" — the per-engine Conda environment mapping
///     (GPAW/MACE/QE/SIESTA/…) that the simulation wizards resolve silently.
///   • "Run" — the per-engine shell command template each job launches with
///     (MPI ranks, OMP pinning, solver invocation) plus the core count those
///     templates substitute for {cores}.
/// Values persist through QSettings, which SettingsManager mirrors to
/// ~/.calango/settings.json; the theme + thread readout apply live when the
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
    /// Build the "Python & Environments" tab (engine → Conda env table).
    QWidget* buildPythonEnvTab();
    /// Build the "Run" tab (engine → launch command template + core count).
    QWidget* buildRunTab();

    QLineEdit* envPathEdit_;
    QLabel* statusLabel_;
    QComboBox* themeCombo_ = nullptr;
    QSpinBox* threadsSpin_ = nullptr;
    QLineEdit* condaDirEdit_ = nullptr;
    QLabel* condaStatusLabel_ = nullptr;
    QTableWidget* engineEnvTable_ = nullptr;
    QTableWidget* runCommandTable_ = nullptr;
    QSpinBox* runCoresSpin_ = nullptr;
};

} // namespace calango::gui
