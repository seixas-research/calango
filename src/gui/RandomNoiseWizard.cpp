#include "gui/RandomNoiseWizard.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace calango::gui {

RandomNoiseWizard::RandomNoiseWizard(
    std::shared_ptr<const core::Structure> reference, QWidget* parent)
    : QDialog(parent)
    , reference_(std::move(reference))
{
    setWindowTitle(tr("Random Noise Setup"));
    auto* layout = new QVBoxLayout(this);

    auto* note = new QLabel(
        tr("Displace the structure at random, many times over, into a "
           "<b>trajectory</b>. Evaluate it (energies, forces, …) downstream — "
           "Orchestration's <b>Structure Container</b> node loads the saved "
           "trajectory and fans a Single-point Calculation out once per "
           "structure."),
        this);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    auto* noiseGroup = new QGroupBox(tr("Perturbation"), this);
    auto* form = new QFormLayout(noiseGroup);

    distributionCombo_ = new QComboBox(noiseGroup);
    // Order matches core::NoiseOptions::Distribution.
    distributionCombo_->addItems({tr("Gaussian (normal)"), tr("Uniform")});
    distributionCombo_->setToolTip(
        tr("Gaussian is the physical choice — it is what a harmonic mode at a "
           "finite temperature actually produces. Uniform gives every "
           "displacement inside the amplitude equal weight, which samples the "
           "shell more evenly and is the better generator for training data."));
    form->addRow(tr("Distribution:"), distributionCombo_);

    amplitudeSpin_ = new QDoubleSpinBox(noiseGroup);
    amplitudeSpin_->setRange(0.001, 5.0);
    amplitudeSpin_->setDecimals(3);
    amplitudeSpin_->setSingleStep(0.01);
    amplitudeSpin_->setValue(0.05);
    amplitudeSpin_->setSuffix(tr(" Å"));
    amplitudeSpin_->setToolTip(
        tr("Gaussian: σ per Cartesian component. Uniform: half-width of the "
           "interval.\n\n"
           "0.02–0.05 Å probes the harmonic region around a relaxed geometry; "
           "0.1 Å and above starts sampling anharmonicity, which is what you "
           "want for training data and not what you want for a curvature "
           "check.\n\n"
           "With the linear ramp on, this is the amplitude the LAST frame "
           "reaches — earlier frames get a fraction of it."));
    form->addRow(tr("Amplitude:"), amplitudeSpin_);

    seedSpin_ = new QSpinBox(noiseGroup);
    seedSpin_->setRange(0, 2147483647);
    seedSpin_->setValue(42);
    seedSpin_->setToolTip(
        tr("The same seed regenerates the same ensemble exactly — ramp on or "
           "off — which is what makes a re-run comparable with the run "
           "before it."));
    form->addRow(tr("Random seed:"), seedSpin_);

    positionsCheck_ = new QCheckBox(tr("Perturb atomic positions"), noiseGroup);
    positionsCheck_->setChecked(true);
    form->addRow(QString(), positionsCheck_);

    cellCheck_ = new QCheckBox(
        tr("Perturb unit cell vectors (random strain)"), noiseGroup);
    const bool hasCell = reference_ && reference_->cell().isDefined();
    cellCheck_->setEnabled(hasCell);
    cellCheck_->setToolTip(
        hasCell ? tr("Atoms follow the cell affinely (fractional coordinates "
                     "preserved), so this is a random strain rather than a "
                     "tearing of the structure.")
                : tr("This structure has no unit cell, so there are no cell "
                     "vectors to strain."));
    form->addRow(QString(), cellCheck_);

    countSpin_ = new QSpinBox(noiseGroup);
    countSpin_->setRange(1, 1000);
    countSpin_->setValue(20);
    countSpin_->setToolTip(
        tr("How many displaced copies to generate, on top of the always-"
           "included unperturbed reference frame — 20 is enough to see a "
           "spread, 100+ to characterize one."));
    form->addRow(tr("Structures:"), countSpin_);

    accumulationCombo_ = new QComboBox(noiseGroup);
    accumulationCombo_->addItem(
        tr("Independent (each structure from the original)"));
    accumulationCombo_->addItem(
        tr("Cumulative (random walk from the previous structure)"));
    accumulationCombo_->setToolTip(
        tr("Independent draws are uncorrelated, which is what a distribution "
           "wants. A cumulative walk drifts steadily away from the reference "
           "and is for tracing a path outwards, not for sampling around a "
           "point."));
    form->addRow(tr("Accumulation:"), accumulationCombo_);

    rampCombo_ = new QComboBox(noiseGroup);
    rampCombo_->addItem(tr("Constant amplitude"));
    rampCombo_->addItem(tr("Linear ramp (0 → max)"));
    rampCombo_->setToolTip(
        tr("Constant: every displaced frame uses the full amplitude above — "
           "today's behaviour.\n\n"
           "Linear ramp: frame 0 (the reference) is already zero noise; the "
           "displaced frames scale linearly up to the full amplitude at the "
           "LAST frame. With only one displaced structure requested, that one "
           "frame IS the last frame and gets the full amplitude — the "
           "smallest possible ramp is exactly zero, then full.\n\n"
           "Each frame still draws its own independent random displacements "
           "— only the amplitude they are drawn at is interpolated, so the "
           "same seed still regenerates the same trajectory exactly."));
    form->addRow(tr("Amplitude schedule:"), rampCombo_);
    layout->addWidget(noiseGroup);

    // Generate, then Close. The ensemble can be looked at (it opens as a
    // scrubbable trajectory) and regenerated with a different seed or
    // amplitude before it is saved anywhere.
    auto* actionRow = new QWidget(this);
    auto* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    generateButton_ = new QPushButton(tr("Generate structures"), actionRow);
    generateButton_->setToolTip(
        tr("Build the ensemble now and open it in the viewport as a scrubbable "
           "trajectory. Press again after changing anything above to rebuild "
           "it.\n\n"
           "Save it from there (File → Save Trajectory As…) to evaluate it "
           "through Orchestration's Structure Container node."));
    actionLayout->addWidget(generateButton_);
    actionLayout->addStretch(1);
    auto* close = new QPushButton(tr("Close"), actionRow);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    actionLayout->addWidget(close);
    layout->addWidget(actionRow);

    generationStatus_ = new QLabel(this);
    generationStatus_->setWordWrap(true);
    layout->addWidget(generationStatus_);

    connect(generateButton_, &QPushButton::clicked, this,
            &RandomNoiseWizard::generateStructures);
    // Any change invalidates what was generated, so the status line says so
    // rather than letting a stale ensemble be mistaken for the current
    // settings.
    for (QSpinBox* spin : {countSpin_, seedSpin_})
        connect(spin, &QSpinBox::valueChanged, this,
                [this] { updateGenerationState(); });
    connect(amplitudeSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { updateGenerationState(); });
    for (QComboBox* combo : {distributionCombo_, accumulationCombo_, rampCombo_})
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this] { updateGenerationState(); });
    for (QCheckBox* check : {positionsCheck_, cellCheck_})
        connect(check, &QCheckBox::toggled, this,
                [this] { updateGenerationState(); });

    layout->addStretch(1);
    updateGenerationState();
    resize(480, 560);
}

core::NoiseOptions RandomNoiseWizard::noiseOptions() const
{
    core::NoiseOptions options;
    options.distribution = distributionCombo_->currentIndex() == 0
        ? core::NoiseOptions::Distribution::Gaussian
        : core::NoiseOptions::Distribution::Uniform;
    options.amplitude = amplitudeSpin_->value();
    options.seed = static_cast<unsigned int>(seedSpin_->value());
    options.perturbPositions = positionsCheck_->isChecked();
    options.perturbCell = cellCheck_->isChecked() && cellCheck_->isEnabled();
    return options;
}

void RandomNoiseWizard::generateStructures()
{
    frames_.clear();
    if (!reference_ || reference_->empty()) {
        generationStatus_->setText(tr("<b>No structure to perturb.</b>"));
        updateGenerationState();
        return;
    }
    const core::NoiseOptions options = noiseOptions();
    if (!options.perturbPositions && !options.perturbCell) {
        generationStatus_->setText(
            tr("<b>Nothing selected to perturb.</b> Tick positions, the cell, "
               "or both."));
        updateGenerationState();
        return;
    }

    const core::Structure original = *reference_;
    const int count = countSpin_->value();
    const bool cumulative = accumulationCombo_->currentIndex() == 1;
    const bool ramped = rampCombo_->currentIndex() == 1;
    frames_.reserve(static_cast<std::size_t>(count) + 1);
    // Frame 0 is the untouched reference: the statistics are a spread AROUND
    // something, and without it the script has nothing to centre on. It is
    // also, unmodified, exactly the ramp's zero-noise endpoint.
    frames_.push_back(std::make_shared<core::Structure>(original));

    core::Structure walker = original;
    for (int k = 1; k <= count; ++k) {
        core::NoiseOptions member = options;
        // A distinct stream per member, derived from the one seed, so the whole
        // ensemble stays reproducible from the single number on the page —
        // ramp on or off: the ramp only rescales the amplitude passed into
        // this call, it never touches the seed or the number of draws.
        member.seed = options.seed + static_cast<unsigned int>(k);
        if (ramped)
            member.amplitude *= core::rampAmplitudeFactor(k, count);
        if (cumulative) {
            core::applyRandomNoise(walker, member);
            frames_.push_back(std::make_shared<core::Structure>(walker));
        } else {
            core::Structure fresh = original;
            core::applyRandomNoise(fresh, member);
            frames_.push_back(std::make_shared<core::Structure>(std::move(fresh)));
        }
    }

    Q_EMIT structuresGenerated(frames_);
    updateGenerationState();
}

void RandomNoiseWizard::updateGenerationState()
{
    if (!generationStatus_)
        return;
    if (frames_.empty()) {
        generationStatus_->setText(
            tr("<i>Press <b>Generate structures</b> to build the "
               "trajectory.</i>"));
        generationStatus_->setToolTip(
            tr("It opens in the viewport, so you can scrub through it before "
               "saving it anywhere."));
        return;
    }
    const bool ramped = rampCombo_->currentIndex() == 1;
    generationStatus_->setText(
        ramped ? tr("<b>%1 structures ready</b> (frame 0 is the unperturbed "
                    "reference; amplitude ramps linearly to the full value "
                    "at the last frame).")
                     .arg(frames_.size())
               : tr("<b>%1 structures ready</b> (frame 0 is the unperturbed "
                    "reference).")
                     .arg(frames_.size()));
    generationStatus_->setToolTip(
        tr("Regenerate after changing anything above: the trajectory shown "
           "is the last one generated, not the settings currently on "
           "screen."));
}

} // namespace calango::gui
