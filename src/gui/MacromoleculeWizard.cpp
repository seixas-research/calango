#include "gui/MacromoleculeWizard.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

MacromoleculeWizard::MacromoleculeWizard(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Macromolecule Builder"));
    resize(600, 500);

    auto* root = new QVBoxLayout(this);
    headerLabel_ = new QLabel(this);
    QFont headerFont = headerLabel_->font();
    headerFont.setPointSizeF(headerFont.pointSizeF() * 1.15);
    headerFont.setBold(true);
    headerLabel_->setFont(headerFont);
    root->addWidget(headerLabel_);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(buildBoxPage());
    stack_->addWidget(buildArchitecturePage());
    root->addWidget(stack_, 1);

    estimateLabel_ = new QLabel(this);
    estimateLabel_->setWordWrap(true);
    root->addWidget(estimateLabel_);

    auto* bar = new QHBoxLayout;
    backButton_ = new QPushButton(tr("‹ Back"), this);
    auto* cancelButton = new QPushButton(tr("Cancel"), this);
    nextButton_ = new QPushButton(tr("Next ›"), this);
    generateButton_ = new QPushButton(tr("Generate"), this);
    generateButton_->setDefault(true);
    bar->addWidget(backButton_);
    bar->addStretch(1);
    bar->addWidget(cancelButton);
    bar->addWidget(nextButton_);
    bar->addWidget(generateButton_);
    root->addLayout(bar);

    connect(backButton_, &QPushButton::clicked, this, &MacromoleculeWizard::goBack);
    connect(nextButton_, &QPushButton::clicked, this, &MacromoleculeWizard::goNext);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(generateButton_, &QPushButton::clicked, this,
            &MacromoleculeWizard::generate);

    updateControls();
    updateStage();
}

QWidget* MacromoleculeWizard::buildBoxPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* boxGroup = new QGroupBox(tr("Periodic Simulation Box"), page);
    auto* boxForm = new QFormLayout(boxGroup);

    densityTargetCheck_ =
        new QCheckBox(tr("Size the box from a density target"), boxGroup);
    densityTargetCheck_->setChecked(true);
    densityTargetCheck_->setToolTip(
        tr("On: the cubic box edge is derived from the total chain mass and "
           "the density below, so the cell is at the density you asked for.\n"
           "Off: the explicit edges are used and the resulting density is "
           "whatever the chains happen to give."));
    boxForm->addRow(densityTargetCheck_);
    connect(densityTargetCheck_, &QCheckBox::toggled, this,
            &MacromoleculeWizard::updateControls);

    densitySpin_ = new QDoubleSpinBox(boxGroup);
    densitySpin_->setRange(0.05, 5.0);
    densitySpin_->setDecimals(3);
    densitySpin_->setSingleStep(0.01);
    densitySpin_->setValue(0.92); // bulk polyethylene
    densitySpin_->setSuffix(tr(" g/cm³"));
    boxForm->addRow(tr("Target density ρ:"), densitySpin_);
    connect(densitySpin_, &QDoubleSpinBox::valueChanged, this,
            [this] { updateControls(); });

    auto* boxRow = new QHBoxLayout;
    const char* labels[3] = {"Lx", "Ly", "Lz"};
    for (int i = 0; i < 3; ++i) {
        boxRow->addWidget(new QLabel(QLatin1String(labels[i]), boxGroup));
        boxSpins_[i] = new QDoubleSpinBox(boxGroup);
        boxSpins_[i]->setRange(5.0, 500.0);
        boxSpins_[i]->setDecimals(1);
        boxSpins_[i]->setValue(30.0);
        boxSpins_[i]->setSuffix(tr(" Å"));
        boxRow->addWidget(boxSpins_[i]);
    }
    boxRow->addStretch(1);
    boxForm->addRow(tr("Box dimensions:"), boxRow);
    layout->addWidget(boxGroup);

    auto* packGroup = new QGroupBox(tr("Packing"), page);
    auto* packForm = new QFormLayout(packGroup);
    chainCountSpin_ = new QSpinBox(packGroup);
    chainCountSpin_->setRange(1, 500);
    chainCountSpin_->setValue(1);
    chainCountSpin_->setToolTip(
        tr("1 builds a single isolated chain; more packs an amorphous cell by "
           "Monte Carlo insertion with overlap rejection."));
    packForm->addRow(tr("Chains:"), chainCountSpin_);
    connect(chainCountSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this] { updateControls(); });

    minDistanceSpin_ = new QDoubleSpinBox(packGroup);
    minDistanceSpin_->setRange(1.0, 6.0);
    minDistanceSpin_->setDecimals(2);
    minDistanceSpin_->setSingleStep(0.1);
    minDistanceSpin_->setValue(2.2);
    minDistanceSpin_->setSuffix(tr(" Å"));
    minDistanceSpin_->setToolTip(
        tr("Minimum approach enforced between non-bonded atoms. Below about "
           "2 Å the cell will not survive its first minimization step."));
    packForm->addRow(tr("Minimum atom distance:"), minDistanceSpin_);

    seedSpin_ = new QSpinBox(packGroup);
    seedSpin_->setRange(0, 1000000);
    seedSpin_->setValue(42);
    packForm->addRow(tr("Random seed:"), seedSpin_);
    layout->addWidget(packGroup);
    layout->addStretch(1);
    return page;
}

QWidget* MacromoleculeWizard::buildArchitecturePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* chemistryGroup = new QGroupBox(tr("Monomer Chemistry"), page);
    auto* chemistryForm = new QFormLayout(chemistryGroup);
    monomerCombo_ = new QComboBox(chemistryGroup);
    // Order matches core::PolymerBuilder::Monomer.
    monomerCombo_->addItem(tr("Polyethylene  —[CH₂-CH₂]—"));
    monomerCombo_->addItem(tr("Polypropylene  —[CH₂-CH(CH₃)]—"));
    monomerCombo_->addItem(tr("Polystyrene  —[CH₂-CH(C₆H₅)]—"));
    monomerCombo_->addItem(tr("PTFE  —[CF₂-CF₂]—"));
    monomerCombo_->addItem(tr("Poly(vinyl chloride)  —[CH₂-CHCl]—"));
    monomerCombo_->addItem(tr("Nylon-6,6"));
    chemistryForm->addRow(tr("Monomer:"), monomerCombo_);
    connect(monomerCombo_, &QComboBox::currentIndexChanged, this,
            &MacromoleculeWizard::updateControls);

    degreeSpin_ = new QSpinBox(chemistryGroup);
    degreeSpin_->setRange(1, 5000);
    degreeSpin_->setValue(20);
    degreeSpin_->setToolTip(tr("Monomers per chain (N)."));
    chemistryForm->addRow(tr("Degree of polymerization N:"), degreeSpin_);
    connect(degreeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this] { updateControls(); });

    endCapCombo_ = new QComboBox(chemistryGroup);
    // Order matches core::PolymerBuilder::EndCap.
    endCapCombo_->addItem(tr("—H (hydrogen)"));
    endCapCombo_->addItem(tr("—CH₃ (methyl)"));
    endCapCombo_->addItem(tr("—OH (hydroxyl)"));
    endCapCombo_->setToolTip(
        tr("Caps the dangling valence at each chain end. Without one the "
           "fragment is a diradical, not a molecule."));
    chemistryForm->addRow(tr("End-group capping:"), endCapCombo_);
    layout->addWidget(chemistryGroup);

    auto* stereoGroup = new QGroupBox(tr("Stereochemistry && Conformation"), page);
    auto* stereoForm = new QFormLayout(stereoGroup);
    tacticityCombo_ = new QComboBox(stereoGroup);
    // Order matches core::PolymerBuilder::Tacticity.
    tacticityCombo_->addItem(tr("Isotactic (all substituents one side)"));
    tacticityCombo_->addItem(tr("Syndiotactic (alternating)"));
    tacticityCombo_->addItem(tr("Atactic (random)"));
    tacticityCombo_->setCurrentIndex(
        static_cast<int>(core::PolymerBuilder::Tacticity::Atactic));
    stereoForm->addRow(tr("Tacticity:"), tacticityCombo_);

    tacticityNoteLabel_ = new QLabel(stereoGroup);
    tacticityNoteLabel_->setWordWrap(true);
    stereoForm->addRow(tacticityNoteLabel_);

    conformationCombo_ = new QComboBox(stereoGroup);
    // Order matches core::PolymerBuilder::Conformation.
    conformationCombo_->addItem(tr("Extended (all-trans zig-zag)"));
    conformationCombo_->addItem(tr("Helical (fixed torsion)"));
    conformationCombo_->addItem(tr("Random walk"));
    conformationCombo_->addItem(tr("Self-avoiding walk (SAW)"));
    conformationCombo_->setCurrentIndex(
        static_cast<int>(core::PolymerBuilder::Conformation::SelfAvoidingWalk));
    conformationCombo_->setToolTip(
        tr("Extended is the crystalline chain. A random walk may pass through "
           "itself, giving overlapping atoms; the self-avoiding walk rejects "
           "those steps and is what an amorphous cell needs."));
    stereoForm->addRow(tr("Chain conformation:"), conformationCombo_);
    connect(conformationCombo_, &QComboBox::currentIndexChanged, this,
            &MacromoleculeWizard::updateControls);

    helixTorsionSpin_ = new QDoubleSpinBox(stereoGroup);
    helixTorsionSpin_->setRange(60.0, 300.0);
    helixTorsionSpin_->setDecimals(1);
    helixTorsionSpin_->setValue(165.0);
    helixTorsionSpin_->setSuffix(tr("°"));
    helixTorsionSpin_->setToolTip(
        tr("Backbone torsion per step. 180° is all-trans; PTFE's 15/7 helix "
           "sits near 165°."));
    stereoForm->addRow(tr("Helix torsion:"), helixTorsionSpin_);
    layout->addWidget(stereoGroup);
    layout->addStretch(1);
    return page;
}

void MacromoleculeWizard::updateControls()
{
    if (!monomerCombo_ || !densityTargetCheck_)
        return;
    const auto monomer =
        static_cast<core::PolymerBuilder::Monomer>(monomerCombo_->currentIndex());

    const bool useDensity = densityTargetCheck_->isChecked();
    if (densitySpin_) densitySpin_->setEnabled(useDensity);
    for (QDoubleSpinBox* spin : boxSpins_)
        if (spin)
            spin->setEnabled(!useDensity);

    // Tacticity is meaningless without a stereocentre: polyethylene and PTFE
    // carry two identical substituents on every backbone carbon, so there is
    // no "side" for a group to be on.
    const bool stereo = core::PolymerBuilder::hasTacticity(monomer);
    if (tacticityCombo_) tacticityCombo_->setEnabled(stereo);
    if (tacticityNoteLabel_) {
        tacticityNoteLabel_->setText(
            stereo ? tr("This monomer has a stereocentre, so tacticity applies.")
                   : tr("%1 has two identical substituents on every backbone "
                        "carbon — there is no stereocentre, so tacticity does "
                        "not apply.")
                         .arg(QString::fromStdString(
                             core::PolymerBuilder::toString(monomer))));
    }

    if (helixTorsionSpin_ && conformationCombo_)
        helixTorsionSpin_->setEnabled(
            conformationCombo_->currentIndex()
            == static_cast<int>(core::PolymerBuilder::Conformation::Helical));

    if (estimateLabel_ && degreeSpin_ && chainCountSpin_) {
        const double chainMass =
            core::PolymerBuilder::monomerMassU(monomer) * degreeSpin_->value();
        const double totalMass = chainMass * chainCountSpin_->value();
        QString text = tr("Estimated: %1 chain(s), %2 u per chain, %3 u total.")
                           .arg(chainCountSpin_->value())
                           .arg(chainMass, 0, 'f', 1)
                           .arg(totalMass, 0, 'f', 1);
        if (useDensity && densitySpin_) {
            const double volume =
                totalMass * 1.66053907 / densitySpin_->value();
            text += tr("  Box edge ≈ %1 Å.").arg(std::cbrt(volume), 0, 'f', 1);
        }
        estimateLabel_->setText(text);
    }
}

void MacromoleculeWizard::updateStage()
{
    stack_->setCurrentIndex(stage_);
    headerLabel_->setText(
        stage_ == 0 ? tr("Stage 1 of 2 — Simulation Box Configuration")
                    : tr("Stage 2 of 2 — Polymer Architecture & Tactics"));
    backButton_->setEnabled(stage_ > 0);
    nextButton_->setVisible(stage_ == 0);
    generateButton_->setVisible(stage_ == 1);
    updateControls();
}

void MacromoleculeWizard::goNext()
{
    if (stage_ < 1) {
        ++stage_;
        updateStage();
    }
}

void MacromoleculeWizard::goBack()
{
    if (stage_ > 0) {
        --stage_;
        updateStage();
    }
}

void MacromoleculeWizard::generate()
{
    core::PolymerBuilder::Params params;
    params.monomer =
        static_cast<core::PolymerBuilder::Monomer>(monomerCombo_->currentIndex());
    params.tacticity = static_cast<core::PolymerBuilder::Tacticity>(
        tacticityCombo_->currentIndex());
    params.conformation = static_cast<core::PolymerBuilder::Conformation>(
        conformationCombo_->currentIndex());
    params.endCap =
        static_cast<core::PolymerBuilder::EndCap>(endCapCombo_->currentIndex());
    params.degreeOfPolymerization = degreeSpin_->value();
    params.chainCount = chainCountSpin_->value();
    params.useDensityTarget = densityTargetCheck_->isChecked();
    params.densityGCm3 = densitySpin_->value();
    params.boxLx = boxSpins_[0]->value();
    params.boxLy = boxSpins_[1]->value();
    params.boxLz = boxSpins_[2]->value();
    params.minAtomDistance = minDistanceSpin_->value();
    params.helixTorsionDeg = helixTorsionSpin_->value();
    params.seed = static_cast<unsigned>(seedSpin_->value());

    try {
        result_ = core::PolymerBuilder::generate(params);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, windowTitle(), QString::fromUtf8(e.what()));
        return;
    }
    if (result_->chains == 0) {
        QMessageBox::warning(
            this, windowTitle(),
            tr("No chain could be placed without overlap. Lower the density, "
               "shorten the chains, or reduce the minimum atom distance."));
        result_.reset();
        return;
    }
    // A partial pack is still useful, but the user must know it happened —
    // silently returning fewer chains would change the density they asked for.
    if (result_->failedChains > 0) {
        QMessageBox::information(
            this, windowTitle(),
            tr("%1 of %2 chains could not be packed without overlap. The cell "
               "is at %3 g/cm³ rather than the target — lower the density or "
               "the minimum atom distance for a full pack.")
                .arg(result_->failedChains)
                .arg(params.chainCount)
                .arg(result_->densityGCm3, 0, 'f', 3));
    }
    accept();
}

} // namespace calango::gui
