#include "gui/EgqcaWindow.hpp"

#include "gui/EgqcaPlotWidget.hpp"
#include "gui/GuiUtils.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace calango::gui {

namespace {

/// Blue (low T) to red (high T), matching the working paper's own Fig. 2c /
/// 2e / 2f colour convention for a multi-temperature family of curves.
QColor temperatureColor(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return QColor(static_cast<int>(std::lround(40 + t * 190)), // R
                  static_cast<int>(std::lround(60 + (1.0 - std::abs(t - 0.5) * 2) * 60)), // G
                  static_cast<int>(std::lround(230 - t * 190))); // B
}

} // namespace

EgqcaWindow::EgqcaWindow(const QString& directory, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("EGQCA — Alloy Thermodynamics"));
    resize(1000, 680);

    auto* layout = new QVBoxLayout(this);

    // -- Load the ensemble ---------------------------------------------------
    const QJsonObject root =
        readJsonObject(directory + QStringLiteral("/cluster_expansion.json"));
    QStringList species;
    for (const QJsonValue& sp : root[QStringLiteral("species")].toArray())
        species << sp.toString();

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    if (species.size() != 2) {
        statusLabel_->setText(
            tr("EGQCA is a binary theory (the working paper's own scope — "
               "Sec. 2). This ensemble has %1 species; EGQCA cannot use it.")
                .arg(species.size()));
        return;
    }

    int pureAIndex = -1, pureBIndex = -1;
    const QJsonArray configs = root[QStringLiteral("configurations")].toArray();
    int sitesPerCluster = -1;
    for (const QJsonValue& entry : configs) {
        const QJsonObject config = entry.toObject();
        const QJsonValue degeneracy = config[QStringLiteral("degeneracy")];
        const QJsonValue energy = config[QStringLiteral("energy_per_atom")];
        if (degeneracy.isNull() || degeneracy.isUndefined() || energy.isNull()
            || energy.isUndefined())
            continue;
        const int natoms = config[QStringLiteral("natoms")].toInt(0);
        if (natoms <= 0)
            continue;
        if (sitesPerCluster < 0)
            sitesPerCluster = natoms;
        else if (natoms != sitesPerCluster)
            continue; // a mixed-size ensemble — not a single cluster class set

        const QJsonObject composition =
            config[QStringLiteral("composition")].toObject();
        const double fractionB = composition[species.at(1)].toDouble();
        core::EgqcaCluster cluster;
        cluster.bAtomCount =
            static_cast<int>(std::lround(fractionB * natoms));
        cluster.degeneracy = degeneracy.toInt();
        cluster.energyEv = energy.toDouble() * natoms;
        cluster.label = config[QStringLiteral("formula")].toString().toStdString();
        if (cluster.bAtomCount == 0)
            pureAIndex = static_cast<int>(input_.clusters.size());
        if (cluster.bAtomCount == sitesPerCluster)
            pureBIndex = static_cast<int>(input_.clusters.size());
        input_.clusters.push_back(cluster);
    }
    input_.sitesPerCluster = std::max(0, sitesPerCluster);

    if (input_.clusters.empty() || pureAIndex < 0 || pureBIndex < 0) {
        statusLabel_->setText(
            tr("This ensemble has no usable clusters — every configuration "
               "needs a degeneracy (rebuild it with a Calango version that "
               "writes one) and an energy, and the ensemble must include "
               "both pure end-members."));
        return;
    }
    hasClusters_ = true;
    statusLabel_->setText(
        tr("%1 clusters loaded (%2-%3, n = %4 sites). Set the ranges and "
           "press Solve.")
            .arg(input_.clusters.size())
            .arg(species.at(0), species.at(1))
            .arg(input_.sitesPerCluster));

    // -- Reference enthalpies, defaulted from the ensemble's own endpoints —
    // the paper's own recommended choice (Sec. 2, "Reference the ensemble's
    // own endpoints"), same convention the Cluster Expansion / Convex Hull
    // path already uses.
    input_.referenceEnthalpyA =
        input_.clusters[static_cast<std::size_t>(pureAIndex)].energyEv
        / input_.sitesPerCluster;
    input_.referenceEnthalpyB =
        input_.clusters[static_cast<std::size_t>(pureBIndex)].energyEv
        / input_.sitesPerCluster;

    // -- Controls -------------------------------------------------------------
    auto* controls = new QGroupBox(tr("Parameters"), this);
    auto* form = new QFormLayout(controls);

    referenceASpin_ = new QDoubleSpinBox(controls);
    referenceASpin_->setDecimals(4);
    referenceASpin_->setRange(-1000.0, 1000.0);
    referenceASpin_->setValue(input_.referenceEnthalpyA);
    referenceASpin_->setSuffix(tr(" eV"));
    form->addRow(tr("H_%1 (reference):").arg(species.at(0)), referenceASpin_);

    referenceBSpin_ = new QDoubleSpinBox(controls);
    referenceBSpin_->setDecimals(4);
    referenceBSpin_->setRange(-1000.0, 1000.0);
    referenceBSpin_->setValue(input_.referenceEnthalpyB);
    referenceBSpin_->setSuffix(tr(" eV"));
    form->addRow(tr("H_%1 (reference):").arg(species.at(1)), referenceBSpin_);

    minCompositionSpin_ = new QDoubleSpinBox(controls);
    minCompositionSpin_->setDecimals(3);
    minCompositionSpin_->setRange(0.0, 1.0);
    minCompositionSpin_->setSingleStep(0.01);
    minCompositionSpin_->setValue(0.02);
    maxCompositionSpin_ = new QDoubleSpinBox(controls);
    maxCompositionSpin_->setDecimals(3);
    maxCompositionSpin_->setRange(0.0, 1.0);
    maxCompositionSpin_->setSingleStep(0.01);
    maxCompositionSpin_->setValue(0.98);
    auto* compositionRow = new QWidget(controls);
    auto* compositionLayout = new QHBoxLayout(compositionRow);
    compositionLayout->setContentsMargins(0, 0, 0, 0);
    compositionLayout->addWidget(minCompositionSpin_);
    compositionLayout->addWidget(new QLabel(QStringLiteral("–"), compositionRow));
    compositionLayout->addWidget(maxCompositionSpin_);
    form->addRow(tr("Composition x range:"), compositionRow);

    compositionStepsSpin_ = new QSpinBox(controls);
    compositionStepsSpin_->setRange(3, 401);
    compositionStepsSpin_->setValue(49);
    form->addRow(tr("Composition steps:"), compositionStepsSpin_);

    minTemperatureSpin_ = new QDoubleSpinBox(controls);
    minTemperatureSpin_->setDecimals(0);
    minTemperatureSpin_->setRange(1.0, 10000.0);
    minTemperatureSpin_->setValue(300.0);
    minTemperatureSpin_->setSuffix(tr(" K"));
    maxTemperatureSpin_ = new QDoubleSpinBox(controls);
    maxTemperatureSpin_->setDecimals(0);
    maxTemperatureSpin_->setRange(1.0, 10000.0);
    maxTemperatureSpin_->setValue(1400.0);
    maxTemperatureSpin_->setSuffix(tr(" K"));
    auto* temperatureRow = new QWidget(controls);
    auto* temperatureLayout = new QHBoxLayout(temperatureRow);
    temperatureLayout->setContentsMargins(0, 0, 0, 0);
    temperatureLayout->addWidget(minTemperatureSpin_);
    temperatureLayout->addWidget(new QLabel(QStringLiteral("–"), temperatureRow));
    temperatureLayout->addWidget(maxTemperatureSpin_);
    form->addRow(tr("Temperature range:"), temperatureRow);

    temperatureStepsSpin_ = new QSpinBox(controls);
    temperatureStepsSpin_->setRange(2, 200);
    temperatureStepsSpin_->setValue(15);
    temperatureStepsSpin_->setToolTip(
        tr("Also the number of curves drawn on the free-energy plot — one "
           "per temperature, coloured blue (low) to red (high), the same "
           "convention Fig. 2c/e/f use in the working paper."));
    form->addRow(tr("Temperature steps:"), temperatureStepsSpin_);

    auto* solveButton = new QPushButton(tr("Solve"), controls);
    connect(solveButton, &QPushButton::clicked, this, &EgqcaWindow::solve);
    form->addRow(QString(), solveButton);

    // -- Plots ------------------------------------------------------------------
    freeEnergyPlot_ = new EgqcaPlotWidget(this);
    probabilityPlot_ = new EgqcaPlotWidget(this);
    auto* plotsSplitter = new QSplitter(Qt::Horizontal, this);
    plotsSplitter->addWidget(freeEnergyPlot_);
    plotsSplitter->addWidget(probabilityPlot_);

    auto* exportRow = new QHBoxLayout;
    exportRow->addStretch(1);
    auto* exportFreeEnergy = new QPushButton(tr("Export Free Energy Data…"), this);
    connect(exportFreeEnergy, &QPushButton::clicked, freeEnergyPlot_,
            &EgqcaPlotWidget::exportData);
    exportRow->addWidget(exportFreeEnergy);
    auto* exportProbabilities =
        new QPushButton(tr("Export Cluster Probability Data…"), this);
    connect(exportProbabilities, &QPushButton::clicked, probabilityPlot_,
            &EgqcaPlotWidget::exportData);
    exportRow->addWidget(exportProbabilities);

    auto* body = new QHBoxLayout;
    body->addWidget(controls);
    auto* plotsColumn = new QVBoxLayout;
    plotsColumn->addWidget(plotsSplitter, 1);
    plotsColumn->addLayout(exportRow);
    body->addLayout(plotsColumn, 1);
    layout->addLayout(body, 1);

    solve();
}

void EgqcaWindow::solve()
{
    input_.minComposition = minCompositionSpin_->value();
    input_.maxComposition = maxCompositionSpin_->value();
    input_.compositionSteps = compositionStepsSpin_->value();
    input_.minTemperatureK = minTemperatureSpin_->value();
    input_.maxTemperatureK = maxTemperatureSpin_->value();
    input_.temperatureSteps = temperatureStepsSpin_->value();
    input_.referenceEnthalpyA = referenceASpin_->value();
    input_.referenceEnthalpyB = referenceBSpin_->value();

    result_ = core::solveEgqca(input_);
    if (!result_.ok) {
        statusLabel_->setText(tr("EGQCA did not solve: %1")
                                  .arg(QString::fromStdString(result_.note)));
        freeEnergyPlot_->clear();
        probabilityPlot_->clear();
        return;
    }
    QString status =
        tr("Solved: %1 x %2 grid.").arg(result_.compositionSteps).arg(result_.temperatureSteps);
    if (!result_.vibrationalAvailable)
        status += tr(" No phonon DOS on this ensemble — GQCA (no vibrational term).");
    for (const auto& warning : result_.warnings)
        status += QStringLiteral(" ") + QString::fromStdString(warning);
    statusLabel_->setText(status);
    refreshPlots();
}

void EgqcaWindow::refreshPlots()
{
    // -- Delta G / M vs x, one curve per temperature -------------------------
    std::vector<EgqcaPlotWidget::Series> freeEnergySeries;
    for (int it = 0; it < result_.temperatureSteps; ++it) {
        EgqcaPlotWidget::Series series;
        double temperatureK = 0.0;
        for (int ix = 0; ix < result_.compositionSteps; ++ix) {
            const auto& point =
                result_.points[static_cast<std::size_t>(ix) * result_.temperatureSteps
                               + it];
            if (!point.converged)
                continue;
            temperatureK = point.temperatureK;
            series.points.emplace_back(point.composition,
                                       point.mixingFreeEnergyEv * 1000.0); // meV
        }
        if (series.points.empty())
            continue;
        const double t = result_.temperatureSteps > 1
            ? static_cast<double>(it) / (result_.temperatureSteps - 1)
            : 0.5;
        series.color = temperatureColor(t);
        series.label = tr("%1 K").arg(temperatureK, 0, 'f', 0);
        freeEnergySeries.push_back(std::move(series));
    }
    freeEnergyPlot_->setSeries(std::move(freeEnergySeries), tr("x"),
                              tr("Delta G / M (meV/cluster)"));

    // -- p_j vs T at the composition grid point nearest x = 0.5 --------------
    int nearestIx = 0;
    double bestDistance = std::numeric_limits<double>::max();
    for (int ix = 0; ix < result_.compositionSteps; ++ix) {
        const auto& point = result_.points[static_cast<std::size_t>(ix)
                                           * result_.temperatureSteps];
        const double distance = std::abs(point.composition - 0.5);
        if (distance < bestDistance) {
            bestDistance = distance;
            nearestIx = ix;
        }
    }
    std::vector<EgqcaPlotWidget::Series> probabilitySeries(input_.clusters.size());
    for (std::size_t j = 0; j < input_.clusters.size(); ++j) {
        probabilitySeries[j].label =
            input_.clusters[j].label.empty()
                ? tr("cluster %1").arg(j)
                : QString::fromStdString(input_.clusters[j].label);
        // Golden-angle hue rotation — the same convention grain casts use
        // (see GrainCasts.cpp) — for however many clusters there are,
        // rather than a fixed palette that runs out.
        probabilitySeries[j].color =
            QColor::fromHsv(static_cast<int>(std::fmod(j * 137.508, 360.0)),
                            210, 235);
    }
    double xUsed = 0.0;
    for (int it = 0; it < result_.temperatureSteps; ++it) {
        const auto& point =
            result_.points[static_cast<std::size_t>(nearestIx)
                               * result_.temperatureSteps
                           + it];
        if (!point.converged)
            continue;
        xUsed = point.composition;
        for (std::size_t j = 0; j < input_.clusters.size(); ++j)
            probabilitySeries[j].points.emplace_back(point.temperatureK,
                                                     point.clusterProbabilities[j]);
    }
    probabilityPlot_->setSeries(std::move(probabilitySeries), tr("T (K)"),
                               tr("p_j  (x = %1)").arg(xUsed, 0, 'f', 3));
}

} // namespace calango::gui
