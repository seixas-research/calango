#include "gui/GeometryOptimizationWizard.hpp"

#include "gui/GuiUtils.hpp"

#include "core/AseScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "gui/GeometryConstraintsDialog.hpp"

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
    cell_.updateEnabled();
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

    fmax_.build(form, page, tr("Force convergence (fmax):"));

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

    // Variable-cell relaxation. Built by the shared helper so this wizard and
    // the Cluster Expansion batch cannot drift apart on what "relax the cell"
    // means — a hull built from fixed-cell energies is not comparable to one
    // built from relaxed-cell energies, so the two have to offer the same
    // filters and the same masks.
    cell_.build(page, form, [this] { refreshPreview(); });
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
    c.fmax = fmax_.value();
    c.maxSteps = maxStepsSpin_->value();
    cell_.applyTo(c);
    // VASP's own internal route reads NSW from its own field, not this
    // stage's "Max relaxation steps" — that stage is exactly what
    // skipTaskSettingsStage() bypasses on this route, so its widgets sit at
    // their untouched defaults rather than a value the user actually chose.
    if (vaspInternalRelaxationSelected())
        c.maxSteps = vaspNsw();
    return c;
}

QString GeometryOptimizationWizard::generateScript() const
{
    return QString::fromStdString(
        core::AseScriptGenerator::generate(config(), "structure.extxyz"));
}

} // namespace calango::gui
