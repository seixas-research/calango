#include "gui/GrapheneOxidePairCorrelationDialog.hpp"

#include "core/ThermodynamicIntegration.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/LinePlotWidget.hpp"

#include <QAbstractItemView>
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
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace calango::gui {

namespace {

/// A diverging blue-white-red map for alpha in [-1, 1] — the standard
/// "ordering / random / clustering" reading, clamped for anything that
/// (rarely, at low neighbor counts) falls outside that range.
QColor alphaColor(double alpha)
{
    const double t = std::clamp(alpha, -1.0, 1.0);
    if (t < 0.0) {
        const int mix = static_cast<int>(255 * (1.0 + t)); // t=-1 -> 0, t=0 -> 255
        return QColor(mix, mix, 255);
    }
    const int mix = static_cast<int>(255 * (1.0 - t));
    return QColor(255, mix, mix);
}

/// Counting-statistics standard error on alpha_ij for one shell — the same
/// formula tests/GrapheneOxidePairCorrelationTest.cpp cross-checks:
/// sigma_p = sqrt(p(1-p)/N), sigma_alpha = sigma_p / c_j. NaN (shown as "—")
/// when there is no data for this (i, j, shell) combination at all.
double countingError(const core::WarrenCowleyResult& wc, std::size_t shell,
                     std::size_t i, std::size_t j)
{
    if (shell >= wc.shells.size())
        return std::numeric_limits<double>::quiet_NaN();
    const auto& s = wc.shells[shell];
    if (i >= s.neighborsOfSpecies.size() || j >= wc.concentrations.size())
        return std::numeric_limits<double>::quiet_NaN();
    const double n = s.neighborsOfSpecies[i];
    const double cj = wc.concentrations[j];
    if (n <= 0.0 || cj <= 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    const double p = s.pairCounts[i][j] / n;
    const double sigmaP = std::sqrt(std::max(p * (1.0 - p), 0.0) / n);
    return sigmaP / cj;
}

} // namespace

GrapheneOxidePairCorrelationDialog::GrapheneOxidePairCorrelationDialog(
    std::shared_ptr<core::Structure> structure,
    std::vector<std::shared_ptr<core::Structure>> frames, QWidget* parent)
    : QDialog(parent)
    , structure_(std::move(structure))
    , frames_(std::move(frames))
{
    setWindowTitle(tr("GO Pair Correlation"));
    resize(780, 720);

    auto* layout = new QVBoxLayout(this);

    auto* topRow = new QHBoxLayout;
    auto* scopeGroup = new QGroupBox(tr("Scope"), this);
    auto* scopeLayout = new QVBoxLayout(scopeGroup);
    scopeCurrentRadio_ = new QRadioButton(tr("Current structure only"), scopeGroup);
    scopeCurrentRadio_->setChecked(true);
    scopeTrajectoryRadio_ =
        new QRadioButton(tr("Every frame of the loaded trajectory"), scopeGroup);
    const bool multiFrame = frames_.size() > 1;
    scopeTrajectoryRadio_->setEnabled(multiFrame);
    scopeTrajectoryRadio_->setToolTip(
        tr("Whether MC sampling is driving group ORDERING or CLUSTERING is a "
           "trajectory question: the evolution plot below (a selected "
           "alpha_ij vs. frame) is what answers it."));
    scopeLayout->addWidget(scopeCurrentRadio_);
    scopeLayout->addWidget(scopeTrajectoryRadio_);
    topRow->addWidget(scopeGroup, 1);

    auto* shellGroup = new QGroupBox(tr("Shells"), this);
    auto* shellForm = new QHBoxLayout(shellGroup);
    shellForm->addWidget(new QLabel(tr("Count:"), shellGroup));
    shellCountSpin_ = new QSpinBox(shellGroup);
    shellCountSpin_->setRange(1, 8);
    shellCountSpin_->setValue(3);
    shellCountSpin_->setToolTip(
        tr("Coordination shells of the honeycomb lattice, radius cutoffs "
           "discovered from a real pristine sheet's own bonding — shell 1 "
           "has 3 neighbors, shell 2 has 6, shell 3 has 3."));
    shellForm->addWidget(shellCountSpin_);
    topRow->addWidget(shellGroup);
    layout->addLayout(topRow);

    summaryLabel_ = new QLabel(
        tr("Compute to map functional-group decoration onto Warren-Cowley "
           "short-range order."),
        this);
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    // -- Shell-resolved matrix --------------------------------------------
    auto* matrixGroup = new QGroupBox(tr("alpha_ij matrix"), this);
    auto* matrixLayout = new QVBoxLayout(matrixGroup);
    auto* shellRow = new QHBoxLayout;
    shellRow->addWidget(new QLabel(tr("Shell:"), matrixGroup));
    shellCombo_ = new QComboBox(matrixGroup);
    shellRow->addWidget(shellCombo_, 1);
    matrixLayout->addLayout(shellRow);
    matrixTable_ = new QTableWidget(matrixGroup);
    matrixTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    matrixTable_->setSelectionMode(QAbstractItemView::NoSelection);
    matrixTable_->setToolTip(
        tr("Row = central species, column = neighbor species. Blue: "
           "alpha < 0, unlike-species pairs preferred (ordering/attraction). "
           "Red: alpha > 0, like-species pairs preferred (clustering). "
           "White: alpha = 0, random decoration."));
    matrixLayout->addWidget(matrixTable_, 1);
    layout->addWidget(matrixGroup, 2);
    connect(shellCombo_, &QComboBox::currentIndexChanged, this,
            &GrapheneOxidePairCorrelationDialog::rebuildMatrixView);

    // -- Evolution ----------------------------------------------------------
    auto* evolutionGroup = new QGroupBox(tr("Evolution (trajectory only)"), this);
    auto* evolutionLayout = new QVBoxLayout(evolutionGroup);
    auto* pairRow = new QHBoxLayout;
    pairRow->addWidget(new QLabel(tr("alpha ("), evolutionGroup));
    pairICombo_ = new QComboBox(evolutionGroup);
    pairRow->addWidget(pairICombo_);
    pairRow->addWidget(new QLabel(tr(","), evolutionGroup));
    pairJCombo_ = new QComboBox(evolutionGroup);
    pairRow->addWidget(pairJCombo_);
    pairRow->addWidget(new QLabel(tr(") vs. frame, shell 1:"), evolutionGroup));
    pairRow->addStretch(1);
    evolutionLayout->addLayout(pairRow);
    evolutionStatsLabel_ = new QLabel(evolutionGroup);
    evolutionStatsLabel_->setWordWrap(true);
    evolutionLayout->addWidget(evolutionStatsLabel_);
    evolutionPlot_ = new LinePlotWidget(evolutionGroup);
    evolutionPlot_->setAxisLabels(tr("frame"), tr("alpha_ij(shell 1)"));
    evolutionLayout->addWidget(evolutionPlot_, 1);
    layout->addWidget(evolutionGroup, 1);
    connect(pairICombo_, &QComboBox::currentIndexChanged, this,
            &GrapheneOxidePairCorrelationDialog::rebuildEvolutionPlot);
    connect(pairJCombo_, &QComboBox::currentIndexChanged, this,
            &GrapheneOxidePairCorrelationDialog::rebuildEvolutionPlot);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* computeButton =
        buttons->addButton(tr("Compute"), QDialogButtonBox::ActionRole);
    auto* csvButton =
        buttons->addButton(tr("Export CSV…"), QDialogButtonBox::ActionRole);
    auto* imageButton =
        buttons->addButton(tr("Export Plot…"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(computeButton, &QPushButton::clicked, this,
            &GrapheneOxidePairCorrelationDialog::compute);
    connect(csvButton, &QPushButton::clicked, this,
            &GrapheneOxidePairCorrelationDialog::exportCsv);
    connect(imageButton, &QPushButton::clicked, this,
            &GrapheneOxidePairCorrelationDialog::exportImage);
}

void GrapheneOxidePairCorrelationDialog::compute()
{
    results_.clear();
    shellCutoffsUsed_ = core::honeycombShellCutoffs(shellCountSpin_->value());

    if (shellCutoffsUsed_.empty()) {
        summaryLabel_->setText(
            tr("Could not resolve %1 shell(s) from the honeycomb lattice — "
               "try fewer.")
                .arg(shellCountSpin_->value()));
        matrixTable_->setRowCount(0);
        matrixTable_->setColumnCount(0);
        return;
    }

    const auto analyze = [this](const std::shared_ptr<core::Structure>& frame) {
        if (frame)
            results_.push_back(
                core::analyzeGrapheneOxidePairCorrelation(*frame, shellCutoffsUsed_));
    };
    if (scopeTrajectoryRadio_->isChecked() && !frames_.empty()) {
        for (const auto& frame : frames_)
            analyze(frame);
    } else {
        analyze(structure_);
    }

    rebuildPairCombos();
    rebuildMatrixView();
    rebuildEvolutionPlot();

    if (results_.empty() || results_.back().wc.species.empty()) {
        summaryLabel_->setText(tr("No framework carbons to analyze."));
        return;
    }
    const auto& last = results_.back();
    QStringList concentrations;
    for (std::size_t i = 0; i < last.speciesNames.size(); ++i)
        concentrations << tr("%1 %2%")
                              .arg(QString::fromStdString(last.speciesNames[i]))
                              .arg(last.wc.concentrations[i] * 100.0, 0, 'f', 1);
    summaryLabel_->setText(
        tr("%1 frame(s) analyzed, %2 shell(s): %3.")
            .arg(results_.size())
            .arg(shellCutoffsUsed_.size())
            .arg(concentrations.join(QStringLiteral(", "))));
}

void GrapheneOxidePairCorrelationDialog::rebuildPairCombos()
{
    const int prevI = pairICombo_->currentIndex();
    const int prevJ = pairJCombo_->currentIndex();
    shellCombo_->clear();
    pairICombo_->clear();
    pairJCombo_->clear();
    if (results_.empty())
        return;
    const auto& last = results_.back();
    for (std::size_t k = 0; k < shellCutoffsUsed_.size(); ++k)
        shellCombo_->addItem(tr("Shell %1").arg(k + 1), static_cast<int>(k));
    for (std::size_t i = 0; i < last.speciesNames.size(); ++i) {
        const QString name = QString::fromStdString(last.speciesNames[i]);
        pairICombo_->addItem(name, static_cast<int>(i));
        pairJCombo_->addItem(name, static_cast<int>(i));
    }
    if (prevI >= 0 && prevI < pairICombo_->count())
        pairICombo_->setCurrentIndex(prevI);
    if (prevJ >= 0 && prevJ < pairJCombo_->count())
        pairJCombo_->setCurrentIndex(prevJ);
    else if (pairJCombo_->count() > 1)
        pairJCombo_->setCurrentIndex(1); // default to a genuine CROSS pair
}

void GrapheneOxidePairCorrelationDialog::rebuildMatrixView()
{
    if (results_.empty()) {
        matrixTable_->setRowCount(0);
        matrixTable_->setColumnCount(0);
        return;
    }
    const auto& last = results_.back();
    const int shellIndex = shellCombo_->currentData().isValid()
        ? shellCombo_->currentData().toInt()
        : 0;
    if (shellIndex < 0 || static_cast<std::size_t>(shellIndex) >= last.wc.shells.size())
        return;
    const auto& shell = last.wc.shells[static_cast<std::size_t>(shellIndex)];
    const auto n = last.speciesNames.size();

    matrixTable_->setRowCount(static_cast<int>(n));
    matrixTable_->setColumnCount(static_cast<int>(n));
    QStringList headers;
    for (const auto& name : last.speciesNames)
        headers << QString::fromStdString(name);
    matrixTable_->setHorizontalHeaderLabels(headers);
    matrixTable_->setVerticalHeaderLabels(headers);

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            const double alpha = shell.alpha[i][j];
            auto* item = new QTableWidgetItem();
            if (std::isnan(alpha)) {
                item->setText(QStringLiteral("—"));
            } else {
                const double sigma = countingError(last.wc,
                                                   static_cast<std::size_t>(shellIndex),
                                                   i, j);
                item->setText(std::isnan(sigma)
                                  ? QString::number(alpha, 'f', 3)
                                  : QStringLiteral("%1 ± %2")
                                        .arg(alpha, 0, 'f', 3)
                                        .arg(sigma, 0, 'f', 3));
                item->setBackground(alphaColor(alpha));
            }
            item->setTextAlignment(Qt::AlignCenter);
            matrixTable_->setItem(static_cast<int>(i), static_cast<int>(j), item);
        }
    }
    matrixTable_->resizeColumnsToContents();
}

void GrapheneOxidePairCorrelationDialog::rebuildEvolutionPlot()
{
    evolutionPlot_->clear();
    evolutionStatsLabel_->clear();
    if (results_.empty() || pairICombo_->count() == 0 || pairJCombo_->count() == 0)
        return;
    const std::size_t si = static_cast<std::size_t>(pairICombo_->currentIndex());
    const std::size_t sj = static_cast<std::size_t>(pairJCombo_->currentIndex());

    std::vector<double> x;
    std::vector<double> series;
    for (std::size_t f = 0; f < results_.size(); ++f) {
        const auto& wc = results_[f].wc;
        if (wc.shells.empty() || si >= wc.shells[0].alpha.size()
            || sj >= wc.shells[0].alpha[si].size())
            continue;
        const double alpha = wc.shells[0].alpha[si][sj];
        if (std::isnan(alpha))
            continue;
        x.push_back(static_cast<double>(f));
        series.push_back(alpha);
    }
    evolutionPlot_->setData(x, series);

    if (series.empty()) {
        evolutionStatsLabel_->setText(tr("No data for this pair at shell 1."));
        return;
    }
    if (series.size() < 2) {
        evolutionStatsLabel_->setText(
            tr("Single structure: alpha = %1 (see the counting-statistics "
               "error shown in the matrix above).")
                .arg(series.front(), 0, 'f', 3));
        return;
    }
    // Trajectory: autocorrelation-corrected standard error over frames
    // (core::analyseSeries(), the same block-averaging-plus-autocorrelation
    // convention core::ThermodynamicIntegration already established for
    // MD/MC series), with the plain block-averaged figure alongside it as
    // the requested independent cross-check.
    const core::TiSeriesStatistics stats = core::analyseSeries(series);
    evolutionStatsLabel_->setText(
        tr("%1 frame(s): mean = %2 ± %3 (autocorrelation time %4 frames; "
           "block-averaged error %5).")
            .arg(series.size())
            .arg(stats.mean, 0, 'f', 3)
            .arg(stats.standardError, 0, 'f', 3)
            .arg(stats.correlationTime, 0, 'f', 1)
            .arg(stats.blockStandardError, 0, 'f', 3));
}

void GrapheneOxidePairCorrelationDialog::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export GO Pair Correlation"),
        QStringLiteral("go_pair_correlation.csv"), tr("CSV file (*.csv)"));
    if (path.isEmpty())
        return;
    writeTextFile(this, path, [&](QTextStream& out) {
        out << "frame,shell,species_i,species_j,alpha,counting_error,"
               "concentration_i,concentration_j\n";
        for (std::size_t f = 0; f < results_.size(); ++f) {
            const auto& r = results_[f];
            for (std::size_t k = 0; k < r.wc.shells.size(); ++k) {
                const auto& shell = r.wc.shells[k];
                for (std::size_t i = 0; i < r.speciesNames.size(); ++i) {
                    for (std::size_t j = 0; j < r.speciesNames.size(); ++j) {
                        const double alpha = shell.alpha[i][j];
                        if (std::isnan(alpha))
                            continue;
                        out << f << ',' << (k + 1) << ','
                            << QString::fromStdString(r.speciesNames[i]) << ','
                            << QString::fromStdString(r.speciesNames[j]) << ','
                            << alpha << ',' << countingError(r.wc, k, i, j) << ','
                            << r.wc.concentrations[i] << ',' << r.wc.concentrations[j]
                            << '\n';
                    }
                }
            }
        }
    });
}

void GrapheneOxidePairCorrelationDialog::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export GO Pair Correlation Plot"),
        QStringLiteral("go_pair_correlation.png"), tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    const QPixmap image = evolutionPlot_->grab();
    if (!image.save(path))
        QMessageBox::warning(this, windowTitle(), tr("Could not save %1").arg(path));
}

} // namespace calango::gui
