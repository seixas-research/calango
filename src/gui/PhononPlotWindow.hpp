#pragma once

#include <QDialog>

class QDoubleSpinBox;

namespace calango::gui {

class BandPdosView;

/// Post-processing viewer for a finished phonon job: the phonon band
/// structure (frequency in cm⁻¹ vs the high-symmetry k-path) side by side
/// with the phonon density of states, sharing the frequency axis. Reads
/// phonon_band.json / phonon_dos.json written by PhononScriptGenerator and
/// drives a BandPdosView in phonon mode. hasData() reports whether a band
/// file was found and parsed.
class PhononPlotWindow : public QDialog {
    Q_OBJECT

public:
    explicit PhononPlotWindow(const QString& directory, QWidget* parent = nullptr);

    bool hasData() const { return hasData_; }

private:
    void loadDirectory(const QString& directory);

    BandPdosView* view_;
    QDoubleSpinBox* minSpin_;
    QDoubleSpinBox* maxSpin_;
    bool hasData_ = false;
};

} // namespace calango::gui
