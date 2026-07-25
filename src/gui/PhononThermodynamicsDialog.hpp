#pragma once

#include "core/PhononThermodynamics.hpp"

#include <QDialog>
#include <QString>

#include <vector>

class QDoubleSpinBox;
class QLabel;

namespace calango::gui {

class ThermoPlotWidget;

/// "Phonon Viewer → Phonon Thermodynamics…": temperature-dependent harmonic
/// vibrational thermodynamics derived from the phonon DOS.
///
/// U(T), F(T) and S(T) come from core::computePhononThermodynamics; this
/// dialog is the presentation layer plus the exports.
///
/// The curves occupy two SEPARATE plots side by side — energies (U, F, eV) on
/// the left and entropy (S, meV/K) on the right — because they differ by about
/// four orders of magnitude: sharing one panel flattens whichever is smaller
/// onto the baseline. A hover on either plot drives the crosshair on both, so
/// reading U, F and S at one temperature is a single gesture; each column
/// exports its own CSV and image.
class PhononThermodynamicsDialog : public QDialog {
    Q_OBJECT

public:
    /// `frequenciesCm` / `dos` are the PhDOS as loaded from phonon_dos.json.
    /// `label` names the source (job directory / formula) in the header.
    PhononThermodynamicsDialog(std::vector<double> frequenciesCm,
                               std::vector<double> dos, const QString& label,
                               QWidget* parent = nullptr);

private Q_SLOTS:
    void recompute();

private:
    /// `entropy` picks which column's series are written / drawn.
    void exportCsv(bool entropy);
    void exportImage(bool entropy);
    /// Build one column (title, plot, its two export buttons).
    QWidget* buildColumn(bool entropy, ThermoPlotWidget*& plot);

    std::vector<double> frequenciesCm_;
    std::vector<double> dos_;
    core::PhononThermoResult result_;

    ThermoPlotWidget* energyPlot_ = nullptr;
    ThermoPlotWidget* entropyPlot_ = nullptr;
    QDoubleSpinBox* minTempSpin_ = nullptr;
    QDoubleSpinBox* maxTempSpin_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
};

} // namespace calango::gui
