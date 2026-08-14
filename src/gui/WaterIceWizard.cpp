#include "gui/WaterIceWizard.hpp"

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

namespace calango::gui {

WaterIceWizard::WaterIceWizard(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Water & Ice Generator"));
    resize(560, 460);

    auto* root = new QVBoxLayout(this);
    headerLabel_ = new QLabel(this);
    QFont headerFont = headerLabel_->font();
    headerFont.setPointSizeF(headerFont.pointSizeF() * 1.15);
    headerFont.setBold(true);
    headerLabel_->setFont(headerFont);
    root->addWidget(headerLabel_);

    stack_ = new QStackedWidget(this);
    // Phase first in construction order (the box page reads its controls to
    // decide what to show), but Box is stage 1 in the flow.
    stack_->addWidget(buildBoxPage());
    stack_->addWidget(buildPhasePage());
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

    connect(backButton_, &QPushButton::clicked, this, &WaterIceWizard::goBack);
    connect(nextButton_, &QPushButton::clicked, this, &WaterIceWizard::goNext);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(generateButton_, &QPushButton::clicked, this,
            &WaterIceWizard::generate);

    updatePhaseControls();
    updateStage();
}

QWidget* WaterIceWizard::buildBoxPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* crystalGroup = new QGroupBox(tr("Crystalline Cell Replication"), page);
    auto* crystalForm = new QFormLayout(crystalGroup);
    auto* replicateRow = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        replicateSpins_[i] = new QSpinBox(crystalGroup);
        replicateSpins_[i]->setRange(1, 20);
        replicateSpins_[i]->setValue(2);
        replicateRow->addWidget(replicateSpins_[i]);
        if (i < 2)
            replicateRow->addWidget(new QLabel(QStringLiteral("×"), crystalGroup));
        connect(replicateSpins_[i], QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this] { updatePhaseControls(); });
    }
    replicateRow->addStretch(1);
    crystalForm->addRow(tr("Replication (nx × ny × nz):"), replicateRow);
    layout->addWidget(crystalGroup);

    auto* liquidGroup = new QGroupBox(tr("Liquid Density Target"), page);
    auto* liquidForm = new QFormLayout(liquidGroup);
    moleculeSpin_ = new QSpinBox(liquidGroup);
    moleculeSpin_->setRange(1, 100000);
    moleculeSpin_->setValue(256);
    moleculeSpin_->setToolTip(
        tr("The box is sized to hold exactly this many molecules at the "
           "density below, so the two settings are never in conflict."));
    liquidForm->addRow(tr("H₂O molecules:"), moleculeSpin_);

    densitySpin_ = new QDoubleSpinBox(liquidGroup);
    densitySpin_->setRange(0.1, 3.0);
    densitySpin_->setDecimals(3);
    densitySpin_->setSingleStep(0.01);
    densitySpin_->setValue(0.997);
    densitySpin_->setSuffix(tr(" g/cm³"));
    liquidForm->addRow(tr("Target density:"), densitySpin_);

    minDistanceSpin_ = new QDoubleSpinBox(liquidGroup);
    minDistanceSpin_->setRange(1.5, 5.0);
    minDistanceSpin_->setDecimals(2);
    minDistanceSpin_->setSingleStep(0.1);
    minDistanceSpin_->setValue(2.6);
    minDistanceSpin_->setSuffix(tr(" Å"));
    minDistanceSpin_->setToolTip(
        tr("Minimum O–O separation while packing. 2.6 Å sits just inside the "
           "first peak of the real O–O radial distribution; raising it much "
           "further makes a dense box unpackable."));
    liquidForm->addRow(tr("Minimum O–O distance:"), minDistanceSpin_);
    layout->addWidget(liquidGroup);

    seedSpin_ = new QSpinBox(page);
    seedSpin_->setRange(0, 1000000);
    seedSpin_->setValue(42);
    seedSpin_->setToolTip(
        tr("Same seed, same structure — both the proton disorder and the "
           "liquid packing are reproducible."));
    auto* seedForm = new QFormLayout;
    seedForm->addRow(tr("Random seed:"), seedSpin_);
    layout->addLayout(seedForm);
    layout->addStretch(1);
    return page;
}

QWidget* WaterIceWizard::buildPhasePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    phaseCombo_ = new QComboBox(page);
    // Order matches core::IceBuilder::Phase.
    phaseCombo_->addItem(tr("Liquid Water (amorphous pack)"));
    phaseCombo_->addItem(tr("Ice Ih (hexagonal — ordinary ice)"));
    phaseCombo_->addItem(tr("Ice Ic (cubic)"));
    phaseCombo_->addItem(tr("Ice VII (high pressure, interpenetrating)"));
    phaseCombo_->setCurrentIndex(
        static_cast<int>(core::IceBuilder::Phase::IceIh));
    form->addRow(tr("Phase:"), phaseCombo_);
    connect(phaseCombo_, &QComboBox::currentIndexChanged, this,
            &WaterIceWizard::updatePhaseControls);

    geometryCombo_ = new QComboBox(page);
    // Order matches core::IceBuilder::WaterGeometry.
    geometryCombo_->addItem(tr("Rigid H₂O (0.9572 Å, 104.52°)"));
    geometryCombo_->addItem(tr("TIP3P (0.9572 Å, 104.52°)"));
    geometryCombo_->addItem(tr("TIP4P (0.9572 Å, 104.52°)"));
    geometryCombo_->addItem(tr("SPC/E (1.0 Å, 109.47°)"));
    geometryCombo_->setToolTip(
        tr("Structural preset only: the O–H length and H–O–H angle the "
           "molecules are built with. The charges and Lennard-Jones parameters "
           "that make these force fields are not written anywhere — they belong "
           "to the MD engine's topology, not to a coordinate file."));
    form->addRow(tr("Water geometry:"), geometryCombo_);

    phaseNoteLabel_ = new QLabel(page);
    phaseNoteLabel_->setWordWrap(true);
    layout->addWidget(phaseNoteLabel_);
    layout->addStretch(1);
    return page;
}

void WaterIceWizard::updatePhaseControls()
{
    if (!phaseCombo_)
        return;
    const auto phase =
        static_cast<core::IceBuilder::Phase>(phaseCombo_->currentIndex());
    const bool liquid = phase == core::IceBuilder::Phase::LiquidWater;

    for (QSpinBox* spin : replicateSpins_)
        if (spin)
            spin->setEnabled(!liquid);
    if (moleculeSpin_) moleculeSpin_->setEnabled(liquid);
    if (densitySpin_) densitySpin_->setEnabled(liquid);
    if (minDistanceSpin_) minDistanceSpin_->setEnabled(liquid);

    if (phaseNoteLabel_) {
        phaseNoteLabel_->setText(
            liquid
                ? tr("Molecules are packed at random orientations subject to a "
                     "minimum O–O separation. This is a starting configuration "
                     "for equilibration, not an equilibrated liquid — run MD "
                     "before measuring anything from it.")
                : tr("The protons are placed by solving the Bernal-Fowler ice "
                     "rules (two per oxygen, one per O–O bond) on the hydrogen-"
                     "bond graph, then randomized over ice-rule-preserving "
                     "cycle flips. Each seed gives a different proton "
                     "arrangement on the same oxygen lattice — which is what "
                     "proton disorder means."));
    }

    if (estimateLabel_) {
        if (liquid) {
            estimateLabel_->setText(
                tr("Estimated: %1 molecules (%2 atoms) at %3 g/cm³.")
                    .arg(moleculeSpin_->value())
                    .arg(moleculeSpin_->value() * 3)
                    .arg(densitySpin_->value(), 0, 'f', 3));
        } else {
            // Molecules per conventional cell, by phase.
            const int perCell = phase == core::IceBuilder::Phase::IceIh ? 4
                : phase == core::IceBuilder::Phase::IceIc               ? 8
                                                                        : 16;
            const int cells = replicateSpins_[0]->value()
                * replicateSpins_[1]->value() * replicateSpins_[2]->value();
            estimateLabel_->setText(
                tr("Estimated: %1 molecules (%2 atoms).")
                    .arg(perCell * cells)
                    .arg(perCell * cells * 3));
        }
    }
}

void WaterIceWizard::updateStage()
{
    stack_->setCurrentIndex(stage_);
    headerLabel_->setText(stage_ == 0
                              ? tr("Stage 1 of 2 — Box & Domain Setup")
                              : tr("Stage 2 of 2 — Water & Ice Phase Selection"));
    backButton_->setEnabled(stage_ > 0);
    nextButton_->setVisible(stage_ == 0);
    generateButton_->setVisible(stage_ == 1);
    updatePhaseControls();
}

void WaterIceWizard::goNext()
{
    if (stage_ < 1) {
        ++stage_;
        updateStage();
    }
}

void WaterIceWizard::goBack()
{
    if (stage_ > 0) {
        --stage_;
        updateStage();
    }
}

void WaterIceWizard::generate()
{
    core::IceBuilder::Params params;
    params.phase = static_cast<core::IceBuilder::Phase>(phaseCombo_->currentIndex());
    params.geometry =
        static_cast<core::IceBuilder::WaterGeometry>(geometryCombo_->currentIndex());
    params.nx = replicateSpins_[0]->value();
    params.ny = replicateSpins_[1]->value();
    params.nz = replicateSpins_[2]->value();
    params.densityGCm3 = densitySpin_->value();
    params.moleculeCount = moleculeSpin_->value();
    params.minOODistance = minDistanceSpin_->value();
    params.seed = static_cast<unsigned>(seedSpin_->value());

    try {
        result_ = core::IceBuilder::generate(params);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, windowTitle(), QString::fromUtf8(e.what()));
        return;
    }
    if (result_->iceRuleViolations > 0) {
        // Never ship a cell that violates the ice rules silently: the oxygens
        // would look right and the hydrogen bonding would be wrong.
        QMessageBox::warning(
            this, windowTitle(),
            tr("The proton solver left %1 O–O bond(s) violating the ice rules. "
               "This usually means the supercell is too small for the bond "
               "cutoff — enlarge it and try again.")
                .arg(result_->iceRuleViolations));
        result_.reset();
        return;
    }
    accept();
}

} // namespace calango::gui
