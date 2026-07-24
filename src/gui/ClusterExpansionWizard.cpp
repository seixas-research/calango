#include "gui/ClusterExpansionWizard.hpp"


#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <set>

namespace calango::gui {

ClusterExpansionWizard::ClusterExpansionWizard(
    std::vector<std::shared_ptr<core::Structure>> frames, QWidget* parent)
    : SimulationWizardBase(parent)
    , frames_(std::move(frames))
{
    buildUi();
}

QString ClusterExpansionWizard::wizardTitle() const
{
    return tr("Cluster Expansion Calculation");
}

QString ClusterExpansionWizard::settingsHeader() const
{
    return tr("Ensemble & Batch Optimization");
}

QString ClusterExpansionWizard::exportFileName() const
{
    return QStringLiteral("cluster_expansion_run.py");
}

QStringList ClusterExpansionWizard::ensembleSpecies() const
{
    std::set<QString> species;
    for (const auto& frame : frames_) {
        if (!frame)
            continue;
        for (const auto& atom : frame->atoms())
            species.insert(QLatin1String(atom.symbol()));
    }
    QStringList sorted;
    for (const QString& s : species)
        sorted << s;
    return sorted;
}

QWidget* ClusterExpansionWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // --- What is being run --------------------------------------------------
    summaryLabel_ = new QLabel(page);
    summaryLabel_->setWordWrap(true);
    const auto species = ensembleSpecies();
    if (frames_.size() < 2) {
        summaryLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
        summaryLabel_->setText(
            tr("This document holds a single structure. The batch will still "
               "run, but a convex hull needs several compositions — open the "
               "trajectory produced by Build → Cluster Expansion first."));
    } else {
        summaryLabel_->setText(
            tr("%1 configurations will be relaxed in sequence, one calculator "
               "instance per configuration. Species present: %2.")
                .arg(frames_.size())
                .arg(species.join(QStringLiteral(", "))));
    }
    layout->addWidget(summaryLabel_);

    // --- Per-configuration relaxation --------------------------------------
    auto* relaxGroup = new QGroupBox(tr("Per-configuration relaxation"), page);
    auto* relaxForm = new QFormLayout(relaxGroup);

    singlePointCheck_ =
        new QCheckBox(tr("Single-point only (no relaxation)"), relaxGroup);
    singlePointCheck_->setToolTip(
        tr("Evaluate each configuration at the builder's ideal-lattice "
           "geometry.\\nMuch cheaper, and the right first pass over a large "
           "ensemble — but formation energies then exclude relaxation energy, "
           "which can reorder near-degenerate configurations."));
    relaxForm->addRow(QString(), singlePointCheck_);

    optimizerCombo_ = new QComboBox(relaxGroup);
    // Order matches core::Optimizer.
    optimizerCombo_->addItems({QStringLiteral("BFGS"), QStringLiteral("LBFGS"),
                               QStringLiteral("FIRE"), QStringLiteral("GPMin"),
                               QStringLiteral("MDMin")});
    optimizerCombo_->setCurrentIndex(1); // LBFGS: cheapest per step in batch
    relaxForm->addRow(tr("Optimizer:"), optimizerCombo_);

    fmaxSpin_ = new QDoubleSpinBox(relaxGroup);
    fmaxSpin_->setRange(0.001, 1.0);
    fmaxSpin_->setDecimals(3);
    fmaxSpin_->setSingleStep(0.005);
    fmaxSpin_->setValue(0.05);
    fmaxSpin_->setSuffix(tr(" eV/Å"));
    relaxForm->addRow(tr("Force convergence:"), fmaxSpin_);

    maxStepsSpin_ = new QSpinBox(relaxGroup);
    maxStepsSpin_->setRange(1, 100000);
    maxStepsSpin_->setValue(200);
    maxStepsSpin_->setToolTip(
        tr("Cap per configuration. With N configurations the worst case is "
           "N × this many force evaluations, so keep it modest on DFT "
           "backends."));
    relaxForm->addRow(tr("Max steps (each):"), maxStepsSpin_);

    continueOnFailureCheck_ =
        new QCheckBox(tr("Continue when a configuration fails"), relaxGroup);
    continueOnFailureCheck_->setChecked(true);
    continueOnFailureCheck_->setToolTip(
        tr("A decoration that fails to converge (or crashes the calculator) is "
           "recorded as failed and the batch moves on. Off: the whole run "
           "stops at the first failure."));
    relaxForm->addRow(QString(), continueOnFailureCheck_);
    layout->addWidget(relaxGroup);

    connect(singlePointCheck_, &QCheckBox::toggled, this, [this](bool on) {
        for (QWidget* w : {static_cast<QWidget*>(optimizerCombo_),
                           static_cast<QWidget*>(fmaxSpin_),
                           static_cast<QWidget*>(maxStepsSpin_)}) {
            w->setEnabled(!on);
        }
        refreshPreview();
    });

    // --- Formation energy ---------------------------------------------------
    auto* hullGroup = new QGroupBox(tr("Formation energy && convex hull"), page);
    auto* hullForm = new QFormLayout(hullGroup);

    concentrationCombo_ = new QComboBox(hullGroup);
    concentrationCombo_->addItem(tr("Automatic"), QString());
    for (const QString& s : species)
        concentrationCombo_->addItem(s, s);
    concentrationCombo_->setToolTip(
        tr("Species whose site fraction x is the diagram's horizontal axis. "
           "Automatic picks the alphabetically-second species, so repeated "
           "runs of the same ensemble keep the same orientation."));
    hullForm->addRow(tr("Concentration axis:"), concentrationCombo_);

    endpointReferenceCheck_ =
        new QCheckBox(tr("Reference the ensemble's own endpoints"), hullGroup);
    endpointReferenceCheck_->setChecked(true);
    endpointReferenceCheck_->setToolTip(
        tr("On (recommended): the lowest-energy configurations at the extreme "
           "compositions define μ_A and μ_B, so E_form is exactly zero there "
           "and the calculator's absolute energy scale cancels.\\n"
           "Off: supply elemental reference energies computed elsewhere — "
           "necessary when the ensemble does not contain both pure "
           "endpoints."));
    hullForm->addRow(QString(), endpointReferenceCheck_);

    const auto referenceSpin = [hullGroup] {
        auto* spin = new QDoubleSpinBox(hullGroup);
        spin->setRange(-10000.0, 10000.0);
        spin->setDecimals(6);
        spin->setSuffix(QObject::tr(" eV/atom"));
        spin->setEnabled(false);
        return spin;
    };
    referenceASpin_ = referenceSpin();
    referenceBSpin_ = referenceSpin();
    hullForm->addRow(tr("μ at x = 0:"), referenceASpin_);
    hullForm->addRow(tr("μ at x = 1:"), referenceBSpin_);
    connect(endpointReferenceCheck_, &QCheckBox::toggled, this, [this](bool on) {
        referenceASpin_->setEnabled(!on);
        referenceBSpin_->setEnabled(!on);
        refreshPreview();
    });
    layout->addWidget(hullGroup);

    auto* hint = new QLabel(
        tr("On completion the Results panel gains a <b>Convex Hull</b> tab: "
           "E_form vs x, with configurations on the hull (stable) drawn "
           "filled and connected by tie-lines, and everything above it hollow "
           "and labelled by its energy above the hull."),
        page);
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::RichText);
    layout->addWidget(hint);

    // Any change to these alters the generated script.
    for (QComboBox* combo : {optimizerCombo_, concentrationCombo_})
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this] { refreshPreview(); });
    connect(fmaxSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(maxStepsSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    connect(continueOnFailureCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    for (QDoubleSpinBox* spin : {referenceASpin_, referenceBSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });

    layout->addStretch(1);
    return page;
}

core::ClusterExpansionRunConfig ClusterExpansionWizard::runConfig() const
{
    core::ClusterExpansionRunConfig config;
    config.calculator = baseCalculatorConfig();
    config.calculator.task = core::TaskKind::GeometryOptimization;
    config.calculator.optimizer =
        static_cast<core::Optimizer>(optimizerCombo_->currentIndex());
    config.calculator.fmax = fmaxSpin_->value();
    config.calculator.maxSteps = maxStepsSpin_->value();
    config.singlePointOnly = singlePointCheck_->isChecked();
    config.concentrationElement =
        concentrationCombo_->currentData().toString().toStdString();
    config.useEnsembleEndpoints = endpointReferenceCheck_->isChecked();
    config.referenceA = referenceASpin_->value();
    config.referenceB = referenceBSpin_->value();
    config.continueOnFailure = continueOnFailureCheck_->isChecked();
    return config;
}

QString ClusterExpansionWizard::generateScript() const
{
    return QString::fromStdString(
        core::ClusterExpansionScriptGenerator::generate(runConfig()));
}

} // namespace calango::gui
