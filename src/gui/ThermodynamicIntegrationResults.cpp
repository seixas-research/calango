#include "gui/ThermodynamicIntegrationResults.hpp"

#include "gui/TiIntegrandPlot.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace calango::gui {

namespace {

core::TiQuadrature quadratureFromName(const QString& name)
{
    if (name == QLatin1String("simpson"))
        return core::TiQuadrature::Simpson;
    if (name == QLatin1String("trapezoid"))
        return core::TiQuadrature::Trapezoid;
    return core::TiQuadrature::GaussLegendre;
}

core::TiReference referenceFromName(const QString& name)
{
    if (name == QLatin1String("einstein_crystal"))
        return core::TiReference::EinsteinCrystal;
    if (name == QLatin1String("lennard_jones_fluid"))
        return core::TiReference::LennardJonesFluid;
    return core::TiReference::IdealGas;
}

/// Parse one λ window and run its error analysis.
///
/// The mean is recomputed from the series rather than trusted from the file,
/// and the two are compared: the script accumulates it with Welford while the
/// run is going, so a disagreement means the series that was written is not the
/// series that was averaged — which would make every error bar below describe a
/// different quantity than the mean it decorates.
core::TiWindowSample readWindow(const QJsonObject& entry, QStringList* warnings)
{
    core::TiWindowSample window;
    window.index = entry.value(QStringLiteral("index")).toInt(-1);
    window.lambda = entry.value(QStringLiteral("lambda")).toDouble();
    if (entry.value(QStringLiteral("status")).toString()
        != QLatin1String("ok")) {
        window.ok = false;
        window.failure =
            entry.value(QStringLiteral("error")).toString().toStdString();
        if (window.failure.empty())
            window.failure = "the window reported a non-ok status";
        return window;
    }

    std::vector<double> series;
    const QJsonArray raw = entry.value(QStringLiteral("series_eV")).toArray();
    series.reserve(static_cast<std::size_t>(raw.size()));
    for (const QJsonValue& value : raw)
        series.push_back(value.toDouble());

    const double reportedMean =
        entry.value(QStringLiteral("mean_dudl_eV")).toDouble();
    if (series.size() < 2) {
        // No series to analyse. The mean is still usable, but there is no
        // honest error bar for it, so the window carries a zero error AND a
        // warning rather than a fabricated one.
        window.dudlEv = reportedMean;
        window.dudlVarianceEv2 =
            entry.value(QStringLiteral("variance_dudl_eV2")).toDouble();
        window.samples = entry.value(QStringLiteral("samples")).toInt();
        window.ok = window.samples > 0;
        if (warnings && window.ok)
            *warnings << QObject::tr(
                             "Window %1 carries no sampled series, so its "
                             "error bar could not be estimated.")
                             .arg(window.index);
        return window;
    }

    const core::TiSeriesStatistics stats = core::analyseSeries(series);
    window.dudlEv = stats.mean;
    window.dudlErrorEv = stats.standardError;
    window.dudlVarianceEv2 = stats.variance;
    window.correlationTime = stats.correlationTime;
    window.samples = stats.samples;
    window.ok = true;

    if (warnings && std::abs(stats.mean - reportedMean)
            > 1.0e-6 * std::max(1.0, std::abs(reportedMean)))
        *warnings << QObject::tr(
                         "Window %1: the running mean written by the run "
                         "(%2 eV) disagrees with the mean of the series it "
                         "wrote (%3 eV).")
                         .arg(window.index)
                         .arg(reportedMean, 0, 'g', 8)
                         .arg(stats.mean, 0, 'g', 8);
    // Two independent error estimators. Disagreeing by more than ~2x means the
    // production run is too short for either of them to be believed.
    if (warnings && stats.blockStandardError > 0.0 && stats.standardError > 0.0
        && (stats.blockStandardError > 2.5 * stats.standardError
            || stats.standardError > 2.5 * stats.blockStandardError))
        *warnings << QObject::tr(
                         "Window %1: the autocorrelation (%2 eV) and block "
                         "(%3 eV) error estimates disagree — the production "
                         "run is too short for either.")
                         .arg(window.index)
                         .arg(stats.standardError, 0, 'g', 3)
                         .arg(stats.blockStandardError, 0, 'g', 3);
    return window;
}

std::vector<core::TiWindowSample> readPath(const QJsonObject& path,
                                           QStringList* warnings)
{
    std::vector<core::TiWindowSample> windows;
    const QJsonArray entries = path.value(QStringLiteral("windows")).toArray();
    for (const QJsonValue& value : entries)
        windows.push_back(readWindow(value.toObject(), warnings));
    return windows;
}

QString formatEnergy(double value, double error)
{
    return QStringLiteral("%1 ± %2")
        .arg(value, 0, 'f', 6)
        .arg(error, 0, 'g', 3);
}

} // namespace

TiRunReport readThermodynamicIntegrationRun(const QString& jsonPath)
{
    TiRunReport report;
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        report.text =
            QObject::tr("Could not open %1.").arg(jsonPath);
        return report;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        report.text = QObject::tr("%1 is not a thermodynamic-integration "
                                  "summary.")
                          .arg(jsonPath);
        return report;
    }
    const QJsonObject root = document.object();
    report.parsed = true;

    report.system.atomCount = root.value(QStringLiteral("natoms")).toInt();
    report.system.temperatureK =
        root.value(QStringLiteral("temperature_K")).toDouble();
    report.system.pressureGPa =
        root.value(QStringLiteral("pressure_GPa")).toDouble();
    report.system.volumeA3 = root.value(QStringLiteral("volume_A3")).toDouble();
    for (const QJsonValue& mass :
         root.value(QStringLiteral("masses_amu")).toArray())
        report.system.massesAmu.push_back(mass.toDouble());

    core::TiReferenceParameters parameters;
    parameters.einsteinSpringEvPerA2 =
        root.value(QStringLiteral("einstein_spring_eV_per_A2")).toDouble(1.0);
    parameters.einsteinFixedCenterOfMass =
        root.value(QStringLiteral("einstein_fixed_com")).toBool(true);
    parameters.ljEpsilonEv =
        root.value(QStringLiteral("lj_epsilon_eV")).toDouble(0.0104);
    parameters.ljSigmaA =
        root.value(QStringLiteral("lj_sigma_A")).toDouble(3.4);

    const core::TiReference reference =
        referenceFromName(root.value(QStringLiteral("reference")).toString());
    const core::TiQuadrature rule =
        quadratureFromName(root.value(QStringLiteral("quadrature")).toString());
    const int expected =
        root.value(QStringLiteral("windows_expected")).toInt(0);

    const QJsonObject paths = root.value(QStringLiteral("paths")).toObject();
    const QJsonObject forward =
        paths.value(QStringLiteral("forward")).toObject();
    report.windows = readPath(forward, &report.warnings);

    // The volume the reference free energy is evaluated at. Under NVT every
    // window agrees; under NPT they do not, and a single V is then a fiction —
    // so the spread is measured and reported rather than averaged away.
    std::vector<double> volumes;
    for (const QJsonValue& value :
         forward.value(QStringLiteral("windows")).toArray()) {
        const double mean = value.toObject()
                                .value(QStringLiteral("mean_volume_A3"))
                                .toDouble();
        if (mean > 0.0)
            volumes.push_back(mean);
    }
    if (!volumes.empty()) {
        const double mean =
            std::accumulate(volumes.begin(), volumes.end(), 0.0)
            / volumes.size();
        const auto [lo, hi] =
            std::minmax_element(volumes.begin(), volumes.end());
        report.system.volumeA3 = mean;
        if (mean > 0.0 && (*hi - *lo) > 0.01 * mean)
            report.warnings << QObject::tr(
                                   "The cell volume varies by %1 %% across the "
                                   "λ path (%2 to %3 Å³). The reference free "
                                   "energy is a function of V, so a single "
                                   "value is a fiction here — integrate at "
                                   "fixed volume.")
                                   .arg(100.0 * (*hi - *lo) / mean, 0, 'f', 1)
                                   .arg(*lo, 0, 'f', 2)
                                   .arg(*hi, 0, 'f', 2);
    }

    report.assembly = core::assembleThermodynamicIntegration(
        report.system, reference, parameters, report.windows, rule, expected);
    report.complete = report.assembly.integration.complete;
    for (const auto& warning : report.assembly.warnings)
        report.warnings << QString::fromStdString(warning);

    if (paths.contains(QStringLiteral("backward"))) {
        const QJsonObject backward =
            paths.value(QStringLiteral("backward")).toObject();
        const auto reverse = readPath(backward, nullptr);
        report.hysteresis = core::compareHysteresis(
            report.assembly.integration,
            core::integrateThermodynamicPath(reverse, rule, expected));
    }

    // ---- The report -------------------------------------------------------
    QStringList lines;
    lines << QObject::tr("Thermodynamic integration — %1")
                 .arg(QString::fromStdString(core::toString(reference)));
    lines << QString(60, QLatin1Char('='));
    lines << QObject::tr("Atoms:        %1").arg(report.system.atomCount);
    lines << QObject::tr("Volume:       %1 Å³")
                 .arg(report.system.volumeA3, 0, 'f', 3);
    lines << QObject::tr("Temperature:  %1 K")
                 .arg(report.system.temperatureK, 0, 'f', 2);
    lines << QObject::tr("Pressure:     %1 GPa")
                 .arg(report.system.pressureGPa, 0, 'g', 4);
    lines << QObject::tr("λ windows:    %1 expected").arg(expected);
    lines << QString();

    lines << QObject::tr("  idx     lambda      <dU/dl> (eV)        1σ (eV)"
                         "    τ_int   samples");
    for (const auto& window : report.windows) {
        if (!window.ok) {
            lines << QStringLiteral("  %1  %2  FAILED: %3")
                         .arg(window.index, 3)
                         .arg(window.lambda, 10, 'f', 6)
                         .arg(QString::fromStdString(window.failure));
            continue;
        }
        lines << QStringLiteral("  %1  %2  %3  %4  %5  %6")
                     .arg(window.index, 3)
                     .arg(window.lambda, 10, 'f', 6)
                     .arg(window.dudlEv, 18, 'f', 6)
                     .arg(window.dudlErrorEv, 12, 'g', 4)
                     .arg(window.correlationTime, 7, 'f', 1)
                     .arg(window.samples, 8);
    }
    lines << QString();

    if (!report.complete) {
        lines << QObject::tr("*** INCOMPLETE — NO FREE ENERGY IS REPORTED ***");
        QStringList missing;
        for (const int index : report.assembly.integration.missingWindows)
            missing << QString::number(index);
        lines << QObject::tr("Missing / failed windows: %1")
                     .arg(missing.join(QStringLiteral(", ")));
        lines << QObject::tr(
            "An integral over the surviving windows is a DIFFERENT integral, "
            "not a noisier one: the quadrature weights are a property of the "
            "node set. Re-run the missing windows.");
    } else {
        const auto& integration = report.assembly.integration;
        lines << QObject::tr("ΔF (integration)   %1 eV")
                     .arg(formatEnergy(integration.deltaFEv,
                                       integration.totalErrorEv));
        lines << QObject::tr("    statistical    %1 eV")
                     .arg(integration.statisticalErrorEv, 0, 'g', 3);
        lines << QObject::tr("    λ-grid         %1 eV")
                     .arg(integration.quadratureErrorEv, 0, 'g', 3);
        lines << QObject::tr("F_ref              %1 eV   (%2)")
                     .arg(report.assembly.reference.freeEnergyEv, 0, 'f', 6)
                     .arg(QString::fromStdString(
                         report.assembly.reference.description));
        lines << QString();
        lines << QObject::tr("F = F_ref + ΔF     %1 eV      (%2 eV/atom)")
                     .arg(report.assembly.helmholtzEv, 0, 'f', 6)
                     .arg(report.assembly.helmholtzEvPerAtom, 0, 'f', 6);
        lines << QObject::tr("PV                 %1 eV")
                     .arg(report.assembly.pvEv, 0, 'f', 6);
        lines << QObject::tr("G = F + PV         %1 eV      (%2 eV/atom)")
                     .arg(report.assembly.gibbsEv, 0, 'f', 6)
                     .arg(report.assembly.gibbsEvPerAtom, 0, 'f', 6);
        lines << QObject::tr("uncertainty        ± %1 eV    (± %2 eV/atom)")
                     .arg(report.assembly.errorEv, 0, 'g', 3)
                     .arg(report.assembly.errorEvPerAtom, 0, 'g', 3);
        lines << QString();
        lines << QObject::tr("Every energy above is for the WHOLE CELL unless "
                             "the line says eV/atom.");
    }

    if (report.hysteresis.valid) {
        lines << QString();
        lines << QObject::tr("Hysteresis (forward vs backward sweep)");
        lines << QObject::tr("  forward   %1 eV")
                     .arg(report.hysteresis.forwardEv, 0, 'f', 6);
        lines << QObject::tr("  backward  %1 eV")
                     .arg(report.hysteresis.backwardEv, 0, 'f', 6);
        lines << QObject::tr("  gap       %1 eV (combined 1σ %2 eV)")
                     .arg(report.hysteresis.differenceEv, 0, 'f', 6)
                     .arg(report.hysteresis.combinedErrorEv, 0, 'g', 3);
        lines << (report.hysteresis.significant
                      ? QObject::tr("  *** The path is NOT reversible. ΔF is "
                                    "not what either sweep says it is: the "
                                    "windows were under-equilibrated, or the "
                                    "system crossed a barrier or a phase "
                                    "boundary along the path.")
                      : QObject::tr("  The path closes within its error bar."));
    }

    if (!report.warnings.isEmpty()) {
        lines << QString();
        lines << QObject::tr("Warnings");
        for (const QString& warning : report.warnings)
            lines << QStringLiteral("  - ") + warning;
    }

    report.text = lines.join(QLatin1Char('\n'));
    return report;
}

QString thermodynamicIntegrationCsv(const TiRunReport& report)
{
    QStringList lines;
    // The settings and the assembled answer come first, commented. A bare table
    // of ⟨∂U/∂λ⟩ is not reproducible: the same numbers integrate to a different
    // free energy under a different quadrature rule, and F_ref depends on T, V
    // and the masses. Anyone who opens this file a year from now needs the
    // conditions in it.
    lines << QStringLiteral("# Calango — thermodynamic integration");
    lines << QStringLiteral("# atoms,%1").arg(report.system.atomCount);
    lines << QStringLiteral("# temperature_K,%1")
                 .arg(QString::number(report.system.temperatureK, 'g', 10));
    lines << QStringLiteral("# pressure_GPa,%1")
                 .arg(QString::number(report.system.pressureGPa, 'g', 10));
    lines << QStringLiteral("# volume_A3,%1")
                 .arg(QString::number(report.system.volumeA3, 'g', 10));
    lines << QStringLiteral("# complete,%1")
                 .arg(report.complete ? QStringLiteral("yes")
                                      : QStringLiteral("no"));
    if (report.complete) {
        const auto& integration = report.assembly.integration;
        lines << QStringLiteral("# delta_F_eV,%1")
                     .arg(QString::number(integration.deltaFEv, 'g', 12));
        lines << QStringLiteral("# delta_F_error_eV,%1")
                     .arg(QString::number(integration.totalErrorEv, 'g', 6));
        lines << QStringLiteral("# F_ref_eV,%1")
                     .arg(QString::number(report.assembly.reference.freeEnergyEv,
                                          'g', 12));
        lines << QStringLiteral("# F_eV,%1")
                     .arg(QString::number(report.assembly.helmholtzEv, 'g', 12));
        lines << QStringLiteral("# F_eV_per_atom,%1")
                     .arg(QString::number(report.assembly.helmholtzEvPerAtom,
                                          'g', 12));
        lines << QStringLiteral("# PV_eV,%1")
                     .arg(QString::number(report.assembly.pvEv, 'g', 12));
        lines << QStringLiteral("# G_eV,%1")
                     .arg(QString::number(report.assembly.gibbsEv, 'g', 12));
        lines << QStringLiteral("# G_eV_per_atom,%1")
                     .arg(QString::number(report.assembly.gibbsEvPerAtom,
                                          'g', 12));
        lines << QStringLiteral("# uncertainty_eV,%1")
                     .arg(QString::number(report.assembly.errorEv, 'g', 6));
    }
    if (report.hysteresis.valid) {
        lines << QStringLiteral("# hysteresis_gap_eV,%1")
                     .arg(QString::number(report.hysteresis.differenceEv,
                                          'g', 12));
        lines << QStringLiteral("# hysteresis_significant,%1")
                     .arg(report.hysteresis.significant
                              ? QStringLiteral("yes")
                              : QStringLiteral("no"));
    }
    for (const QString& warning : report.warnings)
        lines << QStringLiteral("# warning,\"%1\"")
                     .arg(QString(warning).replace(QLatin1Char('"'),
                                                   QLatin1String("\"\"")));

    // A failed window keeps its row, with empty fields and its reason. Dropping
    // it would make the file describe a shorter path that converged, which is
    // the one thing an incomplete run must not look like.
    lines << QStringLiteral("index,lambda,dudl_eV,dudl_error_eV,tau_int,"
                            "samples,status");
    for (const auto& window : report.windows) {
        if (!window.ok) {
            lines << QStringLiteral("%1,%2,,,,,\"%3\"")
                         .arg(window.index)
                         .arg(QString::number(window.lambda, 'g', 12))
                         .arg(QString::fromStdString(window.failure)
                                  .replace(QLatin1Char('"'),
                                           QLatin1String("\"\"")));
            continue;
        }
        lines << QStringLiteral("%1,%2,%3,%4,%5,%6,ok")
                     .arg(window.index)
                     .arg(QString::number(window.lambda, 'g', 12))
                     .arg(QString::number(window.dudlEv, 'g', 12))
                     .arg(QString::number(window.dudlErrorEv, 'g', 6))
                     .arg(QString::number(window.correlationTime, 'g', 6))
                     .arg(window.samples);
    }
    lines << QString();
    return lines.join(QLatin1Char('\n'));
}

void showThermodynamicIntegrationResults(QWidget* parent,
                                         const QString& jsonPath,
                                         std::function<void()> loadAnother)
{
    const TiRunReport report = readThermodynamicIntegrationRun(jsonPath);

    auto* dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(
        QObject::tr("Thermodynamic Integration — Free Energy"));
    dialog->resize(760, 620);
    auto* layout = new QVBoxLayout(dialog);

    // The integrand ABOVE the numbers, deliberately. Delta_F is a number and
    // every characteristic TI failure is a SHAPE — an endpoint running away,
    // a grid too coarse across a steep region, one window that never
    // equilibrated, forward and backward not coinciding. A report that leads
    // with the free energy asks the reader to trust that none of those
    // happened.
    auto* plot = new TiIntegrandPlot(dialog);
    {
        // Only the forward sweep is plotted: the reader keeps ONE window list
        // and retains the backward sweep only as its integral, in
        // `hysteresis`. Reconstructing a backward curve here would mean
        // inventing points, so the hysteresis verdict stays in the text below
        // where it is a number rather than a line.
        plot->setWindows(report.windows, {});
        const auto diagnostics = core::endpointDiagnostics(report.windows);
        plot->setEndpointWarning(diagnostics.suspected,
                                 QString::fromStdString(diagnostics.message));
    }
    layout->addWidget(plot, 3);

    auto* view = new QPlainTextEdit(dialog);
    view->setReadOnly(true);
    // Fixed pitch: the per-window table is aligned by column, and a
    // proportional font turns it into a smear.
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setPlainText(report.text);
    layout->addWidget(view, 2);

    // Loading and saving live HERE, on the window that shows a run, rather than
    // on the menu bar. The Simulation menu used to carry an "Open … Results…"
    // entry whose whole job was to ask for a file; a finished run now opens
    // this window by itself, and the two buttons cover what remains — reaching
    // a run from an earlier session or a cluster, and getting these numbers out
    // to a plotting script.
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog,
                     &QDialog::reject);

    if (loadAnother) {
        auto* loadButton = buttons->addButton(QObject::tr("Load Results…"),
                                              QDialogButtonBox::ActionRole);
        loadButton->setToolTip(
            QObject::tr("Open the ti.json of another run — one from an earlier "
                        "session, or brought back from a cluster"));
        QObject::connect(loadButton, &QPushButton::clicked, dialog,
                         [loadAnother] { loadAnother(); });
    }

    auto* exportDataButton = buttons->addButton(QObject::tr("Export Results…"),
                                                QDialogButtonBox::ActionRole);
    exportDataButton->setToolTip(
        QObject::tr("The per-window table and the assembled free energies, as "
                    "CSV or as the report text shown below"));
    QObject::connect(
        exportDataButton, &QPushButton::clicked, dialog, [dialog, report] {
            QString selected;
            const QString path = QFileDialog::getSaveFileName(
                dialog, QObject::tr("Export Results"),
                QStringLiteral("thermodynamic_integration.csv"),
                QObject::tr("Per-window data (*.csv);;Report text (*.txt)"),
                &selected);
            if (path.isEmpty())
                return;
            // The chosen filter decides, not the typed suffix: a user who
            // picks "Report text" and types no extension means the report.
            const bool asText = selected.contains(QLatin1String(".txt"))
                                || path.endsWith(QLatin1String(".txt"),
                                                 Qt::CaseInsensitive);
            QFile out(path);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QMessageBox::warning(dialog, QObject::tr("Export Results"),
                                     QObject::tr("Could not write %1.")
                                         .arg(path));
                return;
            }
            const QString content = asText
                                        ? report.text + QLatin1Char('\n')
                                        : thermodynamicIntegrationCsv(report);
            out.write(content.toUtf8());
        });

    auto* exportButton = buttons->addButton(QObject::tr("Export Plot…"),
                                            QDialogButtonBox::ActionRole);
    QObject::connect(exportButton, &QPushButton::clicked, dialog,
                     [dialog, plot] {
                         const QString path = QFileDialog::getSaveFileName(
                             dialog, QObject::tr("Export Plot"), QString(),
                             QObject::tr("PNG image (*.png)"));
                         if (!path.isEmpty())
                             plot->exportImage(path, 3.0);
                     });

    layout->addWidget(buttons);
    dialog->show();
}

} // namespace calango::gui
