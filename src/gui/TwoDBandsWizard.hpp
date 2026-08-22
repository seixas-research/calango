#pragma once

#include "gui/SimulationWizardBase.hpp"

#include <QList>
#include <QPair>
#include <QString>

#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QSpinBox;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Modules → 2D Materials → "2D Bands…": the band structure of a sheet as a
/// SURFACE E_n(k_x, k_y) rather than as a path through the Brillouin zone.
///
/// Same workflow as the Electronic Structure wizard, and deliberately so: for
/// GPAW and VASP, a mandatory completed single point supplies the converged
/// density, the run is non-self-consistent on top of it, and cutoff / XC /
/// mode are inherited rather than re-asked. The difference is the sampling. A
/// 1D band structure walks a path Γ→M→K→Γ, which is a set of cuts through the
/// dispersion; this samples the plane itself, which is what makes a Dirac
/// cone look like a cone and a band touching visible as a touching rather
/// than as a near-miss between two lines.
///
/// Four engines, per core::TwoDBandsBackend — see its own doc comment and
/// core::generateTwoDBandsScript() for the per-engine chaining, all of it
/// reused from the 1D Electronic Structure module rather than invented here.
/// GPAW and VASP restart a baseline (`.gpw` / `CHGCAR`); Quantum ESPRESSO and
/// SIESTA have no single-file restart artifact this application establishes
/// for either, so they run self-contained (their own SCF, sized by the
/// cutoff/k-grid controls this wizard shows only for them) — this class owns
/// its own small engine picker rather than the standard one
/// (showsEngineAndDftControls() stays off, since the standard GPAW/VASP/QE/
/// SIESTA chrome offers far more than a baseline-restart or a minimal
/// self-contained SCF needs), and switches which group is visible when the
/// engine changes.
class TwoDBandsWizard : public SimulationWizardBase {
    Q_OBJECT

public:
    TwoDBandsWizard(std::shared_ptr<const core::Structure> structure,
                    QWidget* parent = nullptr);

    /// Populate the baseline selector with completed Single-Point Calculations
    /// that saved a density — GPAW's `.gpw` or VASP's `CHGCAR`: (display
    /// label, absolute path). Only consulted for those two engines; Quantum
    /// ESPRESSO and SIESTA run self-contained and never read it.
    void setDensityBaselines(const QList<QPair<QString, QString>>& baselines);

protected:
    QString wizardTitle() const override;
    QStringList calculatorElements() const override;
    QString settingsHeader() const override { return QString(); } // unused
    QWidget* buildSettingsPage() override { return nullptr; }     // unused
    bool hasTaskSettingsStage() const override { return false; }
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("bands_2d.py");
    }
    QString calculatorSettingsHeader() const override
    {
        return tr("Engine & Brillouin-Zone Sampling");
    }
    /// GPAW, VASP, Quantum ESPRESSO and SIESTA — see the class doc for how
    /// each one's chaining differs.
    bool calculatorAllowed(core::CalculatorKind kind) const override;
    /// The standard chrome is hidden — buildCalculatorExtras() below carries
    /// its own minimal engine picker instead, since the standard one offers
    /// far more (full GPAW mode/XC/convergence groups, VASP INCAR groups)
    /// than a baseline-restart or a minimal self-contained SCF needs.
    bool showsEngineAndDftControls() const override { return false; }
    /// True only in the sense that GPAW/VASP inherit cutoff/XC/mode from a
    /// baseline; Espresso/Siesta do not inherit anything (no baseline
    /// exists for them here) — see the class doc. Left true because this
    /// flag's only visible effect elsewhere is suppressing GPAW-specific
    /// rows this wizard never shows in the first place.
    bool inheritsCalculatorFromBaseline() const override { return true; }
    QWidget* buildCalculatorExtras() override;
    /// Swap which group (baseline restart vs. self-contained SCF) is
    /// visible, and hide the GPAW-only extras (spin-orbit, Brillouin-zone
    /// map, band count) for the other three engines.
    void updateCalculatorExtras(core::CalculatorKind kind) override;
    /// Quantum ESPRESSO / SIESTA only: warns, before staging anything, when
    /// no pseudopotential directory is configured for whichever of the two
    /// is selected — see SimulationWizardBase::preflightSecondary()'s own
    /// doc for why this is weaker than VASP's per-element check.
    bool preflightSecondary() override;
    /// The selected baseline density file, for remote staging (see the base
    /// class's own doc comment).
    QString baselineDensityPathToStage() const override;

private:
    /// Warn in-page when the selected structure is not a sheet: the k_z = 0
    /// plane is the whole Brillouin zone only for something periodic in x and
    /// y with vacuum along z. The generated script raises on a genuinely
    /// non-periodic cell, but a bulk crystal produces a plausible-looking
    /// surface that means something else — so it is worth saying up front.
    void refreshDimensionalityNote();
    /// Estimated cost of the current grid, which is quadratic and the one
    /// setting that can turn a two-minute job into an overnight one.
    void refreshCostNote();

    std::shared_ptr<const core::Structure> structure_;

    /// This wizard's own engine picker — see the class doc for why it is
    /// not the standard one.
    QComboBox* engineCombo_ = nullptr;
    /// Baseline restart group — GPAW (.gpw) / VASP (CHGCAR) only.
    QGroupBox* baselineGroup_ = nullptr;
    QComboBox* baselineCombo_ = nullptr;
    /// Self-contained SCF group — Espresso / Siesta only, since neither has
    /// a baseline artifact this application restarts from (see the class
    /// doc). Mirrors the 1D Electronic Structure module's own defaults.
    QGroupBox* scfGroup_ = nullptr;
    QDoubleSpinBox* ecutSpin_ = nullptr;
    QSpinBox* scfKptsSpin_ = nullptr;

    /// The grid/band-window group's own layout, kept so updateCalculatorExtras()
    /// can hide the "Bands diagonalized" row for SIESTA specifically (its
    /// finite atomic basis sets the count implicitly — see
    /// TwoDBandsConfig::totalBands) without hiding the whole group.
    QFormLayout* gridForm_ = nullptr;
    QSpinBox* samplesSpin_ = nullptr;
    QSpinBox* belowSpin_ = nullptr;
    QSpinBox* aboveSpin_ = nullptr;
    QSpinBox* totalBandsSpin_ = nullptr;
    QCheckBox* spinOrbitCheck_ = nullptr;
    /// Opt-in second sampling: the flat first-Brillouin-zone map. The N spin
    /// is only meaningful while the check box is on, and is disabled
    /// otherwise so the dependency is visible rather than remembered.
    QGroupBox* bzMapGroup_ = nullptr;
    QCheckBox* bzMapCheck_ = nullptr;
    QSpinBox* bzMapSamplesSpin_ = nullptr;
    QLabel* dimensionalityNote_ = nullptr;
    QLabel* costNote_ = nullptr;
};

} // namespace calango::gui
