#include "gui/GrapheneOxideGroupAnalysisDialog.hpp"

#include "core/GrapheneOxideBuilder.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/HistogramPlotWidget.hpp"
#include "gui/LinePlotWidget.hpp"
#include "gui/ViewportWidget.hpp"
#include "render/StructureRenderer.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace calango::gui {

namespace {

using Group = core::GrapheneOxideBuilder::Group;
using Analysis = core::GrapheneOxideGroupAnalysis;

/// The same fixed, conventional-chemistry-figure hues MainWindow's own
/// applyFunctionalGroupCasts()/grapheneOxideGroupCastColors() use — a
/// palette CONSTANT, not chemistry logic, so re-declaring it here (that
/// function is private to MainWindow.cpp) is not a second classification
/// implementation, just the same colours available to a second window.
std::array<QColor, core::GrapheneOxideBuilder::kGroupCount> groupCastColors()
{
    return {
        QColor(0xC2, 0x3B, 0x3B), // epoxide  -- muted red
        QColor(0x3E, 0x6F, 0xB5), // hydroxyl -- muted blue
        QColor(0x4C, 0x9A, 0x5C), // carboxyl -- muted green
        QColor(0xD1, 0x8A, 0x3E), // carbonyl -- muted orange
    };
}

QColor pristineCastColor()
{
    return QColor(0x8C, 0x8F, 0x94);
}

QColor highlightCastColor()
{
    return QColor(0xC2, 0x3B, 0x3B);
}

double meanOf(const std::vector<double>& values)
{
    if (values.empty())
        return 0.0;
    double sum = 0.0;
    for (const double v : values)
        sum += v;
    return sum / static_cast<double>(values.size());
}

const Analysis::Distribution* findDistribution(
    const std::vector<Analysis::Distribution>& list, const std::string& label)
{
    for (const auto& d : list)
        if (d.label == label)
            return &d;
    return nullptr;
}

} // namespace

GrapheneOxideGroupAnalysisDialog::GrapheneOxideGroupAnalysisDialog(
    std::shared_ptr<core::Structure> structure,
    std::vector<std::shared_ptr<core::Structure>> frames, ViewportWidget* viewport,
    QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
    , frames_(std::move(frames))
    , viewport_(viewport)
{
    setWindowTitle(tr("GO Functional Group Analysis"));
    resize(820, 720);

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
        tr("Runs the census and every geometric distribution on each frame — "
           "the evolution plots below (mean bond length / angle vs. frame) "
           "are what show whether an MDMC run is annealing the decoration "
           "toward or away from a particular arrangement."));
    scopeLayout->addWidget(scopeCurrentRadio_);
    scopeLayout->addWidget(scopeTrajectoryRadio_);
    layout->addWidget(scopeGroup);

    summaryLabel_ = new QLabel(
        tr("Compute to classify functional groups and measure the geometry "
           "around them."),
        this);
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);
    skippedLabel_ = new QLabel(this);
    skippedLabel_->setWordWrap(true);
    skippedLabel_->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(skippedLabel_);

    censusTable_ = new QTableWidget(this);
    censusTable_->setColumnCount(4);
    censusTable_->setHorizontalHeaderLabels(
        {tr("Group"), tr("Instances"), tr("Surface carbons"),
         tr("Surface conc.")});
    censusTable_->horizontalHeader()->setStretchLastSection(true);
    censusTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    censusTable_->setSelectionMode(QAbstractItemView::NoSelection);
    censusTable_->setMaximumHeight(190);
    layout->addWidget(censusTable_);

    // -- Geometry: one tab per environment, histogram(s) of the LAST frame
    //    plus an evolution (mean vs. frame) plot per environment. -------
    geometryTabs_ = new QTabWidget(this);
    const auto histogramRow =
        [this](std::initializer_list<std::pair<HistogramPlotWidget**, QString>> hists) {
            auto* row = new QWidget(geometryTabs_);
            auto* rowLayout = new QHBoxLayout(row);
            for (const auto& [slot, color] : hists) {
                auto* hist = new HistogramPlotWidget(row);
                hist->setBarColor(QColor(color));
                *slot = hist;
                rowLayout->addWidget(hist);
            }
            return row;
        };
    const auto evolutionRow =
        [this](std::initializer_list<LinePlotWidget**> plots) {
            auto* row = new QWidget(geometryTabs_);
            auto* rowLayout = new QHBoxLayout(row);
            for (LinePlotWidget** slot : plots) {
                auto* plot = new LinePlotWidget(row);
                plot->setAxisLabels(tr("frame"), tr("mean value"));
                *slot = plot;
                rowLayout->addWidget(plot);
            }
            return row;
        };
    const auto makeTab = [this](QWidget* histRow, QWidget* evolutionRow,
                                const QString& title) {
        auto* page = new QWidget(geometryTabs_);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->addWidget(histRow, 2);
        pageLayout->addWidget(evolutionRow, 1);
        geometryTabs_->addTab(page, title);
    };

    makeTab(histogramRow({{&ccPristineHist_, QStringLiteral("#1f77b4")},
                          {&ccFunctionalizedHist_, QStringLiteral("#d62728")}}),
           evolutionRow({&ccPristineEvolution_, &ccFunctionalizedEvolution_}),
           tr("C-C Bonds"));
    makeTab(histogramRow({{&cccPristineHist_, QStringLiteral("#1f77b4")},
                          {&cccFunctionalizedHist_, QStringLiteral("#d62728")}}),
           evolutionRow({&cccPristineEvolution_, &cccFunctionalizedEvolution_}),
           tr("C-C-C Angles"));
    makeTab(histogramRow({{&cocHist_, QStringLiteral("#2ca02c")}}),
           evolutionRow({&cocEvolution_}), tr("C-O-C Angle (epoxide)"));
    makeTab(histogramRow({{&cohHydroxylHist_, QStringLiteral("#3E6FB5")},
                          {&cohCarboxylHist_, QStringLiteral("#4C9A5C")}}),
           evolutionRow({&cohHydroxylEvolution_, &cohCarboxylEvolution_}),
           tr("C-O-H Angles"));

    ccPristineHist_->setLabels(tr("bond length (A)"), tr("count"));
    ccFunctionalizedHist_->setLabels(tr("bond length (A)"), tr("count"));
    cccPristineHist_->setLabels(tr("angle (deg)"), tr("count"));
    cccFunctionalizedHist_->setLabels(tr("angle (deg)"), tr("count"));
    cocHist_->setLabels(tr("angle (deg)"), tr("count"));
    cohHydroxylHist_->setLabels(tr("angle (deg)"), tr("count"));
    cohCarboxylHist_->setLabels(tr("angle (deg)"), tr("count"));
    ccPristineHist_->setPlaceholder(tr("No pristine C-C bonds in this frame"));
    ccFunctionalizedHist_->setPlaceholder(
        tr("No functionalized-adjacent C-C bonds in this frame"));
    cccPristineHist_->setPlaceholder(tr("No pristine-center C-C-C angles"));
    cccFunctionalizedHist_->setPlaceholder(
        tr("No functionalized-center C-C-C angles"));
    cocHist_->setPlaceholder(tr("No epoxides in this frame"));
    cohHydroxylHist_->setPlaceholder(tr("No hydroxyls in this frame"));
    cohCarboxylHist_->setPlaceholder(tr("No carboxyls in this frame"));
    layout->addWidget(geometryTabs_, 1);

    // -- Overlay ----------------------------------------------------------
    auto* overlayRow = new QHBoxLayout;
    overlayRow->addWidget(new QLabel(tr("Highlight:"), this));
    overlayCombo_ = new QComboBox(this);
    overlayCombo_->setToolTip(
        tr("Recolors the CURRENT structure: the chosen group type in one "
           "color, everything else in grey — always the current structure, "
           "even when the scope above is the whole trajectory."));
    overlayRow->addWidget(overlayCombo_, 1);
    layout->addLayout(overlayRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* computeButton =
        buttons->addButton(tr("Compute"), QDialogButtonBox::ActionRole);
    applyButton_ =
        buttons->addButton(tr("Apply Coloring"), QDialogButtonBox::ActionRole);
    applyButton_->setEnabled(false);
    auto* csvButton =
        buttons->addButton(tr("Export CSV…"), QDialogButtonBox::ActionRole);
    auto* imageButton =
        buttons->addButton(tr("Export Plots…"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(computeButton, &QPushButton::clicked, this,
            &GrapheneOxideGroupAnalysisDialog::compute);
    connect(applyButton_, &QPushButton::clicked, this,
            &GrapheneOxideGroupAnalysisDialog::applyColoring);
    connect(csvButton, &QPushButton::clicked, this,
            &GrapheneOxideGroupAnalysisDialog::exportCsv);
    connect(imageButton, &QPushButton::clicked, this,
            &GrapheneOxideGroupAnalysisDialog::exportImages);
}

int GrapheneOxideGroupAnalysisDialog::currentFrameIndex() const
{
    if (!structure_)
        return -1;
    for (std::size_t i = 0; i < frames_.size(); ++i)
        if (frames_[i].get() == structure_.get())
            return static_cast<int>(i);
    return -1;
}

void GrapheneOxideGroupAnalysisDialog::compute()
{
    results_.clear();
    if (scopeTrajectoryRadio_->isChecked() && !frames_.empty()) {
        for (const auto& frame : frames_)
            if (frame)
                results_.push_back(core::analyzeGrapheneOxideGroups(*frame));
    } else if (structure_) {
        results_.push_back(core::analyzeGrapheneOxideGroups(*structure_));
    }

    applyButton_->setEnabled(structure_ != nullptr);
    rebuildCensusTable();
    rebuildDistributionPlots();
    rebuildOverlayCombo();

    if (results_.empty()) {
        summaryLabel_->setText(tr("No atoms to analyze."));
        skippedLabel_->clear();
        return;
    }

    const Analysis& last = results_.back();
    int placedGroups = 0;
    for (const auto& g : last.groups)
        placedGroups += g.instances;
    summaryLabel_->setText(
        tr("%1 frame(s) analyzed. Last frame: %2 framework carbons, %3 "
           "pristine (%4%), %5 functional group instance(s), %6 above / %7 "
           "below plane, %8 antiposition pair(s).")
            .arg(results_.size())
            .arg(last.frameworkCarbons)
            .arg(last.pristineCarbons)
            .arg(last.pristineFraction() * 100.0, 0, 'f', 1)
            .arg(placedGroups)
            .arg(last.abovePlane)
            .arg(last.belowPlane)
            .arg(last.antipositionPairs));
    if (last.skippedForNoHydrogen.empty()) {
        skippedLabel_->clear();
    } else {
        QStringList notes;
        for (const auto& note : last.skippedForNoHydrogen)
            notes << QString::fromStdString(note);
        skippedLabel_->setText(tr("Skipped: %1").arg(notes.join(QStringLiteral("; "))));
    }
}

void GrapheneOxideGroupAnalysisDialog::rebuildCensusTable()
{
    static const std::array<Group, core::GrapheneOxideBuilder::kGroupCount> kGroups = {
        Group::Epoxide, Group::Hydroxyl, Group::Carboxyl, Group::Carbonyl};

    if (results_.empty()) {
        censusTable_->setRowCount(0);
        return;
    }
    const Analysis& last = results_.back();
    censusTable_->setRowCount(static_cast<int>(kGroups.size()) + 1);
    int row = 0;
    for (Group g : kGroups) {
        const auto& count = last.groups[static_cast<std::size_t>(g)];
        QString name = QString::fromLatin1(core::GrapheneOxideBuilder::name(g));
        name[0] = name[0].toUpper();
        int col = 0;
        censusTable_->setItem(row, col++, new QTableWidgetItem(name));
        censusTable_->setItem(
            row, col++, new QTableWidgetItem(QString::number(count.instances)));
        censusTable_->setItem(
            row, col++,
            new QTableWidgetItem(QString::number(count.surfaceCarbons)));
        censusTable_->setItem(
            row, col++,
            new QTableWidgetItem(
                QString::number(last.surfaceConcentration(g) * 100.0, 'f', 2)
                + QStringLiteral("%")));
        ++row;
    }
    censusTable_->setItem(row, 0, new QTableWidgetItem(tr("Pristine sp2 carbon")));
    censusTable_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("—")));
    censusTable_->setItem(row, 2,
                          new QTableWidgetItem(QString::number(last.pristineCarbons)));
    censusTable_->setItem(
        row, 3,
        new QTableWidgetItem(QString::number(last.pristineFraction() * 100.0, 'f', 2)
                             + QStringLiteral("%")));
    censusTable_->resizeColumnsToContents();
}

void GrapheneOxideGroupAnalysisDialog::rebuildDistributionPlots()
{
    const auto fillHistogram = [](HistogramPlotWidget* hist,
                                  const std::vector<Analysis::Distribution>& list,
                                  const std::string& label) {
        const auto* dist = findDistribution(list, label);
        hist->setSamples(dist ? dist->samples : std::vector<double>{}, 24);
    };
    if (!results_.empty()) {
        const Analysis& last = results_.back();
        fillHistogram(ccPristineHist_, last.ccBondLengths, "C-C (pristine)");
        fillHistogram(ccFunctionalizedHist_, last.ccBondLengths,
                      "C-C (functionalized-adjacent)");
        fillHistogram(cccPristineHist_, last.cccAngles, "C-C-C (pristine center)");
        fillHistogram(cccFunctionalizedHist_, last.cccAngles,
                      "C-C-C (functionalized center)");
        fillHistogram(cocHist_, last.cocAngles, "C-O-C (epoxide)");
        fillHistogram(cohHydroxylHist_, last.cohAngles, "C-O-H (hydroxyl)");
        fillHistogram(cohCarboxylHist_, last.cohAngles, "C-O-H (carboxyl)");
    } else {
        for (HistogramPlotWidget* hist :
             {ccPristineHist_, ccFunctionalizedHist_, cccPristineHist_,
              cccFunctionalizedHist_, cocHist_, cohHydroxylHist_, cohCarboxylHist_})
            hist->setSamples({}, 24);
    }

    // Evolution: mean of each environment's samples, one point per analyzed
    // frame. A frame with none of that environment simply has no point for
    // it (a gap, not a zero) -- a mean of zero samples is not a physical
    // zero and must not be plotted as one.
    const auto fillEvolution = [this](LinePlotWidget* plot,
                                      std::vector<Analysis::Distribution>
                                          Analysis::*field,
                                      const std::string& label) {
        std::vector<double> x;
        std::vector<double> y;
        for (std::size_t i = 0; i < results_.size(); ++i) {
            const auto* dist = findDistribution(results_[i].*field, label);
            if (dist && !dist->samples.empty()) {
                x.push_back(static_cast<double>(i));
                y.push_back(meanOf(dist->samples));
            }
        }
        plot->setData(std::move(x), std::move(y));
    };
    fillEvolution(ccPristineEvolution_, &Analysis::ccBondLengths, "C-C (pristine)");
    fillEvolution(ccFunctionalizedEvolution_, &Analysis::ccBondLengths,
                 "C-C (functionalized-adjacent)");
    fillEvolution(cccPristineEvolution_, &Analysis::cccAngles,
                 "C-C-C (pristine center)");
    fillEvolution(cccFunctionalizedEvolution_, &Analysis::cccAngles,
                 "C-C-C (functionalized center)");
    fillEvolution(cocEvolution_, &Analysis::cocAngles, "C-O-C (epoxide)");
    fillEvolution(cohHydroxylEvolution_, &Analysis::cohAngles, "C-O-H (hydroxyl)");
    fillEvolution(cohCarboxylEvolution_, &Analysis::cohAngles, "C-O-H (carboxyl)");
}

void GrapheneOxideGroupAnalysisDialog::rebuildOverlayCombo()
{
    overlayCombo_->clear();
    overlayCombo_->addItem(tr("All group kinds"), -2);
    overlayCombo_->addItem(tr("Pristine framework"), -1);
    for (std::size_t g = 0; g < core::GrapheneOxideBuilder::kGroupCount; ++g) {
        QString name = QString::fromLatin1(
            core::GrapheneOxideBuilder::name(static_cast<Group>(g)));
        name[0] = name[0].toUpper();
        overlayCombo_->addItem(name, static_cast<int>(g));
    }
}

void GrapheneOxideGroupAnalysisDialog::applyColoring()
{
    if (!structure_ || !viewport_)
        return;

    const std::vector<int> labels =
        core::GrapheneOxideBuilder::functionalGroupLabels(*structure_);
    if (labels.size() != structure_->size())
        return;
    const int selection = overlayCombo_->currentData().toInt();

    auto& style = viewport_->style();
    style.castStyles.clear();
    style.atomCasts.assign(labels.size(), 0);

    if (selection == -2) {
        // All kinds, same key applyFunctionalGroupCasts() builds.
        style.castName = tr("Pristine framework");
        style.castColor = pristineCastColor();
        const auto colors = groupCastColors();
        std::array<int, core::GrapheneOxideBuilder::kGroupCount> castOfGroup{};
        for (std::size_t g = 0; g < core::GrapheneOxideBuilder::kGroupCount; ++g) {
            if (std::find(labels.begin(), labels.end(), static_cast<int>(g))
                == labels.end())
                continue;
            render::StructureRenderer::CastStyle cast = style.castStyle(0);
            cast.castColor = colors[g];
            QString name = QString::fromLatin1(
                core::GrapheneOxideBuilder::name(static_cast<Group>(g)));
            name[0] = name[0].toUpper();
            cast.name = name;
            style.castStyles.push_back(cast);
            castOfGroup[g] = static_cast<int>(style.castStyles.size());
        }
        for (std::size_t i = 0; i < labels.size(); ++i)
            if (labels[i] >= 0)
                style.atomCasts[i] =
                    castOfGroup[static_cast<std::size_t>(labels[i])];
    } else {
        // One kind (or pristine) highlighted, everything else grey.
        style.castName = tr("Everything else");
        style.castColor = pristineCastColor();
        render::StructureRenderer::CastStyle highlight = style.castStyle(0);
        highlight.castColor = highlightCastColor();
        highlight.name = overlayCombo_->currentText();
        style.castStyles.push_back(highlight);
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const bool matches =
                selection == -1 ? labels[i] < 0 : labels[i] == selection;
            style.atomCasts[i] = matches ? 1 : 0;
        }
    }
    if (style.castStyles.empty()) {
        style.castName.clear();
    } else {
        style.colorMode = render::ColorMode::Cast;
        for (auto& cast : style.castStyles)
            cast.colorMode = render::ColorMode::Cast;
    }
    viewport_->styleChanged(/*rebuildGeometry=*/true);
    Q_EMIT castsApplied();
}

void GrapheneOxideGroupAnalysisDialog::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export GO Functional Group Analysis"),
        QStringLiteral("go_group_analysis.csv"), tr("CSV file (*.csv)"));
    if (path.isEmpty())
        return;

    writeTextFile(this, path, [&](QTextStream& out) {
        out << "frame,group,instances,surface_carbons,surface_concentration,"
               "pristine_carbons,framework_carbons,above_plane,below_plane,"
               "antiposition_pairs\n";
        for (std::size_t i = 0; i < results_.size(); ++i) {
            const Analysis& r = results_[i];
            for (std::size_t g = 0; g < core::GrapheneOxideBuilder::kGroupCount;
                 ++g) {
                const auto group = static_cast<Group>(g);
                out << i << ','
                    << core::GrapheneOxideBuilder::name(group) << ','
                    << r.groups[g].instances << ',' << r.groups[g].surfaceCarbons
                    << ',' << r.surfaceConcentration(group) << ','
                    << r.pristineCarbons << ',' << r.frameworkCarbons << ','
                    << r.abovePlane << ',' << r.belowPlane << ','
                    << r.antipositionPairs << '\n';
            }
        }
        out << "\nframe,distribution,n,mean\n";
        const auto writeDist =
            [&](const std::vector<Analysis::Distribution>& list, std::size_t frame) {
                for (const auto& d : list)
                    out << frame << ',' << QString::fromStdString(d.label) << ','
                        << d.samples.size() << ',' << meanOf(d.samples) << '\n';
            };
        for (std::size_t i = 0; i < results_.size(); ++i) {
            writeDist(results_[i].ccBondLengths, i);
            writeDist(results_[i].cccAngles, i);
            writeDist(results_[i].cocAngles, i);
            writeDist(results_[i].cohAngles, i);
        }
    });
}

void GrapheneOxideGroupAnalysisDialog::exportImages()
{
    if (!geometryTabs_->currentWidget())
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export GO Functional Group Analysis Plots"),
        QStringLiteral("go_group_analysis.png"), tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    // No shared renderTo() painter entry point on these widgets (same gap
    // RingPercolationDialog::exportImage() works around) -- grab the
    // currently-shown tab page as it stands on screen.
    const QPixmap image = geometryTabs_->currentWidget()->grab();
    if (!image.save(path))
        QMessageBox::warning(this, windowTitle(), tr("Could not save %1").arg(path));
}

} // namespace calango::gui
