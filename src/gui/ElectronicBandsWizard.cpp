#include "gui/ElectronicBandsWizard.hpp"

#include "gui/GuiUtils.hpp"

#include "core/ElectronicScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/EmbeddedKPathEditor.hpp"

#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <set>

namespace calango::gui {

ElectronicBandsWizard::ElectronicBandsWizard(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
{
    buildUi();
    // The NSCF-from-baseline workflow (mandatory density, cutoff/XC/mode
    // inheritance, PDOS) is GPAW-specific, so open on GPAW rather than the
    // first allowed engine.
    selectCalculator(core::CalculatorKind::Gpaw);
}

QString ElectronicBandsWizard::wizardTitle() const
{
    return tr("Electronic Structure Setup");
}

QStringList ElectronicBandsWizard::calculatorElements() const
{
    return structureElements(structure_.get());
}

QWidget* ElectronicBandsWizard::buildCalculatorExtras()
{
    // The former separate k-Path stage is merged here: this single page hosts
    // baseline selection + PDOS settings + the interactive k-path builder.
    pdosGroup_ = new QGroupBox(tr("Density of states"), this);
    auto* form = new QFormLayout(pdosGroup_);

    // Charge-density baseline: a completed Single-Point Calculation whose
    // converged density (.gpw) the bands/PDOS run reuses non-self-consistently
    // (GPAW calc.fixed_density). This is MANDATORY — the controller
    // (MainWindow::showBandStructure) refuses to open the wizard when no
    // completed SCF baseline exists, so there is no inline-SCF fallback option.
    baselineCombo_ = new QComboBox(pdosGroup_);
    baselineCombo_->setToolTip(
        tr("The completed Single-Point Calculation whose converged charge "
           "density this run restarts from. The band structure and PDOS are "
           "evaluated at fixed density (NSCF); cutoff, XC and mode are inherited "
           "from it."));
    form->addRow(tr("Baseline SCF density:"), baselineCombo_);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    pdosCheck_ = new QCheckBox(
        tr("Compute element/orbital PDOS (GPAW backend)"), pdosGroup_);
    pdosCheck_->setChecked(true);
    form->addRow(QString(), pdosCheck_);

    // Fixed-density PDOS k-mesh, defaulted to 2× the baseline SCF grid along
    // each non-vacuum direction (a vacuum direction sampled with 1 k-point
    // stays 1). The PDOS is re-sampled at this denser mesh via fixed_density.
    auto* pdosKptRow = new QHBoxLayout;
    for (auto*& spin : pdosKptsSpin_) {
        spin = new QSpinBox(pdosGroup_);
        spin->setRange(1, 128);
        spin->setValue(14);
        pdosKptRow->addWidget(spin);
        connect(spin, &QSpinBox::valueChanged, this, [this] {
            // A manual edit freezes the auto-scaling.
            pdosKptsUserEdited_ = true;
            refreshPreview();
        });
    }
    form->addRow(tr("PDOS k-mesh:"), pdosKptRow);

    energyPointsSpin_ = new QSpinBox(pdosGroup_);
    energyPointsSpin_->setRange(50, 20000);
    energyPointsSpin_->setValue(401);
    energyPointsSpin_->setSingleStep(50);
    energyPointsSpin_->setToolTip(
        tr("Number of energy sampling points for the projected DOS; higher "
           "values give a finer energy grid."));
    form->addRow(tr("Energy points (N):"), energyPointsSpin_);
    connect(energyPointsSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    pdosWidthSpin_ = new QDoubleSpinBox(pdosGroup_);
    pdosWidthSpin_->setRange(0.001, 5.0);
    pdosWidthSpin_->setDecimals(3);
    pdosWidthSpin_->setSingleStep(0.01);
    pdosWidthSpin_->setValue(0.1);
    pdosWidthSpin_->setSuffix(tr(" eV"));
    pdosWidthSpin_->setToolTip(
        tr("Gaussian broadening σ applied to the projected DOS.\n"
           "The raw PDOS is a sum of delta functions at the eigenvalues; σ "
           "turns it into a smooth curve. ~0.05–0.2 eV is typical — smaller "
           "resolves sharp d-band features but needs a denser k-mesh."));
    form->addRow(tr("Gaussian smearing σ:"), pdosWidthSpin_);
    // The PDOS controls only make sense when PDOS is requested.
    for (QWidget* w : {static_cast<QWidget*>(pdosKptsSpin_[0]),
                       static_cast<QWidget*>(pdosKptsSpin_[1]),
                       static_cast<QWidget*>(pdosKptsSpin_[2]),
                       static_cast<QWidget*>(energyPointsSpin_),
                       static_cast<QWidget*>(pdosWidthSpin_)}) {
        connect(pdosCheck_, &QCheckBox::toggled, w, &QWidget::setEnabled);
    }
    connect(pdosCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    connect(pdosWidthSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    // Seed the PDOS k-mesh from the (default) SCF k-grid now that the base DFT
    // controls exist.
    applyPdosKmeshDefault();

    // --- Spin Configurations ---------------------------------------------
    // The SCF's collinear polarization is inherited from the baseline density
    // along with everything else about the SCF. What is still open here is how
    // the BANDS are evaluated on top of it — and that is where spin-orbit
    // coupling lives, because SOC is applied to the converged states rather
    // than to the density.
    spinGroup_ = new QGroupBox(tr("Spin Configurations"), this);
    auto* spinForm = new QFormLayout(spinGroup_);

    spinOrbitCheck_ = new QCheckBox(tr("Spin-Orbit Coupling"), spinGroup_);
    spinOrbitCheck_->setToolTip(
        tr("Re-diagonalize the band energies in the spinor basis "
           "(gpaw.spinorbit.soc_eigenstates), non-perturbatively.\n\n"
           "This is what lifts the degeneracies a scalar-relativistic run "
           "leaves in place: the Γ-point valence band splitting of a III-V "
           "semiconductor, Rashba splitting at a heavy-element surface, the "
           "band inversion of a topological insulator.\n\n"
           "The result is ONE channel of doubled, spin-mixed bands rather than "
           "a spin-up/spin-down pair, and the Fermi level moves with them. For "
           "light elements the shift is meV; for 5d/6p systems it is the "
           "difference between the right answer and the wrong one."));
    spinForm->addRow(QString(), spinOrbitCheck_);
    connect(spinOrbitCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    // DFT+U on this page too: a band structure is exactly where a missing U
    // shows up (a known insulator coming out metallic), so being sent back to
    // re-run the baseline to add one is the wrong shape of workflow.
    auto* hubbardButton = new QPushButton(tr("Hubbard parameters…"), spinGroup_);
    hubbardButton->setToolTip(
        tr("Add an on-site Coulomb repulsion U to a named orbital shell "
           "(GPAW setups={…}). For narrow d/f bands that a semilocal "
           "functional over-delocalizes — the usual symptom is a metallic "
           "band structure for a known insulator.\n\n"
           "Note: with a baseline density selected the bands are evaluated at "
           "that FIXED density, so the U in force is the one the baseline was "
           "converged with. A U set here applies when this run converges its "
           "own SCF."));
    connect(hubbardButton, &QPushButton::clicked, this,
            &ElectronicBandsWizard::editHubbardParameters);
    spinForm->addRow(QString(), hubbardButton);

    // Merged stage: the interactive Brillouin-zone / k-path builder comes
    // first, with the spin and PDOS configuration below it, in one container so
    // the wizard has a single setup stage.
    auto* container = new QWidget(this);
    auto* vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 0);

    vbox->addWidget(new QLabel(tr("High-symmetry k-path:"), container));
    kpath_ = new EmbeddedKPathEditor(structure_, container);
    vbox->addWidget(kpath_, 1);
    connect(kpath_, &EmbeddedKPathEditor::pathChanged, this,
            [this] { refreshPreview(); });

    vbox->addWidget(spinGroup_);
    vbox->addWidget(pdosGroup_);

    return container;
}

void ElectronicBandsWizard::applyPdosKmeshDefault()
{
    if (pdosKptsUserEdited_)
        return;
    for (int axis = 0; axis < 3; ++axis) {
        if (!pdosKptsSpin_[axis])
            continue;
        const int base = calculatorKpoint(axis);
        // Vacuum directions (single k-point) stay at 1; everything else ×2.
        const int scaled = base <= 1 ? base : base * 2;
        const QSignalBlocker blocker(pdosKptsSpin_[axis]);
        pdosKptsSpin_[axis]->setValue(scaled);
    }
}

void ElectronicBandsWizard::calculatorKgridChanged()
{
    // The baseline SCF k-grid changed — rescale the PDOS mesh default unless
    // the user has taken it over.
    applyPdosKmeshDefault();
}

void ElectronicBandsWizard::updateCalculatorExtras(core::CalculatorKind kind)
{
    // Only the GPAW backend produces a projected DOS, applies spin-orbit
    // coupling or takes a `setups` DFT+U dictionary; showing the controls for
    // the others would promise output they cannot generate.
    const bool gpaw = kind == core::CalculatorKind::Gpaw;
    if (pdosGroup_)
        pdosGroup_->setVisible(gpaw);
    if (spinGroup_)
        spinGroup_->setVisible(gpaw);
}

bool ElectronicBandsWizard::calculatorAllowed(core::CalculatorKind kind) const
{
    switch (kind) {
    case core::CalculatorKind::Gpaw:
    case core::CalculatorKind::Siesta:
    case core::CalculatorKind::Vasp:
    case core::CalculatorKind::QuantumEspresso:
        return true;
    default:
        return false;
    }
}

QString ElectronicBandsWizard::generateScript() const
{
    core::ElectronicConfig config;
    switch (selectedCalculator()) {
    case core::CalculatorKind::Gpaw:
        config.backend = core::ElectronicBackend::Gpaw;
        break;
    case core::CalculatorKind::QuantumEspresso:
        config.backend = core::ElectronicBackend::Espresso;
        break;
    case core::CalculatorKind::Siesta:
        config.backend = core::ElectronicBackend::Siesta;
        break;
    case core::CalculatorKind::Vasp:
        config.backend = core::ElectronicBackend::Vasp;
        break;
    default:
        // The combo is DFT-only, so this is unreachable; fall back to the
        // free-electron reference model for safety.
        config.backend = core::ElectronicBackend::FreeElectrons;
        break;
    }

    // Keep the ',' section breaks; ASE's cell.bandpath() understands them.
    config.kpath = kpath_->path().toStdString();
    // Single source of truth for path sampling: the k-path builder's
    // "points per segment" times the number of segments. The wizard used to
    // carry a second, independent "k-points along path" box that could
    // silently disagree with it.
    config.npoints = kpath_->pointsPerSegment() * kpath_->segmentCount();
    config.spinOrbit = spinOrbitCheck_ && spinOrbitCheck_->isChecked();
    config.pdos = pdosCheck_->isChecked();
    config.pdosWidthEv = pdosWidthSpin_->value();
    config.pdosPoints = energyPointsSpin_->value();
    for (int axis = 0; axis < 3; ++axis)
        config.pdosKpts[axis] = pdosKptsSpin_[axis]->value();
    // Full GPAW parameter set from Stage 2 (mode, xc, eigensolver, mixer,
    // convergence, smearing, k-grid) — the same controls Geometry
    // Optimization and Single-point use.
    config.gpaw = baseCalculatorConfig();

    // SCF baseline from the shared Stage-3 DFT knobs.
    const core::CalculatorConfig base = baseCalculatorConfig();
    config.ecutEv = base.planeWaveCutoffEv;
    config.scfKpts = base.kpts[0];

    // A selected charge-density baseline (GPAW only) turns the run NSCF: load
    // that .gpw and evaluate bands/PDOS at fixed density. The path is absolute,
    // so the NSCF job reads the prior run's density in place — no staging.
    if (baselineCombo_ && config.backend == core::ElectronicBackend::Gpaw) {
        const QString path = baselineCombo_->currentData().toString();
        if (!path.isEmpty())
            config.baselineDensityPath = path.toStdString();
    }

    return QString::fromStdString(core::generateElectronicScript(config));
}

void ElectronicBandsWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    for (const auto& [label, path] : baselines)
        baselineCombo_->addItem(label, path);
}

} // namespace calango::gui
