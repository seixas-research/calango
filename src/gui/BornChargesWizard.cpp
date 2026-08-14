#include "gui/BornChargesWizard.hpp"

#include "gui/GuiUtils.hpp"

#include "core/Structure.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QWidget>


namespace calango::gui {

BornChargesWizard::BornChargesWizard(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
{
    buildUi();
    selectCalculator(core::CalculatorKind::Gpaw);
    updateCostEstimate();
}

QString BornChargesWizard::wizardTitle() const
{
    return tr("Born Effective Charges Setup");
}

QString BornChargesWizard::settingsHeader() const
{
    return tr("Born Charge Settings");
}

QStringList BornChargesWizard::calculatorElements() const
{
    return structureElements(structure_.get());
}

QWidget* BornChargesWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // -- Mandatory ground-state baseline ------------------------------------
    // Same entry point as Electronic Structure and Optics: the run starts from
    // a completed Single-Point Calculation rather than re-specifying the
    // calculator. What it inherits is different, though, and the note says so
    // — Z* cannot be evaluated at a fixed density.
    auto* baselineGroup = new QGroupBox(tr("Ground-State Baseline"), page);
    auto* baselineForm = new QFormLayout(baselineGroup);
    auto* baselineNote = new QLabel(
        tr("Displacements are taken about a completed Single-Point run's "
           "geometry, reusing its calculator settings but <b>not</b> its "
           "density."),
        baselineGroup);
    baselineNote->setWordWrap(true);
    baselineNote->setTextFormat(Qt::RichText);
    baselineNote->setToolTip(
        tr("Every displaced run is rebuilt from the baseline's calculator, so "
           "all of them use settings you already validated.\n\n"
           "Unlike Optics or Electronic Structure this cannot reuse the "
           "baseline's density: Z* IS the response of the charge distribution "
           "to a displacement, so each geometry re-converges its own SCF. The "
           "baseline fixes what is computed, not how much work it is."));
    baselineForm->addRow(baselineNote);
    baselineCombo_ = new QComboBox(baselineGroup);
    baselineForm->addRow(tr("Baseline SCF (.gpw):"), baselineCombo_);
    inheritanceNote_ = new QLabel(baselineGroup);
    inheritanceNote_->setWordWrap(true);
    inheritanceNote_->setTextFormat(Qt::RichText);
    baselineForm->addRow(inheritanceNote_);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { onBaselineChanged(); });
    layout->addWidget(baselineGroup);

    auto* settingsGroup = new QGroupBox(tr("Displacements"), page);
    auto* form = new QFormLayout(settingsGroup);
    layout->addWidget(settingsGroup);
    layout->addStretch(1);

    auto* note = new QLabel(
        tr("<b>Z*<sub>k,αβ</sub> = (Ω/e) ∂P<sub>α</sub>/∂u<sub>kβ</sub></b>, "
           "the charge coupling a displacement to the polarization. Needs a "
           "<b>periodic insulator</b>."),
        settingsGroup);
    note->setWordWrap(true);
    note->setToolTip(
        tr("Neither the formal ionic charge nor a scalar: a 3×3 tensor per "
           "atom, which in ferroelectrics routinely exceeds the nominal "
           "valence twofold.\n\n"
           "It sets the LO-TO splitting and the infrared intensities — a "
           "Γ-point phonon spectrum of a polar insulator computed without it "
           "is wrong by construction.\n\n"
           "Computed by central finite differences of the Berry-phase "
           "polarization, which is why a metal is refused: its polarization is "
           "not defined."));
    note->setTextFormat(Qt::RichText);
    form->addRow(note);

    displacementSpin_ = new QDoubleSpinBox(settingsGroup);
    displacementSpin_->setRange(0.001, 0.20);
    displacementSpin_->setDecimals(3);
    displacementSpin_->setSingleStep(0.005);
    displacementSpin_->setValue(0.01);
    displacementSpin_->setSuffix(tr(" Å"));
    displacementSpin_->setToolTip(
        tr("Amplitude of the ± displacement in the central difference.\n\n"
           "Squeezed between two errors: too large leaves the linear regime "
           "the derivative is defined in, too small buries the polarization "
           "difference in SCF noise. 0.01 Å is the usual compromise — tighten "
           "the SCF convergence before reducing it."));
    form->addRow(tr("Displacement δ:"), displacementSpin_);
    connect(displacementSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    atomsEdit_ = new QLineEdit(settingsGroup);
    atomsEdit_->setPlaceholderText(tr("all atoms — e.g. \"0, 2, 5-8\""));
    atomsEdit_->setToolTip(
        tr("0-based atom indices to displace; blank does every atom.\n\n"
           "Each atom costs six SCF runs, so restricting this to the "
           "symmetry-inequivalent sites is usually the difference between an "
           "overnight job and a tractable one — the rest follow by symmetry."));
    form->addRow(tr("Atoms:"), atomsEdit_);
    connect(atomsEdit_, &QLineEdit::textChanged, this, [this] {
        updateCostEstimate();
        refreshPreview();
    });

    sumRuleCheck_ = new QCheckBox(tr("Impose the acoustic sum rule"),
                                 settingsGroup);
    sumRuleCheck_->setChecked(true);
    sumRuleCheck_->setToolTip(
        tr("Subtract the mean residual so Σ_k Z*_k = 0.\n\n"
           "The sum rule is exact — translating the whole crystal cannot "
           "polarize it — so whatever the sum comes out to measures this "
           "calculation's own convergence error. The uncorrected tensors are "
           "reported alongside the corrected ones, so the size of the "
           "correction stays visible rather than being quietly absorbed."));
    form->addRow(QString(), sumRuleCheck_);
    connect(sumRuleCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    costLabel_ = new QLabel(settingsGroup);
    costLabel_->setWordWrap(true);
    form->addRow(QString(), costLabel_);
    return page;
}

std::vector<int> BornChargesWizard::selectedAtoms() const
{
    if (!atomsEdit_)
        return {};
    // Empty == every atom; shared parser, same selection syntax as the
    // fatband atom fields.
    return parseAtomIndexList(
        atomsEdit_->text(),
        structure_ ? static_cast<int>(structure_->size()) : 0);
}

void BornChargesWizard::updateCostEstimate()
{
    if (!costLabel_)
        return;
    const auto indices = selectedAtoms();
    const int total = structure_ ? static_cast<int>(structure_->size()) : 0;
    const int count = indices.empty() ? total : static_cast<int>(indices.size());
    if (count <= 0) {
        costLabel_->setText(tr("<i>No atoms selected.</i>"));
        return;
    }
    // State the real cost up front. Six self-consistent runs per atom is the
    // kind of number that decides whether this job is worth starting.
    costLabel_->setText(
        tr("<b>%n atom(s)</b> → %1 self-consistent runs (6 per atom: ± along "
           "x, y, z).", nullptr, count)
            .arg(6 * count));
}

void BornChargesWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    baselineCombo_->clear();
    for (const auto& [label, path] : baselines)
        baselineCombo_->addItem(label, path);
    onBaselineChanged();
}

void BornChargesWizard::onBaselineChanged()
{
    inherited_ = applyBaselineProvenance(baselineCombo_, inheritanceNote_);
    updateCostEstimate();
    refreshPreview();
}

QString BornChargesWizard::pythonExecutable() const
{
    if (inherited_ && !inherited_->pythonExecutable.isEmpty())
        return inherited_->pythonExecutable;
    return SimulationWizardBase::pythonExecutable();
}

core::BornChargesConfig BornChargesWizard::config() const
{
    core::BornChargesConfig cfg;
    cfg.calculator = baseCalculatorConfig();
    cfg.calculator.task = core::TaskKind::SinglePoint;
    if (baselineCombo_)
        cfg.baselinePath =
            baselineCombo_->currentData().toString().toStdString();
    cfg.displacement = displacementSpin_->value();
    cfg.atomIndices = selectedAtoms();
    cfg.acousticSumRule = sumRuleCheck_->isChecked();
    return cfg;
}

QString BornChargesWizard::generateScript() const
{
    return QString::fromStdString(core::generateBornChargesScript(config()));
}

} // namespace calango::gui
