#include "gui/PiezoelectricWizard.hpp"

#include "gui/GuiUtils.hpp"

#include "core/StrainVoigt.hpp"
#include "core/Structure.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

namespace {
constexpr const char* kVoigtLabels[6] = {
    "eps_1 (xx)", "eps_2 (yy)", "eps_3 (zz)",
    "eps_4 (yz)", "eps_5 (xz)", "eps_6 (xy)",
};
} // namespace

PiezoelectricWizard::PiezoelectricWizard(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
{
    detectVacuumAxis();
    buildUi();
    selectCalculator(core::CalculatorKind::Gpaw);
    updateCostEstimate();
}

void PiezoelectricWizard::detectVacuumAxis()
{
    if (!structure_)
        return;
    // Primary signal: the geometric read every other 2D-aware wizard seeds
    // its vacuum-axis combo from — a large fractional-coordinate gap along
    // one axis. This is what catches monolayer 2H-MoS2's own case: built
    // with pbc=[True,True,True] (the ordinary ASE slab convention) and a
    // ~10 A vacuum gap along c, which no pbc flag alone reveals.
    vacuumAxis_ = guessVacuumAxis(structure_.get());
    if (vacuumAxis_ >= 0)
        return;
    // Fallback: an explicit pbc=False the geometric guess did not need
    // (e.g. a thin cell where the gap fraction fell under the guess's
    // threshold).
    const auto pbc = structure_->cell().pbc();
    for (int axis = 0; axis < 3; ++axis)
        if (!pbc[static_cast<std::size_t>(axis)]) {
            vacuumAxis_ = axis;
            return;
        }
}

QString PiezoelectricWizard::wizardTitle() const
{
    return tr("Piezoelectric Tensor Setup");
}

QString PiezoelectricWizard::settingsHeader() const
{
    return tr("Strain and Polarization Settings");
}

QStringList PiezoelectricWizard::calculatorElements() const
{
    return structureElements(structure_.get());
}

QWidget* PiezoelectricWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // -- Mandatory ground-state baseline ------------------------------------
    auto* baselineGroup = new QGroupBox(tr("Ground-State Baseline"), page);
    auto* baselineForm = new QFormLayout(baselineGroup);
    auto* baselineNote = new QLabel(
        tr("Strains are applied to a completed Single-Point run's relaxed "
           "geometry, reusing its calculator settings but <b>not</b> its "
           "density — every strained SCF re-converges."),
        baselineGroup);
    baselineNote->setWordWrap(true);
    baselineNote->setTextFormat(Qt::RichText);
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

    // -- Strain components ---------------------------------------------------
    auto* voigtGroup = new QGroupBox(tr("Strain Components"), page);
    auto* voigtLayout = new QVBoxLayout(voigtGroup);
    auto* voigtNote = new QLabel(
        tr("<b>e<sub>i,alpha</sub> = dP<sub>i</sub>/d(eps<sub>alpha</sub>)</b>, "
           "the Voigt strain components to differentiate. Leave every "
           "available box unchecked for all of them."),
        voigtGroup);
    voigtNote->setWordWrap(true);
    voigtNote->setTextFormat(Qt::RichText);
    voigtLayout->addWidget(voigtNote);

    dimensionalityNote_ = new QLabel(voigtGroup);
    dimensionalityNote_->setWordWrap(true);
    dimensionalityNote_->setTextFormat(Qt::RichText);
    static const char* const kAxisNames[3] = {"a", "b", "c"};
    if (vacuumAxis_ >= 0 && vacuumAxis_ <= 2) {
        dimensionalityNote_->setText(
            tr("<b style='color:#2a7a2a;'>2D structure detected</b> — "
               "vacuum along %1. Straining a cell dimension that is not a "
               "real lattice parameter is not a physical strain, so the "
               "out-of-plane component(s) below are disabled; e<sub>ij</sub> "
               "will additionally be reported per unit area (C/m), not just "
               "per volume (C/m&sup2;).")
                .arg(QString::fromLatin1(kAxisNames[vacuumAxis_])));
    } else {
        dimensionalityNote_->setVisible(false);
    }
    voigtLayout->addWidget(dimensionalityNote_);

    auto* voigtRow = new QHBoxLayout();
    for (int i = 0; i < 6; ++i) {
        voigtCheck_[i] = new QCheckBox(QString::fromLatin1(kVoigtLabels[i]), voigtGroup);
        // Voigt pair (j, k) for component i is core::kVoigtPairs[i]; a
        // component touching the detected vacuum axis is disabled rather
        // than merely unchecked, so it cannot be turned back on by mistake.
        if (vacuumAxis_ >= 0 && vacuumAxis_ <= 2
            && core::voigtInvolvesAxis(i, vacuumAxis_)) {
            voigtCheck_[i]->setEnabled(false);
            voigtCheck_[i]->setToolTip(
                tr("Disabled: this component strains the vacuum axis of the "
                   "detected 2D structure, which is not a physical "
                   "deformation."));
        }
        voigtRow->addWidget(voigtCheck_[i]);
        connect(voigtCheck_[i], &QCheckBox::toggled, this, [this] {
            updateCostEstimate();
            refreshPreview();
        });
    }
    voigtLayout->addLayout(voigtRow);
    layout->addWidget(voigtGroup);

    // -- Strain magnitude / points / ions -------------------------------------
    auto* settingsGroup = new QGroupBox(tr("Strain Stencil"), page);
    auto* form = new QFormLayout(settingsGroup);
    layout->addWidget(settingsGroup);

    strainSpin_ = new QDoubleSpinBox(settingsGroup);
    strainSpin_->setRange(0.001, 0.05);
    strainSpin_->setDecimals(4);
    strainSpin_->setSingleStep(0.001);
    strainSpin_->setValue(0.005);
    strainSpin_->setSuffix(tr(" (dimensionless)"));
    strainSpin_->setToolTip(
        tr("Magnitude of the smallest sample strain (0.005 = 0.5%%).\n\n"
           "Squeezed between two errors, exactly like the Born Charges "
           "displacement: too large leaves the linear regime the finite "
           "difference assumes, too small buries the polarization change in "
           "SCF/Berry-phase noise."));
    form->addRow(tr("Strain magnitude delta:"), strainSpin_);
    connect(strainSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    pointsCombo_ = new QComboBox(settingsGroup);
    pointsCombo_->addItem(tr("2 (+-delta, central difference)"), 2);
    pointsCombo_->addItem(tr("4 (+-delta, +-2*delta, least-squares fit)"), 4);
    pointsCombo_->setToolTip(
        tr("More points give a better-conditioned fit and let the linearity "
           "of P(eps) be checked directly in the results viewer, at roughly "
           "double the SCF cost per component."));
    form->addRow(tr("Points per component:"), pointsCombo_);
    connect(pointsCombo_, &QComboBox::currentIndexChanged, this, [this] {
        updateCostEstimate();
        refreshPreview();
    });

    relaxIonsCheck_ = new QCheckBox(
        tr("Relax internal coordinates at each strained cell (relaxed-ion)"),
        settingsGroup);
    relaxIonsCheck_->setToolTip(
        tr("Off (default): CLAMPED-ION — internal coordinates stay at their "
           "strain-scaled positions; only the SCF re-converges.\n\n"
           "On: RELAXED-ION — a geometry optimization of the positions runs "
           "at each fixed, strained cell before the polarization is read "
           "off. This is the physical, zero-stress piezoelectric response, "
           "at the cost of one relaxation per strain point on top of the "
           "SCF."));
    form->addRow(QString(), relaxIonsCheck_);
    connect(relaxIonsCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    symmetryCheck_ = new QCheckBox(
        tr("Use point-group symmetry (spglib)"), settingsGroup);
    symmetryCheck_->setChecked(true);
    symmetryCheck_->setToolTip(
        tr("Refuses outright, before running anything, if the reference "
           "structure is CENTROSYMMETRIC — piezoelectricity is forbidden by "
           "symmetry for every such point group, exactly, not "
           "approximately.\n\n"
           "Otherwise, symmetrizes the assembled tensor: this both zeroes "
           "whatever component the point group forbids and cleans up the "
           "numerical noise in the components it allows."));
    form->addRow(QString(), symmetryCheck_);
    connect(symmetryCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    costLabel_ = new QLabel(settingsGroup);
    costLabel_->setWordWrap(true);
    form->addRow(QString(), costLabel_);

    // -- Optional e -> d conversion --------------------------------------
    auto* elasticGroup = new QGroupBox(
        tr("Convert to d_ij (optional elastic stiffness)"), page);
    auto* elasticLayout = new QVBoxLayout(elasticGroup);
    elasticCheck_ = new QCheckBox(
        tr("Supply an elastic stiffness tensor C (Voigt, GPa)"), elasticGroup);
    elasticCheck_->setToolTip(
        tr("d_i,alpha = sum_beta e_i,beta * S_beta,alpha, with S = C^-1 — "
           "the piezoelectric STRAIN tensor (pm/V), the constant a device "
           "designer usually wants, alongside the STRESS tensor e_ij this "
           "wizard always computes.\n\n"
           "Leave unchecked to skip: nothing here can compute C from first "
           "principles, so it is the user's to supply (e.g. from a "
           "separate elastic-constants calculation)."));
    elasticLayout->addWidget(elasticCheck_);
    elasticTable_ = new QTableWidget(6, 6, elasticGroup);
    elasticTable_->setHorizontalHeaderLabels(
        {"1", "2", "3", "4", "5", "6"});
    elasticTable_->setVerticalHeaderLabels(
        {"1", "2", "3", "4", "5", "6"});
    elasticTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    elasticTable_->setMaximumHeight(190);
    disableTypeToEdit(elasticTable_);
    elasticTable_->setEnabled(false);
    elasticLayout->addWidget(elasticTable_);
    layout->addWidget(elasticGroup);
    connect(elasticCheck_, &QCheckBox::toggled, this, [this](bool on) {
        elasticTable_->setEnabled(on);
        refreshPreview();
    });
    connect(elasticTable_, &QTableWidget::itemChanged, this,
            [this] { refreshPreview(); });

    layout->addStretch(1);
    return page;
}

std::vector<int> PiezoelectricWizard::selectedVoigtComponents() const
{
    std::vector<int> out;
    for (int i = 0; i < 6; ++i)
        // The disabled state already keeps a 2D structure's out-of-plane
        // boxes unchecked, but isChecked() alone is what generatePiezo
        // electricScript() also filters defensively — testing isEnabled()
        // here too costs nothing and means this list can never disagree
        // with what the checkboxes visually show.
        if (voigtCheck_[i] && voigtCheck_[i]->isChecked()
            && voigtCheck_[i]->isEnabled())
            out.push_back(i);
    return out; // empty means "every available component" downstream
}

std::optional<std::array<std::array<double, 6>, 6>>
PiezoelectricWizard::elasticStiffness() const
{
    if (!elasticCheck_ || !elasticCheck_->isChecked() || !elasticTable_)
        return std::nullopt;
    std::array<std::array<double, 6>, 6> c{};
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            const auto* item = elasticTable_->item(i, j);
            bool ok = false;
            const double value = item ? item->text().toDouble(&ok) : 0.0;
            // A checked box with an incomplete table refuses rather than
            // silently treating a blank cell as zero GPa, which would
            // silently corrupt every d_ij it touches.
            if (!ok)
                return std::nullopt;
            c[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = value;
        }
    }
    return c;
}

void PiezoelectricWizard::updateCostEstimate()
{
    if (!costLabel_)
        return;
    const auto voigt = selectedVoigtComponents();
    const int available = (vacuumAxis_ >= 0 && vacuumAxis_ <= 2)
        ? static_cast<int>(core::inPlaneVoigtComponents(vacuumAxis_).size())
        : 6;
    const int count = voigt.empty() ? available : static_cast<int>(voigt.size());
    const int points = pointsCombo_ ? pointsCombo_->currentData().toInt() : 2;
    costLabel_->setText(
        tr("<b>%1 strain component(s)</b> -> %2 self-consistent run(s) "
           "(%3 per component), plus the reused baseline for eps = 0.")
            .arg(count)
            .arg(count * points)
            .arg(points));
}

void PiezoelectricWizard::setDensityBaselines(
    const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    baselineCombo_->clear();
    for (const auto& [label, path] : baselines)
        baselineCombo_->addItem(label, path);
    onBaselineChanged();
}

void PiezoelectricWizard::onBaselineChanged()
{
    inherited_ = applyBaselineProvenance(baselineCombo_, inheritanceNote_);
    updateCostEstimate();
    refreshPreview();
}

QString PiezoelectricWizard::pythonExecutable() const
{
    if (inherited_ && !inherited_->pythonExecutable.isEmpty())
        return inherited_->pythonExecutable;
    return SimulationWizardBase::pythonExecutable();
}

core::PiezoelectricConfig PiezoelectricWizard::config() const
{
    core::PiezoelectricConfig cfg;
    cfg.calculator = baseCalculatorConfig();
    cfg.calculator.task = core::TaskKind::SinglePoint;
    if (baselineCombo_)
        cfg.baselinePath = baselineCombo_->currentData().toString().toStdString();
    cfg.voigtComponents = selectedVoigtComponents();
    cfg.vacuumAxis = vacuumAxis_;
    cfg.strainMagnitude = strainSpin_ ? strainSpin_->value() : 0.005;
    cfg.pointsPerComponent = pointsCombo_ ? pointsCombo_->currentData().toInt() : 2;
    cfg.relaxIons = relaxIonsCheck_ && relaxIonsCheck_->isChecked();
    cfg.useSymmetry = !symmetryCheck_ || symmetryCheck_->isChecked();
    cfg.elasticStiffnessGpa = elasticStiffness();
    return cfg;
}

QString PiezoelectricWizard::generateScript() const
{
    return QString::fromStdString(core::generatePiezoelectricScript(config()));
}

} // namespace calango::gui
