#include "gui/GrapheneOxideGcmcWizard.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

namespace calango::gui {

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

    auto* intro = new QLabel(
        tr("The move set ADDS and REMOVES groups, so the composition is "
           "sampled rather than fixed. Δμ is measured from the references "
           "below, which this run computes for itself with the calculator "
           "you chose."),
        box);
    intro->setWordWrap(true);
    form->addRow(intro);

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
           "which is the reducing limit."));
    form->addRow(tr("Δμ_H:"), deltaMuH_);

    deltaMuO_ = makeMu(0.0);
    deltaMuO_->setObjectName(QStringLiteral("gcmcDeltaMuO"));
    deltaMuO_->setToolTip(
        tr("Δμ_O, relative to μ_O⁰ = E(H₂O) − E(H₂) — oxygen from water in "
           "equilibrium with hydrogen, the standard humid-environment "
           "reference.\n\n"
           "More positive = more oxidizing = more oxygen groups."));
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

    for (QDoubleSpinBox* spin : {deltaMuH_, deltaMuO_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] {
            refreshPotentialSummary();
            refreshPreview();
        });
    }
    for (QDoubleSpinBox* spin : {swapWeight_, insertWeight_, deleteWeight_}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    }
    refreshPotentialSummary();
    return page;
}

void GrapheneOxideGcmcWizard::refreshPotentialSummary()
{
    if (!potentialSummary_)
        return;
    // The absolute potentials are μ⁰ + Δμ, and μ⁰ is not known until the
    // run computes it — so what is shown here is the DEFINITION plus the
    // offsets, never an invented number. Reporting a made-up μ⁰ would be
    // worse than reporting none: it is the one quantity a reader would
    // take on trust.
    potentialSummary_->setText(
        tr("<i>μ_H = ½E(H₂) %1 %2 eV &nbsp;·&nbsp; "
           "μ_O = E(H₂O) − E(H₂) %3 %4 eV</i><br>"
           "The two reference energies are computed once at the start of "
           "the run, with this calculator, and reported in the log.")
            .arg(deltaMuH_->value() < 0 ? QStringLiteral("−")
                                        : QStringLiteral("+"))
            .arg(std::abs(deltaMuH_->value()), 0, 'f', 3)
            .arg(deltaMuO_->value() < 0 ? QStringLiteral("−")
                                        : QStringLiteral("+"))
            .arg(std::abs(deltaMuO_->value()), 0, 'f', 3));
}

core::GrapheneOxideMcmdConfig GrapheneOxideGcmcWizard::collectConfig() const
{
    core::GrapheneOxideMcmdConfig config =
        GrapheneOxideMcmdWizard::collectConfig();
    config.grandCanonical = true;
    if (deltaMuH_)
        config.deltaMuHEv = deltaMuH_->value();
    if (deltaMuO_)
        config.deltaMuOEv = deltaMuO_->value();
    if (swapWeight_)
        config.swapWeight = swapWeight_->value();
    if (insertWeight_)
        config.insertWeight = insertWeight_->value();
    if (deleteWeight_)
        config.deleteWeight = deleteWeight_->value();
    return config;
}

} // namespace calango::gui
