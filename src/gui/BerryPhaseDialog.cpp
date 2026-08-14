#include "gui/BerryPhaseDialog.hpp"

#include "core/LocaleSafeNumber.hpp"
#include "gui/CurvatureMapWidget.hpp"
#include "gui/SpectrumPlotWidget.hpp"
#include "gui/WannierModelSource.hpp"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <cmath>

namespace calango::gui {

BerryPhaseDialog::BerryPhaseDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Berry Phase"));
    auto* outer = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Wilson loops, Berry curvature, anomalous Hall conductivity, "
           "polarization and hybrid Wannier centres — all from H(R)."),
        this);
    intro->setWordWrap(true);
    intro->setToolTip(
        tr("Berry phases are computed as Wilson loops (products of overlap "
           "matrices), which are gauge covariant by construction: an arbitrary "
           "phase on an eigenvector enters once as a bra and once as a ket and "
           "cancels around the loop.\n\n"
           "Berry curvature uses the Kubo form, a sum over band pairs of "
           "matrix elements of dH/dk, so it never differentiates an "
           "eigenvector and never needs one to vary smoothly with k."));
    outer->addWidget(intro);

    source_ = new WannierModelSource(this);
    outer->addWidget(source_);
    connect(source_, &WannierModelSource::modelChanged, this,
            [this] { computeButton_->setEnabled(true); });

    auto* settings = new QGroupBox(tr("Sampling"), this);
    auto* form = new QFormLayout(settings);

    auto* meshRow = new QHBoxLayout;
    const int defaults[3] = {48, 48, 1};
    for (int axis = 0; axis < 3; ++axis) {
        kmesh_[axis] = new QSpinBox(settings);
        kmesh_[axis]->setRange(1, 400);
        kmesh_[axis]->setValue(defaults[axis]);
        meshRow->addWidget(kmesh_[axis]);
    }
    meshRow->addStretch(1);
    form->addRow(tr("BZ mesh:"), meshRow);

    loopPoints_ = new QSpinBox(settings);
    loopPoints_->setRange(8, 4096);
    loopPoints_->setValue(64);
    loopPoints_->setToolTip(
        tr("Points per Wilson loop. The loop is exact only in the limit of "
           "many points; a phase that should be quantised and is not usually "
           "means this is too small."));
    form->addRow(tr("Loop points:"), loopPoints_);

    occupiedCount_ = new QSpinBox(settings);
    occupiedCount_->setRange(1, 200);
    occupiedCount_->setValue(1);
    occupiedCount_->setToolTip(
        tr("How many bands, counting up from the lowest, make up the occupied "
           "manifold. Berry-phase quantities are properties of the manifold as "
           "a whole, not of a single band, whenever the bands are entangled."));
    form->addRow(tr("Occupied bands:"), occupiedCount_);

    plane_ = new QComboBox(settings);
    plane_->addItem(tr("xy  (Ω_xy, σ_xy)"), 0);
    plane_->addItem(tr("yz  (Ω_yz, σ_yz)"), 1);
    plane_->addItem(tr("zx  (Ω_zx, σ_zx)"), 2);
    form->addRow(tr("Plane:"), plane_);

    mapSamples_ = new QSpinBox(settings);
    mapSamples_->setRange(8, 400);
    mapSamples_->setValue(48);
    form->addRow(tr("Map resolution:"), mapSamples_);
    outer->addWidget(settings);

    auto* row = new QHBoxLayout;
    computeButton_ = new QPushButton(tr("Compute"), this);
    computeButton_->setEnabled(false);
    connect(computeButton_, &QPushButton::clicked, this,
            &BerryPhaseDialog::compute);
    row->addWidget(computeButton_);
    row->addStretch(1);
    auto* exportButton = new QPushButton(tr("Export Data…"), this);
    connect(exportButton, &QPushButton::clicked, this,
            &BerryPhaseDialog::exportData);
    row->addWidget(exportButton);
    auto* close = new QPushButton(tr("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    row->addWidget(close);
    outer->addLayout(row);

    auto* tabs = new QTabWidget(this);
    report_ = new QPlainTextEdit(tabs);
    report_->setReadOnly(true);
    tabs->addTab(report_, tr("Summary"));
    map_ = new CurvatureMapWidget(tabs);
    tabs->addTab(map_, tr("Berry Curvature Ω(k)"));
    flowPlot_ = new SpectrumPlotWidget(tabs);
    flowPlot_->setToolTip(
        tr("Hybrid Wannier centre flow. The net winding over a full period IS "
           "the Chern number, which is why a topological phase shows the "
           "centres sliding by one lattice vector and a trivial one shows them "
           "returning to where they started."));
    tabs->addTab(flowPlot_, tr("Wannier Centre Flow"));
    outer->addWidget(tabs, 1);

    resize(900, 860);
}

core::BerryPhase::Options BerryPhaseDialog::readOptions() const
{
    core::BerryPhase::Options options;
    for (int axis = 0; axis < 3; ++axis)
        options.kmesh[axis] = kmesh_[axis]->value();
    options.loopPoints = loopPoints_->value();
    options.occupiedBands.clear();
    for (int i = 0; i < occupiedCount_->value(); ++i)
        options.occupiedBands.push_back(i);
    return options;
}

void BerryPhaseDialog::compute()
{
    if (!source_->hasModel())
        return;
    try {
        const int plane = plane_->currentData().toInt();
        const int alpha = plane;            // 0->x, 1->y, 2->z
        const int beta = (plane + 1) % 3;   // xy, yz, zx respectively

        const core::BerryPhase berry(source_->model(), readOptions());

        const auto hall = berry.anomalousHall(alpha, beta);
        const auto polarization = berry.polarization(alpha);
        const auto loop = berry.wilsonLoopAlong(alpha, {0.0, 0.0, 0.0});

        lastMap_ = berry.curvaturePlane(alpha, beta, mapSamples_->value(),
                                        mapSamples_->value(), 0.0, alpha, beta);
        map_->setMap(lastMap_);

        lastFlow_ = berry.wannierCentreFlow(beta, alpha, 64);
        std::vector<QPair<QString, std::vector<double>>> series;
        const std::size_t bands =
            lastFlow_.centres.empty() ? 0 : lastFlow_.centres.front().size();
        for (std::size_t band = 0; band < bands; ++band) {
            std::vector<double> values;
            values.reserve(lastFlow_.centres.size());
            for (const auto& step : lastFlow_.centres)
                values.push_back(band < step.size() ? step[band] : 0.0);
            series.push_back({tr("centre %1").arg(band + 1), values});
        }
        flowPlot_->setSeries(lastFlow_.transverse, series,
                             tr("transverse k (fractional)"),
                             tr("Wannier centre (lattice units)"));

        QString text;
        text += tr("Berry-phase summary\n");
        text += QStringLiteral("=====================================\n\n");
        text += tr("  Wilson loop at the zone origin\n");
        text += tr("    Berry phase        = %1 rad  (%2 π)\n")
                    .arg(loop.berryPhase, 0, 'f', 6)
                    .arg(loop.berryPhase / 3.14159265358979323846, 0, 'f', 4);
        text += tr("\n  Anomalous Hall\n");
        text += tr("    Chern number       = %1\n")
                    .arg(hall.chernNumber, 0, 'f', 6);
        text += tr("    sigma in e^2/h     = %1\n")
                    .arg(hall.sigmaInConductanceQuanta, 0, 'f', 6);
        text += tr("    sigma (SI)         = %1 S/m\n")
                    .arg(hall.sigmaSI, 0, 'g', 6);
        text += tr("\n  Polarization (Berry phase, modern theory)\n");
        text += tr("    phase              = %1 rad\n")
                    .arg(polarization.phaseRadians, 0, 'f', 6);
        text += tr("    dipole per cell    = %1 e·Å\n")
                    .arg(polarization.dipolePerCell, 0, 'f', 6);
        text += tr("    P                  = %1 C/m²\n")
                    .arg(polarization.siValue, 0, 'g', 6);
        text += tr("    quantum            = %1 C/m²  "
                   "(P is defined only modulo this)\n")
                    .arg(polarization.quantumSI, 0, 'g', 6);
        text += tr("\n  Hybrid Wannier centre flow\n");
        text += tr("    net winding        = %1  "
                   "(the Chern number, by a second route)\n")
                    .arg(lastFlow_.winding, 0, 'f', 6);
        text += tr("\n  Curvature map: %1 to %2 Å² over the plane.\n")
                    .arg(lastMap_.minimum, 0, 'g', 4)
                    .arg(lastMap_.maximum, 0, 'g', 4);
        text += tr("\n  The Chern number and the centre winding are computed "
                   "by\n  independent paths; when they disagree the sampling "
                   "is too coarse.\n");
        report_->setPlainText(text);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Berry Phase"),
                             tr("The solver refused this input:\n%1")
                                 .arg(QString::fromUtf8(e.what())));
    }
}

void BerryPhaseDialog::exportData()
{
    if (lastMap_.values.empty()) {
        QMessageBox::information(this, tr("Export Data"),
                                 tr("Nothing computed yet."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Berry curvature"),
        QStringLiteral("berry_curvature.csv"), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export Data"),
                             tr("Could not write %1.").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "# k1(frac),k2(frac),Omega(A^2)\n";
    for (std::size_t i = 0; i < lastMap_.values.size(); ++i)
        for (std::size_t j = 0; j < lastMap_.values[i].size(); ++j)
            out << QString::fromStdString(
                       core::localeSafeFormat(lastMap_.axis1[i]))
                << ','
                << QString::fromStdString(
                       core::localeSafeFormat(lastMap_.axis2[j]))
                << ','
                << QString::fromStdString(
                       core::localeSafeFormat(lastMap_.values[i][j]))
                << '\n';
}

void BerryPhaseDialog::setWannierRuns(const QList<QPair<QString, QString>>& runs)
{
    if (source_)
        source_->setWannierRuns(runs);
}

} // namespace calango::gui
