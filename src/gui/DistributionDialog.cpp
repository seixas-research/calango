#include "gui/DistributionDialog.hpp"

#include "gui/LinePlotWidget.hpp"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <set>

namespace calango::gui {

DistributionDialog::DistributionDialog(std::shared_ptr<const core::Structure> structure,
                                       QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
    , kindCombo_(new QComboBox(this))
    , elementACombo_(new QComboBox(this))
    , elementBCombo_(new QComboBox(this))
    , cutoffSpin_(new QDoubleSpinBox(this))
    , binsSpin_(new QSpinBox(this))
    , pbcCheck_(new QCheckBox(tr("Periodic boundary conditions"), this))
    , computeButton_(new QPushButton(tr("Compute"), this))
    , exportButton_(new QPushButton(tr("Export Data…"), this))
    , plot_(new LinePlotWidget(this))
{
    setWindowTitle(tr("Bond Length / Angle Distributions"));
    resize(760, 540);

    kindCombo_->addItems({tr("Bond length distribution"),
                          tr("Bond angle distribution (j–i–k)")});

    // Element filters: "Any" plus every element present.
    std::set<int> elements;
    for (const core::Atom& atom : structure_->atoms())
        elements.insert(atom.atomicNumber);
    for (QComboBox* combo : {elementACombo_, elementBCombo_}) {
        combo->addItem(tr("Any"), 0);
        for (const int z : elements)
            combo->addItem(QLatin1String(core::Elements::data(z).symbol), z);
    }

    cutoffSpin_->setRange(0.5, 12.0);
    cutoffSpin_->setDecimals(2);
    cutoffSpin_->setSingleStep(0.1);
    cutoffSpin_->setValue(3.0);
    cutoffSpin_->setSuffix(tr(" Å"));

    binsSpin_->setRange(10, 1000);
    binsSpin_->setValue(90);

    const bool periodic = structure_->cell().isDefined()
        && (structure_->cell().pbc()[0] || structure_->cell().pbc()[1]
            || structure_->cell().pbc()[2]);
    pbcCheck_->setChecked(periodic);
    pbcCheck_->setEnabled(structure_->cell().isDefined());

    auto* pairRow = new QHBoxLayout;
    pairRow->addWidget(elementACombo_, 1);
    pairRow->addWidget(elementBCombo_, 1);

    auto* form = new QFormLayout;
    form->addRow(tr("Distribution:"), kindCombo_);
    form->addRow(tr("Species (pair / center–neighbor):"), pairRow);
    form->addRow(tr("Cutoff:"), cutoffSpin_);
    form->addRow(tr("Bins:"), binsSpin_);
    form->addRow(pbcCheck_);
    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(computeButton_, 1);
    buttonRow->addWidget(exportButton_, 1);
    form->addRow(buttonRow);
    exportButton_->setEnabled(false);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(plot_, 1);
    layout->addWidget(buttons);

    connect(kindCombo_, &QComboBox::currentIndexChanged, this,
            &DistributionDialog::compute);
    connect(computeButton_, &QPushButton::clicked, this, &DistributionDialog::compute);
    connect(exportButton_, &QPushButton::clicked, this, &DistributionDialog::exportData);
    connect(&watcher_, &QFutureWatcher<core::HistogramResult>::finished,
            this, &DistributionDialog::computeFinished);

    compute(); // show something immediately
}

void DistributionDialog::compute()
{
    core::DistributionOptions options;
    options.cutoff = cutoffSpin_->value();
    options.bins = binsSpin_->value();
    options.usePbc = pbcCheck_->isChecked();
    options.elementA = elementACombo_->currentData().toInt();
    options.elementB = elementBCombo_->currentData().toInt();
    const bool angles = kindCombo_->currentIndex() == 1;
    lastWasAngles_ = angles;

    computeButton_->setEnabled(false);
    computeButton_->setText(tr("Computing…"));

    // Copy the structure so the worker thread owns its data outright.
    watcher_.setFuture(QtConcurrent::run([structure = *structure_, options, angles] {
        return angles ? core::computeBondAngleDistribution(structure, options)
                      : core::computeBondLengthDistribution(structure, options);
    }));
}

void DistributionDialog::computeFinished()
{
    lastResult_ = watcher_.result();
    plot_->setAxisLabels(lastWasAngles_ ? QStringLiteral("angle [°]")
                                        : QStringLiteral("r [Å]"),
                         tr("count"));
    plot_->setData(lastResult_.x, lastResult_.y);
    computeButton_->setEnabled(true);
    computeButton_->setText(tr("Compute"));
    exportButton_->setEnabled(!lastResult_.x.empty());
}

void DistributionDialog::exportData()
{
    if (lastResult_.x.empty())
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Distribution Data"),
        lastWasAngles_ ? QStringLiteral("bond_angles.csv")
                       : QStringLiteral("bond_lengths.csv"),
        tr("CSV (*.csv);;Data file (*.dat)"));
    if (path.isEmpty())
        return;
    const bool csv = !path.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export Distribution Data"),
                              tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "# Calango " << (lastWasAngles_ ? "bond angle" : "bond length")
        << " distribution, species " << elementACombo_->currentText() << "-"
        << elementBCombo_->currentText() << ", cutoff "
        << QString::number(cutoffSpin_->value(), 'f', 2) << " A\n";
    if (csv) {
        out << (lastWasAngles_ ? "angle_deg,count\n" : "r_angstrom,count\n");
        for (std::size_t i = 0; i < lastResult_.x.size(); ++i)
            out << QString::number(lastResult_.x[i], 'f', 6) << ','
                << QString::number(lastResult_.y[i], 'f', 1) << '\n';
    } else {
        for (std::size_t i = 0; i < lastResult_.x.size(); ++i)
            out << QString::asprintf("%14.6f %12.1f\n", lastResult_.x[i],
                                     lastResult_.y[i]);
    }
}

} // namespace calango::gui
