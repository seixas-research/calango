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
///   • "Rendering" — which shader profile draws atoms, bonds and volumetric
///     isosurfaces, plus what the current OpenGL driver reports. Selections
///     are global (they describe the installation's rendering, like the
///     theme); per-object appearance stays on render::Style in the
///     Representation panel.
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
    /// "External Files": pseudopotential libraries per engine and the ML
    /// model directory. Machine properties, not run parameters.
    QWidget* buildExternalFilesTab();
    /// Build the "Run" tab (engine → launch command template + core count).
    QWidget* buildRunTab();
    /// Build the "Rendering" tab (per-slot shader profile + GL diagnostics).
    /// Refresh the per-slot descriptions and the capability warnings from the
    /// live GL context. Called on construction and after every selection, so
    /// an unsupported choice explains itself immediately rather than at the
    /// next redraw.
    /// Report the directory runs will ACTUALLY use, including the
    /// fallback when the configured one is unusable.
    void updateSimulationsStatus();

    QLineEdit* envPathEdit_;
    QLabel* statusLabel_;
    QComboBox* themeCombo_ = nullptr;
    QSpinBox* threadsSpin_ = nullptr;
    QLineEdit* condaDirEdit_ = nullptr;
    QLabel* condaStatusLabel_ = nullptr;
    QTableWidget* engineEnvTable_ = nullptr;
    QTableWidget* externalFilesTable_ = nullptr;
    QTableWidget* runCommandTable_ = nullptr;
    QSpinBox* runCoresSpin_ = nullptr;
    /// One combo per render::ShaderSlot, in slot order.
    QLineEdit* simulationsDirEdit_ = nullptr;
    QLabel* simulationsStatusLabel_ = nullptr;
};

} // namespace calango::gui
