#include "gui/RdfDialog.hpp"

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
#include <set>
#include <utility>

namespace calango::gui {

RdfDialog::RdfDialog(std::shared_ptr<const core::Structure> structure,
                     std::vector<std::shared_ptr<core::Structure>> frames,
                     QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
    , frames_(std::move(frames))
    , elementACombo_(new QComboBox(this))
    , elementBCombo_(new QComboBox(this))
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
    setWindowTitle(tr("Radial Distribution Function g(r)"));
    resize(760, 560);

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

    // Trajectory frame range: 1-based Start/End plus Stride; only shown
    // when the document actually carries multiple frames.
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
    form->addRow(tr("Element pair:"), pairRow);
    form->addRow(tr("r max:"), rMaxSpin_);
    form->addRow(tr("Bins:"), binsSpin_);
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
    exportButton_->setToolTip(tr("Save the g(r) curve as CSV or "
                                 "whitespace-separated .dat"));

    plot_->setAxisLabels(QStringLiteral("r [Å]"), QStringLiteral("g(r)"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(plot_, 1);
    layout->addWidget(buttons);

    connect(computeButton_, &QPushButton::clicked, this, &RdfDialog::compute);
    connect(exportButton_, &QPushButton::clicked, this, &RdfDialog::exportData);
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

    // Copy the selected frames (or the single structure) so the worker
    // thread owns its data outright — playback/edits can't race it.
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
            return core::computeRdfAveraged(selection, options);
        }));
}

void RdfDialog::computeFinished()
{
    lastResult_ = watcher_.result();
    plot_->setData(lastResult_.r, lastResult_.g);
    computeButton_->setEnabled(true);
    computeButton_->setText(tr("Compute"));
    exportButton_->setEnabled(!lastResult_.r.empty());
}

void RdfDialog::exportData()
{
    if (lastResult_.r.empty())
        return;

    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export g(r) Data"), QStringLiteral("rdf.csv"),
        tr("CSV (*.csv);;Data file (*.dat)"), &selectedFilter);
    if (path.isEmpty())
        return;
    const bool csv = !path.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export g(r) Data"),
                              tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    const QString pairA = elementACombo_->currentText();
    const QString pairB = elementBCombo_->currentText();
    if (csv) {
        // A CSV starts with its header row; the '#' provenance comments
        // stay with the .dat flavour below, where they are the convention.
        out << "r_angstrom,g_r\n";
        for (std::size_t i = 0; i < lastResult_.r.size(); ++i)
            out << QString::number(lastResult_.r[i], 'f', 6) << ','
                << QString::number(lastResult_.g[i], 'f', 6) << '\n';
    } else {
        out << "# Calango RDF, pair " << pairA << "-" << pairB
            << ", averaged over " << lastFrameCount_ << " frame(s)\n";
        out << "#      r[A]           g(r)\n";
        for (std::size_t i = 0; i < lastResult_.r.size(); ++i)
            out << QString::asprintf("%14.6f %14.6f\n", lastResult_.r[i],
                                     lastResult_.g[i]);
    }
}

} // namespace calango::gui
