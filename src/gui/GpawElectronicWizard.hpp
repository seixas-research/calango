#pragma once

#include "gui/GpawElectronicRows.hpp"
#include "gui/SimulationWizardBase.hpp"

namespace calango::gui {

/// Intermediate base for wizards whose calculator stage carries the full
/// GPAW electronic-structure form: the SCF convergence + smearing rows and
/// the spin-configuration rows, both built by the shared GpawElectronicRows.
///
/// Ten wizards used to repeat the same six forwarding overrides verbatim;
/// this class is that block, once. `electronic_` stays protected because
/// every subclass reads its values in generateScript().
class GpawElectronicWizard : public SimulationWizardBase {
public:
    using SimulationWizardBase::SimulationWizardBase;

protected:
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
    bool hasSpinExtras() const override { return true; }

    GpawElectronicRows electronic_;
};

} // namespace calango::gui
