#pragma once

#include "core/CalculatorConfig.hpp"

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QObject;
class QSpinBox;

namespace calango::gui {

/// The electronic-structure controls a DFT wizard injects into
/// SimulationWizardBase's shared thematic GPAW group boxes:
///
///   "Electronic Convergence && Smearing" — smearing method + width σ, SCF
///                                          energy tolerance, max SCF steps
///   "Spin Configurations"                — polarization mode, initial magnetic
///                                          moments
///
/// One owner of these rows rather than a copy per wizard: the Single-Point and
/// Geometry Optimization setups must present the *same* GPAW form — an SCF is
/// an SCF whether it stands alone or runs inside a relaxation loop, and a
/// second hand-maintained copy is exactly how the two drift apart. A wizard
/// holds one of these, forwards its buildConvergenceRows()/buildSpinRows()
/// hooks to it, and calls applyTo() when assembling its CalculatorConfig.
///
/// The rows own no layout of their own — they are appended to the form layout
/// the base class hands over, and the widgets are parented through it.
class GpawElectronicRows {
public:
    /// Smearing method / width and the SCF convergence controls. `owner` is
    /// the wizard the rows live in; it is the connection context, so the
    /// internal enable/disable wiring is torn down when the wizard goes away
    /// rather than at some unspecified point during child-widget deletion
    /// (these rows are not a QObject and cannot supply that guarantee).
    void buildConvergenceRows(QFormLayout* form, QObject* owner);
    /// Spin polarization mode and the initial magnetic moments.
    void buildSpinRows(QFormLayout* form, QObject* owner);

    /// Grey out what the current selections make meaningless (the width of a
    /// disabled smearing, the moments of an unpolarized run). Safe before the
    /// rows are built.
    void updateEnabled();

    /// Write the collected values into `config`. Leaves every other field —
    /// task kind, optimizer, ensemble — to the calling wizard.
    void applyTo(core::CalculatorConfig& config) const;

private:
    QComboBox* smearingCombo_ = nullptr;
    QDoubleSpinBox* smearingWidthSpin_ = nullptr;
    QDoubleSpinBox* scfTolSpin_ = nullptr;
    QSpinBox* scfStepsSpin_ = nullptr;
    QComboBox* spinModeCombo_ = nullptr; ///< Unpolarized / Collinear / Non-collinear
    QLineEdit* magMomentEdit_ = nullptr;
};

} // namespace calango::gui
