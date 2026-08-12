// Orchestration batching, transformation nodes, provenance and resume.
//
// Four things are pinned here, each of which is silent when it breaks:
//
//   * BATCH FAN-OUT. A Structure Container holding Cu, Au and Pt makes the
//     downstream pipeline run three times, once per structure, and each pass
//     works on ITS OWN element — not three copies of the first one. The check
//     is the chemical symbol in each pass's staged input, so a batch that
//     silently reuses one structure fails here rather than producing three
//     identical "results" for three different metals.
//
//   * TRANSFORMS. Supercell Builder repeats the cell that reaches it (2x1x1
//     doubles the atom count) and Defect Generator removes the atom it was
//     told to. Both run in process, so this half of the test needs ASE but no
//     job launching at all.
//
//   * PROVENANCE. Every executed node leaves a provenance.json naming its
//     parents, the files staged into it, where each came from and its
//     SHA-256; the orchestration folder leaves a manifest of the whole graph.
//     The checksum is what makes "this run consumed that run's output"
//     verifiable rather than merely plausible.
//
//   * RESUME. A pipeline whose last node fails keeps every upstream result;
//     re-configuring the failed node and pressing Resume re-runs THAT node
//     only — the parent's job directory is byte-identical afterwards, which
//     is the assertion that six hours of upstream work were not thrown away.
//
// EMT copper/gold/platinum keeps every force evaluation in microseconds.
// Self-skips (exit 0) when the interpreter or ASE is unavailable, like the
// other integration benchmarks.

#include "core/AseScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "core/UnitCell.hpp"
#include "gui/OrchestrationDocument.hpp"
#include "core/WorkflowReport.hpp"
#include "gui/OrchestrationWindow.hpp"
#include "gui/SettingsManager.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>

#include <array>
#include <cstdio>
#include <memory>

namespace {

int failures = 0;

void check(bool ok, const char* label)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", label);
    if (!ok)
        ++failures;
}

QString readAll(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

QJsonObject readJson(const QString& path)
{
    return QJsonDocument::fromJson(readAll(path).toUtf8()).object();
}

/// fcc conventional cell of `z` with lattice constant `a`.
std::shared_ptr<calango::core::Structure> fcc(int z, double a)
{
    using calango::core::Atom;
    using calango::core::Structure;
    using calango::core::UnitCell;
    auto structure = std::make_shared<Structure>();
    structure->setCell(UnitCell({a, 0.0, 0.0}, {0.0, a, 0.0}, {0.0, 0.0, a}));
    const std::array<std::array<double, 3>, 4> sites = {
        {{0.0, 0.0, 0.0}, {0.0, 0.5, 0.5}, {0.5, 0.0, 0.5}, {0.5, 0.5, 0.0}}};
    for (const auto& site : sites) {
        Atom atom;
        atom.atomicNumber = z;
        atom.position = {a * site[0], a * site[1], a * site[2]};
        structure->addAtom(atom);
    }
    return structure;
}

/// Number of atoms in an extxyz file: its first line.
int extxyzCount(const QString& path)
{
    const QString text = readAll(path);
    if (text.isEmpty())
        return -1;
    bool ok = false;
    const int count = text.section(QLatin1Char('\n'), 0, 0).trimmed().toInt(&ok);
    return ok ? count : -1;
}

/// True when every atom line of the extxyz names `symbol`.
bool extxyzIsAllOf(const QString& path, const QString& symbol)
{
    const QStringList lines = readAll(path).split(QLatin1Char('\n'));
    if (lines.size() < 3)
        return false;
    int seen = 0;
    for (int i = 2; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty())
            continue;
        if (line.section(QLatin1Char(' '), 0, 0) != symbol)
            return false;
        ++seen;
    }
    return seen > 0;
}

/// A script that just records what it was given and succeeds. Writes the atom
/// count and the first element it sees, so a batch pass is identifiable from
/// its own results directory without parsing anything ASE wrote.
QString probeScript()
{
    return QStringLiteral(
        "import pathlib, json\n"
        "lines = pathlib.Path('structure.extxyz').read_text().splitlines()\n"
        "symbols = sorted({l.split()[0] for l in lines[2:] if l.strip()})\n"
        "json.dump({'natoms': int(lines[0]), 'symbols': symbols},\n"
        "          open('probe.json', 'w'))\n"
        "print('CALANGO_DONE', flush=True)\n");
}

/// The probe, plus the metrics.json every real generated script writes.
///
/// Used by the report test so the chain it exercises is the real one — script
/// writes an artifact, the report extracts from that artifact — rather than a
/// report handed its numbers by the test.
QString probeScriptWithMetrics()
{
    return QStringLiteral(
        "import pathlib, json\n"
        "lines = pathlib.Path('structure.extxyz').read_text().splitlines()\n"
        "symbols = sorted({l.split()[0] for l in lines[2:] if l.strip()})\n"
        "json.dump({'natoms': int(lines[0]), 'symbols': symbols},\n"
        "          open('probe.json', 'w'))\n"
        "json.dump({'metrics': [{'step': 1, 'energy': -4.5, 'max_force': 0.31},\n"
        "                       {'step': 2, 'energy': -4.75, 'max_force': 0.02}]},\n"
        "          open('metrics.json', 'w'))\n"
        "print('CALANGO_DONE', flush=True)\n");
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication::setOrganizationName(QStringLiteral("CalangoTest"));
    QCoreApplication::setApplicationName(
        QStringLiteral("OrchestrationBatchTest"));

    QTemporaryDir sandbox;
    if (!sandbox.isValid()) {
        std::printf("SKIP: no temporary directory available\n");
        return 0;
    }
    qputenv("CALANGO_CONFIG_DIR",
            (sandbox.path() + QStringLiteral("/.calango")).toLocal8Bit());

    QApplication app(argc, argv);

    calango::pybridge::PythonEngine python;
    const QString pythonExe = QString::fromStdString(python.executable());
    if (pythonExe.isEmpty()
        || pythonExe == QCoreApplication::applicationFilePath()
        || !pythonExe.contains(QStringLiteral("python"))) {
        std::printf("SKIP: no usable interpreter (resolved to %s)\n",
                    qPrintable(pythonExe));
        return 0;
    }
    {
        QProcess probe;
        probe.start(pythonExe,
                    {QStringLiteral("-c"), QStringLiteral("import ase")});
        if (!probe.waitForFinished(30000) || probe.exitCode() != 0) {
            probe.kill();
            probe.waitForFinished(2000);
            std::printf("SKIP: %s cannot import ase\n", qPrintable(pythonExe));
            return 0;
        }
    }

    using calango::gui::DefectOperation;
    using calango::gui::DefectSpec;
    using calango::gui::OrchestrationNodeItem;
    using calango::gui::OrchestrationTask;
    using calango::gui::OrchestrationWindow;
    using calango::gui::SupercellSpec;

    const OrchestrationWindow::MaterialList materials = {
        {QStringLiteral("Cu"), fcc(29, 3.61)},
        {QStringLiteral("Au"), fcc(79, 4.08)},
        {QStringLiteral("Pt"), fcc(78, 3.92)},
    };
    const auto pythonResolver =
        [&pythonExe](calango::core::CalculatorKind) { return pythonExe; };
    const auto terminal = [](const OrchestrationNodeItem* node) {
        return node->status() == OrchestrationNodeItem::Status::Done
            || node->status() == OrchestrationNodeItem::Status::Failed
            || node->status() == OrchestrationNodeItem::Status::Skipped;
    };
    /// Spin the event loop until `node` reaches a terminal state, then drain
    /// what the completion handler queued behind it.
    const auto settle = [&terminal](OrchestrationNodeItem* node, int ms) {
        QElapsedTimer timer;
        timer.start();
        while (!terminal(node) && timer.elapsed() < ms)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        for (int turn = 0; turn < 20; ++turn)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    };

    // ---- Scenario 1: the transform nodes, with no batching ----------------
    //
    // Container(Cu) -> Supercell(2x1x1) -> DefectGenerator(remove 0) -> probe.
    // Every step is checkable from the file the next step reads.
    std::printf("Transform chain (container, supercell, defect):\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_transforms"));
    {
        OrchestrationWindow window(materials, pythonResolver);
        QStringList refusals;
        window.setRefusalHandler(
            [&refusals](const QString& message) { refusals << message; });

        OrchestrationNodeItem* container = window.addProcessNode(
            OrchestrationTask::Container, 0, calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* cell = window.addProcessNode(
            OrchestrationTask::Supercell, 0, calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* defect = window.addProcessNode(
            OrchestrationTask::DefectGenerator, 0,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* probe = window.addProcessNode(
            OrchestrationTask::SinglePoint, 0,
            calango::core::CalculatorKind::EMT);
        check(container && cell && defect && probe,
              "container, supercell, defect and probe nodes exist");
        // A fresh container holds the material it was created on, so it is
        // runnable the moment it is placed.
        check(container->batchItems().size() == 1,
              "a new container starts holding its own material");

        window.linkNodes(container, cell);
        window.linkNodes(cell, defect);
        window.linkNodes(defect, probe);
        window.setNodeSupercell(cell, SupercellSpec::diagonal(2, 1, 1));
        DefectSpec spec;
        DefectOperation remove;
        remove.kind = DefectOperation::Kind::Remove;
        remove.indices = QStringLiteral("0");
        spec.operations.append(remove);
        window.setNodeDefectSpec(defect, spec);
        window.configureNode(probe, probeScript(), pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);

        window.sendToProcesses();
        settle(probe, 120000);
        check(refusals.isEmpty(), "nothing was refused");
        check(container->status() == OrchestrationNodeItem::Status::Done
                  && cell->status() == OrchestrationNodeItem::Status::Done
                  && defect->status() == OrchestrationNodeItem::Status::Done
                  && probe->status() == OrchestrationNodeItem::Status::Done,
              "every node in the chain finished");

        // 4 atoms -> 8 after 2x1x1 -> 7 after removing one.
        check(extxyzCount(container->jobDirectory()
                          + QStringLiteral("/transformed.extxyz")) == 4,
              "the container emits its structure (4 atoms)");
        check(extxyzCount(cell->jobDirectory()
                          + QStringLiteral("/transformed.extxyz")) == 8,
              "2 x 1 x 1 doubles the cell to 8 atoms");
        check(extxyzCount(defect->jobDirectory()
                          + QStringLiteral("/transformed.extxyz")) == 7,
              "removing one atom leaves 7");
        const QJsonObject result = readJson(probe->jobDirectory()
                                            + QStringLiteral("/probe.json"));
        check(result.value(QStringLiteral("natoms")).toInt() == 7,
              "and the downstream job actually received those 7 atoms");

        // Provenance: the defect node names its parent, its staged input and
        // that input's checksum, and the checksum matches the file the parent
        // actually produced.
        const QJsonObject record =
            readJson(defect->jobDirectory() + QStringLiteral("/provenance.json"));
        const QJsonObject logical =
            record.value(QStringLiteral("logical")).toObject();
        const QJsonArray parents =
            logical.value(QStringLiteral("parents")).toArray();
        check(parents.size() == 1
                  && parents[0].toObject().value(QStringLiteral("id")).toInt()
                      == cell->id(),
              "the defect node's provenance names the supercell node as parent");
        check(logical.value(QStringLiteral("parameters")).toString()
                  .contains(QStringLiteral("remove 0")),
              "and records the recipe it applied");
        const QJsonArray inputs = record.value(QStringLiteral("data"))
                                      .toObject()
                                      .value(QStringLiteral("inputs"))
                                      .toArray();
        check(inputs.size() == 1
                  && inputs[0].toObject().value(QStringLiteral("hashed")).toBool(),
              "its one staged input is recorded with a checksum");
        const QString stagedHash =
            inputs[0].toObject().value(QStringLiteral("sha256")).toString();
        const QString parentHash = calango::gui::fileSha256(
            cell->jobDirectory() + QStringLiteral("/transformed.extxyz"));
        check(!stagedHash.isEmpty() && stagedHash == parentHash,
              "and the checksum is that of the parent's own output file");

        // The orchestration-level manifest: the whole graph, on disk.
        const QJsonObject manifest =
            readJson(window.orchestrationRoot()
                     + QStringLiteral("/orchestration.json"));
        check(manifest.value(QStringLiteral("nodes")).toArray().size() == 4
                  && manifest.value(QStringLiteral("edges")).toArray().size() == 3,
              "the manifest records all four nodes and all three links");
    }

    // ---- Scenario 2: batch fan-out over three metals -----------------------
    std::printf("Batch fan-out (Cu, Au, Pt):\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_batch"));
    {
        OrchestrationWindow window(materials, pythonResolver);
        QStringList refusals;
        window.setRefusalHandler(
            [&refusals](const QString& message) { refusals << message; });

        OrchestrationNodeItem* container = window.addProcessNode(
            OrchestrationTask::Container, 0, calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* probe = window.addProcessNode(
            OrchestrationTask::SinglePoint, 0,
            calango::core::CalculatorKind::EMT);
        window.linkNodes(container, probe);
        window.setNodeBatchItems(container, materials); // Cu, Au, Pt
        window.configureNode(probe, probeScript(), pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);

        window.sendToProcesses();
        check(window.batchLength() == 3,
              "three structures in the container means three passes");
        // Three passes: wait for the third run to have both STARTED (its
        // directory is recorded at launch) and finished.
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 180000
               && !(probe->jobHistory().size() >= 3 && terminal(probe)))
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        for (int turn = 0; turn < 20; ++turn)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

        check(refusals.isEmpty(), "nothing was refused");
        check(probe->status() == OrchestrationNodeItem::Status::Done,
              "the downstream node finished its last pass");
        check(probe->jobHistory().size() == 3,
              "and ran three times, once per structure");
        check(container->jobHistory().size() == 3,
              "the container itself emitted three times");

        // The payoff: each pass worked on ITS OWN element. A batch that
        // reused one structure would give three identical symbol lists here
        // while every status still read "done".
        const QStringList symbols = {QStringLiteral("Cu"), QStringLiteral("Au"),
                                     QStringLiteral("Pt")};
        bool perPassCorrect = true;
        for (int pass = 0; pass < probe->jobHistory().size(); ++pass) {
            const QString dir = probe->jobHistory()[pass];
            const QJsonObject result =
                readJson(dir + QStringLiteral("/probe.json"));
            const QJsonArray seen =
                result.value(QStringLiteral("symbols")).toArray();
            if (seen.size() != 1 || seen[0].toString() != symbols[pass])
                perPassCorrect = false;
            if (!extxyzIsAllOf(dir + QStringLiteral("/structure.extxyz"),
                               symbols[pass]))
                perPassCorrect = false;
        }
        check(perPassCorrect,
              "pass 1 is Cu, pass 2 is Au, pass 3 is Pt — in the container's "
              "order");

        // Each pass has its own labelled folder, so the three studies are
        // told apart on disk and not only in memory.
        const QStringList batches =
            QDir(window.orchestrationRoot())
                .entryList({QStringLiteral("batch_*")}, QDir::Dirs);
        check(batches.size() == 3
                  && batches.contains(QStringLiteral("batch_2_Au")),
              "and each pass staged into its own labelled folder");

        // Provenance carries the batch coordinate, which is the only thing
        // distinguishing three otherwise identical records.
        const QJsonObject record = readJson(probe->jobHistory()[1]
                                            + QStringLiteral("/provenance.json"));
        const QJsonObject batch = record.value(QStringLiteral("batch")).toObject();
        check(batch.value(QStringLiteral("index")).toInt() == 1
                  && batch.value(QStringLiteral("total")).toInt() == 3
                  && batch.value(QStringLiteral("label")).toString()
                      == QStringLiteral("Au"),
              "the second pass's record says it is item 2 of 3, \"Au\"");
    }

    // ---- Scenario 3: resume strictly from the failed node -------------------
    std::printf("Resume from failure:\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_resume"));
    {
        OrchestrationWindow window(materials, pythonResolver);
        QStringList refusals;
        window.setRefusalHandler(
            [&refusals](const QString& message) { refusals << message; });

        OrchestrationNodeItem* upstream = window.addProcessNode(
            OrchestrationTask::SinglePoint, 0,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* broken = window.addProcessNode(
            OrchestrationTask::SinglePoint, 0,
            calango::core::CalculatorKind::EMT);
        window.linkNodes(upstream, broken);
        window.configureNode(upstream, probeScript(), pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        // A parameter mistake, standing in for the six-hour run that dies on
        // one bad number.
        window.configureNode(
            broken,
            QStringLiteral("raise SystemExit('bad parameter')\n"), pythonExe,
            QString(), calango::core::CalculatorKind::EMT);

        window.sendToProcesses();
        settle(broken, 120000);
        check(upstream->status() == OrchestrationNodeItem::Status::Done,
              "the upstream node finished");
        check(broken->status() == OrchestrationNodeItem::Status::Failed,
              "and the downstream node failed");
        check(window.canResume(), "so the pipeline reports itself resumable");

        // What must survive: the successful upstream run, byte for byte.
        const QString upstreamDir = upstream->jobDirectory();
        const QString upstreamResult =
            readAll(upstreamDir + QStringLiteral("/probe.json"));
        const QString rootBefore = window.orchestrationRoot();
        check(!upstreamResult.isEmpty(),
              "and its results are on disk");
        const QJsonObject failedRecord =
            readJson(broken->jobDirectory() + QStringLiteral("/provenance.json"));
        check(failedRecord.value(QStringLiteral("execution"))
                      .toObject()
                      .value(QStringLiteral("status"))
                      .toString()
                  == QStringLiteral("failed"),
              "the failed attempt left a provenance record saying so");

        // Fix the parameters and resume.
        refusals.clear();
        window.configureNode(broken, probeScript(), pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        window.resumeFromFailure();
        settle(broken, 120000);

        check(refusals.isEmpty(), "resuming refused nothing");
        check(broken->status() == OrchestrationNodeItem::Status::Done,
              "the fixed node now completes");
        check(window.orchestrationRoot() == rootBefore,
              "in the SAME orchestration folder");
        check(upstream->jobDirectory() == upstreamDir
                  && upstream->jobHistory().size() == 1,
              "and the upstream node was not re-run");
        check(readAll(upstreamDir + QStringLiteral("/probe.json"))
                  == upstreamResult,
              "its results are untouched");
        check(broken->jobHistory().size() == 2
                  && broken->jobHistory()[0] != broken->jobHistory()[1],
              "the retry got its own directory, keeping the failed attempt");
        const QJsonObject retryRecord =
            readJson(broken->jobDirectory() + QStringLiteral("/provenance.json"));
        check(retryRecord.value(QStringLiteral("logical"))
                  .toObject()
                  .value(QStringLiteral("attempt"))
                  .toInt() == 2,
              "and its record is marked attempt 2");
    }

    // ---- Scenario 4: Fit to Screen frames the whole graph -------------------
    std::printf("Fit to Screen:\n");
    {
        OrchestrationWindow window(materials, pythonResolver);
        window.resize(640, 400);
        window.show();
        QCoreApplication::processEvents();
        for (int i = 0; i < 5; ++i)
            window.addProcessNode(OrchestrationTask::SinglePoint, 0,
                                  calango::core::CalculatorKind::EMT);
        const QRectF nodes = window.nodesBoundingRect();
        check(!nodes.isNull() && nodes.width() > 640,
              "five staggered nodes are wider than the viewport");
        check(!window.visibleSceneRect().contains(nodes),
              "so at the default zoom they do not all fit");
        window.fitToScreen();
        QCoreApplication::processEvents();
        const QRectF visible = window.visibleSceneRect();
        check(visible.contains(nodes), "after Fit to Screen every node is visible");
        // And with room to spare: nothing touches the border.
        check(visible.left() < nodes.left() && visible.right() > nodes.right()
                  && visible.top() < nodes.top()
                  && visible.bottom() > nodes.bottom(),
              "with a margin on every side");
    }

    // ---- Scenario 5: structures arrive through the port, not the node ------
    //
    // The shape every node built from the UI has: no material of its own, fed
    // by a Container. A node with neither is refused rather than run on a
    // geometry nobody chose.
    std::printf("Port-fed structures:\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_ports"));
    {
        OrchestrationWindow window(materials, pythonResolver);
        QStringList refusals;
        window.setRefusalHandler(
            [&refusals](const QString& message) { refusals << message; });

        OrchestrationNodeItem* orphan = window.addProcessNode(
            OrchestrationTask::SinglePoint, calango::core::CalculatorKind::EMT);
        check(orphan && !orphan->structure(),
              "a node added without a material owns no structure");
        window.configureNode(orphan, probeScript(), pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        window.sendToProcesses();
        settle(orphan, 60000);
        check(orphan->status() != OrchestrationNodeItem::Status::Done,
              "and with nothing linked to it, it does not run");
        check(std::any_of(refusals.cbegin(), refusals.cend(),
                          [](const QString& m) {
                              return m.contains(
                                  QStringLiteral("Structure Container"));
                          }),
              "the refusal tells the user to link a Structure Container");
    }

    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_ports2"));
    {
        OrchestrationWindow window(materials, pythonResolver);
        QStringList refusals;
        window.setRefusalHandler(
            [&refusals](const QString& message) { refusals << message; });

        OrchestrationNodeItem* container = window.addProcessNode(
            OrchestrationTask::Container, calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* probe = window.addProcessNode(
            OrchestrationTask::SinglePoint, calango::core::CalculatorKind::EMT);
        check(container->batchItems().isEmpty(),
              "a container added without a material starts empty");
        window.linkNodes(container, probe);
        // Only the second material, so "the structure came from the container"
        // is decidable: it is Au, not the Cu that used to be node 0's default.
        window.setNodeBatchItems(container, {materials[1]});
        window.configureNode(probe, probeScript(), pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        window.sendToProcesses();
        settle(probe, 60000);
        check(refusals.isEmpty() && probe->status()
                  == OrchestrationNodeItem::Status::Done,
              "fed by a container, the same node runs");
        const QJsonObject result =
            readJson(probe->jobDirectory() + QStringLiteral("/probe.json"));
        const QJsonArray symbols =
            result.value(QStringLiteral("symbols")).toArray();
        check(symbols.size() == 1
                  && symbols[0].toString() == QStringLiteral("Au"),
              "on the structure the container held");
    }

    // ---- Scenario 6: the exported workflow document ------------------------
    //
    // The file calango-cli runs on a cluster. It has to be self-contained
    // (structures inside it), self-describing (each node carries its own slot
    // table) and round-trippable — a serializer nothing reads back is one
    // whose bugs are found on a login node.
    std::printf("Workflow document:\n");
    {
        OrchestrationWindow window(materials, pythonResolver);
        OrchestrationNodeItem* container = window.addProcessNode(
            OrchestrationTask::Container, calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* cell = window.addProcessNode(
            OrchestrationTask::Supercell, calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* relax = window.addProcessNode(
            OrchestrationTask::GeometryOptimization,
            calango::core::CalculatorKind::Gpaw);
        window.linkNodes(container, cell);
        window.linkNodes(cell, relax);
        window.setNodeBatchItems(container, materials);
        window.setNodeSupercell(cell, SupercellSpec::diagonal(2, 1, 1));
        window.configureNode(relax, probeScript(), pythonExe,
                             QStringLiteral("gpaw -P {cores} python {script}"),
                             calango::core::CalculatorKind::Gpaw);

        QStringList warnings;
        const QJsonObject document =
            calango::gui::OrchestrationDocument::build(window, &warnings);
        check(warnings.isEmpty(), "the document builds with no structure lost");
        check(document.value(QStringLiteral("schema")).toString()
                  == QStringLiteral("calango.workflow/2"),
              "and carries its schema version");

        const QJsonArray nodes =
            document.value(QStringLiteral("nodes")).toArray();
        check(nodes.size() == 3
                  && document.value(QStringLiteral("edges")).toArray().size() == 2,
              "three nodes and two links");
        const QJsonObject containerJson = nodes[0].toObject();
        const QJsonArray structures =
            containerJson.value(QStringLiteral("structures")).toArray();
        check(structures.size() == 3,
              "the container's three structures travel inside the file");
        check(structures[1].toObject().value(QStringLiteral("data"))
                  .toString().contains(QStringLiteral("Au")),
              "as extxyz text, not as a path into somebody's laptop");
        const QJsonObject relaxJson = nodes[2].toObject();
        check(relaxJson.value(QStringLiteral("launch")).toObject()
                  .value(QStringLiteral("template")).toString()
                  .contains(QStringLiteral("{script}")),
              "a simulation node exports the launch template");
        check(relaxJson.value(QStringLiteral("family")).toString()
                  == QStringLiteral("simulation")
                  && containerJson.value(QStringLiteral("family")).toString()
                      == QStringLiteral("transform"),
              "and each node says which family it is in");

        // -- VASP through a workflow chain ---------------------------------
        //
        // VASP is the engine in this list that needs the most from outside the
        // script: an external binary, an MPI launch, and a POTCAR tree found
        // through an environment variable. Every one of those is carried by
        // the node rather than by the script text, so a workflow that
        // serialized the script alone would reload into something that cannot
        // run — and the failure would appear only when the job started.
        {
            using calango::core::CalculatorKind;
            calango::core::CalculatorConfig vaspConfig;
            vaspConfig.calculator = CalculatorKind::Vasp;
            vaspConfig.task = calango::core::TaskKind::GeometryOptimization;
            vaspConfig.planeWaveCutoffEv = 520.0;
            vaspConfig.kpts[0] = vaspConfig.kpts[1] = vaspConfig.kpts[2] = 5;
            vaspConfig.vaspXc = "PBEsol";
            vaspConfig.vaspPotcarPath = "/opt/vasp/POTCARs";
            const QString vaspScript = QString::fromStdString(
                calango::core::AseScriptGenerator::generate(
                    vaspConfig, "structure.extxyz"));

            OrchestrationWindow vaspWindow(materials, pythonResolver);
            OrchestrationNodeItem* source = vaspWindow.addProcessNode(
                OrchestrationTask::Container, 0, CalculatorKind::Vasp);
            OrchestrationNodeItem* node = vaspWindow.addProcessNode(
                OrchestrationTask::GeometryOptimization, 0,
                CalculatorKind::Vasp);
            vaspWindow.linkNodes(source, node);
            vaspWindow.configureNode(node, vaspScript, QString(), QString(),
                                     CalculatorKind::Vasp);

            // 1. The INCAR/KPOINTS/POTCAR settings reach the script at all.
            check(vaspScript.contains(QStringLiteral("encut=520")),
                  "VASP: ENCUT reaches the generated script");
            check(vaspScript.contains(QStringLiteral("kpts=(5, 5, 5)")),
                  "VASP: the k-mesh reaches it");
            check(vaspScript.contains(QStringLiteral("xc=\"PBEsol\"")),
                  "VASP: so does the functional");
            check(vaspScript.contains(QStringLiteral("VASP_PP_PATH")),
                  "VASP: and the POTCAR root is exported into the run");
            // 2. The chain contract: reads the staged input, writes the output
            //    the next node's link extracts.
            check(vaspScript.contains(QStringLiteral("structure.extxyz")),
                  "VASP: reads the structure the link stages for it");
            check(vaspScript.contains(QStringLiteral("optimized.extxyz")),
                  "VASP: and writes the geometry the next node inherits");

            QStringList vaspWarnings;
            const QJsonObject vaspDoc =
                calango::gui::OrchestrationDocument::build(vaspWindow,
                                                           &vaspWarnings);
            const QJsonArray vaspNodes =
                vaspDoc.value(QStringLiteral("nodes")).toArray();
            check(vaspNodes.size() == 2, "VASP: the chain exports both nodes");
            QJsonObject vaspNode;
            for (const QJsonValue& value : vaspNodes)
                if (value.toObject().value(QStringLiteral("family")).toString()
                    == QStringLiteral("simulation"))
                    vaspNode = value.toObject();

            // 3. The three bindings a reloaded VASP node needs, none of which
            //    live in the script.
            check(vaspNode.value(QStringLiteral("engine_id")).toInt(-1)
                      == static_cast<int>(CalculatorKind::Vasp),
                  "VASP: the engine survives serialization by id, not by its "
                  "display name");
            const QJsonObject launch =
                vaspNode.value(QStringLiteral("launch")).toObject();
            check(launch.value(QStringLiteral("template")).toString()
                      .contains(QStringLiteral("vasp_std")),
                  "VASP: the launch template names the solver binary");
            check(launch.value(QStringLiteral("solver_env")).toString()
                      == QStringLiteral("ASE_VASP_COMMAND"),
                  "VASP: and the env var ASE reads it from — a VASP node that "
                  "exported no solver_env would reload as a job with nothing "
                  "to execute");
            check(vaspNode.value(QStringLiteral("script")).toString()
                      .contains(QStringLiteral("encut=520")),
                  "VASP: the configured script travels with the node");

            // 4. Round-trip: reload and confirm the engine came back. Losing
            //    it would silently demote the node to the default engine,
            //    which now runs GPAW rather than failing.
            OrchestrationWindow reloadedVasp(materials, pythonResolver);
            QString vaspError;
            check(calango::gui::OrchestrationDocument::load(
                      reloadedVasp, vaspDoc, &vaspError),
                  "VASP: the workflow file reloads");
            bool engineKept = false;
            for (OrchestrationNodeItem* item : reloadedVasp.nodes())
                if (item->engine() == CalculatorKind::Vasp)
                    engineKept = true;
            check(engineKept,
                  "VASP: and the reloaded node is still a VASP node");
        }

        // Self-describing: a two-slot node exports the slot table, so a reader
        // never needs its own copy of the module knowledge.
        OrchestrationWindow slots(materials, pythonResolver);
        slots.addProcessNode(OrchestrationTask::ChargedDefects2d,
                             calango::core::CalculatorKind::Gpaw);
        QStringList ignored;
        const QJsonArray exported =
            calango::gui::OrchestrationDocument::build(slots, &ignored)
                .value(QStringLiteral("nodes"))
                .toArray()[0]
                .toObject()
                .value(QStringLiteral("inputs"))
                .toArray();
        check(exported.size() == 2
                  && exported[1].toObject().value(QStringLiteral("staged_as"))
                          .toString() == QStringLiteral("baseline_2.gpw"),
              "an analysis node exports its own input-slot table");

        // Round trip.
        OrchestrationWindow reloaded(materials, pythonResolver);
        QString error;
        check(calango::gui::OrchestrationDocument::load(reloaded, document,
                                                        &error),
              "the document loads back");
        check(reloaded.nodes().size() == 3 && reloaded.links().size() == 2,
              "with the same graph");
        check(reloaded.nodes()[0]->batchItems().size() == 3
                  && reloaded.nodes()[0]->batchItems()[2].first
                      == QStringLiteral("Pt"),
              "the container's contents survive, in order");
        check(reloaded.nodes()[0]->batchItems()[0].second
                  && reloaded.nodes()[0]->batchItems()[0].second->size() == 4,
              "and are real structures again");
        check(reloaded.nodes()[1]->supercell().describe()
                  == QStringLiteral("2 x 1 x 1"),
              "transform parameters survive");
        check(reloaded.nodes()[2]->isConfigured()
                  && reloaded.nodes()[2]->configuredScript() == probeScript(),
              "and so does a committed script");

        QJsonObject wrong = document;
        wrong.insert(QStringLiteral("schema"),
                     QStringLiteral("calango.workflow/99"));
        OrchestrationWindow refused(materials, pythonResolver);
        check(!calango::gui::OrchestrationDocument::load(refused, wrong, &error)
                  && error.contains(QStringLiteral("calango.workflow/2")),
              "a document from a future schema is refused, not half-loaded");

        // Open Workflow: the same document, through the panel's own file path.
        const QString file =
            sandbox.path() + QStringLiteral("/exported_workflow.json");
        check(calango::gui::OrchestrationDocument::write(document, file,
                                                         &error),
              "the document writes to a file");

        OrchestrationWindow opened(materials, pythonResolver);
        QStringList refusalsOnOpen;
        opened.setRefusalHandler([&refusalsOnOpen](const QString& message) {
            refusalsOnOpen << message;
        });
        check(opened.openWorkflow(file) && refusalsOnOpen.isEmpty(),
              "Open Workflow loads it into an empty canvas without asking");
        check(opened.nodes().size() == 3 && opened.links().size() == 2,
              "with the whole graph");
        check(opened.nodes()[0]->batchItems().size() == 3,
              "and the container's structures");

        // Replacing an existing pipeline asks first, and declining keeps it.
        QStringList questions;
        bool allow = false;
        opened.setConfirmHandler([&questions, &allow](const QString& q) {
            questions << q;
            return allow;
        });
        opened.addProcessNode(OrchestrationTask::SinglePoint,
                              calango::core::CalculatorKind::EMT);
        check(!opened.openWorkflow(file) && questions.size() == 1
                  && questions.front().contains(QStringLiteral("replaces")),
              "opening over a pipeline asks before replacing it");
        check(opened.nodes().size() == 4,
              "and declining leaves the canvas exactly as it was");
        allow = true;
        check(opened.openWorkflow(file) && opened.nodes().size() == 3,
              "confirming replaces it");

        // The guarantee that matters: a file that cannot be loaded must leave
        // the pipeline you already had. Neither the old one nor the new one is
        // the one outcome worse than refusing.
        const QString broken =
            sandbox.path() + QStringLiteral("/broken_workflow.json");
        calango::gui::OrchestrationDocument::write(wrong, broken, &error);
        refusalsOnOpen.clear();
        check(!opened.openWorkflow(broken),
              "a document from a future schema is refused on open");
        check(opened.nodes().size() == 3 && opened.links().size() == 2,
              "and the canvas still holds the pipeline it had");
        check(refusalsOnOpen.size() == 1
                  && refusalsOnOpen.front().contains(
                      QStringLiteral("calango.workflow/2")),
              "with the reason reported");

        QFile garbage(sandbox.path() + QStringLiteral("/garbage.json"));
        garbage.open(QIODevice::WriteOnly);
        garbage.write("{ this is not json");
        garbage.close();
        refusalsOnOpen.clear();
        check(!opened.openWorkflow(garbage.fileName())
                  && opened.nodes().size() == 3,
              "so does a file that is not JSON at all");
        refusalsOnOpen.clear();
        check(!opened.openWorkflow(sandbox.path()
                                   + QStringLiteral("/nothing-here.json"))
                  && refusalsOnOpen.size() == 1,
              "and a file that does not exist");
    }

    // ---- Scenario 5b: xTB as an orchestration engine -----------------------
    //
    // The node carries no script of its own, so the runner falls back to the
    // task defaults — and those have to produce an xTB calculator, with the
    // xTB parameters, rather than silently emitting something else. That is
    // the whole "the engine reaches the generator" claim.
    std::printf("xTB engine:\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_xtb"));
    {
        OrchestrationWindow window(materials, pythonResolver);
        QStringList refusals;
        window.setRefusalHandler(
            [&refusals](const QString& message) { refusals << message; });

        OrchestrationNodeItem* container = window.addProcessNode(
            OrchestrationTask::Container, calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* relax = window.addProcessNode(
            OrchestrationTask::GeometryOptimization,
            calango::core::CalculatorKind::Xtb);
        window.linkNodes(container, relax);
        window.setNodeBatchItems(container, {materials[0]});
        check(relax && relax->engine() == calango::core::CalculatorKind::Xtb,
              "a node can be created on the xTB engine");

        // Deliberately NOT configured: this exercises the default-script path.
        window.sendToProcesses();
        settle(relax, 60000);
        const QString script =
            readAll(relax->jobDirectory() + QStringLiteral("/run.py"));
        check(!script.isEmpty(), "the runner generated a script for it");
        check(script.contains(QStringLiteral("xtb")),
              "and the script imports the xTB calculator");
        check(script.contains(QStringLiteral("GFN2-xTB")),
              "carrying the xTB method parameter down to the backend");
        // The node runs only where xtb-python is installed; the script is what
        // this scenario is about, so a failure to execute is not a failure
        // here — but a REFUSAL would mean the canvas rejected the engine.
        check(std::none_of(refusals.cbegin(), refusals.cend(),
                           [](const QString& m) {
                               return m.contains(QStringLiteral("xTB"))
                                   || m.contains(QStringLiteral("not been "
                                                                "configured"));
                           }),
              "and the canvas does not refuse an unconfigured xTB node");

        QStringList warnings;
        const QJsonObject document =
            calango::gui::OrchestrationDocument::build(window, &warnings);
        const QJsonObject relaxJson =
            document.value(QStringLiteral("nodes")).toArray()[1].toObject();
        check(relaxJson.value(QStringLiteral("engine")).toString()
                  == QStringLiteral("xTB"),
              "and the exported workflow names xTB as its engine");
        check(relaxJson.value(QStringLiteral("engine_id")).toInt()
                  == static_cast<int>(calango::core::CalculatorKind::Xtb),
              "with the engine id a reader can act on");
    }

    // ---- Scenario 6b: the supercell transformation matrix ------------------
    //
    // Three multipliers cannot express a rotated cell. The check that matters
    // is that a NON-DIAGONAL matrix survives the round trip intact: a reader
    // that fell back to na/nb/nc would build a different cell and report
    // success, which is the failure the schema version exists to prevent.
    std::printf("Supercell matrix:\n");
    {
        SupercellSpec rotated;                     // sqrt(3) x sqrt(3) R30
        rotated.p[0][0] = 2;  rotated.p[0][1] = 1;  rotated.p[0][2] = 0;
        rotated.p[1][0] = -1; rotated.p[1][1] = 1;  rotated.p[1][2] = 0;
        rotated.p[2][0] = 0;  rotated.p[2][1] = 0;  rotated.p[2][2] = 1;
        check(rotated.determinant() == 3 && !rotated.isDiagonal()
                  && rotated.isValid(),
              "a non-diagonal matrix has |P| = 3 and is not diagonal");
        check(SupercellSpec::diagonal(2, 1, 1).isDiagonal()
                  && SupercellSpec{}.isIdentity(),
              "the diagonal and identity cases are recognised");

        SupercellSpec singular;
        singular.p[1][1] = 0;
        check(!singular.isValid(),
              "a matrix with a zero row is refused as singular");

        const QJsonObject json = rotated.toJson();
        check(!json.contains(QStringLiteral("na")),
              "a non-diagonal matrix writes NO na/nb/nc — an old reader must "
              "not be able to misread it as repetitions");
        check(SupercellSpec::diagonal(2, 3, 1)
                  .toJson()
                  .value(QStringLiteral("nb"))
                  .toInt() == 3,
              "a diagonal one does, so an old reader gets it right");
        const SupercellSpec back = SupercellSpec::fromJson(json);
        bool same = true;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                same = same && back.p[i][j] == rotated.p[i][j];
        check(same, "and the matrix round-trips exactly");
        check(SupercellSpec::fromJson(
                  QJsonObject{{QStringLiteral("na"), 2},
                              {QStringLiteral("nb"), 2},
                              {QStringLiteral("nc"), 1}})
                  .describe() == QStringLiteral("2 x 2 x 1"),
              "a pre-matrix document still reads as diagonal repetitions");

        // Applied for real: |P| = 3 on a 4-atom cell gives 12 atoms.
        QString problem;
        const calango::core::Structure expanded = calango::gui::applySupercell(
            *materials[0].second, rotated, &problem);
        check(problem.isEmpty() && expanded.size() == 12,
              "and applying it gives |P| times as many atoms");
    }

    // ---- Scenario 6c: Auto-Layout ------------------------------------------
    std::printf("Auto-Layout:\n");
    {
        OrchestrationWindow window(materials, pythonResolver);
        // autoLayout() finishes with Fit to Screen, which needs a viewport
        // that has a size.
        window.resize(640, 400);
        window.show();
        QCoreApplication::processEvents();
        // A deliberately tangled diamond: container -> two branches -> join.
        OrchestrationNodeItem* source = window.addProcessNode(
            OrchestrationTask::Container, calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* left = window.addProcessNode(
            OrchestrationTask::SinglePoint, calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* right = window.addProcessNode(
            OrchestrationTask::SinglePoint, calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* join = window.addProcessNode(
            OrchestrationTask::ChargedDefects2d,
            calango::core::CalculatorKind::Gpaw);
        window.linkNodes(source, left);
        window.linkNodes(source, right);
        window.linkNodes(left, join);
        window.linkNodes(right, join);
        // Scatter them so the layout has something to undo.
        source->setPos(900.0, 400.0);
        left->setPos(120.0, -260.0);
        right->setPos(640.0, 700.0);
        join->setPos(-300.0, 90.0);

        window.autoLayout();

        // Execution order reads left to right: every node strictly right of
        // every parent. That is the one property the layout has to be true
        // about — a link running backwards would misdescribe the pipeline.
        bool ordered = true;
        for (const auto& [from, to] : window.links())
            ordered = ordered && from->pos().x() < to->pos().x();
        check(ordered, "every node is placed to the right of its parents");
        // The diamond's two middle nodes share a column and must not overlap.
        check(std::abs(left->pos().x() - right->pos().x()) < 1e-6,
              "siblings share a column");
        check(!left->sceneBoundingRect().intersects(right->sceneBoundingRect()),
              "and do not overlap");
        // Longest path, not shortest: the join is two columns along, not one.
        check(join->pos().x() > left->pos().x()
                  && left->pos().x() > source->pos().x(),
              "the join sits beyond both branches");
        check(window.visibleSceneRect().contains(window.nodesBoundingRect()),
              "and the result is framed on screen");
    }

    // ---- Scenario 7: Clear Orchestration asks first ------------------------
    std::printf("Clear Orchestration:\n");
    {
        OrchestrationWindow window(materials, pythonResolver);
        QStringList asked;
        bool answer = false;
        window.setConfirmHandler([&asked, &answer](const QString& question) {
            asked << question;
            return answer;
        });
        for (int i = 0; i < 3; ++i)
            window.addProcessNode(OrchestrationTask::SinglePoint,
                                  calango::core::CalculatorKind::EMT);
        window.linkNodes(window.nodes()[0], window.nodes()[1]);

        window.clearOrchestration();
        check(asked.size() == 1
                  && asked.front().contains(
                      QStringLiteral("delete all nodes from the workflow")),
              "clearing asks for confirmation first");
        check(window.nodes().size() == 3 && window.links().size() == 1,
              "and declining leaves the canvas exactly as it was");

        answer = true;
        window.clearOrchestration();
        check(window.nodes().empty() && window.links().isEmpty(),
              "confirming removes every node and link");
        check(window.nodesBoundingRect().isNull(), "the canvas is empty");
    }

    // ---- Defect Generator: a SET of singly-defective materials -------------
    //
    // The two modes answer different questions and must not be confusable. One
    // material with three vacancies is a tri-vacancy complex; three materials
    // with one vacancy each is a formation-energy series. Getting them
    // backwards produces a study whose every number is about the wrong system,
    // with nothing anywhere to say so.
    std::printf("Defect Generator, one material per defect:\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_defectset"));
    {
        QStringList refusals;
        OrchestrationWindow window(materials, pythonResolver);
        window.setRefusalHandler(
            [&refusals](const QString& message) { refusals << message; });

        OrchestrationNodeItem* container = window.addProcessNode(
            OrchestrationTask::Container, 0,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* defect = window.addProcessNode(
            OrchestrationTask::DefectGenerator,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* probe = window.addProcessNode(
            OrchestrationTask::SinglePoint, calango::core::CalculatorKind::EMT);
        window.linkNodes(container, defect);
        window.linkNodes(defect, probe);

        // Three DIFFERENT single defects on a 4-atom cell.
        DefectSpec spec;
        spec.mode = DefectSpec::Mode::Separate;
        for (const char* index : {"0", "1", "2"}) {
            DefectOperation remove;
            remove.kind = DefectOperation::Kind::Remove;
            remove.indices = QLatin1String(index);
            spec.operations.append(remove);
        }
        window.setNodeDefectSpec(defect, spec);
        window.configureNode(probe, probeScript(), pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);

        // Each produced material must be its own tab in the workspace, not
        // successive contents of one — showing a set one at a time is not
        // showing the set.
        QList<QPair<int, int>> produced; // (nodeId, variant)
        QObject::connect(
            &window, &OrchestrationWindow::nodeStructureProduced,
            [&produced](int nodeId, int variant, const QString&,
                        const std::shared_ptr<const calango::core::Structure>&) {
                produced.append({nodeId, variant});
            });

        window.sendToProcesses();
        settle(probe, 180000);
        check(refusals.isEmpty(), "nothing was refused");
        check(window.batchLength() == 3,
              "three defects make the pipeline take three passes");

        int defectVariants = 0;
        for (const auto& entry : produced)
            if (entry.first == defect->id())
                ++defectVariants;
        check(defectVariants == 3,
              "the defect node published three materials");
        QList<int> distinct;
        for (const auto& entry : produced)
            if (entry.first == defect->id() && !distinct.contains(entry.second))
                distinct.append(entry.second);
        check(distinct.size() == 3,
              "each under its own variant key, so each gets its own tab");

        // Every pass removed exactly ONE atom from the pristine 4-atom cell.
        // The failure this catches is defects accumulating: 3, then 2, then 1.
        check(extxyzCount(defect->jobDirectory()
                          + QStringLiteral("/transformed.extxyz")) == 3,
              "the last pass is a 3-atom cell — one vacancy, not three");
        const QJsonObject result = readJson(probe->jobDirectory()
                                            + QStringLiteral("/probe.json"));
        check(result.value(QStringLiteral("natoms")).toInt() == 3,
              "and the downstream job computed a singly-defective cell");
    }

    // The combined mode must keep meaning what it always did.
    std::printf("Defect Generator, one material with every defect:\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_defectcombined"));
    {
        QStringList refusals;
        OrchestrationWindow window(materials, pythonResolver);
        window.setRefusalHandler(
            [&refusals](const QString& message) { refusals << message; });
        OrchestrationNodeItem* container = window.addProcessNode(
            OrchestrationTask::Container, 0,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* defect = window.addProcessNode(
            OrchestrationTask::DefectGenerator,
            calango::core::CalculatorKind::EMT);
        window.linkNodes(container, defect);

        DefectSpec spec; // Mode::Combined by default
        for (const char* index : {"0", "1"}) {
            DefectOperation remove;
            remove.kind = DefectOperation::Kind::Remove;
            remove.indices = QLatin1String(index);
            spec.operations.append(remove);
        }
        window.setNodeDefectSpec(defect, spec);

        window.sendToProcesses();
        settle(defect, 120000);
        check(refusals.isEmpty(), "nothing was refused");
        check(window.batchLength() == 1,
              "a combined recipe takes one pass however many operations it has");
        check(extxyzCount(defect->jobDirectory()
                          + QStringLiteral("/transformed.extxyz")) == 2,
              "and produces ONE cell carrying both vacancies (4 - 2 = 2 atoms)");
    }

    // ---- The run's own report ----------------------------------------------
    //
    // The aggregation has to happen AS THE RUN GOES. A batch re-queues its
    // nodes for every container item, so the canvas ends holding only the last
    // pass's statuses — a report assembled at the end from the graph would say
    // nothing about the first eleven structures, and would say it confidently.
    std::printf("Workflow report:\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_report"));
    {
        OrchestrationWindow window(materials, pythonResolver);
        OrchestrationNodeItem* container = window.addProcessNode(
            OrchestrationTask::Container, 0,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* probe = window.addProcessNode(
            OrchestrationTask::SinglePoint, calango::core::CalculatorKind::EMT);
        window.linkNodes(container, probe);
        window.configureNode(probe, probeScriptWithMetrics(), pythonExe,
                             QString(), calango::core::CalculatorKind::EMT);

        // Two structures, so the per-structure aggregation has something to
        // keep apart.
        QList<OrchestrationNodeItem::BatchItem> items;
        items.append({QStringLiteral("first"), materials[0].second});
        items.append({QStringLiteral("second"), materials[0].second});
        window.setNodeBatchItems(container, items);

        calango::core::WorkflowReport delivered;
        int finishedSignals = 0;
        QObject::connect(&window, &OrchestrationWindow::runFinished,
                         [&](const calango::core::WorkflowReport& report) {
                             delivered = report;
                             ++finishedSignals;
                         });

        window.sendToProcesses();
        settle(probe, 180000);
        // The last pass's job completes after the node reaches Done; give the
        // run its moment to emit.
        QElapsedTimer timer;
        timer.start();
        while (finishedSignals == 0 && timer.elapsed() < 30000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        check(finishedSignals == 1, "the run reports itself exactly once");
        check(delivered.batchTotal == 2, "the report knows it was a 2-pass run");
        // Container + probe, twice.
        check(delivered.outcomes.size() == 4,
              "one outcome per node per pass, not per node");
        check(delivered.tallyFor(0).succeeded == 2
                  && delivered.tallyFor(1).succeeded == 2,
              "each structure's path is tallied on its own");
        check(delivered.batchLabels()
                  == QStringList({QStringLiteral("first"),
                                  QStringLiteral("second")}),
              "and each pass carries the label of its structure");
        check(delivered.allSucceeded(), "the run succeeded as a whole");

        // Physics extracted from what the node actually wrote.
        bool sawEnergy = false;
        for (const auto& outcome : delivered.outcomes)
            for (const auto& metric : outcome.metrics)
                sawEnergy = sawEnergy
                    || metric.key == QLatin1String("final_energy_ev");
        check(sawEnergy,
              "a completed calculation contributed its final energy, read from "
              "the metrics.json its script wrote");
        for (const auto& outcome : delivered.outcomes) {
            for (const auto& metric : outcome.metrics) {
                if (metric.key != QLatin1String("final_energy_ev"))
                    continue;
                check(std::abs(metric.number + 4.75) < 1e-9,
                      "and it is the LAST sample of that history");
            }
        }

        // And the same report is on disk, which is what the CLI reads.
        const calango::core::WorkflowReport onDisk =
            calango::core::WorkflowReport::read(window.orchestrationRoot());
        check(onDisk.outcomes.size() == delivered.outcomes.size()
                  && onDisk.batchTotal == delivered.batchTotal,
              "the identical report is written beside the run");
        check(QFile::exists(window.orchestrationRoot()
                            + QStringLiteral("/workflow_report.txt")),
              "with a plain-text twin for a terminal");
    }

    if (failures)
        std::printf("\n%d check(s) FAILED.\n", failures);
    else
        std::printf("\nAll checks passed.\n");
    return failures ? 1 : 0;
}
