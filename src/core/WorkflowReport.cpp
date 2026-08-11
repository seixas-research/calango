#include "core/WorkflowReport.hpp"

#include "core/BandGap.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

#include <algorithm>

namespace calango::core {

namespace {

QJsonArray metricsToJson(const QList<ReportMetric>& metrics)
{
    QJsonArray array;
    for (const ReportMetric& metric : metrics)
        array.append(metric.toJson());
    return array;
}

QList<ReportMetric> metricsFromJson(const QJsonArray& array)
{
    QList<ReportMetric> metrics;
    for (const QJsonValue& value : array)
        metrics.append(ReportMetric::fromJson(value.toObject()));
    return metrics;
}

QJsonObject readJsonObject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

ReportMetric number(const QString& key, const QString& label, double value,
                    const QString& unit, int decimals = 4)
{
    ReportMetric metric;
    metric.key = key;
    metric.label = label;
    metric.number = value;
    metric.unit = unit;
    metric.decimals = decimals;
    return metric;
}

ReportMetric text(const QString& key, const QString& label,
                  const QString& value)
{
    ReportMetric metric;
    metric.key = key;
    metric.label = label;
    metric.numeric = false;
    metric.text = value;
    return metric;
}

/// The last sample carrying `field` in a metrics.json history.
///
/// The LAST rather than the smallest: for a relaxation the final step is the
/// converged geometry, and for molecular dynamics it is where the trajectory
/// ended. A minimum over the history would report the deepest excursion of an
/// unconverged run as though it were the result.
bool lastMetric(const QJsonArray& samples, const char* field, double* value,
                int* step)
{
    for (int i = samples.size() - 1; i >= 0; --i) {
        const QJsonObject entry = samples[i].toObject();
        const QJsonValue found = entry.value(QLatin1String(field));
        if (!found.isDouble())
            continue;
        *value = found.toDouble();
        if (step)
            *step = entry.value(QStringLiteral("step")).toInt();
        return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------

QString ReportMetric::display() const
{
    const QString value = numeric
        ? QString::number(number, 'f', decimals)
        : text;
    return unit.isEmpty() ? value : value + QLatin1Char(' ') + unit;
}

QJsonObject ReportMetric::toJson() const
{
    QJsonObject object{{QStringLiteral("key"), key},
                       {QStringLiteral("label"), label},
                       {QStringLiteral("unit"), unit}};
    if (numeric)
        object.insert(QStringLiteral("value"), number);
    else
        object.insert(QStringLiteral("text"), text);
    return object;
}

ReportMetric ReportMetric::fromJson(const QJsonObject& object)
{
    ReportMetric metric;
    metric.key = object.value(QStringLiteral("key")).toString();
    metric.label = object.value(QStringLiteral("label")).toString();
    metric.unit = object.value(QStringLiteral("unit")).toString();
    if (object.contains(QStringLiteral("value"))) {
        metric.numeric = true;
        metric.number = object.value(QStringLiteral("value")).toDouble();
    } else {
        metric.numeric = false;
        metric.text = object.value(QStringLiteral("text")).toString();
    }
    return metric;
}

QJsonObject NodeOutcome::toJson() const
{
    return QJsonObject{
        {QStringLiteral("node_id"), nodeId},
        {QStringLiteral("task"), task},
        {QStringLiteral("title"), title},
        {QStringLiteral("engine"), engine},
        {QStringLiteral("directory"), directory},
        {QStringLiteral("status"), status},
        {QStringLiteral("batch_index"), batchIndex},
        {QStringLiteral("batch_label"), batchLabel},
        {QStringLiteral("attempt"), attempt},
        {QStringLiteral("note"), note},
        {QStringLiteral("metrics"), metricsToJson(metrics)},
    };
}

NodeOutcome NodeOutcome::fromJson(const QJsonObject& object)
{
    NodeOutcome outcome;
    outcome.nodeId = object.value(QStringLiteral("node_id")).toInt();
    outcome.task = object.value(QStringLiteral("task")).toString();
    outcome.title = object.value(QStringLiteral("title")).toString();
    outcome.engine = object.value(QStringLiteral("engine")).toString();
    outcome.directory = object.value(QStringLiteral("directory")).toString();
    outcome.status = object.value(QStringLiteral("status")).toString();
    outcome.batchIndex = object.value(QStringLiteral("batch_index")).toInt();
    outcome.batchLabel = object.value(QStringLiteral("batch_label")).toString();
    outcome.attempt = object.value(QStringLiteral("attempt")).toInt(1);
    outcome.note = object.value(QStringLiteral("note")).toString();
    outcome.metrics =
        metricsFromJson(object.value(QStringLiteral("metrics")).toArray());
    return outcome;
}

// ---------------------------------------------------------------------------
// The report
// ---------------------------------------------------------------------------

WorkflowReport::Tally WorkflowReport::tally() const
{
    Tally counts;
    for (const NodeOutcome& outcome : outcomes) {
        if (outcome.status == QLatin1String("done"))
            ++counts.succeeded;
        else if (outcome.status == QLatin1String("failed"))
            ++counts.failed;
        else
            ++counts.skipped;
    }
    return counts;
}

WorkflowReport::Tally WorkflowReport::tallyFor(int batchIndex) const
{
    Tally counts;
    for (const NodeOutcome& outcome : outcomes) {
        if (outcome.batchIndex != batchIndex)
            continue;
        if (outcome.status == QLatin1String("done"))
            ++counts.succeeded;
        else if (outcome.status == QLatin1String("failed"))
            ++counts.failed;
        else
            ++counts.skipped;
    }
    return counts;
}

QList<NodeOutcome> WorkflowReport::outcomesFor(int batchIndex) const
{
    QList<NodeOutcome> selected;
    for (const NodeOutcome& outcome : outcomes)
        if (outcome.batchIndex == batchIndex)
            selected.append(outcome);
    return selected;
}

QStringList WorkflowReport::batchLabels() const
{
    QStringList labels;
    for (int pass = 0; pass < std::max(1, batchTotal); ++pass) {
        QString label;
        for (const NodeOutcome& outcome : outcomes) {
            if (outcome.batchIndex == pass && !outcome.batchLabel.isEmpty()) {
                label = outcome.batchLabel;
                break;
            }
        }
        labels << label;
    }
    return labels;
}

bool WorkflowReport::allSucceeded() const
{
    const Tally counts = tally();
    return counts.failed == 0 && counts.skipped == 0 && counts.succeeded > 0;
}

QString WorkflowReport::headline() const
{
    const Tally counts = tally();
    QStringList parts;
    parts << QObject::tr("%n succeeded", nullptr, counts.succeeded);
    if (counts.failed > 0)
        parts << QObject::tr("%n failed", nullptr, counts.failed);
    if (counts.skipped > 0)
        parts << QObject::tr("%n skipped", nullptr, counts.skipped);
    const QString verb = completed ? QObject::tr("Workflow finished")
                                   : QObject::tr("Workflow stopped");
    return QStringLiteral("%1: %2").arg(verb, parts.join(QStringLiteral(", ")));
}

QJsonObject WorkflowReport::toJson() const
{
    QJsonArray array;
    for (const NodeOutcome& outcome : outcomes)
        array.append(outcome.toJson());

    const Tally counts = tally();
    QJsonArray perBatch;
    const QStringList labels = batchLabels();
    for (int pass = 0; pass < std::max(1, batchTotal); ++pass) {
        const Tally passCounts = tallyFor(pass);
        perBatch.append(QJsonObject{
            {QStringLiteral("index"), pass},
            {QStringLiteral("label"),
             pass < labels.size() ? labels[pass] : QString()},
            {QStringLiteral("succeeded"), passCounts.succeeded},
            {QStringLiteral("failed"), passCounts.failed},
            {QStringLiteral("skipped"), passCounts.skipped},
        });
    }

    return QJsonObject{
        {QStringLiteral("schema"), schema},
        {QStringLiteral("started_utc"), startedUtc},
        {QStringLiteral("finished_utc"), finishedUtc},
        {QStringLiteral("root"), root},
        {QStringLiteral("completed"), completed},
        {QStringLiteral("summary"),
         QJsonObject{{QStringLiteral("succeeded"), counts.succeeded},
                     {QStringLiteral("failed"), counts.failed},
                     {QStringLiteral("skipped"), counts.skipped},
                     {QStringLiteral("total"), counts.total()}}},
        {QStringLiteral("batch"),
         QJsonObject{{QStringLiteral("total"), batchTotal},
                     {QStringLiteral("passes"), perBatch}}},
        {QStringLiteral("outcomes"), array},
    };
}

WorkflowReport WorkflowReport::fromJson(const QJsonObject& object)
{
    WorkflowReport report;
    report.schema = object.value(QStringLiteral("schema")).toString();
    report.startedUtc = object.value(QStringLiteral("started_utc")).toString();
    report.finishedUtc = object.value(QStringLiteral("finished_utc")).toString();
    report.root = object.value(QStringLiteral("root")).toString();
    report.completed = object.value(QStringLiteral("completed")).toBool(true);
    report.batchTotal =
        object.value(QStringLiteral("batch")).toObject()
            .value(QStringLiteral("total")).toInt(1);
    for (const QJsonValue& value :
         object.value(QStringLiteral("outcomes")).toArray())
        report.outcomes.append(NodeOutcome::fromJson(value.toObject()));
    return report;
}

QString WorkflowReport::toPlainText() const
{
    QString out;
    QTextStream stream(&out);
    stream << headline() << "\n";
    if (!startedUtc.isEmpty())
        stream << QObject::tr("Started:  ") << startedUtc << "\n";
    if (!finishedUtc.isEmpty())
        stream << QObject::tr("Finished: ") << finishedUtc << "\n";
    if (!root.isEmpty())
        stream << QObject::tr("Folder:   ") << root << "\n";
    stream << "\n";

    const QStringList labels = batchLabels();
    for (int pass = 0; pass < std::max(1, batchTotal); ++pass) {
        const QList<NodeOutcome> passOutcomes = outcomesFor(pass);
        if (passOutcomes.isEmpty())
            continue;
        if (batchTotal > 1) {
            const Tally counts = tallyFor(pass);
            const QString label =
                pass < labels.size() && !labels[pass].isEmpty()
                ? labels[pass]
                : QObject::tr("pass %1").arg(pass + 1);
            stream << QStringLiteral("[%1]  %2 ok, %3 failed, %4 skipped\n")
                          .arg(label)
                          .arg(counts.succeeded)
                          .arg(counts.failed)
                          .arg(counts.skipped);
        }
        for (const NodeOutcome& outcome : passOutcomes) {
            stream << QStringLiteral("  %1  %2")
                          .arg(outcome.status.leftJustified(8),
                               outcome.title);
            for (const ReportMetric& metric : outcome.metrics) {
                stream << QStringLiteral("   %1 = %2")
                              .arg(metric.label, metric.display());
            }
            if (!outcome.note.isEmpty())
                stream << QStringLiteral("   (%1)").arg(outcome.note);
            stream << "\n";
        }
        stream << "\n";
    }
    return out;
}

bool WorkflowReport::write() const
{
    if (root.isEmpty())
        return false;
    bool ok = true;
    QFile json(root + QStringLiteral("/workflow_report.json"));
    if (json.open(QIODevice::WriteOnly | QIODevice::Truncate))
        json.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    else
        ok = false;
    // The text twin is not redundant. It is what a cluster user reads over ssh
    // without a JSON tool, and what a batch script cats into a job log.
    QFile textFile(root + QStringLiteral("/workflow_report.txt"));
    if (textFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        textFile.write(toPlainText().toUtf8());
    else
        ok = false;
    return ok;
}

WorkflowReport WorkflowReport::read(const QString& orchestrationRoot)
{
    const QJsonObject object =
        readJsonObject(orchestrationRoot + QStringLiteral("/workflow_report.json"));
    if (object.isEmpty())
        return WorkflowReport{QString(), {}, {}, {}, 1, true, {}};
    return fromJson(object);
}

// ---------------------------------------------------------------------------
// Extraction
// ---------------------------------------------------------------------------

QList<ReportMetric> extractReportMetrics(const QString& directory,
                                         const QString& taskSlug)
{
    QList<ReportMetric> metrics;
    if (directory.isEmpty())
        return metrics;

    // --- Generic: the metric history every generated script writes ---------
    const QJsonObject metricsFile =
        readJsonObject(directory + QStringLiteral("/metrics.json"));
    const QJsonArray samples =
        metricsFile.value(QStringLiteral("metrics")).toArray();
    double value = 0.0;
    int step = 0;
    if (lastMetric(samples, "energy", &value, &step)) {
        metrics.append(number(QStringLiteral("final_energy_ev"),
                              QObject::tr("Final energy"), value,
                              QStringLiteral("eV"), 6));
    }
    if (lastMetric(samples, "max_force", &value, nullptr)) {
        metrics.append(number(QStringLiteral("max_force_ev_per_a"),
                              QObject::tr("Max force"), value,
                              QStringLiteral("eV/Å"), 4));
    }
    if (taskSlug == QLatin1String("molecular_dynamics")) {
        // Only for MD: a relaxation's "temperature" is whatever the last
        // sample happened to hold, and reporting it as a result would invite
        // it to be read as one.
        if (lastMetric(samples, "temperature", &value, nullptr)) {
            metrics.append(number(QStringLiteral("final_temperature_k"),
                                  QObject::tr("Final T"), value,
                                  QStringLiteral("K"), 1));
        }
        if (lastMetric(samples, "pressure", &value, nullptr)) {
            metrics.append(number(QStringLiteral("final_pressure_gpa"),
                                  QObject::tr("Final P"), value,
                                  QStringLiteral("GPa"), 3));
        }
    }
    if (!samples.isEmpty()) {
        metrics.append(number(QStringLiteral("steps"), QObject::tr("Steps"),
                              samples.last().toObject()
                                  .value(QStringLiteral("step")).toInt(),
                              QString(), 0));
    }

    // --- Band gap ----------------------------------------------------------
    // Computed here rather than read: the script writes the band structure,
    // and the gap is an ANALYSIS of it. Doing it in the one place that already
    // knows how (core::analyzeBandGap) keeps the number in the report equal to
    // the number the bands viewer shows, which two independent readings of the
    // same file would not guarantee.
    const QJsonObject bands =
        readJsonObject(directory + QStringLiteral("/bands.json"));
    if (!bands.isEmpty()) {
        std::vector<std::vector<std::vector<double>>> energies;
        for (const QJsonValue& spinValue :
             bands.value(QStringLiteral("energies")).toArray()) {
            std::vector<std::vector<double>> spin;
            for (const QJsonValue& kValue : spinValue.toArray()) {
                std::vector<double> band;
                for (const QJsonValue& e : kValue.toArray())
                    band.push_back(e.toDouble());
                spin.push_back(std::move(band));
            }
            energies.push_back(std::move(spin));
        }
        if (!energies.empty() && !energies.front().empty()) {
            const BandGapInfo info = analyzeBandGap(
                energies, bands.value(QStringLiteral("efermi")).toDouble());
            metrics.append(info.metallic
                               ? text(QStringLiteral("band_gap_ev"),
                                      QObject::tr("Band gap"),
                                      QObject::tr("metallic"))
                               : number(QStringLiteral("band_gap_ev"),
                                        QObject::tr("Band gap"), info.gap,
                                        QStringLiteral("eV"), 3));
        }
    }

    // --- Module summaries --------------------------------------------------
    // Each of these is a file only one module writes, holding the number that
    // module exists to produce. Read by name rather than by task so a run
    // reached through some other route still reports it.
    const QJsonObject mdmc =
        readJsonObject(directory + QStringLiteral("/mdmc_summary.json"));
    if (!mdmc.isEmpty()) {
        metrics.append(number(QStringLiteral("mdmc_best_energy_ev"),
                              QObject::tr("Best energy"),
                              mdmc.value(QStringLiteral("best_energy_eV")).toDouble(),
                              QStringLiteral("eV"), 6));
        metrics.append(number(QStringLiteral("mdmc_energy_gain_ev"),
                              QObject::tr("Energy gain"),
                              mdmc.value(QStringLiteral("energy_gain_eV")).toDouble(),
                              QStringLiteral("eV"), 6));
        metrics.append(number(QStringLiteral("mdmc_acceptance"),
                              QObject::tr("Acceptance"),
                              mdmc.value(QStringLiteral("acceptance_ratio")).toDouble(),
                              QString(), 3));
    }

    const QJsonObject convergence =
        readJsonObject(directory + QStringLiteral("/convergence.json"));
    if (const QJsonValue reached =
            convergence.value(QStringLiteral("converged"));
        reached.isBool()) {
        metrics.append(text(QStringLiteral("converged"),
                            QObject::tr("Converged"),
                            reached.toBool() ? QObject::tr("yes")
                                             : QObject::tr("no")));
    }

    return metrics;
}

} // namespace calango::core
