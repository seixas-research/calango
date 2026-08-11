#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace calango::core {

/// One physical number extracted from a finished node's artifacts.
///
/// The formatted `display()` and the raw `number` are both kept on purpose. A
/// report is read by a person and by a script, and giving them one field means
/// either the person reads "−1.0834570000000001e+01" or the script parses
/// "−10.835 eV" back out of a string.
struct ReportMetric {
    QString key;   ///< stable machine name, e.g. "final_energy_ev"
    QString label; ///< column heading, e.g. "Final energy"
    QString unit;  ///< "eV", "eV/Å", "" — never folded into the value
    double number = 0.0;
    /// False for a metric whose value is not a number (a formula, a phase
    /// name). `number` is then meaningless and `text` carries it.
    bool numeric = true;
    QString text;
    int decimals = 4;

    /// Value and unit, formatted for a table cell.
    QString display() const;
    QJsonObject toJson() const;
    static ReportMetric fromJson(const QJsonObject& object);
};

/// What became of one node, on one pass of the pipeline.
///
/// One entry per (node, pass) rather than per node: a batch runs the same node
/// once per container item, and "did the relaxation succeed" has a different
/// answer for each structure. Collapsing them would answer the question the
/// report exists to ask.
struct NodeOutcome {
    int nodeId = 0;
    QString task;   ///< slug, "geometry_optimization"
    QString title;  ///< display name
    QString engine; ///< calculator name; empty for transforms
    QString directory;
    /// "done" | "failed" | "skipped" | "pending"
    QString status;
    int batchIndex = 0;
    QString batchLabel; ///< which structure / defect this pass was for
    int attempt = 1;
    QString note; ///< why it failed, when anything said so
    QList<ReportMetric> metrics;

    bool succeeded() const { return status == QLatin1String("done"); }
    QJsonObject toJson() const;
    static NodeOutcome fromJson(const QJsonObject& object);
};

/// The summary of one orchestration run.
///
/// DELIBERATELY FREE OF THE GUI. It depends on QtCore and nothing else — no
/// widgets, no canvas types, no node items — for two reasons that are really
/// one. The obvious one is that `calango-cli` runs pipelines on a cluster where
/// there is no display, and a summary you can only read by opening a window is
/// a summary that is not there when it matters most. The deeper one is that a
/// report assembled by the thing being reported on tends to agree with it: by
/// keeping this a plain record with its own schema, the same file can be
/// written by the C++ engine, written by the Python CLI, and read back by
/// either, and a disagreement between them is visible rather than impossible
/// to express.
///
/// Accumulated AS THE RUN GOES, not reconstructed at the end from the canvas.
/// The canvas cannot answer for a batch: every pass re-queues its nodes and
/// overwrites their status, so by the time the run finishes the only statuses
/// left are the last pass's. What happened to structure 3 of 12 exists only if
/// it was recorded when it happened.
struct WorkflowReport {
    QString schema = QStringLiteral("calango.orchestration.report/1");
    QString startedUtc;
    QString finishedUtc;
    QString root; ///< the orchestration folder everything staged under
    int batchTotal = 1;
    /// True when the run reached its natural end rather than being aborted.
    bool completed = true;
    QList<NodeOutcome> outcomes;

    struct Tally {
        int succeeded = 0;
        int failed = 0;
        int skipped = 0;
        int total() const { return succeeded + failed + skipped; }
    };

    Tally tally() const;
    /// The tally restricted to one pass — the per-structure answer.
    Tally tallyFor(int batchIndex) const;
    QList<NodeOutcome> outcomesFor(int batchIndex) const;
    /// Pass labels in order, index-aligned with batchIndex. Empty strings for
    /// a pipeline with no Container.
    QStringList batchLabels() const;
    /// True when nothing failed and nothing was skipped.
    bool allSucceeded() const;
    /// "Workflow finished: 12 succeeded, 2 failed" — the one line at the top.
    QString headline() const;

    QJsonObject toJson() const;
    static WorkflowReport fromJson(const QJsonObject& object);
    /// The whole report as text, for stdout and for the `.txt` sidecar.
    QString toPlainText() const;

    /// Write `workflow_report.json` and `workflow_report.txt` into `root`.
    /// Returns false if either could not be written — worth reporting, never
    /// worth failing a finished run over.
    bool write() const;
    /// Read the report back from an orchestration folder. Invalid (empty
    /// `schema`) when there is none or it cannot be parsed.
    static WorkflowReport read(const QString& orchestrationRoot);
};

/// Key physical results from a finished node's directory.
///
/// Reads the artifacts the run left behind — metrics.json for the energy and
/// force history, bands.json for a gap, the module summaries for what only
/// they know. Pure file parsing: it holds no opinion about the graph and can
/// be pointed at any job directory, including one produced by a standalone
/// wizard run or by the CLI on a cluster.
///
/// `taskSlug` selects which module-specific artifacts are worth looking for;
/// the generic ones are read for every task. An artifact that is absent is not
/// an error — most tasks write most of these never.
QList<ReportMetric> extractReportMetrics(const QString& directory,
                                         const QString& taskSlug);

} // namespace calango::core
