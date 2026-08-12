#pragma once

#include <QDialog>
#include <QString>

#include <vector>

class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QSlider;

namespace calango::gui {

class BandPdosView;

/// "Phonon Viewer" — the post-processing viewer for a finished phonon job: the
/// phonon band structure (frequency in cm⁻¹ vs the high-symmetry q-path) side
/// by side with the phonon density of states, sharing the frequency axis.
/// Reads phonon_band.json / phonon_dos.json written by PhononScriptGenerator
/// and drives a BandPdosView in phonon mode. hasData() reports whether a band
/// file was found and parsed.
///
/// It is also the entry point for "Phonon Thermodynamics…" (harmonic U/F/S
/// from the PhDOS), which needs exactly the data this window already holds, and
/// a shortcut into the Vibrational Mode Analysis module for the run it is
/// showing.
///
/// It used to OWN that second one: it constructed the dialog, held the
/// structure and viewport it needed, and relayed the resulting trajectory back
/// out to the host. All three are gone. Watching a mode no longer requires
/// keeping a dispersion plot open — the module is a menu entry that selects its
/// own phonon run — so this window only asks the host to open it, on the
/// directory it happens to be showing.
class PhononPlotWindow : public QDialog {
    Q_OBJECT

public:
    PhononPlotWindow(const QString& directory, QWidget* parent = nullptr);

    bool hasData() const { return hasData_; }

Q_SIGNALS:
    /// "Vibrational Analysis…": open the module on THIS run. The host owns
    /// that dialog (it supplies the viewport, the candidate run list and the
    /// tab the mode trajectory lands in), so this window only names the
    /// directory rather than constructing a second copy of the module.
    void vibrationalAnalysisRequested(const QString& directory);

private Q_SLOTS:
    /// "Phonon Thermodynamics…" — harmonic U/F/S from the loaded PhDOS.
    void showThermodynamics();
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
};

} // namespace calango::gui
