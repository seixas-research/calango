#pragma once

#include "core/Structure.hpp"

#include <QDialog>
#include <QString>

#include <memory>
#include <vector>

class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QSlider;

namespace calango::gui {

class BandPdosView;
class ViewportWidget;

/// "Phonon Viewer" — the post-processing viewer for a finished phonon job: the
/// phonon band structure (frequency in cm⁻¹ vs the high-symmetry q-path) side
/// by side with the phonon density of states, sharing the frequency axis.
/// Reads phonon_band.json / phonon_dos.json written by PhononScriptGenerator
/// and drives a BandPdosView in phonon mode. hasData() reports whether a band
/// file was found and parsed.
///
/// It is also the entry point for the two derived analyses, both of which need
/// exactly the data this window already holds: "Phonon Thermodynamics…"
/// (harmonic U/F/S from the PhDOS) and "Vibrational Analysis…" (eigenmode
/// animation on the 3D viewport).
class PhononPlotWindow : public QDialog {
    Q_OBJECT

public:
    /// `structure` and `viewport` enable "Vibrational Analysis…" (the mode
    /// animation needs both); passing null leaves that action disabled while
    /// everything else still works.
    PhononPlotWindow(const QString& directory, QWidget* parent = nullptr,
                     std::shared_ptr<const core::Structure> structure = nullptr,
                     ViewportWidget* viewport = nullptr);

    bool hasData() const { return hasData_; }

Q_SIGNALS:
    /// Forwarded from the Vibrational Analysis dialog: the host opens these
    /// frames as a new workspace tab. Relayed rather than connected directly
    /// because that dialog is created on demand and owns no documents.
    void modeTrajectoryRequested(
        const std::vector<std::shared_ptr<core::Structure>>& frames,
        const QString& label);

private Q_SLOTS:
    /// "Phonon Thermodynamics…" — harmonic U/F/S from the loaded PhDOS.
    void showThermodynamics();
    /// "Vibrational Analysis…" — eigenmode animation on the 3D viewport.
    void showVibrationalAnalysis();
    /// Dispersion only: k-distance + one column per phonon branch.
    void exportBandsCsv();
    /// PhDOS only: frequency + intensity per projection.
    void exportPhdosCsv();

private:
    void loadDirectory(const QString& directory);
    /// Shared writer for the two exporters above; `bands`/`dos` select which
    /// sections are emitted.
    void writeCsv(const QString& caption, const QString& defaultName,
                  bool bands, bool dos);

    /// Enable or explain the σ control after a load; see the identically
    /// named member of BandPdosWindow.
    void updateSmearingControl();

    BandPdosView* view_;
    // -- Live PhDOS smearing ------------------------------------------------
    QGroupBox* smearingBox_ = nullptr;
    QSlider* smearingSlider_ = nullptr;
    QDoubleSpinBox* smearingSpin_ = nullptr;
    QLabel* smearingNote_ = nullptr;
    QDoubleSpinBox* minSpin_;
    QDoubleSpinBox* maxSpin_;
    bool hasData_ = false;
    QString directory_; ///< where the phonon_*.json came from
    /// The PhDOS as loaded, kept so the derived analyses integrate exactly the
    /// data this window is showing rather than re-reading (and possibly
    /// re-interpreting) the file.
    std::vector<double> dosFrequenciesCm_;
    std::vector<double> dosValues_;
    /// Structure the phonons belong to, for the eigenmode animation. Null when
    /// the viewer was opened without one (Results menu on an old job).
    std::shared_ptr<const core::Structure> structure_;
    ViewportWidget* viewport_ = nullptr;
};

} // namespace calango::gui
