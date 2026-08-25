#include "gui/GrapheneOxideGcmcWizard.hpp"

#include "gui/GuiUtils.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

namespace {

/// k_B in eV/K — the same constant ase.units.kB carries, and the one the
/// generated script's own pH term uses. Written here rather than pulled from
/// core::PhysicalConstants so the number quoted in the wizard's live summary
/// is visibly the number in the formula it is quoting.
constexpr double kBoltzmannEvPerK = 8.617333262e-5;

/// The eV the CHE subtracts from ½E(H₂): eU + k_B T ln(10)·pH, with U on the
/// SHE scale. One definition, shared by the live summary and by nothing else
/// — the RUN computes its own from the same formula in Python, because a
/// number this wizard sent along would be a second source of truth for the
/// quantity the acceptance criterion is built on.
double potentialShiftEv(double potentialV, double ph, double temperatureK)
{
    return potentialV + kBoltzmannEvPerK * temperatureK * std::log(10.0) * ph;
}

} // namespace

GrapheneOxideGcmcWizard::GrapheneOxideGcmcWizard(QWidget* parent)
    // DeferUi: the base must NOT build the UI from its own constructor, or
    // its virtuals win over this class's and the reservoir group is never
    // added (and relaxationMode() comes back as the base's). buildUi() below
    // is the most-derived call the base class asks for. Same reason
    // GO/MC-Opt does it.
    : GrapheneOxideMcmdWizard(DeferUi{}, parent)
{
    buildUi();
}

core::GoMcRelaxation GrapheneOxideGcmcWizard::relaxationMode() const
{
    return core::GoMcRelaxation::Optimization;
}

QString GrapheneOxideGcmcWizard::wizardTitle() const
{
    return tr("GO Grand Canonical MC");
}

QWidget* GrapheneOxideGcmcWizard::buildSettingsPage()
{
    QWidget* page = GrapheneOxideMcmdWizard::buildSettingsPage();
    auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
    if (!layout)
        return page;

    auto* box = new QGroupBox(tr("Reservoir (chemical potentials)"), page);
    auto* form = new QFormLayout(box);
    reservoirBox_ = box;

    auto* intro = new QLabel(
        tr("The move set ADDS and REMOVES groups, so the composition is "
           "sampled rather than fixed. The two reference energies E(H₂) and "
           "E(H₂O) are computed by this run, with the calculator you chose; "
           "what differs below is what is DONE with them."),
        box);
    intro->setWordWrap(true);
    form->addRow(intro);

    // -- The scheme --------------------------------------------------------
    //
    // First on the page, because it decides which of the rows under it are
    // controls at all.
    potentialMode_ = new QComboBox(box);
    potentialMode_->setObjectName(QStringLiteral("gcmcPotentialMode"));
    // Order matches core::GoGcmcPotentialMode.
    potentialMode_->addItem(tr("Manual — Δμ about the gas-phase references"));
    potentialMode_->addItem(
        tr("Computational hydrogen electrode (CHE) — μ_H from a potential"));
    potentialMode_->setToolTip(
        tr("What the sheet is in equilibrium WITH.\n\n"
           "• Manual — a humid atmosphere. μ_H⁰ = ½E(H₂) and "
           "μ_O⁰ = E(H₂O) − E(H₂), and you move around them with Δμ_H and "
           "Δμ_O. Two independent knobs, no electrochemistry.\n\n"
           "• CHE — an ELECTRODE. Hydrogen leaves the sheet as a "
           "proton-electron pair rather than as ½H₂, so its chemical "
           "potential is set by the electrode potential:\n"
           "      μ_H = ½E(H₂) − eU − k_B T ln(10)·pH\n"
           "and oxygen follows from water in equilibrium with THAT "
           "reservoir:\n"
           "      μ_O = E(H₂O) − 2μ_H\n"
           "which is what makes oxidation depend on the potential. Δμ_H is "
           "no longer a control — the potential is. At U = 0 V and pH 0 the "
           "two schemes are identical by construction.\n\n"
           "Nørskov, J. K. et al., J. Phys. Chem. B 108, 17886 (2004)."));
    form->addRow(tr("Reference scheme:"), potentialMode_);

    const auto makeMu = [box](double value) {
        auto* spin = new QDoubleSpinBox(box);
        spin->setRange(-20.0, 20.0);
        spin->setDecimals(3);
        spin->setSingleStep(0.1);
        spin->setSuffix(QObject::tr(" eV"));
        spin->setValue(value);
        return spin;
    };

    deltaMuH_ = makeMu(0.0);
    deltaMuH_->setObjectName(QStringLiteral("gcmcDeltaMuH"));
    deltaMuH_->setToolTip(
        tr("Δμ_H, relative to μ_H⁰ = ½ E(H₂).\n\n"
           "More positive = a richer hydrogen reservoir = more hydroxyls. "
           "Strongly negative and essentially no insertion is ever accepted, "
           "which is the reducing limit.\n\n"
           "Manual scheme only: under the CHE the electrode potential fixes "
           "μ_H, and a second additive knob on top of it would be an "
           "unlabelled second potential axis."));
    form->addRow(tr("Δμ_H:"), deltaMuH_);

    // -- CHE: the potential, the pH, and the pH term's temperature ---------
    electrodePotential_ = new QDoubleSpinBox(box);
    electrodePotential_->setObjectName(QStringLiteral("gcmcElectrodePotential"));
    electrodePotential_->setRange(-5.0, 5.0);
    electrodePotential_->setDecimals(3);
    electrodePotential_->setSingleStep(0.1);
    electrodePotential_->setSuffix(tr(" V"));
    electrodePotential_->setValue(0.0);
    electrodePotential_->setToolTip(
        tr("Electrode potential U, on the SHE scale. Positive is "
           "OXIDIZING: it lowers μ_H by eU, so hydrogen is easier to strip "
           "off the sheet and — through μ_O = E(H₂O) − 2μ_H — oxygen is "
           "2eU cheaper to place.\n\n"
           "Why SHE and not RHE: on the RHE scale the potential and pH terms "
           "collapse into one (U_SHE = U_RHE − (k_B T ln10 / e)·pH cancels "
           "the pH term exactly), which is why a CHE free energy quoted vs. "
           "RHE is pH-independent. A wizard offering U vs. RHE AND a pH box "
           "would be offering a control with no effect. Leave pH at 0 and "
           "the two scales coincide."));
    form->addRow(tr("Electrode potential U (vs. SHE):"), electrodePotential_);

    solutionPh_ = new QDoubleSpinBox(box);
    solutionPh_->setObjectName(QStringLiteral("gcmcSolutionPh"));
    solutionPh_->setRange(-2.0, 16.0);
    solutionPh_->setDecimals(2);
    solutionPh_->setSingleStep(0.5);
    solutionPh_->setValue(0.0);
    solutionPh_->setToolTip(
        tr("Solution pH. Enters as −k_B T ln(10)·pH — about 59 meV per pH "
           "unit at 298 K.\n\n"
           "At a FIXED potential vs. SHE, raising the pH is OXIDIZING, in "
           "the same direction as raising U: fewer protons in solution means "
           "a lower μ(H⁺ + e⁻), so the sheet gives one up more readily. That "
           "is the −59 mV per pH unit slope every Pourbaix diagram draws its "
           "oxide boundaries with.\n\n"
           "pH 0 is the SHE's own reference state, where this term "
           "vanishes."));
    form->addRow(tr("pH:"), solutionPh_);

    potentialTemperature_ = new QDoubleSpinBox(box);
    potentialTemperature_->setObjectName(
        QStringLiteral("gcmcPotentialTemperature"));
    potentialTemperature_->setRange(1.0, 3000.0);
    potentialTemperature_->setDecimals(2);
    potentialTemperature_->setSingleStep(5.0);
    potentialTemperature_->setSuffix(tr(" K"));
    potentialTemperature_->setValue(
        samplingTemperatureSpin() ? samplingTemperatureSpin()->value() : 300.0);
    potentialTemperature_->setToolTip(
        tr("Temperature of the pH term ONLY — it scales k_B T ln(10)·pH and "
           "nothing else. With pH 0 it has no effect at all.\n\n"
           "It follows the sampling temperature above until you change it, "
           "because the two ARE one physical temperature. They are separate "
           "fields so that a run can be lined up against a paper's 298.15 K "
           "numbers without also changing the ensemble the walk samples."));
    form->addRow(tr("Temperature (pH term):"), potentialTemperature_);

    deltaMuO_ = makeMu(0.0);
    deltaMuO_->setObjectName(QStringLiteral("gcmcDeltaMuO"));
    deltaMuO_->setToolTip(
        tr("Δμ_O, an offset on top of whichever oxygen reference the scheme "
           "above defines:\n\n"
           "• Manual — μ_O⁰ = E(H₂O) − E(H₂), oxygen from water in "
           "equilibrium with hydrogen, the standard humid-environment "
           "reference.\n"
           "• CHE — μ_O = E(H₂O) − 2μ_H, oxygen from water in equilibrium "
           "with the ELECTRODE, which is the same statement with the "
           "potential-dependent μ_H in it.\n\n"
           "More positive = more oxidizing = more oxygen groups. Kept in "
           "both schemes so a CHE scan can still be explored around its own "
           "reference."));
    form->addRow(tr("Δμ_O:"), deltaMuO_);

    potentialSummary_ = new QLabel(box);
    potentialSummary_->setWordWrap(true);
    potentialSummary_->setTextFormat(Qt::RichText);
    form->addRow(potentialSummary_);

    const auto makeWeight = [box](double value) {
        auto* spin = new QDoubleSpinBox(box);
        spin->setRange(0.0, 100.0);
        spin->setDecimals(2);
        spin->setSingleStep(0.5);
        spin->setValue(value);
        return spin;
    };
    swapWeight_ = makeWeight(1.0);
    swapWeight_->setObjectName(QStringLiteral("gcmcSwapWeight"));
    swapWeight_->setToolTip(
        tr("Relative weight of RELOCATION moves — the conserving move the "
           "other two GO Monte Carlo modules run exclusively.\n\n"
           "Kept in the mix on purpose: a pure insert/delete walk finds the "
           "right NUMBER of groups long before it finds a sensible "
           "arrangement of them at that number."));
    insertWeight_ = makeWeight(1.0);
    insertWeight_->setObjectName(QStringLiteral("gcmcInsertWeight"));
    insertWeight_->setToolTip(tr("Relative weight of INSERTION moves."));
    deleteWeight_ = makeWeight(1.0);
    deleteWeight_->setObjectName(QStringLiteral("gcmcDeleteWeight"));
    deleteWeight_->setToolTip(tr("Relative weight of DELETION moves."));
    form->addRow(tr("Move weight — swap:"), swapWeight_);
    form->addRow(tr("Move weight — insert:"), insertWeight_);
    form->addRow(tr("Move weight — delete:"), deleteWeight_);

    layout->addWidget(box);

    for (QDoubleSpinBox* spin : {deltaMuH_, deltaMuO_, electrodePotential_,
                                 solutionPh_, potentialTemperature_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] {
            refreshPotentialSummary();
            refreshPreview();
        });
    }
    // Marked separately from the refresh above, and AFTER it in connection
    // order only by accident — Qt fires both, and the flag only has to be
    // set by the time the sampling temperature next changes.
    connect(potentialTemperature_, &QDoubleSpinBox::valueChanged, this,
            [this] { potentialTemperatureTouched_ = true; });
    // The pH term's temperature follows the sampling temperature until the
    // user takes it over. setValue() below would itself trip the flag, so it
    // is cleared again immediately after.
    if (QDoubleSpinBox* sampling = samplingTemperatureSpin()) {
        connect(sampling, &QDoubleSpinBox::valueChanged, this,
                [this](double kelvin) {
                    if (potentialTemperatureTouched_ || !potentialTemperature_)
                        return;
                    potentialTemperature_->setValue(kelvin);
                    potentialTemperatureTouched_ = false;
                });
    }
    connect(potentialMode_, &QComboBox::currentIndexChanged, this, [this] {
        syncPotentialModeRows();
        refreshPotentialSummary();
        refreshPreview();
    });
    for (QDoubleSpinBox* spin : {swapWeight_, insertWeight_, deleteWeight_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    }
    syncPotentialModeRows();
    refreshPotentialSummary();
    return page;
}

void GrapheneOxideGcmcWizard::syncPotentialModeRows()
{
    if (!potentialMode_ || !reservoirBox_)
        return;
    const bool che = potentialMode_->currentIndex()
        == static_cast<int>(
            core::GoGcmcPotentialMode::ComputationalHydrogenElectrode);
    setFormRowVisible(reservoirBox_, deltaMuH_, !che);
    setFormRowVisible(reservoirBox_, electrodePotential_, che);
    setFormRowVisible(reservoirBox_, solutionPh_, che);
    setFormRowVisible(reservoirBox_, potentialTemperature_, che);
}

void GrapheneOxideGcmcWizard::refreshPotentialSummary()
{
    if (!potentialSummary_ || !potentialMode_)
        return;
    // The absolute potentials are μ⁰ + Δμ, and μ⁰ is not known until the
    // run computes it — so what is shown here is the DEFINITION plus the
    // offsets, never an invented number. Reporting a made-up μ⁰ would be
    // worse than reporting none: it is the one quantity a reader would
    // take on trust.
    //
    // Under the CHE the potential-dependent part IS known here — it is
    // arithmetic on U, pH and T, with no reference energy in it — so it is
    // shown as a number, folded into the same formulas. That is the whole
    // transparency this mode needs: a user turning the potential knob sees
    // exactly how many eV of it reach μ_H and μ_O.
    const auto signedEv = [](double value) {
        return QStringLiteral("%1 %2")
            .arg(value < 0 ? QStringLiteral("−") : QStringLiteral("+"))
            .arg(std::abs(value), 0, 'f', 3);
    };
    const bool che = potentialMode_->currentIndex()
        == static_cast<int>(
            core::GoGcmcPotentialMode::ComputationalHydrogenElectrode);
    if (!che) {
        potentialSummary_->setText(
            tr("<i>μ_H = ½E(H₂) %1 eV &nbsp;·&nbsp; "
               "μ_O = E(H₂O) − E(H₂) %2 eV</i><br>"
               "The two reference energies are computed once at the start of "
               "the run, with this calculator, and reported in the log.")
                .arg(signedEv(deltaMuH_->value()), signedEv(deltaMuO_->value())));
        return;
    }
    const double shift = potentialShiftEv(electrodePotential_->value(),
                                          solutionPh_->value(),
                                          potentialTemperature_->value());
    // μ_O = E(H₂O) − 2μ_H = E(H₂O) − E(H₂) + 2·shift + Δμ_O, which is the
    // manual formula plus 2·shift — written that way on purpose, so the two
    // schemes can be read against each other on one line and the U = 0
    // identity is visible rather than asserted.
    potentialSummary_->setText(
        tr("<i>eU + k<sub>B</sub>T ln(10)·pH = %1 eV</i><br>"
           "<i>μ_H = ½E(H₂) %2 eV &nbsp;·&nbsp; "
           "μ_O = E(H₂O) − E(H₂) %3 eV %4 eV</i><br>"
           "μ_O carries <b>twice</b> the shift, because water releases two "
           "proton-electron pairs per oxygen — that factor of 2 is the "
           "potential dependence of oxidation. The reference energies are "
           "computed once at the start of the run and reported in the log.")
            .arg(QString::number(shift, 'f', 4), signedEv(-shift),
                 signedEv(2.0 * shift), signedEv(deltaMuO_->value())));
}

core::GrapheneOxideMcmdConfig GrapheneOxideGcmcWizard::collectConfig() const
{
    core::GrapheneOxideMcmdConfig config =
        GrapheneOxideMcmdWizard::collectConfig();
    config.grandCanonical = true;
    if (potentialMode_)
        config.potentialMode = static_cast<core::GoGcmcPotentialMode>(
            potentialMode_->currentIndex());
    if (deltaMuH_)
        config.deltaMuHEv = deltaMuH_->value();
    if (deltaMuO_)
        config.deltaMuOEv = deltaMuO_->value();
    if (electrodePotential_)
        config.electrodePotentialV = electrodePotential_->value();
    if (solutionPh_)
        config.pH = solutionPh_->value();
    if (potentialTemperature_)
        config.potentialTemperatureK = potentialTemperature_->value();
    if (swapWeight_)
        config.swapWeight = swapWeight_->value();
    if (insertWeight_)
        config.insertWeight = insertWeight_->value();
    if (deleteWeight_)
        config.deleteWeight = deleteWeight_->value();
    return config;
}

} // namespace calango::gui
