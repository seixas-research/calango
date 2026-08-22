#pragma once

#include "gui/CellRelaxationControls.hpp"
#include "gui/ForceConvergenceControl.hpp"
#include "gui/GpawElectronicWizard.hpp"

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QSpinBox;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Simulation → "Geometry Optimization…": a 3-stage wizard. Stage 1 is the
/// shared Calculator Settings — for GPAW, the exact same form the Single-Point
/// wizard shows (Mode & Basis Set, Brillouin Zone & k-Points, Electronic
/// Convergence & Smearing, Spin Configurations, Output & Exports), since the
/// SCF a relaxation step runs is the same SCF; Stage 2 is the relaxation
/// settings (optimizer, force convergence, step cap, and optional
/// variable-cell relaxation with a hydrostatic/anisotropic stress mask);
/// Stage 3 is the ASE script review.
class GeometryOptimizationWizard : public GpawElectronicWizard {
    Q_OBJECT

public:
    /// `structure` is the geometry being relaxed. It is only read to populate
    /// the "Geometry constraints…" editor (and to seed the Hubbard editor's
    /// element completer); passing null simply leaves the per-atom constraint
    /// table empty.
    explicit GeometryOptimizationWizard(
        std::shared_ptr<const core::Structure> structure = nullptr,
        QWidget* parent = nullptr);

protected:
    QString wizardTitle() const override;
    QStringList calculatorElements() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    /// Calculator Settings first, then the relaxation settings page.
    /// Dispersion is offered here: a relaxation path is exactly where the missing long-range attraction changes the geometry it settles into.
    bool showsDispersionToggle() const override { return true; }
    bool settingsStageFirst() const override { return false; }
    bool taskHasIonicSteps() const override { return true; }
    /// VASP's own internal relaxation route (IBRION/NSW/ISIF/EDIFFG, chosen
    /// on the Calculator Settings page) makes this stage's optimizer
    /// algorithm, ASE force criterion and cell filter/mask redundant — VASP
    /// is the one taking the ionic steps, not ASE, so those controls
    /// describe a loop that is never created. NSW is surfaced instead on the
    /// VASP settings group itself (vaspNswSpin_); the cell-relaxation FLOOR
    /// (relax cell y/n) is already fully covered by the ISIF combo there.
    /// Per-atom geometry constraints ("Geometry constraints…", also on this
    /// stage) become unreachable on this route in the process — noted as a
    /// known limitation in FUTURE.md rather than solved here.
    bool skipTaskSettingsStage() const override
    {
        return vaspInternalRelaxationSelected();
    }
    bool showsGpawSymmetryToggle() const override { return true; }
    bool showsGpawDensityExport() const override { return true; }
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("optimize.py"); }

private Q_SLOTS:
    /// "Geometry constraints…": open the editor and keep its result.
    void editConstraints();

private:
    core::CalculatorConfig config() const;
    /// One-line description of the active constraints, shown next to the
    /// button so the page says what is frozen without being reopened.
    void refreshConstraintSummary();

    std::shared_ptr<const core::Structure> structure_;
    /// Frozen degrees of freedom. Owned here rather than in the dialog, which
    /// is constructed on demand and destroyed on close.
    std::vector<core::GeometryConstraint> constraints_;
    QLabel* constraintSummary_ = nullptr;

    QComboBox* optimizerCombo_;
    ForceConvergenceControl fmax_;
    QSpinBox* maxStepsSpin_;
    /// Variable-cell relaxation (relax cell, filter, stress mask, Voigt ticks).
    /// Shared with the Cluster Expansion batch, which relaxes cells with the
    /// same options — see CellRelaxationControls.
    CellRelaxationControls cell_{this};
};

} // namespace calango::gui
