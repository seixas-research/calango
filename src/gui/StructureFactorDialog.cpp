#include "gui/StructureFactorDialog.hpp"

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

#include <algorithm>
#include <utility>

namespace calango::gui {

StructureFactorDialog::StructureFactorDialog(
    std::shared_ptr<const core::Structure> structure,
    std::vector<std::shared_ptr<core::Structure>> frames, QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
    , frames_(std::move(frames))
    , qMinSpin_(new QDoubleSpinBox(this))
    , qMaxSpin_(new QDoubleSpinBox(this))
    , qPointsSpin_(new QSpinBox(this))
    , rMaxSpin_(new QDoubleSpinBox(this))
    , binsSpin_(new QSpinBox(this))
    , pbcCheck_(new QCheckBox(tr("Periodic boundary conditions"), this))
    , startFrameSpin_(new QSpinBox(this))
    , endFrameSpin_(new QSpinBox(this))
    , strideSpin_(new QSpinBox(this))
    , computeButton_(new QPushButton(tr("Compute"), this))
    , exportButton_(new QPushButton(tr("Export Data…"), this))
    , plot_(new LinePlotWidget(this))
{
    setWindowTitle(tr("Static Structure Factor S(q)"));
    resize(760, 560);

    qMinSpin_->setRange(0.05, 10.0);
    qMinSpin_->setDecimals(2);
    qMinSpin_->setSingleStep(0.05);
    qMinSpin_->setValue(0.3);
    qMinSpin_->setSuffix(tr(" Å⁻¹"));
    qMaxSpin_->setRange(1.0, 40.0);
    qMaxSpin_->setDecimals(2);
    qMaxSpin_->setValue(12.0);
    qMaxSpin_->setSuffix(tr(" Å⁻¹"));
    qPointsSpin_->setRange(50, 4000);
    qPointsSpin_->setValue(400);

    rMaxSpin_->setRange(2.0, 40.0);
    rMaxSpin_->setValue(10.0);
    rMaxSpin_->setSuffix(tr(" Å"));
    rMaxSpin_->setToolTip(tr("Upper bound of the g(r) Fourier integral — "
                             "larger values improve low-q fidelity"));
    binsSpin_->setRange(50, 4000);
    binsSpin_->setValue(400);

    const bool periodic = structure_->cell().isDefined()
        && (structure_->cell().pbc()[0] || structure_->cell().pbc()[1]
            || structure_->cell().pbc()[2]);
    pbcCheck_->setChecked(periodic);
    pbcCheck_->setEnabled(structure_->cell().isDefined());

    const int frameCount = static_cast<int>(frames_.size());
    auto* frameRow = new QHBoxLayout;
    startFrameSpin_->setRange(1, std::max(1, frameCount));
    startFrameSpin_->setValue(1);
    startFrameSpin_->setPrefix(tr("start "));
    endFrameSpin_->setRange(1, std::max(1, frameCount));
    endFrameSpin_->setValue(std::max(1, frameCount));
    endFrameSpin_->setPrefix(tr("end "));
    strideSpin_->setRange(1, std::max(1, frameCount));
    strideSpin_->setValue(1);
    strideSpin_->setPrefix(tr("stride "));
    frameRow->addWidget(startFrameSpin_, 1);
    frameRow->addWidget(endFrameSpin_, 1);
    frameRow->addWidget(strideSpin_, 1);
    connect(startFrameSpin_, &QSpinBox::valueChanged, this, [this](int start) {
        if (endFrameSpin_->value() < start)
            endFrameSpin_->setValue(start);
    });
    connect(endFrameSpin_, &QSpinBox::valueChanged, this, [this](int end) {
        if (startFrameSpin_->value() > end)
            startFrameSpin_->setValue(end);
    });

    auto* form = new QFormLayout;
    auto* qRow = new QHBoxLayout;
    qRow->addWidget(qMinSpin_, 1);
    qRow->addWidget(qMaxSpin_, 1);
    form->addRow(tr("q range:"), qRow);
    form->addRow(tr("q points:"), qPointsSpin_);
    form->addRow(tr("g(r) r max:"), rMaxSpin_);
    form->addRow(tr("g(r) bins:"), binsSpin_);
    form->addRow(pbcCheck_);
    if (frameCount > 1)
        form->addRow(tr("Frames:"), frameRow);
    else
        for (auto* spin : {startFrameSpin_, endFrameSpin_, strideSpin_})
            spin->setVisible(false);
    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(computeButton_, 1);
    buttonRow->addWidget(exportButton_, 1);
    form->addRow(buttonRow);
    exportButton_->setEnabled(false);

    plot_->setAxisLabels(QStringLiteral("q [Å⁻¹]"), QStringLiteral("S(q)"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(plot_, 1);
    layout->addWidget(buttons);

    connect(computeButton_, &QPushButton::clicked,
            this, &StructureFactorDialog::compute);
    connect(exportButton_, &QPushButton::clicked,
            this, &StructureFactorDialog::exportData);
    connect(&watcher_, &QFutureWatcher<core::StructureFactorResult>::finished,
            this, &StructureFactorDialog::computeFinished);

    compute(); // show something immediately
}

void StructureFactorDialog::compute()
{
    core::StructureFactorOptions options;
    options.qMin = qMinSpin_->value();
    options.qMax = std::max(qMaxSpin_->value(), qMinSpin_->value() + 0.5);
    options.qPoints = qPointsSpin_->value();
    options.rdf.rMax = rMaxSpin_->value();
    options.rdf.bins = binsSpin_->value();
    options.rdf.usePbc = pbcCheck_->isChecked();

    computeButton_->setEnabled(false);
    computeButton_->setText(tr("Computing…"));

    // Copy the selected frames so the worker thread owns its data.
    std::vector<core::Structure> selection;
    if (frames_.size() > 1) {
        const auto start = static_cast<std::size_t>(startFrameSpin_->value() - 1);
        const auto end = static_cast<std::size_t>(endFrameSpin_->value() - 1);
        const auto stride = static_cast<std::size_t>(strideSpin_->value());
        for (std::size_t i = start; i <= end && i < frames_.size(); i += stride)
            if (frames_[i])
                selection.push_back(*frames_[i]);
    }
    if (selection.empty())
        selection.push_back(*structure_);
    lastFrameCount_ = selection.size();

    watcher_.setFuture(QtConcurrent::run(
        [selection = std::move(selection), options] {
            return core::computeStructureFactorAveraged(selection, options);
        }));
}

void StructureFactorDialog::computeFinished()
{
    lastResult_ = watcher_.result();
    plot_->setData(lastResult_.q, lastResult_.s);
    computeButton_->setEnabled(true);
    computeButton_->setText(tr("Compute"));
    exportButton_->setEnabled(!lastResult_.q.empty());
}

void StructureFactorDialog::exportData()
{
    if (lastResult_.q.empty())
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export S(q) Data"), QStringLiteral("structure_factor.csv"),
        tr("CSV (*.csv);;Data file (*.dat)"));
    if (path.isEmpty())
        return;
    const bool csv = !path.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export S(q) Data"),
                              tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "# Calango static structure factor, averaged over " << lastFrameCount_
        << " frame(s), rmax " << QString::number(rMaxSpin_->value(), 'f', 2)
        << " A\n";
    if (csv) {
        out << "q_inv_angstrom,s_q\n";
        for (std::size_t i = 0; i < lastResult_.q.size(); ++i)
            out << QString::number(lastResult_.q[i], 'f', 6) << ','
                << QString::number(lastResult_.s[i], 'f', 6) << '\n';
    } else {
        for (std::size_t i = 0; i < lastResult_.q.size(); ++i)
            out << QString::asprintf("%14.6f %14.6f\n", lastResult_.q[i],
                                     lastResult_.s[i]);
    }
}

} // namespace calango::gui
