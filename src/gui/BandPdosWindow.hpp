#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QDoubleSpinBox;
class QListWidget;

#include <QString>

namespace calango::gui {

class BandPdosView;

/// Modeless viewer for a finished electronic-structure job directory:
/// loads bands.json (+ pdos.json when present) and shows the band
/// structure and PDOS with an adjustable Fermi reference, energy window,
/// per-projection visibility toggles, and CSV/.dat export.
class BandPdosWindow : public QDialog {
    Q_OBJECT

public:
    explicit BandPdosWindow(const QString& directory, QWidget* parent = nullptr);

    /// True when bands.json was found and parsed.
    bool hasData() const { return hasData_; }

private Q_SLOTS:
    void exportBands();
    void exportPdos();

private:
    void loadDirectory(const QString& directory);
    /// fatbands.json — orbital weights per band and k-point. Returns false
    /// (and leaves the controls hidden) when the run produced none.
    bool loadFatbands(const QString& directory);
    /// band_symmetry.json — irrep labels at the high-symmetry points.
    bool loadSymmetry(const QString& directory);

    /// Push the current shift choice into the view: E_F when the toggle is
    /// on, 0 (absolute energies) when it is off.
    void applyFermiShift();
    /// Recompute the gap from the loaded bands + current Fermi level and
    /// repaint the summary box.
    void refreshBandGap();

    BandPdosView* view_;
    QLabel* gapLabel_;
    QCheckBox* showFermiCheck_;
    QCheckBox* shiftFermiCheck_;
    /// Read-only readout of the Fermi level the calculation reported. It is
    /// a property of the finished run, not a viewing preference, so it is
    /// displayed rather than edited.
    QLabel* fermiLabel_;
    double fermiLevel_ = 0.0;
    QDoubleSpinBox* minSpin_;
    QDoubleSpinBox* maxSpin_;
    QListWidget* projectionList_;
    bool hasData_ = false;

    // -- Orbital projections (fatbands) -------------------------------------
    /// Hidden entirely when the run wrote no fatbands.json: an empty channel
    /// list with a dead mode selector reads as a broken feature rather than an
    /// unused one.
    QGroupBox* fatbandGroup_ = nullptr;
    QComboBox* fatbandModeCombo_ = nullptr;
    QListWidget* fatbandList_ = nullptr;

    // -- Band symmetry ------------------------------------------------------
    QGroupBox* symmetryGroup_ = nullptr;
    QCheckBox* symmetryCheck_ = nullptr;
    QCheckBox* symmetryLineCheck_ = nullptr;
    QLabel* symmetrySummary_ = nullptr;
};

} // namespace calango::gui
