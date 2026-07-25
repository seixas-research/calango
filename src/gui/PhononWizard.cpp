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

QString PhononWizard::secondSettingsHeader() const
{
    return tr("Phonon Settings");
}

QWidget* PhononWizard::buildSecondSettingsPage()
{
    // Stage 2 — how the force constants are sampled. This is its own stage
    // rather than a group appended to Calculator Settings: the engine page is
    // already dense, and the supercell / δ / symmetry choices are what a user
    // revisits when a dispersion comes out wrong.
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    auto* group = new QGroupBox(tr("Finite Displacements"), page);
    auto* form = new QFormLayout(group);
    pageLayout->addWidget(group);

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

    symmetryCheck_ = new QCheckBox(tr("Symmetry-reduced displacements"), group);
    symmetryCheck_->setToolTip(
        tr("Displace only along the symmetry-irreducible directions and rebuild "
           "the full force-constant matrix by symmetry.\n"
           "The naive scheme costs 6N force evaluations (±δ along x, y, z for "
           "every atom); most of those are related by the crystal's space "
           "group. Spglib (through phonopy) finds the space group and the "
           "site-symmetry irreps, which for a high-symmetry cell can cut the "
           "count by an order of magnitude.\n"
           "Needs phonopy in the job environment — the script falls back to "
           "the full 6N set if it is missing."));
    symmetryCheck_->setEnabled(periodic_);
    form->addRow(symmetryCheck_);

    residualCheck_ = new QCheckBox(tr("Remove residual forces"), group);
    residualCheck_->setToolTip(
        tr("Compute the forces on the un-displaced geometry and subtract them "
           "from every displaced configuration.\n"
           "A relaxation stops at a finite fmax, so the reference geometry "
           "still carries small forces; left in, they add a spurious linear "
           "term to the force constants that shows up as non-zero acoustic "
           "frequencies at Γ. Costs one extra force evaluation."));
    form->addRow(residualCheck_);

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

    for (QDoubleSpinBox* spin : {deltaSpin_, dosWidthSpin_})
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    for (QSpinBox* spin : {meshSpin_, supercellSpins_[0], supercellSpins_[1],
                           supercellSpins_[2]})
        connect(spin, &QSpinBox::valueChanged, this,
                [this] { refreshPreview(); });
    for (QCheckBox* check : {acousticCheck_, symmetryCheck_, residualCheck_})
        connect(check, &QCheckBox::toggled, this, [this] { refreshPreview(); });

    pageLayout->addStretch(1);
    return page;
}

QWidget* PhononWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // Stage 3 is purely the q-path: the embedded Brillouin-zone builder, same
    // as the Electronic Structure wizard — a phonon dispersion ω(q) is read
    // along exactly the same high-symmetry lines as an electronic band
    // structure.
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
    // Single source of truth for path sampling, same as the Electronic
    // Structure wizard: the k-path builder's "points per segment" times the
    // number of segments. The old Stage-2 "Band-path points" box was a second,
    // independent control that could silently disagree with it.
    pc.bandPathPoints = kpath_ ? kpath_->pointsPerSegment() * kpath_->segmentCount()
                               : 100;
    if (kpath_)
        pc.kpath = kpath_->path().toStdString();
    pc.dosKptGrid = meshSpin_->value();
    pc.dosWidthCm = dosWidthSpin_->value();
    pc.acousticSumRule = acousticCheck_->isChecked();
    pc.symmetryReducedDisplacements = symmetryCheck_->isChecked();
    pc.removeResidualForces = residualCheck_->isChecked();
    return QString::fromStdString(
        core::PhononScriptGenerator::generate(pc, "structure.extxyz"));
}

} // namespace calango::gui
