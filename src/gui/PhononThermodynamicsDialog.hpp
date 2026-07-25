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
/// dialog is the presentation layer plus the exports. Energies (U, F) and
/// entropy (S) are plotted against SEPARATE y-axes: they differ by ~4 orders
/// of magnitude in eV units, so a shared axis would flatten the entropy curve
/// onto the baseline and hide exactly what the user opened the dialog to see.
class PhononThermodynamicsDialog : public QDialog {
    Q_OBJECT

public:
    /// `frequenciesCm` / `dos` are the PhDOS as loaded from phonon_dos.json.
    /// `label` names the source (job directory / formula) in the header.
    PhononThermodynamicsDialog(std::vector<double> frequenciesCm,
                               std::vector<double> dos, const QString& label,
                               QWidget* parent = nullptr);

private Q_SLOTS:
    void exportCsv();
    void exportImage();

private:
    void recompute();

    std::vector<double> frequenciesCm_;
    std::vector<double> dos_;
    core::PhononThermoResult result_;

    ThermoPlotWidget* plot_ = nullptr;
    QDoubleSpinBox* minTempSpin_ = nullptr;
    QDoubleSpinBox* maxTempSpin_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
};

} // namespace calango::gui
