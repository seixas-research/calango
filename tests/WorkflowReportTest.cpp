// Workflow report: aggregation, metric extraction and the on-disk schema.
//
// This target links Qt::Core ONLY. That is the point of it: the report is the
// thing `calango-cli` has to be able to produce on a cluster with no display,
// and a header that quietly includes a widget would break that months after
// anyone remembers why it mattered. Here it breaks the build instead.

#include "core/WorkflowReport.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cstdio>

using calango::core::NodeOutcome;
using calango::core::ReportMetric;
using calango::core::WorkflowReport;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition)
        ++failures;
}

NodeOutcome outcome(int id, const char* title, const char* status, int pass,
                    const char* label)
{
    NodeOutcome node;
    node.nodeId = id;
    node.title = QLatin1String(title);
    node.task = QStringLiteral("geometry_optimization");
    node.status = QLatin1String(status);
    node.batchIndex = pass;
    node.batchLabel = QLatin1String(label);
    return node;
}

void writeFile(const QString& path, const QByteArray& content)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(content);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    std::printf("Tally:\n");
    {
        WorkflowReport report;
        report.batchTotal = 3;
        report.outcomes = {
            outcome(1, "Relax", "done", 0, "Si"),
            outcome(2, "Bands", "done", 0, "Si"),
            outcome(1, "Relax", "failed", 1, "Ge"),
            outcome(2, "Bands", "skipped", 1, "Ge"),
            outcome(1, "Relax", "done", 2, "C"),
            outcome(2, "Bands", "done", 2, "C"),
        };

        const auto all = report.tally();
        check(all.succeeded == 4 && all.failed == 1 && all.skipped == 1,
              "the whole run tallies 4 done, 1 failed, 1 skipped");

        // THE case the report exists for. One structure of three failed, and
        // the other two are unaffected — a single overall verdict would say
        // "the workflow failed" and hide two perfectly good results.
        check(report.tallyFor(0).succeeded == 2
                  && report.tallyFor(0).failed == 0,
              "structure 1 is reported as entirely successful");
        check(report.tallyFor(1).failed == 1
                  && report.tallyFor(1).skipped == 1
                  && report.tallyFor(1).succeeded == 0,
              "structure 2 carries its own failure and the skip it caused");
        check(report.tallyFor(2).succeeded == 2,
              "structure 3 is unaffected by structure 2's failure");
        check(report.batchLabels()
                  == QStringList({QStringLiteral("Si"), QStringLiteral("Ge"),
                                  QStringLiteral("C")}),
              "each pass keeps the label of the structure it ran");
        check(!report.allSucceeded(), "and the run as a whole did not succeed");
        check(report.headline().contains(QStringLiteral("4"))
                  && report.headline().contains(QStringLiteral("1")),
              "the headline states the counts");
    }

    std::printf("JSON round trip:\n");
    {
        WorkflowReport report;
        report.startedUtc = QStringLiteral("2026-08-11T10:00:00Z");
        report.finishedUtc = QStringLiteral("2026-08-11T11:30:00Z");
        report.batchTotal = 2;
        report.completed = false;
        NodeOutcome node = outcome(7, "Single point", "done", 1, "GeO2");
        node.engine = QStringLiteral("GPAW");
        node.note = QStringLiteral("nothing to report");
        ReportMetric energy;
        energy.key = QStringLiteral("final_energy_ev");
        energy.label = QStringLiteral("Final energy");
        energy.unit = QStringLiteral("eV");
        energy.number = -10.834567;
        energy.decimals = 6;
        ReportMetric gap;
        gap.key = QStringLiteral("band_gap_ev");
        gap.label = QStringLiteral("Band gap");
        gap.numeric = false;
        gap.text = QStringLiteral("metallic");
        node.metrics = {energy, gap};
        report.outcomes = {node};

        const WorkflowReport back =
            WorkflowReport::fromJson(report.toJson());
        check(back.startedUtc == report.startedUtc
                  && back.finishedUtc == report.finishedUtc
                  && back.batchTotal == 2 && !back.completed,
              "the header survives a round trip");
        check(back.outcomes.size() == 1
                  && back.outcomes[0].engine == QLatin1String("GPAW")
                  && back.outcomes[0].batchLabel == QLatin1String("GeO2"),
              "so does the outcome");
        check(back.outcomes[0].metrics.size() == 2
                  && back.outcomes[0].metrics[0].numeric
                  && std::abs(back.outcomes[0].metrics[0].number + 10.834567)
                      < 1e-9,
              "a numeric metric keeps its FULL precision, not its display form");
        check(!back.outcomes[0].metrics[1].numeric
                  && back.outcomes[0].metrics[1].text
                      == QLatin1String("metallic"),
              "and a non-numeric one keeps its text");

        // The summary block is what a script reads without walking outcomes.
        const QJsonObject summary =
            report.toJson().value(QStringLiteral("summary")).toObject();
        check(summary.value(QStringLiteral("succeeded")).toInt() == 1
                  && summary.value(QStringLiteral("total")).toInt() == 1,
              "the JSON carries a precomputed summary block");
    }

    std::printf("Written beside the run, and read back:\n");
    {
        QTemporaryDir sandbox;
        WorkflowReport report;
        report.root = sandbox.path();
        report.startedUtc = QStringLiteral("2026-08-11T10:00:00Z");
        report.outcomes = {outcome(1, "Relax", "done", 0, QString().toLatin1())};
        check(report.write(), "the report writes");
        check(QFile::exists(sandbox.path() + QStringLiteral("/workflow_report.json")),
              "workflow_report.json exists");
        check(QFile::exists(sandbox.path() + QStringLiteral("/workflow_report.txt")),
              "workflow_report.txt exists for reading over ssh");

        const WorkflowReport back = WorkflowReport::read(sandbox.path());
        check(back.outcomes.size() == 1
                  && back.outcomes[0].title == QLatin1String("Relax"),
              "and reads back — which is how the CLI regenerates it");
        check(WorkflowReport::read(sandbox.path() + QStringLiteral("/nope"))
                  .schema.isEmpty(),
              "a missing report reads back as invalid rather than as empty-success");

        const QString text = report.toPlainText();
        check(text.contains(QStringLiteral("Relax"))
                  && text.contains(QStringLiteral("done")),
              "the text form names the node and its status");
    }

    std::printf("Metric extraction:\n");
    {
        QTemporaryDir job;
        // metrics.json as every generated script writes it: a HISTORY, from
        // which the report must take the LAST sample. Taking the minimum would
        // report the deepest excursion of an unconverged run as the result.
        writeFile(job.path() + QStringLiteral("/metrics.json"),
                  R"({"metrics":[{"step":1,"energy":-9.0,"max_force":0.9},
                                 {"step":2,"energy":-12.5,"max_force":0.4},
                                 {"step":3,"energy":-11.25,"max_force":0.02}]})");
        const auto metrics = calango::core::extractReportMetrics(
            job.path(), QStringLiteral("geometry_optimization"));

        const auto find = [&metrics](const char* key) -> ReportMetric {
            for (const ReportMetric& metric : metrics)
                if (metric.key == QLatin1String(key))
                    return metric;
            return {};
        };
        check(std::abs(find("final_energy_ev").number + 11.25) < 1e-9,
              "the final energy is the LAST sample, not the lowest");
        check(std::abs(find("max_force_ev_per_a").number - 0.02) < 1e-9,
              "and so is the max force");
        check(std::abs(find("steps").number - 3.0) < 1e-9,
              "the step count is reported");
        check(find("final_temperature_k").key.isEmpty(),
              "a relaxation reports no temperature — it has no meaning there");

        // Band gap: computed from the band structure, not read as a scalar,
        // so it equals what the bands viewer shows.
        writeFile(job.path() + QStringLiteral("/bands.json"),
                  R"({"efermi":0.0,"energies":[[[-3.0,-2.0,1.5,2.5],
                                                [-3.1,-2.1,1.6,2.6]]]})");
        const auto withBands = calango::core::extractReportMetrics(
            job.path(), QStringLiteral("electronic_bands"));
        bool gapFound = false;
        for (const ReportMetric& metric : withBands) {
            if (metric.key != QLatin1String("band_gap_ev"))
                continue;
            gapFound = true;
            check(metric.numeric && std::abs(metric.number - 3.5) < 1e-6,
                  "the band gap is computed from the eigenvalues (VBM -2, CBM 1.5)");
        }
        check(gapFound, "a run with bands.json reports a gap");

        check(calango::core::extractReportMetrics(
                  job.path() + QStringLiteral("/missing"),
                  QStringLiteral("single_point")).isEmpty(),
              "a directory with no artifacts yields no metrics, not zeros");
    }

    std::printf(failures == 0 ? "\nAll workflow report checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? 0 : 1;
}
