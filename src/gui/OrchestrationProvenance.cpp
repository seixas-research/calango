#include "gui/OrchestrationProvenance.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace calango::gui {

namespace {

QJsonObject fileToJson(const ProvenanceFile& file)
{
    QJsonObject object{{QStringLiteral("name"), file.name},
                       {QStringLiteral("bytes"), file.bytes},
                       {QStringLiteral("hashed"), file.hashed}};
    if (!file.role.isEmpty())
        object.insert(QStringLiteral("role"), file.role);
    if (!file.source.isEmpty())
        object.insert(QStringLiteral("source"), file.source);
    if (file.hashed)
        object.insert(QStringLiteral("sha256"), file.sha256);
    if (file.fromNodeId >= 0)
        object.insert(QStringLiteral("from_node"), file.fromNodeId);
    return object;
}

QJsonArray filesToJson(const QList<ProvenanceFile>& files)
{
    QJsonArray array;
    for (const ProvenanceFile& file : files)
        array.append(fileToJson(file));
    return array;
}

} // namespace

QString fileSha256(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex());
}

ProvenanceFile describeFile(const QString& directory, const QString& name,
                            const QString& role, const QString& source,
                            int fromNodeId)
{
    ProvenanceFile file;
    file.name = name;
    file.role = role;
    file.source = source;
    file.fromNodeId = fromNodeId;
    const QFileInfo info(directory + QLatin1Char('/') + name);
    if (info.isDir()) {
        // A staged directory (Charge Density Difference inherits a whole run).
        // Its size is the sum of what is in it and there is no single hash to
        // give; `hashed` stays false and says so.
        for (const QFileInfo& entry :
             QDir(info.absoluteFilePath())
                 .entryInfoList(QDir::Files | QDir::NoDotAndDotDot))
            file.bytes += entry.size();
        return file;
    }
    file.bytes = info.size();
    if (info.exists() && file.bytes <= kHashSizeLimit) {
        file.sha256 = fileSha256(info.absoluteFilePath());
        file.hashed = !file.sha256.isEmpty();
    }
    return file;
}

QList<ProvenanceFile> describeOutputs(const QString& directory,
                                      const QStringList& excluded)
{
    QList<ProvenanceFile> outputs;
    for (const QFileInfo& entry :
         QDir(directory).entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                       QDir::Name)) {
        if (excluded.contains(entry.fileName()))
            continue;
        outputs.append(describeFile(directory, entry.fileName()));
    }
    return outputs;
}

QJsonObject ProvenanceRecord::toJson() const
{
    QJsonObject node{{QStringLiteral("id"), nodeId},
                     {QStringLiteral("task"), task},
                     {QStringLiteral("title"), title},
                     {QStringLiteral("material"), material},
                     {QStringLiteral("directory"), directory}};
    if (!engine.isEmpty())
        node.insert(QStringLiteral("engine"), engine);

    QJsonArray parentArray;
    for (const auto& [id, description] : parents)
        parentArray.append(QJsonObject{{QStringLiteral("id"), id},
                                       {QStringLiteral("input"), description}});
    QJsonObject logical{
        {QStringLiteral("parents"), parentArray},
        {QStringLiteral("configured"), configured},
        {QStringLiteral("attempt"), attempt},
    };
    if (!scriptSha256.isEmpty())
        logical.insert(QStringLiteral("script_sha256"), scriptSha256);
    if (!runCommand.isEmpty())
        logical.insert(QStringLiteral("run_command"), runCommand);
    if (!python.isEmpty())
        logical.insert(QStringLiteral("python"), python);
    if (!parameters.isEmpty())
        logical.insert(QStringLiteral("parameters"), parameters);

    QJsonObject batch{{QStringLiteral("index"), batchIndex},
                      {QStringLiteral("total"), batchTotal}};
    if (!batchLabel.isEmpty())
        batch.insert(QStringLiteral("label"), batchLabel);

    QJsonObject execution{{QStringLiteral("status"), status},
                          {QStringLiteral("exit_code"), exitCode}};
    if (!startedUtc.isEmpty())
        execution.insert(QStringLiteral("started_utc"), startedUtc);
    if (!finishedUtc.isEmpty())
        execution.insert(QStringLiteral("finished_utc"), finishedUtc);

    return QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("calango.orchestration.provenance/1")},
        {QStringLiteral("node"), node},
        {QStringLiteral("batch"), batch},
        {QStringLiteral("logical"), logical},
        {QStringLiteral("data"),
         QJsonObject{{QStringLiteral("inputs"), filesToJson(inputs)},
                     {QStringLiteral("outputs"), filesToJson(outputs)}}},
        {QStringLiteral("execution"), execution},
    };
}

bool writeProvenance(const ProvenanceRecord& record)
{
    if (record.directory.isEmpty())
        return false;
    QFile file(record.directory + QStringLiteral("/provenance.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(QJsonDocument(record.toJson()).toJson(QJsonDocument::Indented))
        > 0;
}

} // namespace calango::gui
