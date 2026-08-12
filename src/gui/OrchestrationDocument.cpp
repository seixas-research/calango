#include "gui/OrchestrationDocument.hpp"

#include "core/Structure.hpp"
#include "gui/EnginePresets.hpp"
#include "gui/RunCommands.hpp"
#include "python_bridge/AseBridge.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <map>

namespace calango::gui {
namespace {

QString familyName(OrchestrationFamily family)
{
    switch (family) {
    case OrchestrationFamily::Transform:
        return QStringLiteral("transform");
    case OrchestrationFamily::Analysis:
        return QStringLiteral("analysis");
    case OrchestrationFamily::Simulation:
        break;
    }
    return QStringLiteral("simulation");
}

/// A structure as extended-XYZ text.
///
/// Round-tripped through a real file because that is the only writer ASE
/// offers, and extxyz is the format that carries the cell and the periodic
/// flags alongside the positions — the two things a workflow loses if it
/// travels as bare coordinates.
QString toExtxyz(const core::Structure& structure, QTemporaryDir& scratch,
                 QString* error)
{
    static int counter = 0;
    const QString path =
        scratch.filePath(QStringLiteral("s%1.extxyz").arg(++counter));
    try {
        pybridge::AseBridge::writeStructure(structure, path.toStdString());
    } catch (const std::exception& e) {
        *error = QString::fromUtf8(e.what());
        return QString();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("the temporary file could not be read back");
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

std::shared_ptr<const core::Structure> fromExtxyz(const QString& text,
                                                  QTemporaryDir& scratch,
                                                  QString* error)
{
    static int counter = 0;
    const QString path =
        scratch.filePath(QStringLiteral("r%1.extxyz").arg(++counter));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QObject::tr("a temporary file could not be written");
        return nullptr;
    }
    file.write(text.toUtf8());
    file.close();
    try {
        return std::make_shared<const core::Structure>(
            pybridge::AseBridge::readStructure(path.toStdString()));
    } catch (const std::exception& e) {
        *error = QString::fromUtf8(e.what());
        return nullptr;
    }
}

QJsonObject structureEntry(const QString& name, const QString& extxyz)
{
    return QJsonObject{{QStringLiteral("name"), name},
                       {QStringLiteral("format"), QStringLiteral("extxyz")},
                       {QStringLiteral("data"), extxyz}};
}

} // namespace

QJsonObject OrchestrationDocument::build(const OrchestrationWindow& window,
                                         QStringList* warnings)
{
    QTemporaryDir scratch;
    QJsonArray nodeArray;
    for (const OrchestrationNodeItem* node : window.nodes()) {
        const OrchestrationTask task = node->task();
        const OrchestrationFamily family = orchestrationTaskFamily(task);
        QJsonObject entry{
            {QStringLiteral("id"), node->id()},
            {QStringLiteral("task"), orchestrationTaskSlug(task)},
            {QStringLiteral("family"), familyName(family)},
            {QStringLiteral("title"), node->title()},
            {QStringLiteral("configured"), node->isConfigured()},
            {QStringLiteral("position"),
             QJsonObject{{QStringLiteral("x"), node->pos().x()},
                         {QStringLiteral("y"), node->pos().y()}}},
        };

        // The slot table travels WITH the node. The reader then never has to
        // know that Raman/IR inherits three runs of which two are optional —
        // which is the only way a separate implementation of the executor can
        // be kept from drifting away from this one.
        QJsonArray inputs;
        for (const OrchestrationInputSlot& slot : orchestrationInputSlots(task))
            inputs.append(QJsonObject{
                {QStringLiteral("label"), slot.label},
                {QStringLiteral("source"), slot.sourceName},
                {QStringLiteral("staged_as"), slot.stagedName},
                {QStringLiteral("optional"), slot.optional},
            });
        entry.insert(QStringLiteral("inputs"), inputs);

        if (family != OrchestrationFamily::Transform) {
            entry.insert(QStringLiteral("engine"),
                         EnginePresets::displayName(node->engine()));
            entry.insert(QStringLiteral("engine_id"),
                         static_cast<int>(node->engine()));
            // The effective launch template plus the rule for reading it: a
            // template naming {script} IS the command line, otherwise it is a
            // solver command that belongs in this environment variable while
            // the job runs `{python} {script}`. Exporting the rule beats
            // asking the reader to keep a per-engine table.
            const QString launchTemplate =
                !node->configuredRunCommand().trimmed().isEmpty()
                ? node->configuredRunCommand()
                : RunCommands::templateFor(node->engine());
            entry.insert(
                QStringLiteral("launch"),
                QJsonObject{
                    {QStringLiteral("template"), launchTemplate},
                    {QStringLiteral("solver_env"),
                     RunCommands::solverCommandVariable(node->engine())},
                });
            if (!node->configuredPython().isEmpty())
                entry.insert(QStringLiteral("python"),
                             node->configuredPython());
            if (node->isConfigured())
                entry.insert(QStringLiteral("script"),
                             node->configuredScript());
        }

        switch (task) {
        case OrchestrationTask::Container: {
            QJsonArray structures;
            for (const auto& [name, structure] : node->batchItems()) {
                QString error;
                if (!structure) {
                    *warnings << QObject::tr(
                                     "\"%1\" in %2 has no geometry and was "
                                     "left out.")
                                     .arg(name, node->title());
                    continue;
                }
                const QString text = toExtxyz(*structure, scratch, &error);
                if (text.isEmpty()) {
                    // Dropped, loudly. A container entry exported without its
                    // geometry would fail on the cluster, hours later, with
                    // nothing on the canvas to explain it.
                    *warnings << QObject::tr(
                                     "\"%1\" in %2 could not be written (%3) "
                                     "and was left out.")
                                     .arg(name, node->title(), error);
                    continue;
                }
                structures.append(structureEntry(name, text));
            }
            entry.insert(QStringLiteral("structures"), structures);
            break;
        }
        case OrchestrationTask::Supercell:
            entry.insert(QStringLiteral("supercell"),
                         node->supercell().toJson());
            break;
        case OrchestrationTask::DefectGenerator:
            entry.insert(QStringLiteral("defects"),
                         node->defectSpec().toJson());
            break;
        case OrchestrationTask::TdbGenerator:
            // Additive: an older reader ignores the key and gets a node it
            // will run with its own defaults, which is a correct (if
            // differently parameterised) assessment rather than a wrong one.
            // That is why kSchema is not bumped here.
            entry.insert(QStringLiteral("tdb_generator"),
                         node->tdbGenerator().toJson());
            break;
        default:
            // A node seeded with its own structure through the scripting API
            // rather than fed from a container. Rare, but it has to survive
            // the round trip or the exported file would compute something
            // else than the canvas does.
            if (node->structure()) {
                QString error;
                const QString text =
                    toExtxyz(*node->structure(), scratch, &error);
                if (text.isEmpty())
                    *warnings << QObject::tr(
                                     "The structure held by %1 could not be "
                                     "written (%2).")
                                     .arg(node->title(), error);
                else
                    entry.insert(QStringLiteral("source_structure"),
                                 structureEntry(node->materialName(), text));
            }
            break;
        }
        nodeArray.append(entry);
    }

    QJsonArray edgeArray;
    for (const auto& [from, to] : window.links())
        edgeArray.append(QJsonObject{{QStringLiteral("from"), from->id()},
                                     {QStringLiteral("to"), to->id()}});

    return QJsonObject{
        {QStringLiteral("schema"), QString::fromLatin1(kSchema)},
        {QStringLiteral("generator"),
         QStringLiteral("Calango %1").arg(QString::fromLatin1(CALANGO_VERSION))},
        {QStringLiteral("nodes"), nodeArray},
        {QStringLiteral("edges"), edgeArray},
    };
}

bool OrchestrationDocument::write(const QJsonObject& document,
                                  const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray bytes =
        QJsonDocument(document).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.flush()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool OrchestrationDocument::load(OrchestrationWindow& window,
                                 const QJsonObject& document, QString* error)
{
    const QString schema = document.value(QStringLiteral("schema")).toString();
    bool readable = false;
    for (const char* known : kReadableSchemas)
        readable = readable || schema == QLatin1String(known);
    if (!readable) {
        if (error)
            *error = QObject::tr(
                         "This is not a Calango workflow file this version can "
                         "read (schema \"%1\", expected \"%2\").")
                         .arg(schema, QString::fromLatin1(kSchema));
        return false;
    }

    QTemporaryDir scratch;
    // Written ids are preserved only as a mapping: the window numbers its own
    // nodes, and two documents loaded in one session would otherwise collide.
    std::map<int, OrchestrationNodeItem*> byDocumentId;
    for (const QJsonValue& value :
         document.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject entry = value.toObject();
        const std::optional<OrchestrationTask> task =
            orchestrationTaskFromSlug(entry.value(QStringLiteral("task")).toString());
        if (!task) {
            if (error)
                *error = QObject::tr("Unknown process type \"%1\".")
                             .arg(entry.value(QStringLiteral("task")).toString());
            return false;
        }
        const auto engine = static_cast<core::CalculatorKind>(
            entry.value(QStringLiteral("engine_id"))
                .toInt(static_cast<int>(core::CalculatorKind::EMT)));
        OrchestrationNodeItem* node = window.addProcessNode(*task, engine);
        if (!node)
            continue;
        const QJsonObject position =
            entry.value(QStringLiteral("position")).toObject();
        node->setPos(position.value(QStringLiteral("x")).toDouble(),
                     position.value(QStringLiteral("y")).toDouble());

        if (entry.value(QStringLiteral("configured")).toBool())
            window.configureNode(
                node, entry.value(QStringLiteral("script")).toString(),
                entry.value(QStringLiteral("python")).toString(),
                entry.value(QStringLiteral("launch"))
                    .toObject()
                    .value(QStringLiteral("template"))
                    .toString(),
                engine);

        switch (*task) {
        case OrchestrationTask::Container: {
            QList<OrchestrationNodeItem::BatchItem> items;
            for (const QJsonValue& item :
                 entry.value(QStringLiteral("structures")).toArray()) {
                const QJsonObject object = item.toObject();
                QString problem;
                const auto structure =
                    fromExtxyz(object.value(QStringLiteral("data")).toString(),
                               scratch, &problem);
                if (!structure) {
                    if (error)
                        *error = QObject::tr("\"%1\" could not be read: %2")
                                     .arg(object.value(QStringLiteral("name"))
                                              .toString(),
                                          problem);
                    return false;
                }
                items.append(
                    {object.value(QStringLiteral("name")).toString(), structure});
            }
            window.setNodeBatchItems(node, items);
            break;
        }
        case OrchestrationTask::Supercell:
            window.setNodeSupercell(
                node, SupercellSpec::fromJson(
                          entry.value(QStringLiteral("supercell")).toObject()));
            break;
        case OrchestrationTask::DefectGenerator:
            window.setNodeDefectSpec(
                node, DefectSpec::fromJson(
                          entry.value(QStringLiteral("defects")).toObject()));
            break;
        case OrchestrationTask::TdbGenerator:
            window.setNodeTdbGenerator(
                node, TdbGeneratorSpec::fromJson(
                          entry.value(QStringLiteral("tdb_generator"))
                              .toObject()));
            break;
        default:
            break;
        }
        byDocumentId[entry.value(QStringLiteral("id")).toInt()] = node;
    }

    // Links last, and in file order — link order IS slot order, so a document
    // whose edges were reordered would feed a two-input node its parents the
    // other way round.
    for (const QJsonValue& value :
         document.value(QStringLiteral("edges")).toArray()) {
        const QJsonObject edge = value.toObject();
        const auto from = byDocumentId.find(
            edge.value(QStringLiteral("from")).toInt());
        const auto to =
            byDocumentId.find(edge.value(QStringLiteral("to")).toInt());
        if (from == byDocumentId.end() || to == byDocumentId.end()) {
            if (error)
                *error = QObject::tr("A link names a node that is not in the "
                                     "file.");
            return false;
        }
        window.linkNodes(from->second, to->second);
    }
    return true;
}

} // namespace calango::gui
