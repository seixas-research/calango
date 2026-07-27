#include "gui/RandomNoiseWizard.hpp"

#include "gui/GuiUtils.hpp"

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
    : SimulationWizardBase(parent)
    , reference_(std::move(reference))
{
    buildUi();
    selectCalculator(core::CalculatorKind::Gpaw);
    electronic_.updateEnabled();
    updateGenerationState();
}

QString RandomNoiseWizard::wizardTitle() const
{
    return tr("Random Noise Setup");
}

QStringList RandomNoiseWizard::calculatorElements() const
{
    return structureElements(reference_.get());
}

QWidget* RandomNoiseWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* note = new QLabel(
        tr("Displace the structure at random, many times over, and evaluate "
           "every copy. What comes back is a <b>distribution</b> — the spread "
           "of energies tells you how steep the well around this geometry is, "
           "and whether the amplitude below is still small enough to be in it. "
           "The ensemble doubles as machine-learning training data, which is "
           "the other reason to build one.<br><br>"
           "Frame 0 is always the unperturbed structure, so the spread has a "
           "reference to be measured against."),
        page);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note);

    auto* noiseGroup = new QGroupBox(tr("Perturbation"), page);
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
           "check."));
    form->addRow(tr("Amplitude:"), amplitudeSpin_);

    seedSpin_ = new QSpinBox(noiseGroup);
    seedSpin_->setRange(0, 2147483647);
    seedSpin_->setValue(42);
    seedSpin_->setToolTip(
        tr("The same seed regenerates the same ensemble exactly, which is what "
           "makes a re-run comparable with the run before it."));
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
        tr("How many displaced copies to generate. Every one of them is a "
           "separate single-point calculation, so this multiplies the job's "
           "cost directly — 20 is enough to see a spread, 100+ to characterize "
           "one."));
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
    layout->addWidget(noiseGroup);

    // Generate, then Run. Two buttons because they are two decisions: the
    // ensemble can be looked at (it opens as a scrubbable trajectory) and
    // regenerated with a different seed or amplitude before any compute time is
    // committed to it.
    auto* actionRow = new QWidget(page);
    auto* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    generateButton_ = new QPushButton(tr("Generate structures"), actionRow);
    generateButton_->setToolTip(
        tr("Build the ensemble now and open it in the viewport as a scrubbable "
           "trajectory. Press again after changing anything above to rebuild "
           "it."));
    runButton_ = new QPushButton(tr("Run simulation"), actionRow);
    runButton_->setToolTip(
        tr("Move on to the calculator settings for the single-point run over "
           "the generated structures."));
    actionLayout->addWidget(generateButton_);
    actionLayout->addWidget(runButton_);
    actionLayout->addStretch(1);
    layout->addWidget(actionRow);

    generationStatus_ = new QLabel(page);
    generationStatus_->setWordWrap(true);
    layout->addWidget(generationStatus_);

    connect(generateButton_, &QPushButton::clicked, this,
            &RandomNoiseWizard::generateStructures);
    // "Run simulation" IS Next — it says what Next means here rather than
    // being a second way of doing it.
    connect(runButton_, &QPushButton::clicked, this,
            &RandomNoiseWizard::goNext);
    // Any change invalidates what was generated, so the status line says so
    // rather than letting a stale ensemble be run under new settings.
    connect(countSpin_, &QSpinBox::valueChanged, this,
            [this] { updateGenerationState(); });
    connect(seedSpin_, &QSpinBox::valueChanged, this,
            [this] { updateGenerationState(); });
    connect(amplitudeSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { updateGenerationState(); });
    connect(distributionCombo_, &QComboBox::currentIndexChanged, this,
            [this] { updateGenerationState(); });
    connect(accumulationCombo_, &QComboBox::currentIndexChanged, this,
            [this] { updateGenerationState(); });
    connect(positionsCheck_, &QCheckBox::toggled, this,
            [this] { updateGenerationState(); });
    connect(cellCheck_, &QCheckBox::toggled, this,
            [this] { updateGenerationState(); });

    layout->addStretch(1);
    return page;
}

QWidget* RandomNoiseWizard::buildSecondSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* note = new QLabel(
        tr("The run evaluates each structure once, at the geometry it was "
           "generated with — nothing is relaxed, which is the point: relaxing "
           "would walk every member back to the same minimum and destroy the "
           "distribution."),
        page);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* group = new QGroupBox(tr("Per-structure results"), page);
    auto* form = new QFormLayout(group);

    forcesCheck_ = new QCheckBox(tr("Record forces"), group);
    forcesCheck_->setChecked(true);
    forcesCheck_->setToolTip(
        tr("The same evaluation produces them, so this is nearly free — and it "
           "is what turns the ensemble from an energy histogram into usable ML "
           "training data."));
    form->addRow(QString(), forcesCheck_);

    stressCheck_ = new QCheckBox(tr("Record the stress tensor"), group);
    stressCheck_->setToolTip(
        tr("Only meaningful for a periodic cell, and not every calculator "
           "implements it — where it is missing the energy is still recorded."));
    stressCheck_->setEnabled(reference_ && reference_->cell().isDefined());
    form->addRow(QString(), stressCheck_);

    continueCheck_ =
        new QCheckBox(tr("Continue past a failed structure"), group);
    continueCheck_->setChecked(true);
    continueCheck_->setToolTip(
        tr("One SCF that will not converge — which a large displacement can "
           "easily produce — should not lose the other ninety-nine. Failed "
           "members are recorded as such and excluded from the statistics."));
    form->addRow(QString(), continueCheck_);
    layout->addWidget(group);

    for (QCheckBox* check : {forcesCheck_, stressCheck_, continueCheck_})
        connect(check, &QCheckBox::toggled, this,
                &RandomNoiseWizard::refreshPreview);

    layout->addStretch(1);
    return page;
}

void RandomNoiseWizard::goNext()
{
    if (frames_.empty())
        generateStructures();
    SimulationWizardBase::goNext();
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
    frames_.reserve(static_cast<std::size_t>(count) + 1);
    // Frame 0 is the untouched reference: the statistics are a spread AROUND
    // something, and without it the script has nothing to centre on.
    frames_.push_back(std::make_shared<core::Structure>(original));

    core::Structure walker = original;
    for (int k = 1; k <= count; ++k) {
        core::NoiseOptions member = options;
        // A distinct stream per member, derived from the one seed, so the whole
        // ensemble stays reproducible from the single number on the page.
        member.seed = options.seed + static_cast<unsigned int>(k);
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
    refreshPreview();
}

void RandomNoiseWizard::updateGenerationState()
{
    // "Run simulation" stays enabled whether or not anything has been
    // generated: goNext() builds the ensemble if it has to, so a disabled
    // button would be refusing to do something it is perfectly able to do.
    const bool ready = !frames_.empty();
    if (!generationStatus_)
        return;
    if (!ready) {
        generationStatus_->setText(
            tr("<i>Press <b>Generate structures</b> to build the ensemble. "
               "It opens in the viewport, so you can scrub through it before "
               "committing any compute time to it.</i>"));
        return;
    }
    generationStatus_->setText(
        tr("<b>%1 structures ready</b> (frame 0 is the unperturbed reference). "
           "Regenerate after changing anything above — the run uses the last "
           "ensemble generated, not the settings currently on screen.")
            .arg(frames_.size()));
}

core::RandomNoiseRunConfig RandomNoiseWizard::runConfig() const
{
    core::RandomNoiseRunConfig config;
    config.calculator = baseCalculatorConfig();
    config.calculator.task = core::TaskKind::SinglePoint;
    electronic_.applyTo(config.calculator);
    config.computeForces = !forcesCheck_ || forcesCheck_->isChecked();
    config.computeStress = stressCheck_ && stressCheck_->isEnabled()
        && stressCheck_->isChecked();
    config.continueOnFailure = !continueCheck_ || continueCheck_->isChecked();
    return config;
}

QString RandomNoiseWizard::generateScript() const
{
    return QString::fromStdString(
        core::RandomNoiseScriptGenerator::generate(runConfig()));
}

} // namespace calango::gui
