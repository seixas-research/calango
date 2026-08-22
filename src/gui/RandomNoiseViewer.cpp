#include "gui/RandomNoiseViewer.hpp"

#include "gui/HistogramPlotWidget.hpp"
#include "gui/PlotPalette.hpp"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace calango::gui {

namespace {

/// Sample standard deviation (ddof = 1), matching what the generated script
/// writes into the summary — the two must agree or the header and the band on
/// the chart disagree by 1/sqrt(N), which looks like a bug in one of them.
double sampleStdDev(const std::vector<double>& values, double mean)
{
    if (values.size() < 2)
        return 0.0;
    double sum = 0.0;
    for (const double v : values)
        sum += (v - mean) * (v - mean);
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
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

/// Read a numeric array, skipping anything that is not a finite number — a
/// failed member is recorded with a NaN energy, which must not enter a
/// histogram as a bin edge.
std::vector<double> finiteNumbers(const QJsonArray& array)
{
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue& value : array) {
        if (!value.isDouble())
            continue;
        const double number = value.toDouble();
        if (std::isfinite(number))
            values.push_back(number);
    }
    return values;
}

} // namespace

// ---------------------------------------------------------------------------
// RandomNoiseViewer
// ---------------------------------------------------------------------------

RandomNoiseViewer::RandomNoiseViewer(const QString& directory, QWidget* parent)
    : QDialog(parent)
    , directory_(directory)
{
    setWindowTitle(tr("Random Noise Viewer"));
    resize(880, 720);

    auto* layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel_);

    // Bin count: shared by both panels deliberately. Two histograms of the
    // same ensemble binned differently invite a comparison of their shapes
    // that the binning, not the physics, would be driving.
    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Histogram bins:"), this));
    binsSpin_ = new QSpinBox(this);
    binsSpin_->setRange(4, 200);
    binsSpin_->setValue(24);
    binsSpin_->setToolTip(
        tr("Bars per distribution. Too few hides the shape; too many turns a "
           "small ensemble into a row of single counts. Roughly √N is the "
           "usual starting point."));
    controls->addWidget(binsSpin_);
    controls->addStretch(1);
    layout->addLayout(controls);

    auto* energyGroup = new QGroupBox(tr("Energy distribution"), this);
    auto* energyLayout = new QVBoxLayout(energyGroup);
    energyPlot_ = new HistogramPlotWidget(energyGroup);
    energyPlot_->setLabels(tr("Total energy (eV)"), tr("Members"));
    energyPlot_->setBarColor(PlotPalette::series);
    energyPlot_->setPlaceholder(tr("No member of this ensemble was evaluated "
                                   "successfully."));
    energyLayout->addWidget(energyPlot_);
    layout->addWidget(energyGroup, 1);

    auto* forceGroup = new QGroupBox(tr("Force distribution"), this);
    auto* forceLayout = new QVBoxLayout(forceGroup);
    forcePlot_ = new HistogramPlotWidget(forceGroup);
    forcePlot_->setLabels(tr("Per-atom |F| (eV/Å)"), tr("Atoms"));
    forcePlot_->setBarColor(QColor(0x2c, 0xa0, 0x2c));
    forcePlot_->setPlaceholder(
        tr("This run did not record forces — re-run with \"Record forces\" "
           "enabled to get the force distribution."));
    forceLayout->addWidget(forcePlot_);
    layout->addWidget(forceGroup, 1);

    auto* buttons = new QDialogButtonBox(this);
    auto* exportButton =
        buttons->addButton(tr("Export…"), QDialogButtonBox::ActionRole);
    exportButton->setToolTip(
        tr("Save the evaluated ensemble as an extended-XYZ trajectory: every "
           "member at the geometry it was evaluated at, with its energy and "
           "forces attached. That is the file an MLIP trainer reads."));
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(exportButton, &QPushButton::clicked, this,
            &RandomNoiseViewer::exportTrajectory);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    hasData_ = load();
    if (hasData_) {
        connect(binsSpin_, &QSpinBox::valueChanged, this,
                [this] { rebinPlots(); });
        rebinPlots();
    }
    // Nothing to export from a run whose trajectory never got written.
    exportButton->setEnabled(!trajectoryPath().isEmpty());
}

QString RandomNoiseViewer::trajectoryPath() const
{
    // The generated script's own default first; the others cover a job staged
    // by an older version or a hand-edited script.
    for (const auto* name : {"noise_singlepoint.extxyz", "perturbed.extxyz",
                             "noise.extxyz"}) {
        const QString path =
            directory_ + QLatin1Char('/') + QLatin1String(name);
        if (QFile::exists(path))
            return path;
    }
    return QString();
}

bool RandomNoiseViewer::load()
{
    QFile file(directory_ + QStringLiteral("/random_noise.json"));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root =
        QJsonDocument::fromJson(file.readAll()).object();
    if (root.isEmpty())
        return false;

    const QJsonObject summary = root.value(QStringLiteral("summary")).toObject();
    for (const QJsonValue& value :
         root.value(QStringLiteral("members")).toArray()) {
        const QJsonObject member = value.toObject();
        const QJsonValue energy = member.value(QStringLiteral("energy"));
        if (energy.isDouble() && std::isfinite(energy.toDouble()))
            energies_.push_back(energy.toDouble());
    }
    forceMagnitudes_ =
        finiteNumbers(root.value(QStringLiteral("force_magnitudes")).toArray());

    // The header reports the script's own numbers where it has them, so the
    // window never quietly disagrees with random_noise.json — and falls back
    // to recomputing from the members when reading an older run's file.
    const auto stat = [&summary](const char* key, double fallback) {
        const QJsonValue value = summary.value(QLatin1String(key));
        return value.isDouble() ? value.toDouble() : fallback;
    };
    const double energyMean = stat("energy_mean", meanOf(energies_));
    const double energyStd =
        stat("energy_std", sampleStdDev(energies_, energyMean));
    const double perAtomStd = stat("energy_per_atom_std",
                                   std::numeric_limits<double>::quiet_NaN());
    const double forceComponentStd =
        stat("force_component_std", std::numeric_limits<double>::quiet_NaN());
    const double forceMagnitudeMean =
        stat("force_magnitude_mean", meanOf(forceMagnitudes_));
    const double forceMagnitudeStd =
        stat("force_magnitude_std",
             sampleStdDev(forceMagnitudes_, forceMagnitudeMean));
    const double forceMax = stat("force_magnitude_max",
                                 std::numeric_limits<double>::quiet_NaN());

    const int members = summary.value(QStringLiteral("members"))
                            .toInt(static_cast<int>(energies_.size()));
    const int evaluated = summary.value(QStringLiteral("evaluated"))
                              .toInt(static_cast<int>(energies_.size()));
    const int failed = summary.value(QStringLiteral("failed"))
                           .toInt(members - evaluated);

    const auto number = [](double value, int precision, const QString& unit) {
        if (!std::isfinite(value))
            return QStringLiteral("—");
        return QStringLiteral("%1 %2").arg(value, 0, 'g', precision).arg(unit);
    };

    QString text;
    text += tr("<b>%1</b> members, <b>%2</b> evaluated")
                .arg(members)
                .arg(evaluated);
    if (failed > 0) {
        text += tr(", <b style=\"color:#e06c5a\">%1 failed</b> "
                   "(excluded from the statistics)")
                    .arg(failed);
    }
    text += QStringLiteral(".<br><br>");

    // σ first and largest: it is the number the run was performed to obtain.
    text += tr("<b>Energy spread σ(E) = %1</b>")
                .arg(number(energyStd, 4, tr("eV")));
    if (std::isfinite(perAtomStd)) {
        text += tr("&nbsp;&nbsp;(%1 per atom)")
                    .arg(number(perAtomStd * 1000.0, 4, tr("meV")));
    }
    text += QStringLiteral("<br>");
    text += tr("<b>Force spread σ(F) = %1</b> over the Cartesian components; "
               "per-atom |F| = %2 ± %3, largest %4")
                .arg(number(forceComponentStd, 4, tr("eV/Å")),
                     number(forceMagnitudeMean, 4, QString()),
                     number(forceMagnitudeStd, 3, tr("eV/Å")),
                     number(forceMax, 4, tr("eV/Å")));
    text += QStringLiteral("<br><br>");

    // Mean shift last: it is the sanity check on the amplitude rather than the
    // result, and it only exists when frame 0 evaluated successfully.
    if (summary.contains(QStringLiteral("mean_shift"))) {
        text += tr("Mean energy %1, which is %2 above the unperturbed "
                   "reference — the anharmonic bias the displacement "
                   "introduces. A shift much larger than σ(E) means the "
                   "amplitude has left the harmonic well.")
                    .arg(number(energyMean, 6, tr("eV")),
                         number(
                             summary.value(QStringLiteral("mean_shift")).toDouble(),
                             3, tr("eV")));
    } else {
        text += tr("Mean energy %1.").arg(number(energyMean, 6, tr("eV")));
    }
    summaryLabel_->setText(text);

    // A run with no successfully evaluated member has a JSON file but nothing
    // to show; the caller treats that as "no data" rather than opening a
    // window of dashes.
    return !energies_.empty();
}

void RandomNoiseViewer::rebinPlots()
{
    const int bins = binsSpin_->value();
    energyPlot_->setSamples(energies_, bins);
    forcePlot_->setSamples(forceMagnitudes_, bins);
}

void RandomNoiseViewer::exportTrajectory()
{
    const QString source = trajectoryPath();
    if (source.isEmpty()) {
        QMessageBox::warning(
            this, tr("Export Ensemble"),
            tr("This job directory holds no evaluated trajectory. It is "
               "written only once at least one member has been evaluated "
               "successfully."));
        return;
    }

    const QString target = QFileDialog::getSaveFileName(
        this, tr("Export Ensemble"),
        QStringLiteral("random_noise_ensemble.extxyz"),
        tr("Extended XYZ (*.extxyz *.xyz);;All files (*)"));
    if (target.isEmpty())
        return;

    // A copy, not a re-serialization. The script already wrote every frame
    // with its energy and forces attached; regenerating the file here would
    // mean parsing and rewriting the very data the export exists to preserve,
    // and any rounding introduced on the way would be silent.
    QFile::remove(target); // QFile::copy refuses to overwrite
    if (!QFile::copy(source, target)) {
        QMessageBox::warning(this, tr("Export Ensemble"),
                             tr("Could not write %1.").arg(target));
        return;
    }
    QMessageBox::information(
        this, tr("Export Ensemble"),
        tr("Wrote %1 — every evaluated member with its energy and forces, "
           "ready to be used as MLIP training data.")
            .arg(QFileInfo(target).fileName()));
}

} // namespace calango::gui
