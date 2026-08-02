#include "gui/TwoDBandsWizard.hpp"

#include "core/Structure.hpp"
#include "core/TwoDBandsScriptGenerator.hpp"
#include "gui/GuiUtils.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace calango::gui {

TwoDBandsWizard::TwoDBandsWizard(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : SimulationWizardBase(parent), structure_(std::move(structure))
{
    buildUi();
    selectCalculator(core::CalculatorKind::Gpaw);
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
           "zone</b> at k<sub>z</sub> = 0 and plots each band as a surface "
           "E<sub>n</sub>(k<sub>x</sub>, k<sub>y</sub>). The run is "
           "non-self-consistent on top of the baseline density below, so the "
           "cutoff, functional and mode come from that calculation."),
        page);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    dimensionalityNote_ = new QLabel(page);
    dimensionalityNote_->setWordWrap(true);
    dimensionalityNote_->setTextFormat(Qt::RichText);
    layout->addWidget(dimensionalityNote_);

    auto* group = new QGroupBox(tr("Baseline && Sampling"), page);
    auto* form = new QFormLayout(group);

    baselineCombo_ = new QComboBox(group);
    baselineCombo_->setToolTip(
        tr("The completed Single-Point Calculation whose converged charge "
           "density this run restarts from. The eigenvalues are evaluated at "
           "fixed density (NSCF)."));
    form->addRow(tr("Baseline SCF density:"), baselineCombo_);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { refreshPreview(); });

    samplesSpin_ = new QSpinBox(group);
    samplesSpin_->setRange(3, 512);
    samplesSpin_->setValue(24);
    samplesSpin_->setToolTip(
        tr("Samples along each reciprocal-lattice direction; the grid is N×N "
           "and the cost is quadratic in N.\n\n"
           "This is the setting that decides whether a Dirac cone reads as a "
           "cone or as a staircase. ~24 is a good overview; resolving a band "
           "touching or a small avoided crossing wants 48 or more."));
    form->addRow(tr("Grid samples (N × N):"), samplesSpin_);
    connect(samplesSpin_, &QSpinBox::valueChanged, this, [this] {
        refreshCostNote();
        refreshPreview();
    });

    costNote_ = new QLabel(group);
    costNote_->setWordWrap(true);
    form->addRow(QString(), costNote_);

    belowSpin_ = new QSpinBox(group);
    belowSpin_->setRange(0, 200);
    belowSpin_->setValue(4);
    form->addRow(tr("Surfaces below E<sub>F</sub>:"), belowSpin_);

    aboveSpin_ = new QSpinBox(group);
    aboveSpin_->setRange(0, 200);
    aboveSpin_->setValue(4);
    aboveSpin_->setToolTip(
        tr("How many bands either side of the Fermi level become surfaces.\n\n"
           "Bands that CROSS the Fermi level are always included whatever "
           "these say — those are the Fermi surface, and dropping one because "
           "a count ran out would remove the feature the plot is for."));
    form->addRow(tr("Surfaces above E<sub>F</sub>:"), aboveSpin_);

    totalBandsSpin_ = new QSpinBox(group);
    totalBandsSpin_->setRange(0, 100000);
    totalBandsSpin_->setValue(0);
    totalBandsSpin_->setSpecialValueText(tr("inherit from the baseline"));
    totalBandsSpin_->setToolTip(
        tr("Bands the fixed-density run diagonalizes. Leave inherited unless "
           "the conduction bands you want are not in the baseline's set — "
           "unconverged empty states are the usual reason an upper surface "
           "looks noisy."));
    form->addRow(tr("Bands diagonalized:"), totalBandsSpin_);

    spinOrbitCheck_ = new QCheckBox(tr("Include spin-orbit coupling"), group);
    spinOrbitCheck_->setToolTip(
        tr("Re-diagonalize the converged states in the spinor basis "
           "(gpaw.spinorbit).\n\n"
           "Often the reason to draw a 2D surface at all: Rashba splitting and "
           "the gap opened at a band touching are simply absent from a scalar "
           "relativistic calculation, and both are features OF the surface "
           "rather than of any one cut through it."));
    form->addRow(QString(), spinOrbitCheck_);

    for (QSpinBox* spin : {belowSpin_, aboveSpin_, totalBandsSpin_})
        connect(spin, &QSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    connect(spinOrbitCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    layout->addWidget(group);

    // ------------------------------------------------------ Brillouin-zone map
    auto* mapGroup = new QGroupBox(tr("Brillouin-zone map"), page);
    auto* mapForm = new QFormLayout(mapGroup);

    bzMapCheck_ =
        new QCheckBox(tr("Also sample the full first Brillouin zone"), mapGroup);
    bzMapCheck_->setToolTip(
        tr("Adds a second, independent sampling for the flat E(k_x, k_y) map "
           "view in the results window: every band evaluated on an N×N "
           "Monkhorst-Pack mesh spanning the primitive 2D reciprocal cell.\n\n"
           "Off by default because it is pure extra cost when only the 3D "
           "surfaces are wanted — and a run without it keeps exactly the "
           "output it always had."));
    mapForm->addRow(bzMapCheck_);

    bzMapSamplesSpin_ = new QSpinBox(mapGroup);
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
        tr("The mesh covers the primitive 2D reciprocal cell; it is reduced "
           "to the first Brillouin zone by Wigner–Seitz folding at render "
           "time, so no zone geometry is baked into the run."),
        mapGroup);
    mapNote->setWordWrap(true);
    mapForm->addRow(mapNote);

    connect(bzMapCheck_, &QCheckBox::toggled, this, [this](bool on) {
        bzMapSamplesSpin_->setEnabled(on);
        refreshPreview();
    });
    connect(bzMapSamplesSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    layout->addWidget(mapGroup);
    layout->addStretch(1);

    refreshDimensionalityNote();
    refreshCostNote();
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
            tr("<b style='color:#d9534f;'>This structure is not periodic in "
               "both x and y</b> (pbc = %1, %2, %3), so it has no "
               "two-dimensional Brillouin zone to sample. Set the periodicity "
               "in <b>Edit Structure…</b> first.")
                .arg(pbc[0] ? tr("T") : tr("F"))
                .arg(pbc[1] ? tr("T") : tr("F"))
                .arg(pbc[2] ? tr("T") : tr("F")));
        return;
    }
    if (pbc[2]) {
        dimensionalityNote_->setText(
            tr("<b>Note:</b> this structure is periodic along z as well, so "
               "the k<sub>z</sub> = 0 plane is one cut through a 3D dispersion "
               "rather than a complete Brillouin zone. That is a valid thing "
               "to plot, but it is not the band structure of a 2D material — "
               "add vacuum along z for that."));
        return;
    }
    dimensionalityNote_->clear();
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
    cfg.gpaw = baseCalculatorConfig();
    cfg.baselineDensityPath =
        baselineCombo_ ? baselineCombo_->currentData().toString().toStdString()
                       : std::string();
    cfg.gridSamples = samplesSpin_ ? samplesSpin_->value() : 24;
    cfg.bandsBelow = belowSpin_ ? belowSpin_->value() : 4;
    cfg.bandsAbove = aboveSpin_ ? aboveSpin_->value() : 4;
    cfg.totalBands = totalBandsSpin_ ? totalBandsSpin_->value() : 0;
    cfg.spinOrbit = spinOrbitCheck_ && spinOrbitCheck_->isChecked();
    cfg.bzMap = bzMapCheck_ && bzMapCheck_->isChecked();
    cfg.bzMapSamples = bzMapSamplesSpin_ ? bzMapSamplesSpin_->value() : 24;
    return QString::fromStdString(core::generateTwoDBandsScript(cfg));
}

} // namespace calango::gui
