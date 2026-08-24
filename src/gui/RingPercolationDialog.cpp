#include "gui/RingPercolationDialog.hpp"

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

/// Cast 0 is the built-in "everything else" bucket (Structure::atoms() not
/// in any intact ring): non-carbon atoms, disrupted-ring carbons, and edge
/// carbons too under-coordinated to close a ring at all. Domains start at
/// cast 1 for the same reason applyFunctionalGroupCasts() reserves cast 0
/// for the pristine framework — it is the state every atom starts in.
QColor disruptedCastColor()
{
    return QColor(140, 140, 140);
}

} // namespace

RingPercolationDialog::RingPercolationDialog(
    std::shared_ptr<core::Structure> structure,
    std::vector<std::shared_ptr<core::Structure>> frames, ViewportWidget* viewport,
    QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
    , frames_(std::move(frames))
    , viewport_(viewport)
{
    setWindowTitle(tr("Aromatic Percolation Analysis"));
    resize(720, 640);

    auto* layout = new QVBoxLayout(this);

    // -- Scope ----------------------------------------------------------
    auto* scopeGroup = new QGroupBox(tr("Scope"), this);
    auto* scopeLayout = new QVBoxLayout(scopeGroup);
    scopeCurrentRadio_ = new QRadioButton(tr("Current structure only"), scopeGroup);
    scopeCurrentRadio_->setChecked(true);
    scopeTrajectoryRadio_ =
        new QRadioButton(tr("Every frame of the loaded trajectory"), scopeGroup);
    const bool multiFrame = frames_.size() > 1;
    scopeTrajectoryRadio_->setEnabled(multiFrame);
    scopeTrajectoryRadio_->setToolTip(
        tr("Runs the same analysis on each frame — the intact-ring fraction "
           "and largest-domain-span plots below are what connect the "
           "oxidation dose to the loss of a percolating sp2 pathway across "
           "a GO/MCMD or GO/MC-Opt trajectory."));
    scopeLayout->addWidget(scopeCurrentRadio_);
    scopeLayout->addWidget(scopeTrajectoryRadio_);
    layout->addWidget(scopeGroup);

    summaryLabel_ = new QLabel(
        tr("Compute to find six-membered carbon rings and their sp2 "
           "percolation."),
        this);
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    table_ = new QTableWidget(this);
    table_->setColumnCount(9);
    table_->setHorizontalHeaderLabels({tr("Frame"), tr("Rings"), tr("Intact"),
                                       tr("Intact frac."), tr("sp2 frac."),
                                       tr("Domains"), tr("Largest (rings)"),
                                       tr("Percolates a/b"), tr("Percolates c")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(table_, 1);

    intactFractionPlot_ = new LinePlotWidget(this);
    intactFractionPlot_->setAxisLabels(tr("frame"), tr("intact-ring fraction"));
    layout->addWidget(intactFractionPlot_, 1);

    largestDomainPlot_ = new LinePlotWidget(this);
    largestDomainPlot_->setAxisLabels(tr("frame"),
                                      tr("largest domain / total rings"));
    layout->addWidget(largestDomainPlot_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* computeButton =
        buttons->addButton(tr("Compute"), QDialogButtonBox::ActionRole);
    applyButton_ =
        buttons->addButton(tr("Apply Coloring"), QDialogButtonBox::ActionRole);
    applyButton_->setEnabled(false);
    applyButton_->setToolTip(
        tr("Colors the CURRENT structure by sp2 domain (cast per domain, "
           "golden-angle hues) — disrupted rings and anything not carbon "
           "stay in the default cast. Always the current structure, even "
           "when the scope above is the whole trajectory."));
    auto* csvButton =
        buttons->addButton(tr("Export CSV…"), QDialogButtonBox::ActionRole);
    auto* imageButton =
        buttons->addButton(tr("Export Plots…"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(computeButton, &QPushButton::clicked, this, &RingPercolationDialog::compute);
    connect(applyButton_, &QPushButton::clicked, this, &RingPercolationDialog::applyColoring);
    connect(csvButton, &QPushButton::clicked, this, &RingPercolationDialog::exportCsv);
    connect(imageButton, &QPushButton::clicked, this, &RingPercolationDialog::exportImage);
}

int RingPercolationDialog::currentFrameIndex() const
{
    if (!structure_)
        return -1;
    for (std::size_t i = 0; i < frames_.size(); ++i)
        if (frames_[i].get() == structure_.get())
            return static_cast<int>(i);
    return -1;
}

void RingPercolationDialog::compute()
{
    results_.clear();
    if (scopeTrajectoryRadio_->isChecked() && !frames_.empty()) {
        for (const auto& frame : frames_) {
            if (frame)
                results_.push_back(core::analyzeRingPercolation(*frame));
        }
    } else if (structure_) {
        results_.push_back(core::analyzeRingPercolation(*structure_));
    }

    applyButton_->setEnabled(structure_ != nullptr);
    rebuildTable();
    rebuildPlots();

    if (results_.empty()) {
        summaryLabel_->setText(tr("No carbon framework to analyze."));
        return;
    }

    const core::RingPercolationResult& last = results_.back();
    const int percolating = (last.percolatesAxis[0] ? 1 : 0)
        + (last.percolatesAxis[1] ? 1 : 0) + (last.percolatesAxis[2] ? 1 : 0);
    summaryLabel_->setText(
        tr("%1 frame(s) analyzed. Last frame: %2 rings, %3% intact, %4 sp2 "
           "domain(s), largest %5 ring(s), percolates %6 axis(axes).")
            .arg(results_.size())
            .arg(last.rings.size())
            .arg(last.intactRingFraction * 100.0, 0, 'f', 1)
            .arg(last.domains.size())
            .arg(last.largestDomain >= 0
                     ? static_cast<int>(last.domains[static_cast<std::size_t>(last.largestDomain)]
                                             .rings.size())
                     : 0)
            .arg(percolating));
}

void RingPercolationDialog::rebuildTable()
{
    table_->setRowCount(static_cast<int>(results_.size()));
    for (std::size_t i = 0; i < results_.size(); ++i) {
        const core::RingPercolationResult& result = results_[i];
        const int row = static_cast<int>(i);
        const int intactCount = std::count_if(
            result.rings.begin(), result.rings.end(),
            [](const core::CarbonRing& ring) { return ring.intact; });
        const int largestRings = result.largestDomain >= 0
            ? static_cast<int>(
                  result.domains[static_cast<std::size_t>(result.largestDomain)].rings.size())
            : 0;

        int col = 0;
        table_->setItem(row, col++, new QTableWidgetItem(QString::number(i)));
        table_->setItem(row, col++,
                        new QTableWidgetItem(QString::number(result.rings.size())));
        table_->setItem(row, col++, new QTableWidgetItem(QString::number(intactCount)));
        table_->setItem(row, col++,
                        new QTableWidgetItem(
                            QString::number(result.intactRingFraction * 100.0, 'f', 1)
                            + QStringLiteral("%")));
        table_->setItem(row, col++,
                        new QTableWidgetItem(
                            QString::number(result.sp2CarbonFraction * 100.0, 'f', 1)
                            + QStringLiteral("%")));
        table_->setItem(row, col++,
                        new QTableWidgetItem(QString::number(result.domains.size())));
        table_->setItem(row, col++, new QTableWidgetItem(QString::number(largestRings)));
        table_->setItem(
            row, col++,
            new QTableWidgetItem(QString(result.percolatesAxis[0] ? QStringLiteral("yes")
                                                                   : QStringLiteral("no"))
                                 + QStringLiteral(" / ")
                                 + (result.percolatesAxis[1] ? QStringLiteral("yes")
                                                              : QStringLiteral("no"))));
        table_->setItem(row, col++,
                        new QTableWidgetItem(result.percolatesAxis[2] ? tr("yes") : tr("no")));
    }
    table_->resizeColumnsToContents();
}

void RingPercolationDialog::rebuildPlots()
{
    std::vector<double> frameIndex(results_.size());
    std::vector<double> intactFraction(results_.size());
    std::vector<double> largestFraction(results_.size());
    for (std::size_t i = 0; i < results_.size(); ++i) {
        frameIndex[i] = static_cast<double>(i);
        intactFraction[i] = results_[i].intactRingFraction;
        const std::size_t total = results_[i].rings.size();
        const int largest = results_[i].largestDomain;
        const std::size_t largestRings = largest >= 0
            ? results_[i].domains[static_cast<std::size_t>(largest)].rings.size()
            : 0;
        largestFraction[i] = total > 0 ? static_cast<double>(largestRings) / total : 0.0;
    }
    intactFractionPlot_->setData(frameIndex, intactFraction);
    largestDomainPlot_->setData(std::move(frameIndex), std::move(largestFraction));
}

void RingPercolationDialog::applyColoring()
{
    if (!structure_ || !viewport_)
        return;

    const core::RingPercolationResult result = core::analyzeRingPercolation(*structure_);

    std::vector<int> atomCasts(structure_->size(), 0);
    // Every intact ring an atom belongs to shares a bond with every OTHER
    // intact ring that same atom belongs to (three hexagons meet at a
    // 3-coordinate carbon, and any two of them share one of its three
    // bonds directly) — so an atom's intact rings are always in the same
    // domain. Last-write-wins here is a formality, not a real conflict.
    for (const core::CarbonRing& ring : result.rings) {
        if (!ring.intact || ring.domain < 0)
            continue;
        const int slot = ring.domain + 1; // cast 0 stays "everything else"
        for (const int atom : ring.atoms)
            if (atom >= 0 && static_cast<std::size_t>(atom) < atomCasts.size())
                atomCasts[static_cast<std::size_t>(atom)] = slot;
    }

    auto& style = viewport_->style();
    style.castStyles.clear();
    style.castColor = disruptedCastColor();
    style.castName = tr("Disrupted / non-carbon");
    for (std::size_t d = 0; d < result.domains.size(); ++d) {
        render::StructureRenderer::CastStyle cast = style.castStyle(0);
        cast.castColor = grainCastColor(static_cast<int>(d));
        cast.colorMode = render::ColorMode::Cast;
        cast.name = tr("sp2 domain %1 (%2 rings)")
                        .arg(d + 1)
                        .arg(result.domains[d].rings.size());
        style.castStyles.push_back(cast);
    }
    style.atomCasts = std::move(atomCasts);
    style.colorMode = render::ColorMode::Cast;
    viewport_->styleChanged(/*rebuildGeometry=*/true);
    Q_EMIT castsApplied();
}

void RingPercolationDialog::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Ring/Percolation Data"),
        QStringLiteral("ring_percolation.csv"), tr("CSV file (*.csv)"));
    if (path.isEmpty())
        return;

    writeTextFile(this, path, [&](QTextStream& out) {
        out << "frame,rings,intact_rings,intact_fraction,sp2_fraction,domains,"
               "largest_domain_rings,percolates_a,percolates_b,percolates_c\n";
        for (std::size_t i = 0; i < results_.size(); ++i) {
            const core::RingPercolationResult& result = results_[i];
            const int intactCount = std::count_if(
                result.rings.begin(), result.rings.end(),
                [](const core::CarbonRing& ring) { return ring.intact; });
            const int largestRings = result.largestDomain >= 0
                ? static_cast<int>(
                      result.domains[static_cast<std::size_t>(result.largestDomain)]
                          .rings.size())
                : 0;
            out << i << ',' << result.rings.size() << ',' << intactCount << ','
                << result.intactRingFraction << ',' << result.sp2CarbonFraction << ','
                << result.domains.size() << ',' << largestRings << ','
                << (result.percolatesAxis[0] ? 1 : 0) << ','
                << (result.percolatesAxis[1] ? 1 : 0) << ','
                << (result.percolatesAxis[2] ? 1 : 0) << '\n';
        }
    });
}

void RingPercolationDialog::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Ring/Percolation Plots"),
        QStringLiteral("ring_percolation.png"), tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    // LinePlotWidget has no shared renderTo() painter entry point (unlike
    // the result-window plot widgets savePlotImage() targets), so this
    // grabs the on-screen widgets directly and stacks them — the same
    // approach VacfDialog::exportImage() uses for its own two LinePlotWidget
    // charts.
    const QPixmap top = intactFractionPlot_->grab();
    const QPixmap bottom = largestDomainPlot_->grab();
    QPixmap combined(std::max(top.width(), bottom.width()), top.height() + bottom.height());
    combined.fill(Qt::white);
    QPainter painter(&combined);
    painter.drawPixmap(0, 0, top);
    painter.drawPixmap(0, top.height(), bottom);
    painter.end();
    if (!combined.save(path))
        QMessageBox::warning(this, windowTitle(), tr("Could not save %1").arg(path));
}

} // namespace calango::gui
