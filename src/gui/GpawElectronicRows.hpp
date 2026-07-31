#pragma once

#include "core/CalculatorConfig.hpp"

#include <QString>

#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QObject;
class QSpinBox;
class QWidget;

namespace calango::gui {

/// The electronic-structure controls a DFT wizard injects into
/// SimulationWizardBase's shared thematic GPAW group boxes:
///
///   "Electronic Convergence && Smearing" — smearing method + width σ, SCF
///                                          energy tolerance, max SCF steps
///   "Spin Configurations"                — polarization mode (the initial
///                                          moments themselves belong to the
///                                          structure; see Edit Structure)
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

    /// Restrict the smearing menu to what the selected engine can actually
    /// run, and retune the per-method notes for it.
    ///
    /// The menu was written for GPAW, which accepts every scheme in it. VASP
    /// does not: Marzari-Vanderbilt, orbital-free and fixed occupations have
    /// no ISMEAR, and the generator silently substitutes a narrow Gaussian for
    /// them. Offering a choice that is then quietly replaced is worse than not
    /// offering it, so those entries are withdrawn rather than approximated.
    ///
    /// Safe to call before the rows are built and safe to call repeatedly; the
    /// current selection is preserved when the new engine still offers it, and
    /// otherwise falls back to the first entry that survives.
    void setCalculatorKind(core::CalculatorKind kind);

    /// Show only the parameters the selected smearing method actually takes,
    /// and grey out what the other selections make meaningless (the moments of
    /// an unpolarized run). Safe before the rows are built.
    ///
    /// Show/hide rather than enable/disable for the smearing parameters: the
    /// methods take genuinely different inputs (a width, a width plus an
    /// expansion order, an occupation list, or nothing at all), and a row of
    /// permanently greyed boxes reads as broken rather than as inapplicable.
    void updateEnabled();

    /// Empty when the current selection is a valid, complete configuration;
    /// otherwise the reason it is not, ready to show the user.
    ///
    /// Exists for exactly one case: "Fixed" occupations carry no default GPAW
    /// could fall back on, so generating a script from an empty list produces
    /// a run that dies on import. Better to say so in the wizard.
    QString validationError() const;

    /// Two controls this class OWNS but does not place: the base class puts
    /// them where they belong physically rather than where they happen to be
    /// created — the SCF energy tolerance beside the eigenstates and density
    /// tolerances it is converged with, and the step cap beside the eigensolver
    /// whose iterations it caps. Null before buildConvergenceRows() runs.
    QWidget* energyToleranceWidget() const;
    QWidget* scfStepsWidget() const;

    /// Write the collected values into `config`. Leaves every other field —
    /// task kind, optimizer, ensemble — to the calling wizard.
    void applyTo(core::CalculatorConfig& config) const;

private:
    /// The method behind the combo's current row. Read through item data
    /// rather than the row number, so the menu can be ordered for the user
    /// (Fermi-Dirac first) while core::SmearingMethod keeps the declaration
    /// order that saved configurations depend on.
    core::SmearingMethod selectedMethod() const;
    /// Parse the occupation-number field: whitespace- or comma-separated
    /// numbers, with `;` starting a second spin channel. Empty on a parse
    /// failure, which validationError() reports.
    std::vector<std::vector<double>> parseFixedOccupations() const;

    /// Whether `method` is one the current engine can run as named. See
    /// setCalculatorKind().
    bool methodSupported(core::SmearingMethod method) const;
    /// (Re)fill the smearing menu with the methods the current engine offers,
    /// keeping the selection when it survives.
    void populateSmearingMethods();

    /// The engine the rows are currently dressed for. GPAW by default, which
    /// is the permissive case — every method is offered until told otherwise.
    core::CalculatorKind kind_ = core::CalculatorKind::Gpaw;
    QComboBox* smearingCombo_ = nullptr;
    QDoubleSpinBox* smearingWidthSpin_ = nullptr;
    QLabel* smearingWidthLabel_ = nullptr;
    QSpinBox* smearingOrderSpin_ = nullptr;
    QLabel* smearingOrderLabel_ = nullptr;
    /// The form the smearing rows live in, kept so the parameter rows below
    /// the method row can be shown and hidden (QFormLayout::setRowVisible).
    QFormLayout* convForm_ = nullptr;
    QLineEdit* fixedOccupationsEdit_ = nullptr;
    /// Per-method explanation shown under the smearing row (what the method is
    /// for, and what GPAW requires of it).
    QLabel* smearingNote_ = nullptr;
    QDoubleSpinBox* scfTolSpin_ = nullptr;
    QSpinBox* scfStepsSpin_ = nullptr;
    QComboBox* spinModeCombo_ = nullptr; ///< Unpolarized / Collinear / Non-collinear
    /// Says where the moments come from now — Edit Structure, not this page.
    QLabel* momentsNote_ = nullptr;
};

} // namespace calango::gui
