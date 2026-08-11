#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

namespace calango::gui {

/// Provenance for one executed orchestration node, written to
/// `provenance.json` in that node's own job directory.
///
/// Two kinds of record live here and they answer different questions:
///
///   LOGICAL provenance — how this run came to be asked for. Which node in the
///   graph it is, which parents fed which named input, whether its script came
///   from a wizard or from task defaults, what parameters a transform applied,
///   which batch item it belongs to. This is what you read to understand a
///   result you did not produce.
///
///   DATA provenance — what actually moved on disk. For every input: the
///   absolute path it was copied FROM, the name it was staged AS, its size and
///   its SHA-256. For every output: name, size and SHA-256. This is what you
///   read to prove two runs consumed the same bytes, or to find the run that
///   produced a file you are holding.
///
/// The pair matters because either alone can lie. Logical provenance says a
/// node inherited "the pristine host"; only the checksum says WHICH pristine
/// host, after the node was re-run three times with a fixed script.
struct ProvenanceFile {
    QString name;   ///< name inside the node's job directory
    QString source; ///< absolute path it was copied from (empty for outputs)
    QString role;   ///< "structure", or the input slot's label
    qint64 bytes = 0;
    /// Empty when the file was too large to hash — see kHashSizeLimit. Never
    /// silently omitted: `hashed` says which of the two happened.
    QString sha256;
    bool hashed = false;
    int fromNodeId = -1; ///< which node produced it (-1: the canvas itself)
};

struct ProvenanceRecord {
    // -- identity -----------------------------------------------------------
    int nodeId = 0;
    QString task;     ///< slug, e.g. "electronic_bands"
    QString title;    ///< display name
    QString material; ///< the node's assigned document
    QString engine;   ///< calculator display name ("GPAW"); empty for transforms
    QString directory;

    // -- logical ------------------------------------------------------------
    /// (parent node id, "slot label <- parent title"), in link order.
    QList<QPair<int, QString>> parents;
    bool configured = false; ///< script came from a wizard, not task defaults
    QString scriptSha256;
    QString runCommand;
    QString python;
    /// What a transform node did ("2 x 2 x 1", "remove 12; substitute 3 with B").
    QString parameters;

    // -- batch --------------------------------------------------------------
    int batchIndex = 0;
    int batchTotal = 1;
    QString batchLabel;
    /// 1 for the first execution of this node in this orchestration, 2 for the
    /// first re-run after a Resume, and so on. The previous attempt's
    /// directory and provenance are left untouched.
    int attempt = 1;

    // -- data ---------------------------------------------------------------
    QList<ProvenanceFile> inputs;
    QList<ProvenanceFile> outputs;

    // -- execution ----------------------------------------------------------
    QString startedUtc;
    QString finishedUtc;
    int exitCode = -1;
    QString status; ///< "running" | "done" | "failed" | "skipped"

    QJsonObject toJson() const;
};

/// Files above this are recorded by name and size but not hashed.
///
/// A GPAW ground state is routinely hundreds of megabytes, and hashing every
/// one of them would make provenance the most expensive part of a cheap
/// pipeline. The record says `"hashed": false` rather than omitting the field,
/// so "no checksum" is never confusable with "checksum of nothing".
inline constexpr qint64 kHashSizeLimit = 8LL * 1024 * 1024;

/// SHA-256 of a file, or empty if it cannot be read. Streams the file rather
/// than loading it, so calling it on something large is slow but not fatal.
QString fileSha256(const QString& path);

/// Describe one file for the record. Hashes it when it is under
/// kHashSizeLimit.
ProvenanceFile describeFile(const QString& directory, const QString& name,
                            const QString& role = QString(),
                            const QString& source = QString(),
                            int fromNodeId = -1);

/// Every top-level file in `directory` except the ones the canvas itself put
/// there (the staged inputs, the script, this record), each described as
/// above. One level deep on purpose, matching how inputs are staged.
///
/// An input record always describes the bytes the node was HANDED, measured
/// when they were staged. A job that overwrites one of its inputs in place
/// therefore keeps the original record and does not reappear here — which is
/// the right reading of "what went in", and the reason the two lists are
/// captured at different times rather than both at the end.
QList<ProvenanceFile> describeOutputs(const QString& directory,
                                      const QStringList& excluded);

/// Write `record` as `<directory>/provenance.json`. Returns false if the file
/// cannot be written — a failure worth reporting but never worth aborting a
/// run over, since the science is already on disk.
bool writeProvenance(const ProvenanceRecord& record);

} // namespace calango::gui
