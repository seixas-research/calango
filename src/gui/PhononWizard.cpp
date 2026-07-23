#include "gui/PhononWizard.hpp"

#include "core/PhononScriptGenerator.hpp"
#include "gui/EmbeddedKPathEditor.hpp"

#include <QGroupBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace calango::gui {

PhononWizard::PhononWizard(bool periodic,
                           std::shared_ptr<const core::Structure> structure,
                           QWidget* parent)
    : SimulationWizardBase(parent)
    , periodic_(periodic)
    , structure_(std::move(structure))
{
    buildUi();
}

QString PhononWizard::wizardTitle() const
{
    return tr("Phonon Setup");
}

QString PhononWizard::settingsHeader() const
{
    return tr("q-Path Definition");
}

QWidget* PhononWizard::buildCalculatorExtras()
{
    // Finite-displacement and DOS-sampling settings describe how the phonon
    // calculation is set up, so they belong on Calculator Settings (Stage 2)
    // alongside the engine's own parameters. Stage 3 is then purely the
    // q-path, matching the Electronic Structure flow.
    auto* group = new QGroupBox(tr("Phonon settings"), this);
    auto* form = new QFormLayout(group);

    if (!periodic_)
        form->addRow(new QLabel(
            tr("Molecular system: Γ-point normal modes (no supercell / mesh)."),
            group));

    deltaSpin_ = new QDoubleSpinBox(group);
    deltaSpin_->setRange(0.001, 0.2);
    deltaSpin_->setDecimals(3);
    deltaSpin_->setSingleStep(0.005);
    deltaSpin_->setValue(0.01);
    deltaSpin_->setSuffix(tr(" Å"));
    deltaSpin_->setToolTip(tr("Finite-displacement step amplitude (± per atom)."));
    form->addRow(tr("Displacement δ:"), deltaSpin_);

    auto* superRow = new QHBoxLayout;
    for (auto*& spin : supercellSpins_) {
        spin = new QSpinBox(group);
        spin->setRange(1, 10);
        spin->setValue(2);
        spin->setEnabled(periodic_);
        superRow->addWidget(spin);
    }
    form->addRow(tr("Supercell (nx·ny·nz):"), superRow);

    acousticCheck_ = new QCheckBox(tr("Enforce acoustic sum rule"), group);
    acousticCheck_->setChecked(true);
    acousticCheck_->setToolTip(
        tr("Make the 3 acoustic branches vanish at Γ (recommended)."));
    acousticCheck_->setEnabled(periodic_);
    form->addRow(acousticCheck_);

    meshSpin_ = new QSpinBox(group);
    meshSpin_->setRange(2, 64);
    meshSpin_->setValue(20);
    meshSpin_->setToolTip(tr("Monkhorst-Pack n×n×n mesh density for the phonon "
                             "DOS / band interpolation."));
    meshSpin_->setEnabled(periodic_);
    form->addRow(tr("Mesh density (DOS):"), meshSpin_);

    dosWidthSpin_ = new QDoubleSpinBox(group);
    dosWidthSpin_->setRange(0.1, 200.0);
    dosWidthSpin_->setDecimals(2);
    dosWidthSpin_->setSingleStep(0.5);
    dosWidthSpin_->setValue(2.0);
    dosWidthSpin_->setSuffix(tr(" cm⁻¹"));
    dosWidthSpin_->setEnabled(periodic_);
    dosWidthSpin_->setToolTip(
        tr("Gaussian broadening σ used when sampling the PhDOS.\n"
           "A finite k-mesh gives a comb of delta peaks; σ smooths it into a "
           "continuous DOS. Too small leaves spikes, too large erases van "
           "Hove features — raise the mesh density before lowering σ."));
    form->addRow(tr("Gaussian smearing σ:"), dosWidthSpin_);

    bandPointsSpin_ = new QSpinBox(group);
    bandPointsSpin_->setRange(20, 1000);
    bandPointsSpin_->setValue(100);
    bandPointsSpin_->setToolTip(tr("q-points along the dispersion path."));
    bandPointsSpin_->setEnabled(periodic_);
    form->addRow(tr("Band-path points:"), bandPointsSpin_);

    for (QDoubleSpinBox* spin : {deltaSpin_, dosWidthSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    for (QSpinBox* spin : {meshSpin_, bandPointsSpin_})
        connect(spin, &QSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    connect(acousticCheck_, &QCheckBox::toggled, this,
            [this] { refreshPreview(); });
    return group;
}

QWidget* PhononWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // Stage 3 is purely the q-path: the embedded Brillouin-zone builder, same
    // as the Electronic Structure wizard — a phonon dispersion is read along
    // exactly the same high-symmetry lines.
    if (periodic_) {
        kpath_ = new EmbeddedKPathEditor(structure_, page);
        layout->addWidget(kpath_, 1);
        connect(kpath_, &EmbeddedKPathEditor::pathChanged, this,
                [this] { refreshPreview(); });
    } else {
        auto* note = new QLabel(
            tr("Γ-point normal modes only: a molecular system has no "
               "Brillouin zone, so there is no dispersion path to define."),
            page);
        note->setWordWrap(true);
        layout->addWidget(note);
        layout->addStretch(1);
    }
    return page;
}

QString PhononWizard::generateScript() const
{
    core::PhononConfig pc;
    pc.calculator = baseCalculatorConfig();
    pc.periodic = periodic_;
    for (int i = 0; i < 3; ++i)
        pc.supercell[i] = supercellSpins_[i]->value();
    pc.deltaAngstrom = deltaSpin_->value();
    pc.bandPathPoints = bandPointsSpin_->value();
    if (kpath_)
        pc.kpath = kpath_->path().toStdString();
    pc.dosKptGrid = meshSpin_->value();
    pc.dosWidthCm = dosWidthSpin_->value();
    pc.acousticSumRule = acousticCheck_->isChecked();
    return QString::fromStdString(
        core::PhononScriptGenerator::generate(pc, "structure.extxyz"));
}

} // namespace calango::gui
