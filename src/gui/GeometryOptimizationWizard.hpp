#pragma once

#include "gui/GpawElectronicRows.hpp"
#include "gui/SimulationWizardBase.hpp"

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
class GeometryOptimizationWizard : public SimulationWizardBase {
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
    /// The GPAW calculator form is shared verbatim with the Single-Point
    /// wizard through GpawElectronicRows — same groups, same rows, same
    /// toggles — so switching between the two setups is not a change of form.
    /// The engine drives which smearing methods are offered, so the base
    /// needs a handle on these rows to refilter them.
    GpawElectronicRows* electronicRows() override { return &electronic_; }
    void buildConvergenceRows(QFormLayout* form) override
    {
        electronic_.buildConvergenceRows(form, this);
    }
    void buildSpinRows(QFormLayout* form) override
    {
        electronic_.buildSpinRows(form, this);
    }
    // Created by `electronic_`, positioned by the base class: the SCF energy
    // tolerance belongs on the tolerance row and the step cap beside the
    // eigensolver, and both of those rows are the base class's to lay out.
    QWidget* gpawEnergyToleranceWidget() override
    {
        return electronic_.energyToleranceWidget();
    }
    QWidget* gpawScfStepsWidget() override
    {
        return electronic_.scfStepsWidget();
    }
    bool hasConvergenceExtras() const override { return true; }
    bool taskHasIonicSteps() const override { return true; }
    bool hasSpinExtras() const override { return true; }
    bool showsGpawSymmetryToggle() const override { return true; }
    bool showsGpawDensityExport() const override { return true; }
    QString generateScript() const override;
    QString exportFileName() const override { return QStringLiteral("optimize.py"); }

private Q_SLOTS:
    void updateCellEnabled();
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
    QDoubleSpinBox* fmaxSpin_;
    QSpinBox* maxStepsSpin_;
    QCheckBox* relaxCellCheck_;
    QComboBox* cellFilterCombo_;
    QComboBox* stressMaskCombo_;   // anisotropic / hydrostatic / custom
    QWidget* voigtRow_;            // custom Voigt mask checkbox row
    /// Indices into stressMaskCombo_. Named because three call sites compare
    /// against them and a bare 2/3 is where an inserted entry breaks the mask.
    static constexpr int kStressMask2Dxy = 2;
    static constexpr int kStressMaskCustom = 3;

    QCheckBox* voigtChecks_[6];    // [xx, yy, zz, yz, xz, xy]

    /// Smearing / SCF / spin rows, shared with the Single-Point wizard.
    GpawElectronicRows electronic_;
};

} // namespace calango::gui
