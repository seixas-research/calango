#pragma once

// <cstdint> must stay even when clangd calls it unused: libstdc++ from GCC 13
// no longer pulls the fixed-width integer types in transitively, so removing it
// breaks the Linux .deb build while the macOS build stays green.
#include <cstdint>
#include <memory>

#include "core/ThermodynamicIntegrationScriptGenerator.hpp"
#include "gui/GpawElectronicWizard.hpp"

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

/// Simulation → "Thermodynamic Integration…".
///
/// The ABSOLUTE Helmholtz / Gibbs free energy, by coupling the system
/// reversibly to a reference whose free energy is known in closed form. Stage 1 is the shared Calculator Settings — the engine chosen
/// there is the TARGET Hamiltonian, and it goes through CalculatorConfig like
/// every other module — stage 2 is the path (reference, λ schedule, windows,
/// sampling), stage 3 is the script review.
///
/// The dynamics are the MD module's: the same ASE integrators, emitted by the
/// same shared block (core/MdIntegratorBlocks.hpp), with the same coupling
/// parameters. There is deliberately no second MD driver — an MD run and a TI
/// window that disagreed about what "thermostat coupling" means would be
/// running different dynamics with nothing in the output to say so.
///
/// TWO THINGS THIS PAGE EXISTS TO STOP THE USER DOING. Sampling λ = 0 against
/// an ideal-gas reference (the endpoint singularity: nothing keeps the atoms
/// apart there, and the target charges an unbounded energy for the overlap) —
/// which is why Gauss-Legendre, whose nodes are strictly interior, is the
/// default and why choosing a schedule that includes the endpoints raises a
/// visible warning. And running with no equilibration, which biases every
/// window in the same direction so the bias survives the λ integral instead of
/// cancelling.
class ThermodynamicIntegrationWizard : public GpawElectronicWizard {
    Q_OBJECT

public:
    explicit ThermodynamicIntegrationWizard(
        std::shared_ptr<const core::Structure> structure = nullptr,
        QWidget* parent = nullptr);

    /// The scripts this run needs, one per job.
    ///
    /// Normally one. When "Dispatch windows as separate jobs" is set above 1
    /// the λ windows — which are independent — are split into that many
    /// scripts, each owning a contiguous slice and all of them writing into the
    /// SAME results directory. The host submits them through the existing job
    /// queue; whichever finishes last finds a complete set of per-window files
    /// and writes the summary.
    ///
    /// script() (the base class's, and what a single-job host runs) always
    /// returns the FIRST of these, so a host that does not know about splitting
    /// still runs a valid script — it simply runs one slice.
    QStringList scripts() const;

    /// How many jobs scripts() will produce.
    int jobCount() const;

Q_SIGNALS:
    /// The "Load Results…" button was pressed. The wizard deliberately does
    /// nothing itself: choosing a ti.json and opening the viewer are the host's
    /// job, and are the same code path the Processes panel and a finished job
    /// already go through.
    void loadResultsRequested();

protected:
    QString wizardTitle() const override;
    QStringList calculatorElements() const override;
    QString settingsHeader() const override;
    QWidget* buildSettingsPage() override;
    /// Calculator Settings first: the engine is the target Hamiltonian, and
    /// the path is defined against it. Same order as Molecular Dynamics.
    bool settingsStageFirst() const override { return false; }
    bool showsDispersionToggle() const override { return true; }
    bool taskHasIonicSteps() const override { return true; }
    QString generateScript() const override;
    QString exportFileName() const override
    {
        return QStringLiteral("thermodynamic_integration.py");
    }

private Q_SLOTS:
    /// Show only the rows the selected reference actually uses, restate the λ
    /// path, and raise the endpoint warning when it applies.
    void updateReferenceRows();
    /// Enable only the coupling knobs the selected ensemble reads, and the
    /// pressure only for a barostatted one.
    void updateEnsembleRows();
    /// Rebuild the summary: the λ values, the total MD cost, and the refusals.
    void refreshPathSummary();

private:
    core::TiRunConfig runConfig() const;
    core::TiReference selectedReference() const;
    core::TiLambdaSchedule selectedSchedule() const;
    core::TiQuadrature selectedQuadrature() const;

    std::shared_ptr<const core::Structure> structure_;
    QString resultsDirectory_;

    QFormLayout* form_ = nullptr;

    QComboBox* referenceCombo_ = nullptr;
    QDoubleSpinBox* einsteinSpringSpin_ = nullptr;
    QCheckBox* einsteinFixComCheck_ = nullptr;
    QDoubleSpinBox* ljEpsilonSpin_ = nullptr;
    QDoubleSpinBox* ljSigmaSpin_ = nullptr;
    QDoubleSpinBox* ljCutoffSpin_ = nullptr;

    QComboBox* scheduleCombo_ = nullptr;
    QDoubleSpinBox* scheduleExponentSpin_ = nullptr;
    QComboBox* quadratureCombo_ = nullptr;
    QSpinBox* windowsSpin_ = nullptr;
    QSpinBox* jobsSpin_ = nullptr;
    QCheckBox* hysteresisCheck_ = nullptr;

    QComboBox* ensembleCombo_ = nullptr;
    QDoubleSpinBox* temperatureSpin_ = nullptr;
    QDoubleSpinBox* pressureSpin_ = nullptr;
    QDoubleSpinBox* timestepSpin_ = nullptr;
    QDoubleSpinBox* frictionSpin_ = nullptr;
    QDoubleSpinBox* tautSpin_ = nullptr;
    QDoubleSpinBox* taupSpin_ = nullptr;
    QSpinBox* equilibrationSpin_ = nullptr;
    QSpinBox* productionSpin_ = nullptr;
    QSpinBox* sampleSpin_ = nullptr;

    QLabel* referenceNote_ = nullptr;
    QLabel* pathSummary_ = nullptr;
    QLabel* endpointWarning_ = nullptr;
};

} // namespace calango::gui
