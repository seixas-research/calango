#include "gui/RdfDialog.hpp"

#include "gui/LinePlotWidget.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <set>

namespace calango::gui {

RdfDialog::RdfDialog(std::shared_ptr<const core::Structure> structure, QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
    , elementACombo_(new QComboBox(this))
    , elementBCombo_(new QComboBox(this))
    , rMaxSpin_(new QDoubleSpinBox(this))
    , binsSpin_(new QSpinBox(this))
    , pbcCheck_(new QCheckBox(tr("Periodic boundary conditions"), this))
    , computeButton_(new QPushButton(tr("Compute"), this))
    , plot_(new LinePlotWidget(this))
{
    setWindowTitle(tr("Radial Distribution Function g(r)"));
    resize(760, 540);

    // Element pair: "Total" plus every element present in the structure.
    std::set<int> elements;
    for (const core::Atom& atom : structure_->atoms())
        elements.insert(atom.atomicNumber);
    for (QComboBox* combo : {elementACombo_, elementBCombo_}) {
        combo->addItem(tr("Any"), 0);
        for (const int z : elements)
            combo->addItem(QLatin1String(core::Elements::data(z).symbol), z);
    }

    rMaxSpin_->setRange(1.0, 40.0);
    rMaxSpin_->setValue(10.0);
    rMaxSpin_->setSuffix(tr(" Å"));
    binsSpin_->setRange(20, 2000);
    binsSpin_->setValue(200);

    // PBC defaults ON when the structure is periodic, OFF otherwise —
    // and stays user-overridable.
    const bool periodic = structure_->cell().isDefined()
        && (structure_->cell().pbc()[0] || structure_->cell().pbc()[1]
            || structure_->cell().pbc()[2]);
    pbcCheck_->setChecked(periodic);
    pbcCheck_->setEnabled(structure_->cell().isDefined());

    auto* pairRow = new QHBoxLayout;
    pairRow->addWidget(elementACombo_, 1);
    pairRow->addWidget(elementBCombo_, 1);

    auto* form = new QFormLayout;
    form->addRow(tr("Element pair:"), pairRow);
    form->addRow(tr("r max:"), rMaxSpin_);
    form->addRow(tr("Bins:"), binsSpin_);
    form->addRow(pbcCheck_);
    form->addRow(computeButton_);

    plot_->setAxisLabels(QStringLiteral("r [Å]"), QStringLiteral("g(r)"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(plot_, 1);
    layout->addWidget(buttons);

    connect(computeButton_, &QPushButton::clicked, this, &RdfDialog::compute);
    connect(&watcher_, &QFutureWatcher<core::RdfResult>::finished,
            this, &RdfDialog::computeFinished);

    compute(); // show something immediately
}

void RdfDialog::compute()
{
    core::RdfOptions options;
    options.rMax = rMaxSpin_->value();
    options.bins = binsSpin_->value();
    options.usePbc = pbcCheck_->isChecked();
    options.elementA = elementACombo_->currentData().toInt();
    options.elementB = elementBCombo_->currentData().toInt();

    computeButton_->setEnabled(false);
    computeButton_->setText(tr("Computing…"));

    // Copy the structure so the worker thread owns its data outright.
    watcher_.setFuture(QtConcurrent::run(
        [structure = *structure_, options] { return core::computeRdf(structure, options); }));
}

void RdfDialog::computeFinished()
{
    const core::RdfResult result = watcher_.result();
    plot_->setData(result.r, result.g);
    computeButton_->setEnabled(true);
    computeButton_->setText(tr("Compute"));
}

} // namespace calango::gui
