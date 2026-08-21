#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QVector>

class QComboBox;
class QKeySequenceEdit;
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
///   • "External Files" — pseudopotential libraries per engine and the ML
///     model directory.
///   • "Run" — the per-engine shell command template each job launches with
///     (MPI ranks, OMP pinning, solver invocation) plus the core count those
///     templates substitute for {cores}.
///   • "Hotkeys" — remappable keyboard shortcuts (viewport mouse modes,
///     camera/view resets, undo/redo/delete, tab cycling), backed by
///     ShortcutRegistry.
/// Values persist through QSettings, which SettingsManager mirrors to
/// ~/.calango/settings.json; the theme + thread readout and the hotkey
/// bindings apply live when the dialog closes (MainWindow::showPreferences).
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
    /// Build the "Hotkeys" tab: one row per ShortcutRegistry::actions(),
    /// each a QKeySequenceEdit capturing a new binding, with per-row and
    /// global "Reset to default" and conflict refusal.
    QWidget* buildHotkeysTab();
    /// Scan the table for two rows bound to the same key and show it, if so.
    /// Interactive edits can never actually PRODUCE this — setBinding() is
    /// only ever called after conflictFor() came back empty — so this exists
    /// for the one case that bypasses that: settings.json is meant to be
    /// hand-editable (SettingsManager's own design), and a hand-edited
    /// hotkeys/bindings object could carry two overrides for the same key.
    /// Called once when the tab is built; cheap enough (~13 actions) to also
    /// call after every change rather than special-case it out.
    void refreshHotkeyConflicts();
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
    /// Row-aligned with ShortcutRegistry::actions().
    QTableWidget* hotkeyTable_ = nullptr;
    QVector<QKeySequenceEdit*> hotkeyEdits_;
    QLabel* hotkeyConflictLabel_ = nullptr;
};

} // namespace calango::gui
