#include "gui/ElectronicBandsWizard.hpp"

#include "core/ElectronicScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/EmbeddedKPathEditor.hpp"

#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

ElectronicBandsWizard::ElectronicBandsWizard(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
{
    buildUi();
}

QString ElectronicBandsWizard::wizardTitle() const
{
    return tr("Electronic Structure Setup");
}

QString ElectronicBandsWizard::settingsHeader() const
{
    return tr("k-Path Definition");
}

QWidget* ElectronicBandsWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("Define the high-symmetry k-path for the band structure. This run "
           "builds on a single-point (SCF) baseline: the ground-state density "
           "is converged first, then the bands are evaluated along the path "
           "below. The SCF cutoff and k-grid are set in Stage 3."),
        page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // The interactive Brillouin zone is the stage, not a dialog launched
    // from it: clicking Γ, X, M… here updates the wizard's path immediately,
    // and "Next ›" carries it straight into Stage 2.
    kpath_ = new EmbeddedKPathEditor(structure_, page);
    layout->addWidget(kpath_, 1);
    connect(kpath_, &EmbeddedKPathEditor::pathChanged, this,
            [this] { refreshPreview(); });

    auto* form = new QFormLayout;
    layout->addLayout(form);

    valenceSpin_ = new QSpinBox(page);
    valenceSpin_->setRange(1, 200);
    valenceSpin_->setValue(4);
    valenceSpin_->setToolTip(
        tr("Electrons per cell — used only by the free-electron backend."));
    form->addRow(tr("Valence electrons:"), valenceSpin_);

    layout->addStretch(1);
    return page;
}

QWidget* ElectronicBandsWizard::buildCalculatorExtras()
{
    // PDOS and its broadening describe how the *calculator* samples the
    // density of states, so they live with the other calculator settings
    // rather than on the k-path page.
    pdosGroup_ = new QGroupBox(tr("Density of states"), this);
    auto* form = new QFormLayout(pdosGroup_);

    // Charge-density baseline: a completed Single-Point Calculation whose
    // converged density (.gpw) the bands/PDOS run reuses non-self-consistently
    // (GPAW calc.fixed_density). Populated by the controller via
    // setDensityBaselines(); "(none)" keeps the self-contained inline-SCF path.
    baselineCombo_ = new QComboBox(pdosGroup_);
    baselineCombo_->addItem(tr("(none — run a self-consistent SCF inline)"),
                            QString());
    baselineCombo_->setToolTip(
        tr("Load the converged charge density saved by a completed Single-Point "
           "Calculation and evaluate the band structure and PDOS at fixed "
           "density (NSCF). Requires the GPAW backend."));
    form->addRow(tr("Charge-density baseline:"), baselineCombo_);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    pdosCheck_ = new QCheckBox(
        tr("Compute element/orbital PDOS (GPAW backend)"), pdosGroup_);
    pdosCheck_->setChecked(true);
    form->addRow(QString(), pdosCheck_);

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
    connect(pdosCheck_, &QCheckBox::toggled, pdosWidthSpin_,
            &QDoubleSpinBox::setEnabled);
    connect(pdosCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    connect(pdosWidthSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    return pdosGroup_;
}

void ElectronicBandsWizard::updateCalculatorExtras(core::CalculatorKind kind)
{
    // Only the GPAW backend produces a projected DOS; showing the controls
    // for the others would promise output they cannot generate.
    if (pdosGroup_)
        pdosGroup_->setVisible(kind == core::CalculatorKind::Gpaw);
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
    config.nvalence = valenceSpin_->value();
    config.pdos = pdosCheck_->isChecked();
    config.pdosWidthEv = pdosWidthSpin_->value();
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
