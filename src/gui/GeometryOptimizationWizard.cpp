#include "gui/GeometryOptimizationWizard.hpp"

#include "gui/GuiUtils.hpp"

#include "core/AseScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/GeometryConstraintsDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>


namespace calango::gui {

GeometryOptimizationWizard::GeometryOptimizationWizard(
    std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : GpawElectronicWizard(parent)
    , structure_(std::move(structure))
{
    buildUi();
    updateCellEnabled();
    electronic_.updateEnabled();
}

QString GeometryOptimizationWizard::wizardTitle() const
{
    return tr("Geometry Optimization Setup");
}

QStringList GeometryOptimizationWizard::calculatorElements() const
{
    return structureElements(structure_.get());
}

QString GeometryOptimizationWizard::settingsHeader() const
{
    return tr("Relaxation Settings");
}

QWidget* GeometryOptimizationWizard::buildSettingsPage()
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    optimizerCombo_ = new QComboBox(page);
    // Order mirrors core::Optimizer.
    optimizerCombo_->addItems({tr("BFGS (quasi-Newton, robust default)"),
                               tr("LBFGS (limited-memory, large systems)"),
                               tr("FIRE (inertial, no Hessian)"),
                               tr("GPMin (Gaussian-process, few steps)"),
                               tr("MDMin (velocity-quench MD)")});
    form->addRow(tr("Optimizer:"), optimizerCombo_);

    fmaxSpin_ = new QDoubleSpinBox(page);
    fmaxSpin_->setRange(0.001, 2.0);
    fmaxSpin_->setDecimals(3);
    fmaxSpin_->setSingleStep(0.01);
    fmaxSpin_->setValue(0.020);
    fmaxSpin_->setSuffix(tr(" eV/Å"));
    form->addRow(tr("Force convergence (fmax):"), fmaxSpin_);

    maxStepsSpin_ = new QSpinBox(page);
    maxStepsSpin_->setRange(1, 100000);
    maxStepsSpin_->setValue(500);
    form->addRow(tr("Max relaxation steps:"), maxStepsSpin_);

    // Frozen degrees of freedom. Behind a button because the editor is a pair
    // of tables — and directly above the cell controls because the two answer
    // the same question from opposite ends: what is this relaxation allowed to
    // move? A slab calculation almost always needs both (bottom layers held,
    // cell fixed).
    auto* constraintRow = new QHBoxLayout;
    auto* constraintButton = new QPushButton(tr("Geometry constraints…"), page);
    constraintButton->setToolTip(
        tr("Hold atoms — or single Cartesian directions of them — fixed during "
           "the relaxation. Select them individually, or by a region such as "
           "z < 5 Å (the bottom layers of a slab)."));
    connect(constraintButton, &QPushButton::clicked, this,
            &GeometryOptimizationWizard::editConstraints);
    constraintRow->addWidget(constraintButton);
    constraintSummary_ = new QLabel(page);
    constraintSummary_->setWordWrap(true);
    constraintRow->addWidget(constraintSummary_, 1);
    form->addRow(constraintRow);
    refreshConstraintSummary();

    relaxCellCheck_ = new QCheckBox(tr("Relax the unit cell (variable-cell)"), page);
    relaxCellCheck_->setToolTip(
        tr("Also optimize the lattice via an ASE cell filter."));
    form->addRow(relaxCellCheck_);
    connect(relaxCellCheck_, &QCheckBox::toggled, this,
            &GeometryOptimizationWizard::updateCellEnabled);

    cellFilterCombo_ = new QComboBox(page);
    cellFilterCombo_->addItem(tr("FrechetCellFilter (recommended)"));
    cellFilterCombo_->addItem(tr("UnitCellFilter"));
    form->addRow(tr("Cell filter:"), cellFilterCombo_);

    stressMaskCombo_ = new QComboBox(page);
    stressMaskCombo_->addItem(tr("Anisotropic (full stress)"));
    stressMaskCombo_->addItem(tr("Hydrostatic (isotropic)"));
    // A named preset rather than "set the six ticks yourself": relaxing a 2D
    // sheet in-plane is the single most common non-trivial mask, and getting
    // it wrong is silent — leaving zz free lets the vacuum gap collapse onto
    // the slab over a few dozen steps, which looks like a converging
    // relaxation until the structure is unrecognizable.
    stressMaskCombo_->addItem(tr("2Dxy (in-plane only)"));
    stressMaskCombo_->addItem(tr("Custom (Voigt mask)"));
    stressMaskCombo_->setItemData(
        2,
        tr("Relax only the components that live in the xy plane — xx, yy and "
           "xy — and hold zz, xz and yz fixed.\n\n"
           "For a slab or a 2D material with a vacuum gap along z: the "
           "in-plane lattice constants are physical and should relax, while "
           "the cell height is an arbitrary padding whose stress is "
           "meaningless. It also keeps the layer flat, since a free xz or yz "
           "shears the vacuum."),
        Qt::ToolTipRole);
    form->addRow(tr("Stress mask:"), stressMaskCombo_);
    connect(stressMaskCombo_, &QComboBox::currentIndexChanged, this,
            &GeometryOptimizationWizard::updateCellEnabled);

    // Custom Voigt mask [xx, yy, zz, yz, xz, xy]: tick the components to relax
    // (e.g. only zz for a 2D layered material / heterostructure).
    voigtRow_ = new QWidget(page);
    auto* voigtLayout = new QHBoxLayout(voigtRow_);
    voigtLayout->setContentsMargins(0, 0, 0, 0);
    const char* labels[6] = {"xx", "yy", "zz", "yz", "xz", "xy"};
    for (int i = 0; i < 6; ++i) {
        voigtChecks_[i] = new QCheckBox(QLatin1String(labels[i]), voigtRow_);
        voigtChecks_[i]->setChecked(true);
        voigtLayout->addWidget(voigtChecks_[i]);
    }
    form->addRow(tr("Voigt components:"), voigtRow_);
    updateCellEnabled();
    return page;
}

void GeometryOptimizationWizard::editConstraints()
{
    GeometryConstraintsDialog dialog(structure_, constraints_, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    constraints_ = dialog.constraints();
    refreshConstraintSummary();
    refreshPreview();
}

void GeometryOptimizationWizard::refreshConstraintSummary()
{
    if (!constraintSummary_)
        return;
    constraintSummary_->setText(constraintSummaryText(
        constraints_, tr("None — every atom relaxes freely.")));
}

void GeometryOptimizationWizard::updateCellEnabled()
{
    const bool relax = relaxCellCheck_->isChecked();
    cellFilterCombo_->setEnabled(relax);
    stressMaskCombo_->setEnabled(relax);

    const int mask = stressMaskCombo_->currentIndex();
    if (mask == kStressMask2Dxy) {
        // Voigt order is [xx, yy, zz, yz, xz, xy]: relax 0, 1 and 5; hold
        // 2, 3 and 4. Written into the ticks rather than only into config()
        // so the preset is VISIBLE — the row below is the read-out of what
        // "2Dxy" means, which is how a user learns the mask instead of
        // trusting a label.
        static constexpr bool kInPlane[6] = {true, true, false,
                                             false, false, true};
        for (int i = 0; i < 6; ++i) {
            const QSignalBlocker blocker(voigtChecks_[i]);
            voigtChecks_[i]->setChecked(kInPlane[i]);
        }
    }
    // Shown for both mask-carrying modes, editable only for Custom: under the
    // preset the ticks are a read-out, and letting them be edited would leave
    // the combo claiming "2Dxy" for an arbitrary mask.
    voigtRow_->setVisible(relax && mask >= kStressMask2Dxy);
    voigtRow_->setEnabled(relax && mask == kStressMaskCustom);
}

core::CalculatorConfig GeometryOptimizationWizard::config() const
{
    core::CalculatorConfig c = baseCalculatorConfig();
    c.task = core::TaskKind::GeometryOptimization;
    // The electronic knobs the shared GPAW form collects (smearing, SCF
    // tolerance / step cap, spin) drive the calculator block of the generated
    // script exactly as they do for a Single-Point run.
    electronic_.applyTo(c);
    c.constraints = constraints_;
    c.optimizer = static_cast<core::Optimizer>(optimizerCombo_->currentIndex());
    c.fmax = fmaxSpin_->value();
    c.maxSteps = maxStepsSpin_->value();
    c.relaxCell = relaxCellCheck_->isChecked();
    c.cellFilter = cellFilterCombo_->currentIndex() == 1
        ? core::CellFilter::UnitCell
        : core::CellFilter::FrechetCell;
    c.cellHydrostatic = stressMaskCombo_->currentIndex() == 1;
    // Both the 2Dxy preset and Custom write an explicit Voigt mask; they
    // differ only in who chose the six bits. updateCellEnabled() has already
    // put the preset's mask into the checkboxes, so one read serves both.
    c.cellCustomMask = stressMaskCombo_->currentIndex() >= kStressMask2Dxy;
    for (int i = 0; i < 6; ++i)
        c.cellMask[i] = voigtChecks_[i]->isChecked();
    return c;
}

QString GeometryOptimizationWizard::generateScript() const
{
    return QString::fromStdString(
        core::AseScriptGenerator::generate(config(), "structure.extxyz"));
}

} // namespace calango::gui
