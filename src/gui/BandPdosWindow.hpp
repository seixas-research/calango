#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QDoubleSpinBox;
class QListWidget;

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

    /// Push the current shift choice into the view: E_F when the toggle is
    /// on, 0 (absolute energies) when it is off.
    void applyFermiShift();
    /// Recompute the gap from the loaded bands + current Fermi level and
    /// repaint the summary box.
    void refreshBandGap();

    BandPdosView* view_;
    QLabel* gapLabel_;
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
};

} // namespace calango::gui
