#include "gui/ElasticWizard.hpp"

#include "gui/GuiUtils.hpp"

#include "core/StrainVoigt.hpp"
#include "core/Structure.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

namespace {
constexpr const char* kVoigtLabels[6] = {
    "eps_1 (xx)", "eps_2 (yy)", "eps_3 (zz)",
    "eps_4 (yz)", "eps_5 (xz)", "eps_6 (xy)",
};
} // namespace

ElasticWizard::ElasticWizard(std::shared_ptr<const core::Structure> structure,
                             QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
{
    detectVacuumAxis();
    buildUi();
    updateCostEstimate();
}

void ElasticWizard::detectVacuumAxis()
{
    if (!structure_)
        return;
    vacuumAxis_ = guessVacuumAxis(structure_.get());
    if (vacuumAxis_ >= 0)
        return;
    const auto pbc = structure_->cell().pbc();
    for (int axis = 0; axis < 3; ++axis)
        if (!pbc[static_cast<std::size_t>(axis)]) {
            vacuumAxis_ = axis;
            return;
        }
}

QString ElasticWizard::wizardTitle() const
{
    return tr("Elastic Properties Setup");
}

QString ElasticWizard::settingsHeader() const
{
    return tr("Strain and Fitting Settings");
}

QStringList ElasticWizard::calculatorElements() const
{
    return structureElements(structure_.get());
}

QWidget* ElasticWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // -- Optional ground-state baseline --------------------------------------
    auto* baselineGroup = new QGroupBox(tr("Ground-State Baseline (optional)"), page);
    auto* baselineForm = new QFormLayout(baselineGroup);
    auto* baselineNote = new QLabel(
        tr("Unlike the Piezoelectric Tensor wizard, a baseline is "
           "<b>optional</b> here: stress and energy are available from any "
           "engine, not just GPAW's Berry phase. Pick \"(none)\" to strain "
           "the current structure with the Calculator Settings below "
           "instead — with a baseline selected, its own calculator is what "
           "actually runs, and the Calculator Settings page is ignored."),
        baselineGroup);
    baselineNote->setWordWrap(true);
    baselineNote->setTextFormat(Qt::RichText);
    baselineForm->addRow(baselineNote);
    baselineCombo_ = new QComboBox(baselineGroup);
    baselineCombo_->addItem(tr("(none) — use the current structure"), QString());
    baselineForm->addRow(tr("Baseline SCF (.gpw):"), baselineCombo_);
    inheritanceNote_ = new QLabel(baselineGroup);
    inheritanceNote_->setWordWrap(true);
    inheritanceNote_->setTextFormat(Qt::RichText);
    baselineForm->addRow(inheritanceNote_);
    connect(baselineCombo_, &QComboBox::currentIndexChanged, this,
            [this] { onBaselineChanged(); });
    layout->addWidget(baselineGroup);

    // -- Method ---------------------------------------------------------------
    auto* methodGroup = new QGroupBox(tr("Fitting Method"), page);
    auto* methodForm = new QFormLayout(methodGroup);
    methodCombo_ = new QComboBox(methodGroup);
    methodCombo_->addItem(tr("Auto (stress if the engine has it, else energy)"),
        static_cast<int>(core::ElasticMethod::Auto));
    methodCombo_->addItem(tr("Stress-strain (primary — needs get_stress())"),
        static_cast<int>(core::ElasticMethod::StressStrain));
    methodCombo_->addItem(tr("Energy-strain (fallback — any engine)"),
        static_cast<int>(core::ElasticMethod::EnergyStrain));
    methodCombo_->setToolTip(
        tr("Stress-strain reads sigma_i = C_ij * eps_j directly off the "
           "stress tensor: a single-component strain sweep already fills a "
           "whole matrix COLUMN, and one strain magnitude suffices.\n\n"
           "Energy-strain fits C_jj = d^2E/deps_j^2 / V0 from the energy "
           "curvature instead, for an engine with no stress — the "
           "off-diagonal C_jk (j != k) additionally needs a combined "
           "two-component strain stencil, so this costs more."));
    methodForm->addRow(tr("Method:"), methodCombo_);
    connect(methodCombo_, &QComboBox::currentIndexChanged, this, [this] {
        // Energy-strain fits a PARABOLA; nudge the point count up so the fit
        // is not left underdetermined by the stress-strain default of 2.
        if (pointsCombo_
            && static_cast<core::ElasticMethod>(methodCombo_->currentData().toInt())
                == core::ElasticMethod::EnergyStrain
            && pointsCombo_->currentData().toInt() < 4) {
            const int index = pointsCombo_->findData(4);
            if (index >= 0)
                pointsCombo_->setCurrentIndex(index);
        }
        updateCostEstimate();
        refreshPreview();
    });
    layout->addWidget(methodGroup);

    // -- Strain components ---------------------------------------------------
    auto* voigtGroup = new QGroupBox(tr("Strain Components"), page);
    auto* voigtLayout = new QVBoxLayout(voigtGroup);
    auto* voigtNote = new QLabel(
        tr("The Voigt strain components to apply. Leave every available box "
           "unchecked for all of them."),
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
               "vacuum along %1. The out-of-plane component(s) below are "
               "disabled (the vacuum axis is not a real lattice parameter); "
               "C_ij will additionally be reported per unit area (N/m), not "
               "just per volume (GPa). Ionic relaxation, when enabled, is "
               "still free to move atoms ALONG the vacuum axis (buckling / "
               "thickness change) — only the CELL is held fixed.")
                .arg(QString::fromLatin1(kAxisNames[vacuumAxis_])));
    } else {
        dimensionalityNote_->setVisible(false);
    }
    voigtLayout->addWidget(dimensionalityNote_);

    auto* voigtRow = new QHBoxLayout();
    for (int i = 0; i < 6; ++i) {
        voigtCheck_[i] = new QCheckBox(QString::fromLatin1(kVoigtLabels[i]), voigtGroup);
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

    // -- Strain magnitude / points / ions / symmetry --------------------------
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
        tr("Magnitude of the smallest sample strain (0.005 = 0.5%%) — the "
           "same squeeze as the Piezoelectric Tensor wizard's: large enough "
           "to clear numerical noise, small enough that F = I + eps stays "
           "in the linear-response regime the method assumes."));
    form->addRow(tr("Strain magnitude delta:"), strainSpin_);
    connect(strainSpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { refreshPreview(); });

    pointsCombo_ = new QComboBox(settingsGroup);
    pointsCombo_->addItem(tr("2 (+-delta, central difference)"), 2);
    pointsCombo_->addItem(tr("4 (+-delta, +-2*delta, least-squares fit)"), 4);
    pointsCombo_->setToolTip(
        tr("Stress-strain fits a LINE (2 points is the exact central "
           "difference). Energy-strain fits a PARABOLA and needs the extra "
           "points to be well-conditioned."));
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
           "strain-scaled positions; only the electronic state re-converges."
           "\n\nOn: RELAXED-ION — a geometry optimization of the positions "
           "runs at each fixed, strained cell before stress/energy is read "
           "off — the physical, mechanical-equilibrium response, at the "
           "cost of one relaxation per strain point."));
    form->addRow(QString(), relaxIonsCheck_);
    connect(relaxIonsCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    symmetryCheck_ = new QCheckBox(
        tr("Use point-group symmetry (spglib)"), settingsGroup);
    symmetryCheck_->setChecked(true);
    symmetryCheck_->setToolTip(
        tr("Symmetrizes the assembled tensor (zeroing whatever C_ij "
           "symmetry forbids and cleaning up numerical noise in the "
           "components it allows) and picks the crystal-class Born "
           "stability criterion. Unlike the Piezoelectric Tensor wizard "
           "there is no centrosymmetric refusal: every rank-4 elastic "
           "tensor is inversion-invariant, so symmetry never forces C_ij "
           "to zero the way it forces the (rank-3) piezoelectric tensor "
           "to."));
    form->addRow(QString(), symmetryCheck_);
    connect(symmetryCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });

    costLabel_ = new QLabel(settingsGroup);
    costLabel_->setWordWrap(true);
    form->addRow(QString(), costLabel_);

    layout->addStretch(1);
    return page;
}

std::vector<int> ElasticWizard::selectedVoigtComponents() const
{
    std::vector<int> out;
    for (int i = 0; i < 6; ++i)
        if (voigtCheck_[i] && voigtCheck_[i]->isChecked() && voigtCheck_[i]->isEnabled())
            out.push_back(i);
    return out;
}

void ElasticWizard::updateCostEstimate()
{
    if (!costLabel_)
        return;
    const auto voigt = selectedVoigtComponents();
    const int available = (vacuumAxis_ >= 0 && vacuumAxis_ <= 2)
        ? static_cast<int>(core::inPlaneVoigtComponents(vacuumAxis_).size())
        : 6;
    const int count = voigt.empty() ? available : static_cast<int>(voigt.size());
    const int points = pointsCombo_ ? pointsCombo_->currentData().toInt() : 2;
    const auto method = methodCombo_
        ? static_cast<core::ElasticMethod>(methodCombo_->currentData().toInt())
        : core::ElasticMethod::Auto;
    const long long pairs = (count > 1) ? static_cast<long long>(count) * (count - 1) / 2 * 4 : 0;
    QString text = tr("<b>%1 strain component(s)</b> -> %2 evaluation(s) "
                       "(%3 per component), plus one shared reference point.")
                       .arg(count)
                       .arg(count * points)
                       .arg(points);
    if (method != core::ElasticMethod::StressStrain)
        text += tr(" If this run ends up using energy-strain, %1 more "
                   "combined-strain point(s) fill in the off-diagonal terms.")
                    .arg(pairs);
    costLabel_->setText(text);
}

void ElasticWizard::setDensityBaselines(const QList<QPair<QString, QString>>& baselines)
{
    if (!baselineCombo_)
        return;
    const QString current = baselineCombo_->currentData().toString();
    baselineCombo_->clear();
    baselineCombo_->addItem(tr("(none) — use the current structure"), QString());
    for (const auto& [label, path] : baselines)
        baselineCombo_->addItem(label, path);
    Q_UNUSED(current);
    onBaselineChanged();
}

void ElasticWizard::onBaselineChanged()
{
    inherited_ = applyBaselineProvenance(baselineCombo_, inheritanceNote_);
    refreshPreview();
}

QString ElasticWizard::pythonExecutable() const
{
    if (inherited_ && !inherited_->pythonExecutable.isEmpty())
        return inherited_->pythonExecutable;
    return SimulationWizardBase::pythonExecutable();
}

core::ElasticConfig ElasticWizard::config() const
{
    core::ElasticConfig cfg;
    cfg.calculator = baseCalculatorConfig();
    cfg.calculator.task = core::TaskKind::SinglePoint;
    if (baselineCombo_)
        cfg.baselinePath = baselineCombo_->currentData().toString().toStdString();
    cfg.voigtComponents = selectedVoigtComponents();
    cfg.vacuumAxis = vacuumAxis_;
    cfg.method = methodCombo_
        ? static_cast<core::ElasticMethod>(methodCombo_->currentData().toInt())
        : core::ElasticMethod::Auto;
    cfg.strainMagnitude = strainSpin_ ? strainSpin_->value() : 0.005;
    cfg.pointsPerComponent = pointsCombo_ ? pointsCombo_->currentData().toInt() : 2;
    cfg.relaxIons = relaxIonsCheck_ && relaxIonsCheck_->isChecked();
    cfg.useSymmetry = !symmetryCheck_ || symmetryCheck_->isChecked();
    return cfg;
}

QString ElasticWizard::generateScript() const
{
    return QString::fromStdString(core::generateElasticScript(config()));
}

} // namespace calango::gui
