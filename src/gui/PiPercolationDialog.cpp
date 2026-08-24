#include "gui/PiPercolationDialog.hpp"

#include "gui/GrainCasts.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/LinePlotWidget.hpp"
#include "gui/ViewportWidget.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace calango::gui {

namespace {

/// Cast 0 is the built-in "everything else" bucket: non-carbon atoms and
/// every carbon that has lost its p_z — an oxidized one, or an unoxidized but
/// four-coordinate one. Domains start at cast 1, the same reservation
/// RingPercolationDialog and applyFunctionalGroupCasts() both make.
QColor sp3CastColor()
{
    return QColor(140, 140, 140);
}

} // namespace

PiPercolationDialog::PiPercolationDialog(
    std::shared_ptr<core::Structure> structure,
    std::vector<std::shared_ptr<core::Structure>> frames,
    ViewportWidget* viewport, QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
    , frames_(std::move(frames))
    , viewport_(viewport)
{
    setWindowTitle(tr("π Percolation Analysis"));
    resize(720, 640);

    auto* layout = new QVBoxLayout(this);

    auto* scopeGroup = new QGroupBox(tr("Scope"), this);
    auto* scopeLayout = new QVBoxLayout(scopeGroup);
    scopeCurrentRadio_ =
        new QRadioButton(tr("Current structure only"), scopeGroup);
    scopeCurrentRadio_->setChecked(true);
    scopeTrajectoryRadio_ =
        new QRadioButton(tr("Every frame of the loaded trajectory"), scopeGroup);
    scopeTrajectoryRadio_->setEnabled(frames_.size() > 1);
    scopeTrajectoryRadio_->setToolTip(
        tr("Runs the same analysis on each frame — the π-carbon fraction and "
           "largest-domain plots below are what connect the oxidation dose to "
           "the loss of a conjugated pathway across a GO/MCMD or GO/MC-Opt "
           "trajectory."));
    scopeLayout->addWidget(scopeCurrentRadio_);
    scopeLayout->addWidget(scopeTrajectoryRadio_);
    layout->addWidget(scopeGroup);

    summaryLabel_ = new QLabel(
        tr("Compute to find the conjugated carbon network and whether it "
           "crosses the cell."),
        this);
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    table_ = new QTableWidget(this);
    table_->setColumnCount(8);
    table_->setHorizontalHeaderLabels(
        {tr("Frame"), tr("Carbons"), tr("π carbons"), tr("π frac."),
         tr("Domains"), tr("Largest (atoms)"), tr("Percolates a/b"),
         tr("Percolates c")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(table_, 1);

    piFractionPlot_ = new LinePlotWidget(this);
    piFractionPlot_->setAxisLabels(tr("frame"), tr("π-carbon fraction"));
    layout->addWidget(piFractionPlot_, 1);

    largestDomainPlot_ = new LinePlotWidget(this);
    largestDomainPlot_->setAxisLabels(tr("frame"),
                                      tr("largest domain / π carbons"));
    layout->addWidget(largestDomainPlot_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* computeButton =
        buttons->addButton(tr("Compute"), QDialogButtonBox::ActionRole);
    applyButton_ =
        buttons->addButton(tr("Apply Coloring"), QDialogButtonBox::ActionRole);
    applyButton_->setEnabled(false);
    applyButton_->setToolTip(
        tr("Colors the CURRENT structure by conjugated domain (cast per "
           "domain, golden-angle hues) — sp3 carbons and anything not carbon "
           "stay in the default cast. Always the current structure, even when "
           "the scope above is the whole trajectory."));
    auto* csvButton =
        buttons->addButton(tr("Export CSV…"), QDialogButtonBox::ActionRole);
    auto* imageButton =
        buttons->addButton(tr("Export Plots…"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(computeButton, &QPushButton::clicked, this,
            &PiPercolationDialog::compute);
    connect(applyButton_, &QPushButton::clicked, this,
            &PiPercolationDialog::applyColoring);
    connect(csvButton, &QPushButton::clicked, this,
            &PiPercolationDialog::exportCsv);
    connect(imageButton, &QPushButton::clicked, this,
            &PiPercolationDialog::exportImage);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

int PiPercolationDialog::currentFrameIndex() const
{
    if (!structure_)
        return -1;
    for (std::size_t i = 0; i < frames_.size(); ++i)
        if (frames_[i].get() == structure_.get())
            return static_cast<int>(i);
    return -1;
}

void PiPercolationDialog::compute()
{
    results_.clear();
    if (scopeTrajectoryRadio_->isChecked() && !frames_.empty()) {
        for (const auto& frame : frames_) {
            if (frame)
                results_.push_back(core::analyzePiPercolation(*frame));
        }
    } else if (structure_) {
        results_.push_back(core::analyzePiPercolation(*structure_));
    }

    applyButton_->setEnabled(structure_ != nullptr);
    rebuildTable();
    rebuildPlots();

    if (results_.empty()) {
        summaryLabel_->setText(tr("No carbon framework to analyze."));
        return;
    }

    const core::PiPercolationResult& last = results_.back();
    const int percolating = (last.percolatesAxis[0] ? 1 : 0)
        + (last.percolatesAxis[1] ? 1 : 0) + (last.percolatesAxis[2] ? 1 : 0);
    summaryLabel_->setText(
        tr("%1 frame(s) analyzed. Last frame: %2 π carbon(s) (%3%), %4 "
           "conjugated domain(s), largest holds %5% of them, percolates %6 "
           "axis(axes).")
            .arg(results_.size())
            .arg(last.piCarbons.size())
            .arg(last.piCarbonFraction * 100.0, 0, 'f', 1)
            .arg(last.domains.size())
            .arg(last.largestDomainFraction * 100.0, 0, 'f', 1)
            .arg(percolating));
}

void PiPercolationDialog::rebuildTable()
{
    table_->setRowCount(static_cast<int>(results_.size()));
    for (std::size_t i = 0; i < results_.size(); ++i) {
        const core::PiPercolationResult& result = results_[i];
        const int row = static_cast<int>(i);
        // Carbon count is recovered from the fraction rather than recounted:
        // the analysis already divided by it, and a second count here could
        // disagree with the one the fraction was built from.
        const int carbons = result.piCarbonFraction > 0.0
            ? static_cast<int>(std::llround(result.piCarbons.size()
                                            / result.piCarbonFraction))
            : 0;
        const int largest = result.largestDomain >= 0
            ? static_cast<int>(
                  result.domains[static_cast<std::size_t>(result.largestDomain)]
                      .atoms.size())
            : 0;
        const auto axes = [](bool a, bool b) {
            return QStringLiteral("%1 / %2")
                .arg(a ? QObject::tr("yes") : QObject::tr("no"),
                     b ? QObject::tr("yes") : QObject::tr("no"));
        };
        const QStringList cells = {
            QString::number(i),
            QString::number(carbons),
            QString::number(result.piCarbons.size()),
            QString::number(result.piCarbonFraction, 'f', 3),
            QString::number(result.domains.size()),
            QString::number(largest),
            axes(result.percolatesAxis[0], result.percolatesAxis[1]),
            result.percolatesAxis[2] ? tr("yes") : tr("no"),
        };
        for (int column = 0; column < cells.size(); ++column)
            table_->setItem(row, column, new QTableWidgetItem(cells.at(column)));
    }
}

void PiPercolationDialog::rebuildPlots()
{
    std::vector<double> frameIndex(results_.size());
    std::vector<double> piFraction(results_.size());
    std::vector<double> largestFraction(results_.size());
    for (std::size_t i = 0; i < results_.size(); ++i) {
        frameIndex[i] = static_cast<double>(i);
        piFraction[i] = results_[i].piCarbonFraction;
        largestFraction[i] = results_[i].largestDomainFraction;
    }
    piFractionPlot_->setData(frameIndex, piFraction);
    largestDomainPlot_->setData(std::move(frameIndex),
                                std::move(largestFraction));
}

void PiPercolationDialog::applyColoring()
{
    if (!structure_ || !viewport_)
        return;

    const core::PiPercolationResult result =
        core::analyzePiPercolation(*structure_);

    // atomDomain is already index-aligned with the structure and already
    // says -1 for every atom outside the network, so this is a shift by one
    // rather than a second traversal — cast 0 is the "everything else"
    // bucket every atom starts in.
    std::vector<int> atomCasts(structure_->size(), 0);
    for (std::size_t i = 0;
         i < atomCasts.size() && i < result.atomDomain.size(); ++i) {
        if (result.atomDomain[i] >= 0)
            atomCasts[i] = result.atomDomain[i] + 1;
    }

    auto& style = viewport_->style();
    style.castStyles.clear();
    style.castColor = sp3CastColor();
    style.castName = tr("sp3 / non-carbon");
    for (std::size_t d = 0; d < result.domains.size(); ++d) {
        render::StructureRenderer::CastStyle cast = style.castStyle(0);
        cast.castColor = grainCastColor(static_cast<int>(d));
        cast.colorMode = render::ColorMode::Cast;
        cast.name = tr("π domain %1 (%2 atoms)")
                        .arg(d + 1)
                        .arg(result.domains[d].atoms.size());
        style.castStyles.push_back(cast);
    }
    style.atomCasts = std::move(atomCasts);
    style.colorMode = render::ColorMode::Cast;
    viewport_->styleChanged(/*rebuildGeometry=*/true);
    Q_EMIT castsApplied();
}

void PiPercolationDialog::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export π Percolation Data"),
        QStringLiteral("pi_percolation.csv"), tr("CSV file (*.csv)"));
    if (path.isEmpty())
        return;

    writeTextFile(this, path, [&](QTextStream& out) {
        out << "frame,carbons,pi_carbons,pi_fraction,domains,"
               "largest_domain_atoms,largest_domain_fraction,"
               "percolates_a,percolates_b,percolates_c\n";
        for (std::size_t i = 0; i < results_.size(); ++i) {
            const core::PiPercolationResult& result = results_[i];
            const int carbons = result.piCarbonFraction > 0.0
                ? static_cast<int>(std::llround(result.piCarbons.size()
                                                / result.piCarbonFraction))
                : 0;
            const int largest = result.largestDomain >= 0
                ? static_cast<int>(
                      result.domains[static_cast<std::size_t>(
                                         result.largestDomain)]
                          .atoms.size())
                : 0;
            out << i << ',' << carbons << ',' << result.piCarbons.size() << ','
                << result.piCarbonFraction << ',' << result.domains.size()
                << ',' << largest << ',' << result.largestDomainFraction << ','
                << (result.percolatesAxis[0] ? 1 : 0) << ','
                << (result.percolatesAxis[1] ? 1 : 0) << ','
                << (result.percolatesAxis[2] ? 1 : 0) << '\n';
        }
    });
}

void PiPercolationDialog::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export π Percolation Plots"),
        QStringLiteral("pi_percolation.png"), tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    // Same approach as RingPercolationDialog's own export: LinePlotWidget has
    // no shared renderTo() painter entry point, so the on-screen widgets are
    // grabbed and stacked.
    const QPixmap top = piFractionPlot_->grab();
    const QPixmap bottom = largestDomainPlot_->grab();
    QPixmap combined(std::max(top.width(), bottom.width()),
                     top.height() + bottom.height());
    combined.fill(Qt::white);
    QPainter painter(&combined);
    painter.drawPixmap(0, 0, top);
    painter.drawPixmap(0, top.height(), bottom);
    painter.end();
    if (!combined.save(path))
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not save %1").arg(path));
}

} // namespace calango::gui
