#include "gui/XasWizard.hpp"

#include "gui/GuiUtils.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace calango::gui {

XasWizard::XasWizard(std::shared_ptr<const core::Structure> structure,
                     QWidget* parent)
    : SimulationWizardBase(parent)
    , structure_(std::move(structure))
{
    buildUi();
    selectCalculator(core::CalculatorKind::Gpaw);
    electronic_.updateEnabled();
    onElementChanged();
}

QString XasWizard::wizardTitle() const
{
    return tr("X-ray Absorption Spectroscopy (XAS) Setup");
}

QStringList XasWizard::calculatorElements() const
{
    return structureElements(structure_.get());
}

QWidget* XasWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* intro = new QLabel(
        tr("An X-ray absorption spectrum is a transition out of a <b>core "
           "level</b> of one atom, so it needs a PAW dataset with a hole in "
           "that level — and none ships with GPAW. The generated script builds "
           "one for the element below, runs a ground state that uses it on the "
           "chosen atom, and evaluates the spectrum from those "
           "wavefunctions.<br><br>"
           "Following the GPAW XAS tutorial. Note that <code>gpaw.xas</code> "
           "runs on GPAW's legacy engine, so this is the one script Calango "
           "generates that does not enable the new one."),
        page);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    // -- The absorbing site -------------------------------------------------
    auto* siteGroup = new QGroupBox(tr("Absorbing site"), page);
    auto* siteForm = new QFormLayout(siteGroup);

    elementCombo_ = new QComboBox(siteGroup);
    elementCombo_->addItems(calculatorElements());
    elementCombo_->setToolTip(
        tr("The element whose core level is excited. The core-hole dataset is "
           "generated for this element."));
    siteForm->addRow(tr("Element:"), elementCombo_);

    atomCombo_ = new QComboBox(siteGroup);
    atomCombo_->setToolTip(
        tr("Which atom of that element absorbs.\n\n"
           "One atom, not all of them: giving the core-hole setup to every "
           "atom of the species would model a solid in which all of them are "
           "excited simultaneously, which is not what an absorption "
           "measurement does. For inequivalent sites, run one calculation per "
           "site and add the spectra."));
    siteForm->addRow(tr("Absorbing atom:"), atomCombo_);

    levelCombo_ = new QComboBox(siteGroup);
    // Order matches core::XasCoreLevel.
    levelCombo_->addItem(tr("K edge — 1s"));
    levelCombo_->addItem(tr("L₁ edge — 2s"));
    levelCombo_->addItem(tr("L₂,₃ edges — 2p"));
    levelCombo_->setToolTip(
        tr("The core level the hole is made in. K (1s) is what almost every "
           "XAS measurement means; the L edges are used for transition metals, "
           "where the 2p → 3d transitions carry the interesting structure."));
    siteForm->addRow(tr("Edge:"), levelCombo_);
    layout->addWidget(siteGroup);

    // -- The core hole ------------------------------------------------------
    auto* holeGroup = new QGroupBox(tr("Core-hole setup"), page);
    auto* holeForm = new QFormLayout(holeGroup);

    holeCombo_ = new QComboBox(holeGroup);
    // Order matches core::XasCoreHole.
    holeCombo_->addItem(tr("Half hole (transition potential, 0.5 e)"));
    holeCombo_->addItem(tr("Full hole (excited final state, 1.0 e)"));
    holeCombo_->addItem(tr("No hole (unperturbed ground state)"));
    holeCombo_->setToolTip(
        tr("How much of the core electron is removed — a real physical choice "
           "that changes the answer, which is why it is not buried in the "
           "script.\n\n"
           "Half hole: the transition-potential approximation. One calculation "
           "gives the whole spectrum, because the final-state relaxation is "
           "averaged between initial and final states. This is what the GPAW "
           "tutorial uses and what most published XAS is.\n\n"
           "Full hole: the excited final state proper. Better for the first "
           "resonance, worse for the rest of the spectrum, and it needs the "
           "delta-Kohn-Sham shift below to sit on an absolute energy scale.\n\n"
           "No hole: the unperturbed ground state — the spectrum you would get "
           "if the core hole did not pull the excited states down. Worth "
           "computing to see how large that effect is."));
    holeForm->addRow(tr("Core hole:"), holeCombo_);

    bandsSpin_ = new QSpinBox(holeGroup);
    bandsSpin_->setRange(5, 500);
    bandsSpin_->setValue(30);
    bandsSpin_->setToolTip(
        tr("Unoccupied bands converged above the occupied ones. The spectrum "
           "is a sum over these, so too few truncates it — visibly, as a "
           "spectrum that simply stops rather than decaying."));
    holeForm->addRow(tr("Unoccupied bands:"), bandsSpin_);
    layout->addWidget(holeGroup);

    // -- Broadening and energy scale ---------------------------------------
    auto* specGroup = new QGroupBox(tr("Spectrum"), page);
    auto* specForm = new QFormLayout(specGroup);

    fwhmSpin_ = new QDoubleSpinBox(specGroup);
    fwhmSpin_->setRange(0.01, 20.0);
    fwhmSpin_->setDecimals(2);
    fwhmSpin_->setSingleStep(0.1);
    fwhmSpin_->setValue(0.5);
    fwhmSpin_->setSuffix(tr(" eV"));
    fwhmSpin_->setToolTip(
        tr("Gaussian broadening applied to every transition."));
    specForm->addRow(tr("Broadening (FWHM):"), fwhmSpin_);

    linBroadCheck_ = new QCheckBox(tr("Broaden more at higher energy"),
                                   specGroup);
    linBroadCheck_->setChecked(true);
    linBroadCheck_->setToolTip(
        tr("Ramp the broadening linearly above the edge. This is the physical "
           "behaviour — core-hole lifetime plus final-state broadening — and "
           "it is what makes a computed spectrum comparable with a measured "
           "one, whose peaks wash out with energy."));
    specForm->addRow(QString(), linBroadCheck_);

    auto* rampRow = new QWidget(specGroup);
    auto* rampLayout = new QHBoxLayout(rampRow);
    rampLayout->setContentsMargins(0, 0, 0, 0);
    const auto rampSpin = [&](double value, double lo, double hi,
                              const QString& suffix) {
        auto* spin = new QDoubleSpinBox(rampRow);
        spin->setRange(lo, hi);
        spin->setDecimals(2);
        spin->setValue(value);
        spin->setSuffix(suffix);
        return spin;
    };
    linBroadFwhm_ = rampSpin(1.5, 0.01, 50.0, tr(" eV"));
    linBroadStart_ = rampSpin(536.0, -1e5, 1e5, tr(" eV"));
    linBroadStop_ = rampSpin(540.0, -1e5, 1e5, tr(" eV"));
    rampLayout->addWidget(new QLabel(tr("to"), rampRow));
    rampLayout->addWidget(linBroadFwhm_);
    rampLayout->addWidget(new QLabel(tr("from"), rampRow));
    rampLayout->addWidget(linBroadStart_);
    rampLayout->addWidget(new QLabel(tr("to"), rampRow));
    rampLayout->addWidget(linBroadStop_);
    rampRow->setToolTip(
        tr("The broadening grows from the FWHM above to this width, linearly "
           "between the two energies. The defaults are the tutorial's, chosen "
           "for the oxygen K edge — move them to your own edge."));
    specForm->addRow(tr("Ramp:"), rampRow);

    dksCheck_ = new QCheckBox(tr("Compute the absolute edge position "
                                 "(delta-Kohn-Sham)"),
                              specGroup);
    dksCheck_->setToolTip(
        tr("The spectrum GPAW produces is on a RELATIVE scale whose zero is "
           "the first unoccupied state, which is not where a measurement puts "
           "it. Two extra total-energy calculations give the shift onto an "
           "absolute scale — roughly doubling the cost of the run.\n\n"
           "Leave it off and type a known edge energy below instead if you "
           "have one."));
    specForm->addRow(QString(), dksCheck_);

    dksSpin_ = new QDoubleSpinBox(specGroup);
    dksSpin_->setRange(0.0, 1e5);
    dksSpin_->setDecimals(2);
    dksSpin_->setValue(0.0);
    dksSpin_->setSuffix(tr(" eV"));
    dksSpin_->setToolTip(
        tr("Energy shift applied to the whole spectrum. 0 leaves the relative "
           "scale."));
    specForm->addRow(tr("Edge energy:"), dksSpin_);
    layout->addWidget(specGroup);

    note_ = new QLabel(page);
    note_->setWordWrap(true);
    note_->setTextFormat(Qt::RichText);
    layout->addWidget(note_);

    connect(elementCombo_, &QComboBox::currentIndexChanged, this,
            &XasWizard::onElementChanged);
    for (QComboBox* combo : {atomCombo_, levelCombo_, holeCombo_})
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this] { refreshPreview(); });
    connect(bandsSpin_, &QSpinBox::valueChanged, this,
            [this] { refreshPreview(); });
    for (QDoubleSpinBox* spin : {fwhmSpin_, linBroadFwhm_, linBroadStart_,
                                 linBroadStop_, dksSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    for (QCheckBox* check : {linBroadCheck_, dksCheck_})
        connect(check, &QCheckBox::toggled, this, [this] {
            updateEnabled();
            refreshPreview();
        });

    updateEnabled();
    layout->addStretch(1);
    return page;
}

void XasWizard::onElementChanged()
{
    if (!atomCombo_ || !structure_)
        return;
    const QString element = elementCombo_->currentText();
    atomCombo_->clear();
    for (int i = 0; i < static_cast<int>(structure_->atoms().size()); ++i) {
        const core::Atom& atom = structure_->atoms()[static_cast<std::size_t>(i)];
        if (QString::fromLatin1(atom.symbol()) != element)
            continue;
        atomCombo_->addItem(tr("#%1  (%2, %3, %4) Å")
                                .arg(i + 1)
                                .arg(atom.position.x, 0, 'f', 3)
                                .arg(atom.position.y, 0, 'f', 3)
                                .arg(atom.position.z, 0, 'f', 3),
                            i);
    }
    updateEnabled();
    refreshPreview();
}

void XasWizard::updateEnabled()
{
    const bool ramp = linBroadCheck_ && linBroadCheck_->isChecked();
    for (QDoubleSpinBox* spin : {linBroadFwhm_, linBroadStart_, linBroadStop_})
        if (spin)
            spin->setEnabled(ramp);
    // The computed shift replaces the typed one, so offering both at once
    // would be offering two answers to the same question.
    if (dksSpin_)
        dksSpin_->setEnabled(!dksCheck_ || !dksCheck_->isChecked());

    if (!note_)
        return;
    const int sites = atomCombo_ ? atomCombo_->count() : 0;
    note_->setText(
        sites > 1
            ? tr("<i>%1 %2 atoms in this structure. This computes the spectrum "
                 "of the one selected; inequivalent sites each need their own "
                 "run, and the measured spectrum is their sum.</i>")
                  .arg(sites)
                  .arg(elementCombo_->currentText())
            : QString());
}

core::XasRunConfig XasWizard::runConfig() const
{
    core::XasRunConfig config;
    config.element = elementCombo_->currentText().toStdString();
    config.absorbingAtom =
        atomCombo_->count() > 0 ? atomCombo_->currentData().toInt() : 0;
    config.coreLevel = static_cast<core::XasCoreLevel>(levelCombo_->currentIndex());
    config.coreHole = static_cast<core::XasCoreHole>(holeCombo_->currentIndex());
    config.extraBands = bandsSpin_->value();
    config.fwhm = fwhmSpin_->value();
    config.linearBroadening = linBroadCheck_->isChecked();
    config.linearBroadeningStart = linBroadFwhm_->value();
    config.linearBroadeningEnergyStart = linBroadStart_->value();
    config.linearBroadeningEnergyStop = linBroadStop_->value();
    config.computeDks = dksCheck_->isChecked();
    config.dksEnergy = dksSpin_->value();

    config.calculator = baseCalculatorConfig();
    config.calculator.task = core::TaskKind::SinglePoint;
    electronic_.applyTo(config.calculator);
    return config;
}

QString XasWizard::generateScript() const
{
    return QString::fromStdString(
        core::XasScriptGenerator::generate(runConfig(), "structure.extxyz"));
}

} // namespace calango::gui
