#include "gui/TwoDBandsWizard.hpp"

#include "core/Structure.hpp"
#include "core/TwoDBandsScriptGenerator.hpp"
#include "gui/GuiUtils.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

namespace calango::gui {

namespace {

/// core::TwoDBandsBackend <-> core::CalculatorKind. One place, so the wizard
/// and the script generator cannot disagree about which kind means which
/// backend — the same convention ElectronicBandsWizard's own
/// core::ElectronicBackend mapping follows.
core::TwoDBandsBackend backendFor(core::CalculatorKind kind)
{
    switch (kind) {
    case core::CalculatorKind::Vasp:
        return core::TwoDBandsBackend::Vasp;
    case core::CalculatorKind::QuantumEspresso:
        return core::TwoDBandsBackend::Espresso;
    case core::CalculatorKind::Siesta:
        return core::TwoDBandsBackend::Siesta;
    default:
        return core::TwoDBandsBackend::Gpaw;
    }
}

/// Self-contained SCF (Espresso, Siesta): no baseline exists for either, so
/// this is what decides which group the wizard shows.
bool selfContained(core::CalculatorKind kind)
{
    return kind == core::CalculatorKind::QuantumEspresso
        || kind == core::CalculatorKind::Siesta;
}

} // namespace

TwoDBandsWizard::TwoDBandsWizard(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : SimulationWizardBase(parent), structure_(std::move(structure))
{
    buildUi();
    selectCalculator(core::CalculatorKind::Gpaw);
}

bool TwoDBandsWizard::calculatorAllowed(core::CalculatorKind kind) const
{
    switch (kind) {
    case core::CalculatorKind::Gpaw:
    case core::CalculatorKind::Vasp:
    case core::CalculatorKind::QuantumEspresso:
    case core::CalculatorKind::Siesta:
        return true;
    default:
        return false;
    }
}

QString TwoDBandsWizard::wizardTitle() const
{
    return tr("2D Bands Setup");
}

QStringList TwoDBandsWizard::calculatorElements() const
{
    return structureElements(structure_.get());
}

QWidget* TwoDBandsWizard::buildCalculatorExtras()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* intro = new QLabel(
        tr("Samples the eigenvalues over the <b>two-dimensional Brillouin "
           "zone</b> at k<sub>z</sub> = 0, as surfaces "
           "E<sub>n</sub>(k<sub>x</sub>, k<sub>y</sub>)."),
        page);
    intro->setWordWrap(true);
    intro->setToolTip(
        tr("For GPAW and VASP the run is non-self-consistent on top of the "
           "baseline density below, so the cutoff, functional and mode all "
           "come from that calculation. Quantum ESPRESSO and SIESTA run "
           "self-contained instead — see the Self-contained SCF group."));
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    dimensionalityNote_ = new QLabel(page);
    dimensionalityNote_->setWordWrap(true);
    dimensionalityNote_->setTextFormat(Qt::RichText);
    layout->addWidget(dimensionalityNote_);

    // This wizard's own engine picker — see the class doc for why it is not
    // the standard one (showsEngineAndDftControls() stays off).
    auto* engineRow = new QWidget(page);
    auto* engineForm = new QFormLayout(engineRow);
    engineForm->setContentsMargins(0, 0, 0, 0);
    engineCombo_ = new QComboBox(engineRow);
    for (const auto& [kind, label] :
         {std::pair{core::CalculatorKind::Gpaw, tr("GPAW")},
          std::pair{core::CalculatorKind::Vasp, tr("VASP")},
          std::pair{core::CalculatorKind::QuantumEspresso,
                    tr("Quantum ESPRESSO")},
          std::pair{core::CalculatorKind::Siesta, tr("SIESTA")}})
        engineCombo_->addItem(label, static_cast<int>(kind));
    engineCombo_->setToolTip(
        tr("GPAW and VASP restart a converged baseline density and evaluate "
           "the grid non-self-consistently. Quantum ESPRESSO and SIESTA "
           "have no single-file restart artifact this application uses, so "
           "they run their own SCF first — see Self-contained SCF below."));
    engineForm->addRow(tr("Engine:"), engineCombo_);
    connect(engineCombo_, &QComboBox::currentIndexChanged, this, [this] {
        selectCalculator(static_cast<core::CalculatorKind>(
            engineCombo_->currentData().toInt()));
    });
    layout->addWidget(engineRow);

    baselineGroup_ = new QGroupBox(tr("Baseline SCF Density"), page);
    auto* baselineForm = new QFormLayout(baselineGroup_);
    baselineCombo_ = new QComboBox(baselineGroup_);
    baselineCombo_->setToolTip(
        tr("The completed Single-Point Calculation whose converged charge "
           "density this run restarts from. The eigenvalues are evaluated at "
           "fixed density (NSCF). GPAW writes single_point.gpw; VASP writes "
           "CHGCAR (leave \"CHGCAR\" ticked under Write in the VASP "
           "settings)."));
    baselineForm->addRow(tr("Baseline SCF density:"), baselineCombo_);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });
    layout->addWidget(baselineGroup_);

    scfGroup_ = new QGroupBox(tr("Self-contained SCF"), page);
    auto* scfForm = new QFormLayout(scfGroup_);
    ecutSpin_ = new QDoubleSpinBox(scfGroup_);
    ecutSpin_->setRange(50.0, 5000.0);
    ecutSpin_->setDecimals(0);
    ecutSpin_->setSingleStep(50.0);
    ecutSpin_->setValue(500.0);
    ecutSpin_->setSuffix(tr(" eV"));
    ecutSpin_->setToolTip(
        tr("Plane-wave cutoff for the self-contained SCF that runs before "
           "the 2D grid — the same knob the 1D Electronic Structure "
           "module's own Quantum ESPRESSO/SIESTA branches use, converted to "
           "Ry for Quantum ESPRESSO."));
    scfForm->addRow(tr("Plane-wave cutoff:"), ecutSpin_);
    scfKptsSpin_ = new QSpinBox(scfGroup_);
    scfKptsSpin_->setRange(1, 64);
    scfKptsSpin_->setValue(7);
    scfKptsSpin_->setToolTip(
        tr("Monkhorst-Pack SCF k-grid, in-plane (n x n). The vacuum axis "
           "always gets exactly one k-point — the geometry check above "
           "already guarantees it is the axis with no periodicity to "
           "sample."));
    scfForm->addRow(tr("SCF k-grid (n × n):"), scfKptsSpin_);
    for (QDoubleSpinBox* spin : {ecutSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    connect(scfKptsSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    layout->addWidget(scfGroup_);

    auto* group = new QGroupBox(tr("Brillouin-Zone Grid"), page);
    gridForm_ = new QFormLayout(group);

    samplesSpin_ = new QSpinBox(group);
    samplesSpin_->setRange(3, 512);
    samplesSpin_->setValue(24);
    samplesSpin_->setToolTip(
        tr("Samples along each reciprocal-lattice direction; the grid is N×N "
           "and the cost is quadratic in N.\n\n"
           "This is the setting that decides whether a Dirac cone reads as a "
           "cone or as a staircase. ~24 is a good overview; resolving a band "
           "touching or a small avoided crossing wants 48 or more."));
    gridForm_->addRow(tr("Grid samples (N × N):"), samplesSpin_);
    connect(samplesSpin_, &QSpinBox::valueChanged, this, [this] {
        refreshCostNote();
        refreshPreview();
    });

    costNote_ = new QLabel(group);
    costNote_->setWordWrap(true);
    gridForm_->addRow(QString(), costNote_);

    belowSpin_ = new QSpinBox(group);
    belowSpin_->setRange(0, 200);
    belowSpin_->setValue(4);
    gridForm_->addRow(tr("Surfaces below E<sub>F</sub>:"), belowSpin_);

    aboveSpin_ = new QSpinBox(group);
    aboveSpin_->setRange(0, 200);
    aboveSpin_->setValue(4);
    aboveSpin_->setToolTip(
        tr("How many bands either side of the Fermi level become surfaces.\n\n"
           "Bands that CROSS the Fermi level are always included whatever "
           "these say — those are the Fermi surface, and dropping one because "
           "a count ran out would remove the feature the plot is for."));
    gridForm_->addRow(tr("Surfaces above E<sub>F</sub>:"), aboveSpin_);

    totalBandsSpin_ = new QSpinBox(group);
    totalBandsSpin_->setRange(0, 100000);
    totalBandsSpin_->setValue(0);
    totalBandsSpin_->setSpecialValueText(tr("inherit from the baseline"));
    totalBandsSpin_->setToolTip(
        tr("Bands the fixed-density run diagonalizes. Leave inherited unless "
           "the conduction bands you want are not in the baseline's set — "
           "unconverged empty states are the usual reason an upper surface "
           "looks noisy."));
    gridForm_->addRow(tr("Bands diagonalized:"), totalBandsSpin_);

    spinOrbitCheck_ = new QCheckBox(tr("Include spin-orbit coupling"), group);
    spinOrbitCheck_->setToolTip(
        tr("Re-diagonalize the converged states in the spinor basis "
           "(gpaw.spinorbit).\n\n"
           "Often the reason to draw a 2D surface at all: Rashba splitting and "
           "the gap opened at a band touching are simply absent from a scalar "
           "relativistic calculation, and both are features OF the surface "
           "rather than of any one cut through it."));
    gridForm_->addRow(QString(), spinOrbitCheck_);

    for (QSpinBox* spin : {belowSpin_, aboveSpin_, totalBandsSpin_})
        connect(spin, &QSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    connect(spinOrbitCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    layout->addWidget(group);

    // ------------------------------------------------------ Brillouin-zone map
    // GPAW only — see TwoDBandsConfig::bzMap's own doc for why.
    bzMapGroup_ = new QGroupBox(tr("Brillouin-zone map"), page);
    auto* mapForm = new QFormLayout(bzMapGroup_);

    bzMapCheck_ = new QCheckBox(tr("Also sample the full first Brillouin zone"),
                                bzMapGroup_);
    bzMapCheck_->setToolTip(
        tr("Adds a second, independent sampling for the flat E(k_x, k_y) map "
           "view in the results window: every band evaluated on an N×N "
           "Monkhorst-Pack mesh spanning the primitive 2D reciprocal cell.\n\n"
           "Off by default because it is pure extra cost when only the 3D "
           "surfaces are wanted — and a run without it keeps exactly the "
           "output it always had."));
    mapForm->addRow(bzMapCheck_);

    bzMapSamplesSpin_ = new QSpinBox(bzMapGroup_);
    bzMapSamplesSpin_->setRange(6, 96);
    bzMapSamplesSpin_->setValue(24);
    // Disabled until the option is on: a live spin next to an unchecked box
    // reads as a setting that does something.
    bzMapSamplesSpin_->setEnabled(false);
    bzMapSamplesSpin_->setToolTip(
        tr("Samples along each reciprocal-lattice direction for the map; the "
           "cost scales as N².\n\n"
           "Like the band surfaces above, the map is computed at fixed "
           "density, so each of the N×N k-points is one more "
           "diagonalization on top of the surface grid."));
    mapForm->addRow(tr("Map k-mesh (N × N):"), bzMapSamplesSpin_);

    auto* mapNote = new QLabel(
        tr("The mesh covers the primitive 2D reciprocal cell; folding to the "
           "first Brillouin zone happens at render time."),
        bzMapGroup_);
    mapNote->setWordWrap(true);
    mapNote->setToolTip(
        tr("Reduced by Wigner–Seitz folding when the surface is drawn, so no "
           "zone geometry is baked into the run."));
    mapForm->addRow(mapNote);

    connect(bzMapCheck_, &QCheckBox::toggled, this, [this](bool on) {
        bzMapSamplesSpin_->setEnabled(on);
        refreshPreview();
    });
    connect(bzMapSamplesSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    layout->addWidget(bzMapGroup_);
    layout->addStretch(1);

    refreshDimensionalityNote();
    refreshCostNote();
    updateCalculatorExtras(core::CalculatorKind::Gpaw);
    return page;
}

void TwoDBandsWizard::refreshDimensionalityNote()
{
    if (!dimensionalityNote_)
        return;
    const core::Structure* s = structure_.get();
    if (!s || !s->cell().isDefined()) {
        dimensionalityNote_->clear();
        return;
    }
    const auto pbc = s->cell().pbc();
    if (!pbc[0] || !pbc[1]) {
        dimensionalityNote_->setText(
            tr("<b style='color:#d9534f;'>Not periodic in both x and y</b> "
               "(pbc = %1, %2, %3) — no 2D Brillouin zone to sample.")
                .arg(pbc[0] ? tr("T") : tr("F"))
                .arg(pbc[1] ? tr("T") : tr("F"))
                .arg(pbc[2] ? tr("T") : tr("F")));
        return;
    }
    if (pbc[2]) {
        dimensionalityNote_->setText(
            tr("<b>Note:</b> periodic along z too, so k<sub>z</sub> = 0 is one "
               "cut through a 3D dispersion."));
        dimensionalityNote_->setToolTip(
            tr("That is a valid thing to plot, but it is not the band "
               "structure of a 2D material — add vacuum along z for that."));
        return;
    }
    dimensionalityNote_->clear();
    dimensionalityNote_->setToolTip(QString());
}

void TwoDBandsWizard::refreshCostNote()
{
    if (!costNote_ || !samplesSpin_)
        return;
    const int n = samplesSpin_->value();
    // Stated as a k-point count rather than a time: how long a k-point takes
    // depends on the system, but "this is 2601 of them" is the number that
    // makes the quadratic growth concrete before the job is queued.
    costNote_->setText(tr("%1 × %1 = %2 k-points evaluated at fixed density.")
                           .arg(n)
                           .arg(n * n));
}

void TwoDBandsWizard::updateCalculatorExtras(core::CalculatorKind kind)
{
    if (engineCombo_) {
        const QSignalBlocker blocker(engineCombo_);
        const int index = engineCombo_->findData(static_cast<int>(kind));
        if (index >= 0)
            engineCombo_->setCurrentIndex(index);
    }

    const bool selfContainedScf = selfContained(kind);
    const bool gpaw = kind == core::CalculatorKind::Gpaw;
    if (baselineGroup_)
        baselineGroup_->setVisible(!selfContainedScf);
    if (scfGroup_)
        scfGroup_->setVisible(selfContainedScf);

    // GPAW-only extras, per TwoDBandsConfig's own per-field doc comments:
    // spin-orbit and the Brillouin-zone map need machinery no other engine
    // here exposes, and SIESTA's finite atomic basis leaves no separate
    // band-count knob to wire either — see the "Bands diagonalized" row.
    if (spinOrbitCheck_)
        spinOrbitCheck_->setVisible(gpaw);
    if (bzMapGroup_)
        bzMapGroup_->setVisible(gpaw);
    if (gridForm_ && totalBandsSpin_)
        gridForm_->setRowVisible(totalBandsSpin_,
                                 kind != core::CalculatorKind::Siesta);

    refreshPreview();
}

bool TwoDBandsWizard::preflightSecondary()
{
    const core::CalculatorKind kind = selectedCalculator();
    if (!selfContained(kind))
        return true;
    const QString path = kind == core::CalculatorKind::QuantumEspresso
        ? espressoPseudoDirectory()
        : siestaPseudoDirectory();
    if (!path.isEmpty() && QFileInfo::exists(path))
        return true;
    QMessageBox::warning(
        this,
        kind == core::CalculatorKind::QuantumEspresso
            ? tr("Quantum ESPRESSO pseudopotentials")
            : tr("SIESTA pseudopotentials"),
        tr("No pseudopotential directory is configured (Preferences → "
           "External Files). Nothing was launched.\n\n"
           "Unlike VASP's POTCAR check, this only confirms the directory "
           "exists — it does not check that a pseudopotential for every "
           "element in the structure is actually in it. The generated "
           "script's own \"EDIT ME\" placeholders are the last line of "
           "defense; review them before running."));
    return false;
}

QString TwoDBandsWizard::baselineDensityPathToStage() const
{
    return baselineCombo_ ? baselineCombo_->currentData().toString()
                          : QString();
}

void TwoDBandsWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    baselineCombo_->clear();
    for (const auto& [label, path] : baselines)
        baselineCombo_->addItem(label, path);
    refreshPreview();
}

QString TwoDBandsWizard::generateScript() const
{
    core::TwoDBandsConfig cfg;
    const core::CalculatorKind kind = selectedCalculator();
    cfg.backend = backendFor(kind);
    cfg.gpaw = baseCalculatorConfig();
    cfg.baselineDensityPath =
        baselineCombo_ ? baselineCombo_->currentData().toString().toStdString()
                       : std::string();
    cfg.ecutEv = ecutSpin_ ? ecutSpin_->value() : 500.0;
    cfg.scfKpts = scfKptsSpin_ ? scfKptsSpin_->value() : 7;
    cfg.espressoPseudoDir = espressoPseudoDirectory().toStdString();
    cfg.siestaPseudoDir = siestaPseudoDirectory().toStdString();
    cfg.vaspPotcarPath = cfg.gpaw.vaspPotcarPath;
    cfg.gridSamples = samplesSpin_ ? samplesSpin_->value() : 24;
    cfg.bandsBelow = belowSpin_ ? belowSpin_->value() : 4;
    cfg.bandsAbove = aboveSpin_ ? aboveSpin_->value() : 4;
    cfg.totalBands = totalBandsSpin_ ? totalBandsSpin_->value() : 0;
    // GPAW-only, per TwoDBandsConfig's own doc — reading these regardless of
    // engine is harmless, since the generator itself ignores them for the
    // other three, but the checks mirror what the UI actually offers.
    cfg.spinOrbit = kind == core::CalculatorKind::Gpaw && spinOrbitCheck_
        && spinOrbitCheck_->isChecked();
    cfg.bzMap = kind == core::CalculatorKind::Gpaw && bzMapCheck_
        && bzMapCheck_->isChecked();
    cfg.bzMapSamples = bzMapSamplesSpin_ ? bzMapSamplesSpin_->value() : 24;
    return QString::fromStdString(core::generateTwoDBandsScript(cfg));
}

} // namespace calango::gui
