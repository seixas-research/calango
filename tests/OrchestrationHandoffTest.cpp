// Orchestration data-handoff validation: what a LINK between two nodes means,
// pinned by running the same two Geometry Optimization processes twice.
//
//   * UNLINKED: two independent nodes on the same rattled structure. Each
//     must run its own full relaxation from the original geometry — the
//     second node's input is the staged material, NOT the first node's
//     result, and both spend the same (non-trivial) number of optimizer
//     steps.
//   * LINKED: parent → child. The child's input IS the parent's relaxed
//     output (byte for byte), so its optimizer finds the forces already
//     under fmax and converges almost immediately (≤ 1 step).
//
// EMT copper keeps every force evaluation in microseconds while behaving
// like a real crystal, so the step counts are physics, not fixture theatre.
// Self-skips (exit 0) when the interpreter or ASE is unavailable, like the
// other integration benchmarks.

#include "core/AseScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "core/TdbDatabase.hpp"
#include "core/TdbExpression.hpp"
#include "core/UnitCell.hpp"
#include "gui/SettingsManager.hpp"
#include "gui/OrchestrationDocument.hpp"
#include "gui/OrchestrationTransforms.hpp"
#include "gui/OrchestrationWindow.hpp"
#include "gui/ProcessManagerPanel.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QPushButton>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

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

/// The optimizer's own account of the run, from the summary the script
/// writes last (its presence means the run reached the end).
struct RelaxSummary {
    int steps = -1;
    bool converged = false;
};

RelaxSummary relaxSummary(const QString& jobDir)
{
    RelaxSummary summary;
    const QJsonObject o =
        QJsonDocument::fromJson(
            readAll(jobDir + QStringLiteral("/geometry_optimization.json"))
                .toUtf8())
            .object();
    summary.steps = o.value(QStringLiteral("steps")).toInt(-1);
    summary.converged = o.value(QStringLiteral("converged")).toBool(false);
    return summary;
}

/// The committed configuration both nodes share: a plain EMT relaxation,
/// exactly what the wizard's "Save process node" would produce.
calango::core::CalculatorConfig relaxConfig()
{
    calango::core::CalculatorConfig config;
    config.task = calango::core::TaskKind::GeometryOptimization;
    config.calculator = calango::core::CalculatorKind::EMT;
    config.fmax = 0.02;   // tight enough that the rattled start needs work
    config.maxSteps = 100;
    return config;
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication::setOrganizationName(QStringLiteral("CalangoTest"));
    QCoreApplication::setApplicationName(
        QStringLiteral("OrchestrationHandoffTest"));

    // Sandbox every config file AND the simulations directory: this test
    // actually runs jobs, and none of that may land in the developer's home.
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) {
        std::printf("SKIP: no temporary directory available\n");
        return 0;
    }
    qputenv("CALANGO_CONFIG_DIR",
            (sandbox.path() + QStringLiteral("/.calango")).toLocal8Bit());

    QApplication app(argc, argv);

    // The interpreter the jobs run under — the embedded engine's own. Skip
    // cleanly if it cannot import ASE (EMT ships inside ASE).
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

    using calango::core::Atom;
    using calango::core::Structure;
    using calango::core::UnitCell;
    using calango::gui::OrchestrationFamily;
    using calango::gui::OrchestrationNodeItem;
    using calango::gui::OrchestrationTask;
    using calango::gui::OrchestrationWindow;
    using calango::gui::TdbGeneratorOutput;
    using calango::gui::TdbGeneratorSpec;
    using calango::gui::orchestrationTaskFromSlug;

    // Copper fcc conventional cell with atom 0 rattled well off its site —
    // a relaxation with real work to do, not a fixture that converges on
    // its first force call.
    auto copper = std::make_shared<Structure>();
    const double a = 3.61;
    copper->setCell(UnitCell({a, 0.0, 0.0}, {0.0, a, 0.0}, {0.0, 0.0, a}));
    const std::array<std::array<double, 3>, 4> sites = {
        {{0.0, 0.0, 0.0}, {0.0, 0.5, 0.5}, {0.5, 0.0, 0.5}, {0.5, 0.5, 0.0}}};
    for (std::size_t i = 0; i < sites.size(); ++i) {
        Atom atom;
        atom.atomicNumber = 29;
        atom.position = {a * sites[i][0], a * sites[i][1], a * sites[i][2]};
        if (i == 0)
            atom.position = {0.35, 0.20, -0.15}; // the rattle
        copper->addAtom(atom);
    }

    const QString script = QString::fromStdString(
        calango::core::AseScriptGenerator::generate(relaxConfig(),
                                                    "structure.extxyz"));
    const auto pythonResolver =
        [&pythonExe](calango::core::CalculatorKind) { return pythonExe; };
    const auto terminal = [](const OrchestrationNodeItem* node) {
        return node->status() == OrchestrationNodeItem::Status::Done
            || node->status() == OrchestrationNodeItem::Status::Failed
            || node->status() == OrchestrationNodeItem::Status::Skipped;
    };
    const auto settle = [&terminal](OrchestrationNodeItem* first,
                                    OrchestrationNodeItem* second) {
        QElapsedTimer timer;
        timer.start();
        while ((!terminal(first) || !terminal(second))
               && timer.elapsed() < 600000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    };

    // ---- Scenario 1: UNLINKED — two independent full relaxations ----------
    std::printf("Unlinked nodes (independent runs):\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_unlinked"));
    RelaxSummary unlinkedFirst;
    {
        OrchestrationWindow window({{QStringLiteral("Cu (rattled)"), copper}},
                              pythonResolver);
        OrchestrationNodeItem* first = window.addProcessNode(
            OrchestrationTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* second = window.addProcessNode(
            OrchestrationTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::EMT);
        check(first && second, "two process nodes on the canvas");
        window.configureNode(first, script, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        window.configureNode(second, script, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);

        window.sendToProcesses();
        settle(first, second);
        check(first->status() == OrchestrationNodeItem::Status::Done
                  && second->status() == OrchestrationNodeItem::Status::Done,
              "both unlinked relaxations finished");

        // Independence, part 1: both nodes started from the SAME staged
        // material — the second node's input is the rattled copper, not
        // anything the first node produced.
        const QString firstInput =
            readAll(first->jobDirectory() + QStringLiteral("/structure.extxyz"));
        const QString secondInput = readAll(second->jobDirectory()
                                            + QStringLiteral("/structure.extxyz"));
        const QString firstRelaxed = readAll(
            first->jobDirectory() + QStringLiteral("/optimized.extxyz"));
        check(!firstInput.isEmpty() && firstInput == secondInput,
              "both nodes start from the same original structure");
        check(!firstRelaxed.isEmpty() && secondInput != firstRelaxed,
              "the second node did NOT inherit the first node's result");

        // Independence, part 2: each spent its own full relaxation. The two
        // runs are byte-identical (same script, same input), so their step
        // counts must agree — and be real work, not a one-step formality.
        unlinkedFirst = relaxSummary(first->jobDirectory());
        const RelaxSummary secondSummary = relaxSummary(second->jobDirectory());
        check(unlinkedFirst.converged && secondSummary.converged,
              "both relaxations converged");
        check(unlinkedFirst.steps >= 3,
              "the rattled start needs a real relaxation (>= 3 steps)");
        check(secondSummary.steps == unlinkedFirst.steps,
              "the second node ran the same full number of steps");
    }

    // ---- Scenario 2: LINKED — the child inherits the relaxed geometry -----
    std::printf("Linked nodes (parent feeds child):\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_linked"));
    {
        OrchestrationWindow window({{QStringLiteral("Cu (rattled)"), copper}},
                              pythonResolver);
        OrchestrationNodeItem* parent = window.addProcessNode(
            OrchestrationTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* child = window.addProcessNode(
            OrchestrationTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::EMT);
        window.linkNodes(parent, child);
        window.configureNode(parent, script, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        window.configureNode(child, script, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);

        window.sendToProcesses();
        check(parent->status() == OrchestrationNodeItem::Status::Running,
              "parent starts running on send");
        check(child->status() == OrchestrationNodeItem::Status::Waiting,
              "child queues as waiting");
        settle(parent, child);
        check(parent->status() == OrchestrationNodeItem::Status::Done
                  && child->status() == OrchestrationNodeItem::Status::Done,
              "both linked relaxations finished");

        // The heart of the handoff: the child's input IS the parent's relaxed
        // output, byte for byte.
        const QString parentRelaxed = readAll(
            parent->jobDirectory() + QStringLiteral("/optimized.extxyz"));
        const QString childInput = readAll(child->jobDirectory()
                                           + QStringLiteral("/structure.extxyz"));
        check(!parentRelaxed.isEmpty() && childInput == parentRelaxed,
              "child's input is exactly the parent's relaxed structure");

        const RelaxSummary parentSummary = relaxSummary(parent->jobDirectory());
        const RelaxSummary childSummary = relaxSummary(child->jobDirectory());
        check(parentSummary.converged && parentSummary.steps >= 3,
              "parent ran the full relaxation");
        check(parentSummary.steps == unlinkedFirst.steps,
              "parent's work matches the unlinked baseline");
        // An already-relaxed geometry has its forces under fmax on the first
        // evaluation: BFGS declares convergence at once.
        check(childSummary.converged && childSummary.steps >= 0
                  && childSummary.steps <= 1,
              "child converges almost immediately (<= 1 step)");
    }

    // ---- Scenario 3: the input-slot contract ------------------------------
    //
    // The analysis modules do not read a structure, they read a COMPLETED RUN,
    // and the canvas has to promise each one a file at a name its wizard was
    // configured against long before any parent executed. Everything below is
    // about that promise: who fills which slot, what happens when nobody does,
    // and that the files land under the agreed names.
    std::printf("Input slots:\n");
    {
        // The slot tables themselves. These are the contract the wizard
        // factory codes against, so a rename on either side has to fail here
        // rather than at run time in Python.
        check(orchestrationInputSlots(OrchestrationTask::GeometryOptimization)
                  .isEmpty(),
              "a relaxation inherits nothing");
        check(orchestrationRequiredInputs(OrchestrationTask::ElectronicBands) == 1,
              "Electronic Bands inherits one run");
        check(orchestrationRequiredInputs(OrchestrationTask::ChargedDefects2d) == 2,
              "2D Charged Defects inherits two");
        // Raman has three slots but only one is required: without Born charges
        // the module reports IR as zero rather than refusing to run.
        check(orchestrationInputSlots(OrchestrationTask::RamanIr).size() == 3
                  && orchestrationRequiredInputs(OrchestrationTask::RamanIr) == 1,
              "Raman/IR has three slots, two of them optional");
        check(orchestrationTaskHasDefaults(OrchestrationTask::SinglePoint)
                  && !orchestrationTaskHasDefaults(OrchestrationTask::Optics),
              "only the self-contained tasks can run unconfigured");

        // The TDB Generator is the one TRANSFORM with an input slot, and its
        // staged name is a contract with the convex-hull viewer: both read the
        // same cluster_expansion.json, so the hull a user looks at and the
        // ensemble the assessment fits cannot drift into two descriptions of
        // one calculation.
        check(orchestrationTaskFamily(OrchestrationTask::TdbGenerator)
                  == OrchestrationFamily::Transform,
              "the TDB Generator runs on the canvas, not as a job");
        check(orchestrationRequiredInputs(OrchestrationTask::TdbGenerator) == 1,
              "and inherits exactly one completed run");
        check(orchestrationInputSlots(OrchestrationTask::TdbGenerator)
                      .front()
                      .stagedName
                  == QStringLiteral("cluster_expansion.json"),
              "staged under the same name the hull viewer reads");
        check(orchestrationTaskHasDefaults(OrchestrationTask::TdbGenerator),
              "and can run unconfigured, because everything it needs — "
              "endpoints, compositions, energies — comes out of that file");
        // The slug names directories, provenance records and saved workflows.
        // It is the only persisted identity a node has.
        check(orchestrationTaskSlug(OrchestrationTask::TdbGenerator)
                  == QStringLiteral("tdb_generator"),
              "with a stable slug");
        check(orchestrationTaskFromSlug(QStringLiteral("tdb_generator"))
                  == OrchestrationTask::TdbGenerator,
              "that round-trips, so a saved workflow containing one loads");

        // ---- The alloy pipeline ------------------------------------------
        // Container(parent lattice) -> SQS -> simulations -> ECI fit -> CVM.
        // What is pinned here is the WIRING, because it is the part that fails
        // silently: a node whose slot names a file nobody writes simply never
        // runs, and a node missing from a dispatch switch runs the wrong
        // transform under the right name.
        check(orchestrationTaskFamily(OrchestrationTask::SqsGenerator)
                      == OrchestrationFamily::Transform
                  && orchestrationTaskFamily(
                         OrchestrationTask::ClusterExpansionFit)
                      == OrchestrationFamily::Transform
                  && orchestrationTaskFamily(OrchestrationTask::CvmEntropy)
                      == OrchestrationFamily::Transform,
              "all three alloy nodes run on the canvas, not as jobs");
        // The SQS Generator is a structure transform in the strict sense: it
        // takes the ordinary geometry handoff, so it must declare NO slot.
        // Declaring one would make it demand a completed run it has no use
        // for, and refuse every graph that feeds it a plain structure.
        check(orchestrationInputSlots(OrchestrationTask::SqsGenerator).isEmpty(),
              "the SQS Generator takes a structure, not a completed run");
        check(!orchestrationTaskHasDefaults(OrchestrationTask::SqsGenerator),
              "and cannot run unconfigured — a default composition would be a "
              "claim about which alloy the user meant");
        // The two ends of the ECI handoff. These two strings being equal IS
        // the contract: the fitter writes a file and the solver stages one,
        // and nothing else connects them.
        check(orchestrationRequiredInputs(OrchestrationTask::ClusterExpansionFit)
                      == 1
                  && orchestrationInputSlots(
                         OrchestrationTask::ClusterExpansionFit)
                             .front()
                             .stagedName
                      == QStringLiteral("cluster_expansion.json"),
              "the ECI Fitter reads the same ensemble file as the hull viewer "
              "and the TDB Generator");
        check(orchestrationRequiredInputs(OrchestrationTask::CvmEntropy) == 1
                  && orchestrationInputSlots(OrchestrationTask::CvmEntropy)
                             .front()
                             .sourceName
                      == QStringLiteral("cluster_expansion_fit.json"),
              "and the CVM node reads exactly the file the ECI Fitter writes");
        for (const auto& [task, slug] :
             {std::pair{OrchestrationTask::SqsGenerator,
                        QStringLiteral("sqs_generator")},
              std::pair{OrchestrationTask::ClusterExpansionFit,
                        QStringLiteral("cluster_expansion_fit")},
              std::pair{OrchestrationTask::CvmEntropy,
                        QStringLiteral("cvm_entropy")}})
            check(orchestrationTaskSlug(task) == slug
                      && orchestrationTaskFromSlug(slug) == task,
                  qPrintable(QStringLiteral("\"%1\" is a stable slug that "
                                            "round-trips")
                                 .arg(slug)));

        // ---- Thermodynamic integration -----------------------------------
        //
        // A Simulation, not an Analysis. The distinction is load-bearing and
        // invisible: orchestrationTaskFamily() has a `default:` arm that
        // returns Analysis, and an Analysis node is refused unless a completed
        // baseline is wired into it — so a TI node that fell through would be
        // permanently unrunnable for a reason spelled nowhere.
        check(orchestrationTaskFamily(OrchestrationTask::LiquidFreeEnergy)
                  == OrchestrationFamily::Simulation,
              "the TI node is a Simulation, not an Analysis");
        check(orchestrationInputSlots(OrchestrationTask::LiquidFreeEnergy)
                  .isEmpty(),
              "it reads a structure, so it stages no completed run");
        // The one Simulation-family task with no defaults. A default TI path
        // would produce an ABSOLUTE free energy from an unchosen reference at
        // an unchosen temperature — a number nobody can look at and see is
        // wrong.
        check(!orchestrationTaskHasDefaults(OrchestrationTask::LiquidFreeEnergy),
              "and refuses to run unconfigured, unlike every other simulation");
        check(orchestrationTaskSlug(OrchestrationTask::LiquidFreeEnergy)
                      == QStringLiteral("liquid_free_energy")
                  && orchestrationTaskFromSlug(
                         QStringLiteral("liquid_free_energy"))
                      == OrchestrationTask::LiquidFreeEnergy,
              "with a stable slug that round-trips");
        check(calango::gui::orchestrationTasks().contains(
                  OrchestrationTask::LiquidFreeEnergy),
              "and it appears in the Add Process list at all");
    }

    // ---- Dump (ML training-data writer) plumbing ---------------------------
    //
    // Just the static contract -- slug, family, input slot -- the same shape
    // the SQS/ECI/CVM check above pins for the alloy pipeline. The actual
    // aggregation (reading every fan-out pass, writing the extxyz under the
    // MACE preset's keys) is exercised end to end in OrchestrationBatchTest.
    std::printf("Dump (ML training-data writer) plumbing:\n");
    {
        check(orchestrationTaskSlug(OrchestrationTask::Dump)
                      == QStringLiteral("dump")
                  && orchestrationTaskFromSlug(QStringLiteral("dump"))
                      == OrchestrationTask::Dump,
              "\"dump\" is a stable slug that round-trips");
        check(orchestrationTaskFamily(OrchestrationTask::Dump)
                  == OrchestrationFamily::Transform,
              "it is a Transform: no calculator, no launch command, and it "
              "runs on the canvas rather than as a job");
        check(orchestrationRequiredInputs(OrchestrationTask::Dump) == 1,
              "it refuses to run unlinked, like every other node that reads "
              "a completed run rather than defaulting to something");
        check(calango::gui::orchestrationTasks().contains(
                  OrchestrationTask::Dump),
              "and it appears in the Add Process list");

        // Round-trip through the saved-workflow document -- the same
        // additive pattern TdbGenerator/SqsGenerator/etc. use
        // (entry.insert("dump", ...) / DumpSpec::fromJson), which an older
        // reader ignores rather than chokes on.
        OrchestrationWindow source({{QStringLiteral("Cu (rattled)"), copper}},
                                   pythonResolver);
        OrchestrationNodeItem* container = source.addProcessNode(
            OrchestrationTask::Container, 0, calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* singlePoint = source.addProcessNode(
            OrchestrationTask::SinglePoint, 0,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* dump = source.addProcessNode(
            OrchestrationTask::Dump, 0, calango::core::CalculatorKind::EMT);
        source.linkNodes(container, singlePoint);
        source.linkNodes(singlePoint, dump);
        calango::gui::DumpSpec spec;
        calango::gui::applyMaceTrainingPreset(&spec);
        spec.outputPath = QStringLiteral("/tmp/whatever_training.extxyz");
        spec.configType = QStringLiteral("bulk");
        spec.includeFailedFrames = true;
        spec.appendToExistingFile = true;
        source.setNodeDump(dump, spec);

        QStringList warnings;
        const QJsonObject document =
            calango::gui::OrchestrationDocument::build(source, &warnings);

        OrchestrationWindow reopened(
            {{QStringLiteral("Cu (rattled)"), copper}}, pythonResolver);
        QString error;
        check(calango::gui::OrchestrationDocument::load(reopened, document,
                                                         &error),
              "the document reloads");
        OrchestrationNodeItem* reopenedDump = nullptr;
        for (OrchestrationNodeItem* node : reopened.nodes())
            if (node->task() == OrchestrationTask::Dump)
                reopenedDump = node;
        check(reopenedDump != nullptr, "the Dump node survives the round trip");
        if (reopenedDump) {
            const calango::gui::DumpSpec& reopenedSpec = reopenedDump->dump();
            check(reopenedSpec.outputPath == spec.outputPath
                      && reopenedSpec.energyKey == spec.energyKey
                      && reopenedSpec.forcesKey == spec.forcesKey
                      && reopenedSpec.stressKey == spec.stressKey
                      && reopenedSpec.configType == spec.configType
                      && reopenedSpec.includeFailedFrames
                          == spec.includeFailedFrames
                      && reopenedSpec.appendToExistingFile
                          == spec.appendToExistingFile,
                  "and every field of its settings round-trips exactly, "
                  "including the MACE preset's key names");
        }
    }

    // ---- The SQS Generator transform ---------------------------------------
    //
    // End to end through the node's compute function, on the same copper cell
    // the rest of this file uses. The composition is the closed form: 2x2x2 of
    // a four-atom fcc conventional cell is 32 sites, and 50/50 is 16 each —
    // exactly, because the generator rounds onto whole sites by largest
    // remainder rather than approximately.
    std::printf("SQS Generator transform:\n");
    {
        using calango::gui::AlloyComposition;
        using calango::gui::SqsGeneratorOutput;
        using calango::gui::SqsGeneratorSpec;

        SqsGeneratorSpec spec;
        spec.na = spec.nb = spec.nc = 2;
        spec.shell1 = 3.0; // brackets the fcc nearest neighbour at 2.55 A
        spec.shell2 = 3.7;
        spec.steps = 4000;
        AlloyComposition half;
        half.species = {{QStringLiteral("Cu"), 0.5}, {QStringLiteral("Au"), 0.5}};
        AlloyComposition quarter;
        quarter.label = QStringLiteral("Cu3Au");
        quarter.species = {{QStringLiteral("Cu"), 0.75},
                           {QStringLiteral("Au"), 0.25}};
        spec.compositions = {half, quarter};

        check(spec.variantCount() == 2,
              "two compositions means two pipeline passes");
        check(half.name() == QStringLiteral("Cu50Au50"),
              "an unnamed composition names itself from its fractions");
        check(quarter.name() == QStringLiteral("Cu3Au"),
              "and a named one keeps its name");

        // The copper fixture carries a rattled atom, which is fine for a
        // relaxation and wrong for an SQS: the sublattice would have one site
        // off its lattice position. A clean cell, then.
        Structure fcc;
        fcc.setCell(UnitCell({a, 0.0, 0.0}, {0.0, a, 0.0}, {0.0, 0.0, a}));
        for (const auto& site : sites) {
            Atom atom;
            atom.atomicNumber = 29;
            atom.position = {a * site[0], a * site[1], a * site[2]};
            fcc.addAtom(atom);
        }

        SqsGeneratorOutput output;
        QString problem;
        check(runSqsGeneration(fcc, spec, 0, &output, &problem),
              "the first composition produces a structure");
        if (!problem.isEmpty())
            std::printf("      %s\n", qPrintable(problem));
        int copper = 0;
        int gold = 0;
        for (const auto& atom : output.structure.atoms())
            (atom.atomicNumber == 29 ? copper : gold) += 1;
        check(output.structure.size() == 32,
              "a 2x2x2 supercell of the fcc conventional cell is 32 sites");
        check(copper == 16 && gold == 16,
              "and 50/50 lands exactly 16/16 — the composition is not "
              "approximate");
        check(output.label == QStringLiteral("Cu50Au50"),
              "the pass is named after the composition, not numbered");
        check(output.summaryJson.contains(QStringLiteral("warren_cowley"))
                  && output.summaryJson.contains(QStringLiteral("deviation")),
              "and the report carries both the objective split and the "
              "short-range order of what was made");

        // The second composition, and the clamp that keeps a shrunken list
        // from reading off the end.
        check(runSqsGeneration(fcc, spec, 1, &output, &problem)
                  && output.label == QStringLiteral("Cu3Au"),
              "index 1 produces the second composition");
        copper = 0;
        gold = 0;
        for (const auto& atom : output.structure.atoms())
            (atom.atomicNumber == 29 ? copper : gold) += 1;
        check(copper == 24 && gold == 8, "75/25 of 32 sites is exactly 24/8");
        check(runSqsGeneration(fcc, spec, 99, &output, &problem),
              "an out-of-range pass index is clamped, not read off the end");

        SqsGeneratorSpec empty;
        check(!runSqsGeneration(fcc, empty, 0, &output, &problem)
                  && !problem.isEmpty(),
              "a node with no compositions is refused, with a reason");
    }

    // ---- The two nodes whose solvers are not linked yet ---------------------
    //
    // TRIPWIRE. core::ClusterExpansionFit and core::ClusterVariation are being
    // written separately; until they are in the build, both nodes REFUSE
    // rather than writing a file of zeros — a downstream CVM curve is smooth
    // and plausible whatever went into it, so a placeholder would be
    // indistinguishable from an answer.
    //
    // When either solver lands, replace the matching check here with a real
    // one. A test asserting "not implemented" is only honest while that is
    // true, and this is the thing that will notice.
    std::printf("ECI Fitter and CVM solver (pending):\n");
    {
        using calango::gui::ClusterExpansionFitOutput;
        using calango::gui::ClusterExpansionFitSpec;
        using calango::gui::CvmEntropyOutput;
        using calango::gui::CvmEntropySpec;

        ClusterExpansionFitSpec fitSpec;
        check(fitSpec.isValid() && fitSpec.crossValidationFolds == 0,
              "the default cluster basis is usable and cross-validates "
              "leave-one-out, the cluster-expansion convention");
        fitSpec.crossValidationFolds = 1;
        check(!fitSpec.isValid(),
              "but one fold is the fit again under another name, and is "
              "refused");
        fitSpec.crossValidationFolds = 5;

        ClusterExpansionFitOutput fitted;
        QString problem;
        // A mis-wired graph must not be reported as the missing solver: the
        // two failures look identical from the canvas and have nothing to do
        // with each other.
        check(!runClusterExpansionFit(QStringLiteral("{}"), fitSpec, &fitted,
                                      &problem)
                  && problem.contains(QStringLiteral("configurations")),
              "an input that is not an ensemble is refused as such");
        QJsonArray configurations;
        for (int i = 0; i < 4; ++i) {
            QJsonObject entry;
            entry.insert(QStringLiteral("concentration"), i / 3.0);
            entry.insert(QStringLiteral("energy_per_atom"), -3.0 - 0.1 * i);
            configurations.append(entry);
        }
        QJsonObject ensemble;
        ensemble.insert(QStringLiteral("configurations"), configurations);
        const QString ensembleJson = QString::fromUtf8(
            QJsonDocument(ensemble).toJson(QJsonDocument::Compact));
        // The refusal must name the REAL obstacle, which is not the one it
        // looks like: core::ClusterExpansionFit exists and takes a design
        // matrix, and cluster_expansion.json carries energies but no cluster
        // correlations to build one from.
        check(!runClusterExpansionFit(ensembleJson, fitSpec, &fitted, &problem)
                  && problem.contains(QStringLiteral("correlations")),
              "a usable ensemble is refused because it carries no cluster "
              "correlations to fit against, and the message says so");

        CvmEntropySpec cvmSpec;
        check(cvmSpec.isValid(), "the default CVM range is a usable one");
        cvmSpec.maxTemperatureK = cvmSpec.minTemperatureK;
        check(!cvmSpec.isValid(), "an empty temperature range is refused");
        cvmSpec.maxTemperatureK = 2000.0;
        CvmEntropyOutput solved;
        check(!runCvmEntropy(QStringLiteral("{}"), cvmSpec, &solved, &problem)
                  && problem.contains(QStringLiteral("ECIs")),
              "a CVM node fed something that is not an ECI file says so");
        // An ECI file with no nearest-neighbour PAIR term: the solver is a
        // nearest-neighbour model, so there is nothing it can use. Refused as
        // a statement about the alloy, not as an error.
        check(!runCvmEntropy(
                  QStringLiteral("{\"eci\":[{\"order\":3,\"value_eV\":0.01}]}"),
                  cvmSpec, &solved, &problem)
                  && problem.contains(QStringLiteral("nearest-neighbour")),
              "an ECI file carrying no nearest-neighbour pair term is refused "
              "with the reason, because a nearest-neighbour CVM has nothing "
              "to consume");
    }

    // ---- Both solvers now run, end to end ----------------------------------
    //
    // These replace the tripwire above. The chain the module exists for is
    // ensemble -> ECI fit -> CVM entropy, and the join between the two nodes
    // is the ECI file: what the fitter writes has to be what the CVM node
    // reads, including the one field (the nearest-neighbour pair ECI) that
    // the downstream node would otherwise have to rediscover.
    std::printf("ECI Fitter -> CVM, end to end:\n");
    {
        using calango::gui::ClusterExpansionFitOutput;
        using calango::gui::ClusterExpansionFitSpec;
        using calango::gui::CvmEntropyOutput;
        using calango::gui::CvmEntropySpec;

        // A design matrix with a KNOWN answer: energies built from an exact
        // linear model, so the fit has something to recover rather than a
        // tolerance to sit inside.
        const double j0 = -3.0, j2 = 0.05;
        QJsonArray configurations;
        QJsonArray labels;
        labels.append(QStringLiteral("point s0"));
        labels.append(QStringLiteral("pair r=2.550 m=12 b0"));
        for (int i = 0; i < 12; ++i) {
            const double x = i / 11.0;
            const double corr = 2.0 * x - 1.0;   // +/-1 correlation
            QJsonArray row;
            row.append(1.0);
            row.append(corr);
            QJsonObject entry;
            entry.insert(QStringLiteral("concentration"), x);
            entry.insert(QStringLiteral("correlation"), row);
            entry.insert(QStringLiteral("energy_per_atom"), j0 + j2 * corr);
            configurations.append(entry);
        }
        QJsonObject ensemble;
        ensemble.insert(QStringLiteral("configurations"), configurations);
        ensemble.insert(QStringLiteral("orbit_labels"), labels);
        const QString ensembleJson = QString::fromUtf8(
            QJsonDocument(ensemble).toJson(QJsonDocument::Compact));

        ClusterExpansionFitSpec fitSpec;
        fitSpec.method = ClusterExpansionFitSpec::Method::Ridge;
        fitSpec.crossValidationFolds = 4;
        ClusterExpansionFitOutput fitted;
        QString problem;
        if (!runClusterExpansionFit(ensembleJson, fitSpec, &fitted, &problem))
            std::printf("    fit refused: %s\n", qPrintable(problem));
        check(!fitted.eciJson.isEmpty(),
              "an ensemble carrying correlations now FITS");
        const QJsonObject eci =
            QJsonDocument::fromJson(fitted.eciJson.toUtf8()).object();
        check(eci.value(QStringLiteral("schema")).toString()
                  == QStringLiteral("calango.cluster_expansion.eci/1"),
              "writing the schema the CVM node expects");
        const double recovered =
            eci.value(QStringLiteral("nearest_neighbour_pair_eci_eV")).toDouble();
        std::printf("    pair ECI recovered %.5f, exact %.5f\n", recovered, j2);
        check(std::abs(recovered - j2) < 0.02,
              "and recovers the pair ECI it was built from");
        // Both errors present: a fit reporting only its training error hides
        // the classic cluster-expansion failure.
        check(eci.contains(QStringLiteral("cv_score"))
                  && eci.contains(QStringLiteral("rmse")),
              "with the CV score and the training RMSE side by side");

        // The join. The CVM node consumes exactly what the fitter wrote.
        CvmEntropySpec cvmSpec;
        cvmSpec.minTemperatureK = 300.0;
        cvmSpec.maxTemperatureK = 1500.0;
        cvmSpec.temperatureSteps = 20;
        CvmEntropyOutput solvedOut;
        if (!runCvmEntropy(fitted.eciJson, cvmSpec, &solvedOut, &problem))
            std::printf("    cvm refused: %s\n", qPrintable(problem));
        check(!solvedOut.entropyJson.isEmpty(),
              "and the CVM node solves straight from the fitter's file");
        const QJsonObject curve =
            QJsonDocument::fromJson(solvedOut.entropyJson.toUtf8()).object();
        check(curve.value(QStringLiteral("schema")).toString()
                  == QStringLiteral("calango.cvm.entropy/1"),
              "under its own schema");
        const QJsonArray points = curve.value(QStringLiteral("points")).toArray();
        check(points.size() == cvmSpec.temperatureSteps,
              "with one point per requested temperature");
        const double ideal =
            curve.value(QStringLiteral("ideal_entropy_kB")).toDouble();
        bool everAbove = false;
        for (const QJsonValue& v : points)
            if (v.toObject().value(QStringLiteral("entropy_kB")).toDouble()
                > ideal + 1e-9)
                everAbove = true;
        check(!everAbove,
              "and no entropy above the ideal bound — correlations can only "
              "remove arrangements, never add them");
        // The limitation travels with the curve, or a homogeneous result gets
        // compared against a published order-disorder temperature.
        check(!curve.value(QStringLiteral("warnings")).toArray().isEmpty(),
              "carrying the warnings that say what this solver cannot do");
    }

    // ---- The three new specs survive a saved workflow -----------------------
    // A node whose settings do not round-trip through the document is a node
    // that computes something else on the cluster than it does on the canvas.
    std::printf("Alloy nodes in a saved workflow:\n");
    {
        using calango::gui::AlloyComposition;
        using calango::gui::CvmEntropySpec;
        using calango::gui::SqsGeneratorSpec;

        OrchestrationWindow window({}, pythonResolver);
        OrchestrationNodeItem* sqsNode = window.addProcessNode(
            OrchestrationTask::SqsGenerator, calango::core::CalculatorKind::EMT);
        SqsGeneratorSpec spec;
        spec.na = 3;
        spec.nb = 2;
        spec.nc = 1;
        spec.tripletCutoff = 4.1;
        spec.replaceElement = QStringLiteral("Ni");
        AlloyComposition composition;
        composition.label = QStringLiteral("HEA");
        composition.species = {{QStringLiteral("Co"), 0.25},
                               {QStringLiteral("Cr"), 0.25},
                               {QStringLiteral("Fe"), 0.25},
                               {QStringLiteral("Ni"), 0.25}};
        spec.compositions = {composition};
        window.setNodeSqsGenerator(sqsNode, spec);

        OrchestrationNodeItem* fitNode = window.addProcessNode(
            OrchestrationTask::ClusterExpansionFit,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* cvmNode = window.addProcessNode(
            OrchestrationTask::CvmEntropy, calango::core::CalculatorKind::EMT);
        CvmEntropySpec cvmSpec;
        cvmSpec.lattice = CvmEntropySpec::Lattice::Bcc;
        cvmSpec.approximation = CvmEntropySpec::Approximation::Pair;
        cvmSpec.temperatureSteps = 37;
        window.setNodeCvmEntropy(cvmNode, cvmSpec);
        window.linkNodes(fitNode, cvmNode);

        QStringList warnings;
        const QJsonObject document =
            calango::gui::OrchestrationDocument::build(window, &warnings);
        OrchestrationWindow reloaded({}, pythonResolver);
        QString error;
        check(calango::gui::OrchestrationDocument::load(reloaded, document,
                                                        &error),
              "a pipeline holding all three alloy nodes reloads");
        check(reloaded.nodes().size() == 3, "with every node");
        const OrchestrationNodeItem* back = reloaded.nodes().front();
        check(back->task() == OrchestrationTask::SqsGenerator
                  && back->sqsGenerator().compositions.size() == 1
                  && back->sqsGenerator().compositions.front().species.size() == 4
                  && back->sqsGenerator().replaceElement
                      == QStringLiteral("Ni"),
              "the quaternary composition and its sublattice survive");
        check(back->sqsGenerator().na == 3 && back->sqsGenerator().nb == 2
                  && back->sqsGenerator().nc == 1
                  && std::fabs(back->sqsGenerator().tripletCutoff - 4.1) < 1e-9,
              "and so do the supercell and the triplet cutoff — the two "
              "settings that decide what was actually computed");
        const OrchestrationNodeItem* backCvm = reloaded.nodes().back();
        check(backCvm->cvmEntropy().lattice == CvmEntropySpec::Lattice::Bcc
                  && backCvm->cvmEntropy().approximation
                      == CvmEntropySpec::Approximation::Pair
                  && backCvm->cvmEntropy().temperatureSteps == 37,
              "the CVM lattice, approximation and grid survive too");
    }

    // ---- The CALPHAD assessment the TDB Generator performs -----------------
    //
    // Checked against a CLOSED FORM, end to end: an ensemble whose formation
    // energies are exactly Omega*x*(1-x) must produce a database whose only
    // Redlich-Kister coefficient is Omega. That single number crosses the
    // eV/atom -> J/mol conversion, the endpoint referencing, the least-squares
    // fit, the .tdb formatting and the parser on the way back, so a factor or
    // a sign lost anywhere in the chain shows up here.
    std::printf("TDB Generator assessment:\n");
    {
        constexpr double kOmega = 18000.0;     // J/mol
        constexpr double kEvPerAtomToJPerMol = 96485.33212331001;
        constexpr double kReferenceA = -3.4;   // eV/atom
        constexpr double kReferenceB = -2.9;
        QJsonArray configurations;
        const auto append = [&](double x, double formationEv) {
            QJsonObject entry;
            entry.insert(QStringLiteral("concentration"), x);
            entry.insert(QStringLiteral("formation_energy"), formationEv);
            entry.insert(QStringLiteral("energy_per_atom"),
                         formationEv + (1.0 - x) * kReferenceA
                             + x * kReferenceB);
            entry.insert(QStringLiteral("formula"),
                         QStringLiteral("Ag%1Au%2")
                             .arg(static_cast<int>(std::lround(100 * (1 - x))))
                             .arg(static_cast<int>(std::lround(100 * x))));
            configurations.append(entry);
        };
        append(0.0, 0.0);
        append(1.0, 0.0);
        for (int i = 1; i < 8; ++i) {
            const double x = i / 8.0;
            append(x, kOmega * x * (1.0 - x) / kEvPerAtomToJPerMol);
        }
        // One failed relaxation, carrying a null formation energy. It must be
        // dropped: read as zero it would enter the fit as a perfectly ideal
        // alloy and pull the coefficient down.
        {
            QJsonObject broken;
            broken.insert(QStringLiteral("concentration"), 0.4);
            broken.insert(QStringLiteral("formation_energy"), QJsonValue());
            broken.insert(QStringLiteral("energy_per_atom"), 0.0);
            configurations.append(broken);
        }
        QJsonObject root;
        root.insert(QStringLiteral("configurations"), configurations);
        root.insert(QStringLiteral("concentration_element"),
                    QStringLiteral("Au"));
        const QString ensemble = QString::fromUtf8(
            QJsonDocument(root).toJson(QJsonDocument::Compact));

        TdbGeneratorSpec spec;
        spec.order = 0; // the regular solution: one coefficient, which is Omega
        TdbGeneratorOutput output;
        QString problem;
        check(runTdbAssessment(ensemble, spec, &output, &problem),
              "an ensemble of formation energies becomes a database");
        if (!problem.isEmpty())
            std::printf("      %s\n", qPrintable(problem));

        calango::core::TdbDatabase parsed;
        std::string error;
        check(parsed.parse(output.databaseText.toStdString(), &error),
              "which parses back with the project's own .tdb parser");
        const calango::core::TdbSubstitutionalPhase model =
            calango::core::tdbSubstitutionalPhase(parsed, "FCC_A1",
                                                  {"AG", "AU"}, 1000.0);
        check(model.ok, "and reads as a substitutional solution");
        const bool recovered = model.ok && !model.interaction.empty()
            && !model.interaction[0][1].empty()
            && std::fabs(model.interaction[0][1][0] - kOmega) < 1.0;
        check(recovered,
              "whose interaction parameter is the Omega the ensemble was "
              "built from");
        check(output.summaryJson.contains(QStringLiteral("rms_residual")),
              "and the summary records the residual the .tdb cannot carry");

        // The refusals. Two pure elements determine the reference and nothing
        // else, and an ensemble missing an endpoint would fold that endpoint's
        // energy into every interaction parameter.
        QJsonObject endpointsOnly;
        QJsonArray justEnds;
        justEnds.append(configurations[0]);
        justEnds.append(configurations[1]);
        endpointsOnly.insert(QStringLiteral("configurations"), justEnds);
        TdbGeneratorOutput ignored;
        QString why;
        check(!runTdbAssessment(
                  QString::fromUtf8(QJsonDocument(endpointsOnly)
                                        .toJson(QJsonDocument::Compact)),
                  spec, &ignored, &why),
              "an ensemble with no alloy between the endpoints is refused");
        check(!why.isEmpty(), "with a reason");
    }

    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_slots"));
    {
        OrchestrationWindow window({{QStringLiteral("Cu (rattled)"), copper}},
                                   pythonResolver);
        // Refusals go here instead of into a modal box nobody is present to
        // dismiss — which is the only reason they can be asserted at all.
        QStringList refusals;
        window.setRefusalHandler(
            [&refusals](const QString& message) { refusals << message; });

        OrchestrationNodeItem* orphan = window.addProcessNode(
            OrchestrationTask::ElectronicBands, 0,
            calango::core::CalculatorKind::Gpaw);
        window.configureNode(orphan, QStringLiteral("print('never runs')\n"),
                             pythonExe, QString(),
                             calango::core::CalculatorKind::Gpaw);
        window.sendToProcesses();
        check(orphan->status() != OrchestrationNodeItem::Status::Running,
              "a bands node with no parent never starts");
        check(refusals.size() >= 1
                  && refusals.front().contains(QStringLiteral("inherits")),
              "and the refusal says it inherits a run it has not been given");
    }

    {
        OrchestrationWindow window({{QStringLiteral("Cu (rattled)"), copper}},
                                   pythonResolver);
        QStringList refusals;
        window.setRefusalHandler(
            [&refusals](const QString& message) { refusals << message; });

        // An analysis node with its parent, but never configured. This is the
        // one an unconditional "fall back to defaults" would happily run —
        // against no baseline at all.
        OrchestrationNodeItem* host = window.addProcessNode(
            OrchestrationTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* bands = window.addProcessNode(
            OrchestrationTask::ElectronicBands, 0,
            calango::core::CalculatorKind::Gpaw);
        window.linkNodes(host, bands);
        window.configureNode(host, script, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        window.sendToProcesses();
        settle(host, bands);
        check(bands->status() != OrchestrationNodeItem::Status::Done,
              "an unconfigured analysis node does not run on defaults");
        check(std::any_of(refusals.cbegin(), refusals.cend(),
                          [](const QString& m) {
                              return m.contains(QStringLiteral("not been configured"));
                          }),
              "and says so rather than failing inside Python");
    }

    // The two-parent case, end to end: link order decides which run is the
    // host and which the defect, and each lands under the name its wizard was
    // told about. Nothing in a .gpw distinguishes them, so if this is wrong
    // the defect diagram is computed with the two cells swapped — and every
    // formation energy comes out negated with no error anywhere.
    std::printf("Two-parent staging (link order):\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_two_parent"));
    {
        OrchestrationWindow window({{QStringLiteral("Cu (rattled)"), copper}},
                                   pythonResolver);
        QStringList refusals;
        window.setRefusalHandler(
            [&refusals](const QString& message) { refusals << message; });

        // Two parents that each write a DISTINCT single_point.gpw, so which
        // file reached which slot is decidable from its contents.
        const auto writer = [](const QString& marker) {
            return QStringLiteral(
                       "with open('single_point.gpw', 'w') as h:\n"
                       "    h.write('%1')\n"
                       "with open('single_point.extxyz', 'w') as h:\n"
                       "    h.write('0\\nmarker=%1\\n')\n"
                       "print('CALANGO_DONE', flush=True)\n")
                .arg(marker);
        };
        OrchestrationNodeItem* pristine = window.addProcessNode(
            OrchestrationTask::SinglePoint, 0,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* defective = window.addProcessNode(
            OrchestrationTask::SinglePoint, 0,
            calango::core::CalculatorKind::EMT);
        // EMT, not GPAW, purely because of how the job is LAUNCHED: a GPAW
        // node resolves to "mpirun -n N gpaw python", which no sandbox has.
        // Nothing here is about the calculator — it is about which parent's
        // file lands in which slot — and the canvas does not tie a task to an
        // engine in any case.
        OrchestrationNodeItem* defects = window.addProcessNode(
            OrchestrationTask::ChargedDefects2d, 0,
            calango::core::CalculatorKind::EMT);

        // One parent only, first: the node must refuse rather than run with
        // half its inputs.
        window.linkNodes(pristine, defects);
        window.configureNode(pristine, writer(QStringLiteral("HOST")),
                             pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        window.configureNode(defective, writer(QStringLiteral("DEFECT")),
                             pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        window.configureNode(
            defects,
            QStringLiteral("import pathlib\n"
                           "print('host=' + pathlib.Path('baseline_1.gpw')"
                           ".read_text(), flush=True)\n"
                           "print('defect=' + pathlib.Path('baseline_2.gpw')"
                           ".read_text(), flush=True)\n"
                           "print('CALANGO_DONE', flush=True)\n"),
            pythonExe, QString(), calango::core::CalculatorKind::EMT);

        window.sendToProcesses();
        settle(pristine, defective);
        QElapsedTimer timer;
        timer.start();
        while (!terminal(defects) && timer.elapsed() < 60000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        check(defects->status() != OrchestrationNodeItem::Status::Done,
              "a two-input node wired to one parent is refused");
        check(std::any_of(refusals.cbegin(), refusals.cend(),
                          [](const QString& m) {
                              return m.contains(QStringLiteral("pristine host"))
                                  && m.contains(QStringLiteral("neutral defect"));
                          }),
              "and the refusal names both inputs it wanted");

        // Now the second link. Same graph otherwise.
        refusals.clear();
        window.linkNodes(defective, defects);
        window.sendToProcesses();
        settle(pristine, defective);
        timer.restart();
        while (!terminal(defects) && timer.elapsed() < 120000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        check(defects->status() == OrchestrationNodeItem::Status::Done,
              "with both parents linked it runs");
        check(refusals.isEmpty(), "and nothing is refused");

        // The payoff: slot 1 holds the FIRST-linked parent's output and slot 2
        // the second's, under exactly the names the wizard was configured
        // against.
        const QString first = readAll(defects->jobDirectory()
                                      + QStringLiteral("/baseline_1.gpw"));
        const QString second = readAll(defects->jobDirectory()
                                       + QStringLiteral("/baseline_2.gpw"));
        check(first == QStringLiteral("HOST"),
              "baseline_1.gpw is the first-linked parent (the host)");
        check(second == QStringLiteral("DEFECT"),
              "baseline_2.gpw is the second-linked parent (the defect)");
        check(first != second, "the two slots are genuinely different files");
    }

    // ---- Diamond graph: fan-out then merge ---------------------------------
    //
    // One shared source feeds TWO children (fan-out: connectNodes() has
    // never restricted outgoing edges, and staging is copy-based, so a
    // second child cannot corrupt what the first one already read), and
    // both branches feed ONE Dump node (merge: connectNodes() now allows
    // more than one incoming edge specifically for Dump, which reads every
    // parent's own pass set directly and concatenates them in link order).
    std::printf("Diamond graph (fan-out then merge into one Dump):\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_diamond"));
    {
        calango::core::CalculatorConfig singlePointConfig;
        singlePointConfig.task = calango::core::TaskKind::SinglePoint;
        singlePointConfig.calculator = calango::core::CalculatorKind::EMT;
        const QString singlePointScript = QString::fromStdString(
            calango::core::AseScriptGenerator::generate(singlePointConfig,
                                                         "structure.extxyz"));

        OrchestrationWindow window({{QStringLiteral("Cu (rattled)"), copper}},
                                   pythonResolver);
        QStringList refusals;
        window.setRefusalHandler(
            [&refusals](const QString& message) { refusals << message; });

        OrchestrationNodeItem* source = window.addProcessNode(
            OrchestrationTask::SinglePoint, 0,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* branchA = window.addProcessNode(
            OrchestrationTask::SinglePoint, 0,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* branchB = window.addProcessNode(
            OrchestrationTask::SinglePoint, 0,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* dump = window.addProcessNode(
            OrchestrationTask::Dump, 0, calango::core::CalculatorKind::EMT);

        // Fan-out: source has TWO children.
        window.linkNodes(source, branchA);
        window.linkNodes(source, branchB);
        // Merge: dump has TWO parents.
        window.linkNodes(branchA, dump);
        window.linkNodes(branchB, dump);

        window.configureNode(source, singlePointScript, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        window.configureNode(branchA, singlePointScript, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        window.configureNode(branchB, singlePointScript, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);

        const QString trainingPath =
            sandbox.path() + QStringLiteral("/diamond_merge.extxyz");
        calango::gui::DumpSpec dumpSpec;
        calango::gui::applyMaceTrainingPreset(&dumpSpec);
        dumpSpec.outputPath = trainingPath;
        window.setNodeDump(dump, dumpSpec);

        // Persistence: the multi-edge graph survives a save/reload round
        // trip -- the same connectNodes() rule applies on load (it calls
        // linkNodes() per saved edge, in file order), so a graph that was
        // valid to draw interactively reloads with every edge intact.
        QStringList warnings;
        const QJsonObject document =
            calango::gui::OrchestrationDocument::build(window, &warnings);
        check(document.value(QStringLiteral("edges")).toArray().size() == 4,
              "the saved document holds all four edges of the diamond -- "
              "fan-out and merge both round-trip through the same flat "
              "edge list an old, single-edge graph already used");

        OrchestrationWindow reopened(
            {{QStringLiteral("Cu (rattled)"), copper}}, pythonResolver);
        QString reopenError;
        check(calango::gui::OrchestrationDocument::load(reopened, document,
                                                         &reopenError),
              "the diamond graph reloads with no error");
        OrchestrationNodeItem* reopenedDump = nullptr;
        int reopenedDumpParents = 0;
        for (OrchestrationNodeItem* node : reopened.nodes())
            if (node->task() == OrchestrationTask::Dump)
                reopenedDump = node;
        if (reopenedDump)
            for (const auto& link : reopened.links())
                if (link.second == reopenedDump)
                    ++reopenedDumpParents;
        check(reopenedDumpParents == 2,
              "and the reloaded Dump node still has both of its merged "
              "parents");

        // Execute the ORIGINAL window (not the reload, which shares no
        // configured scripts) end to end.
        calango::core::WorkflowReport delivered;
        int finishedSignals = 0;
        QObject::connect(&window, &OrchestrationWindow::runFinished,
                         [&](const calango::core::WorkflowReport& report) {
                             delivered = report;
                             ++finishedSignals;
                         });

        window.sendToProcesses();
        check(refusals.isEmpty(),
              "the diamond itself -- fan-out then merge -- is accepted");

        settle(dump, dump);
        QElapsedTimer e2eTimer;
        e2eTimer.start();
        while (finishedSignals == 0 && e2eTimer.elapsed() < 30000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        check(finishedSignals == 1, "the run reports itself exactly once");
        check(delivered.allSucceeded(), "every node in the diamond succeeded");

        check(source->jobHistory().size() == 1,
              "the shared source ran exactly ONCE, not once per child -- "
              "fan-out does not duplicate the parent's own work");
        int sourceOutcomes = 0, branchAOutcomes = 0, branchBOutcomes = 0,
            dumpOutcomes = 0;
        for (const auto& outcome : delivered.outcomes) {
            if (outcome.nodeId == source->id())
                ++sourceOutcomes;
            else if (outcome.nodeId == branchA->id())
                ++branchAOutcomes;
            else if (outcome.nodeId == branchB->id())
                ++branchBOutcomes;
            else if (outcome.nodeId == dump->id())
                ++dumpOutcomes;
        }
        check(sourceOutcomes == 1,
              "...and its outcome appears exactly once in the report");
        check(branchAOutcomes == 1 && branchBOutcomes == 1,
              "both branches completed, each exactly once");
        check(dumpOutcomes == 1,
              "the merge node ran exactly once, not once per parent -- it "
              "waited for BOTH branches before running at all");
        check(QFile::exists(trainingPath), "the merged training file exists");

        const QString verifyDir =
            sandbox.path() + QStringLiteral("/verify_diamond");
        QDir().mkpath(verifyDir);
        const QString verifyScriptPath =
            verifyDir + QStringLiteral("/verify.py");
        const QString verifyOutPath = verifyDir + QStringLiteral("/verify.json");
        {
            QFile verifyScript(verifyScriptPath);
            check(verifyScript.open(QIODevice::WriteOnly | QIODevice::Text),
                  "the verification script is written");
            verifyScript.write(QByteArray(
                "import ase.io, json, sys\n"
                "frames = ase.io.read(sys.argv[1], index=':')\n"
                "out = {'n_frames': len(frames),\n"
                "       'energies': [a.info.get('REF_energy') for a in "
                "frames]}\n"
                "with open(sys.argv[2], 'w') as fh:\n"
                "    json.dump(out, fh)\n"));
        }
        QProcess verifyRun;
        verifyRun.start(pythonExe,
                        {verifyScriptPath, trainingPath, verifyOutPath});
        check(verifyRun.waitForFinished(60000) && verifyRun.exitCode() == 0,
              "the independent read-back script runs cleanly");
        const QJsonObject verified =
            QJsonDocument::fromJson(readAll(verifyOutPath).toUtf8()).object();
        check(verified.value(QStringLiteral("n_frames")).toInt() == 2,
              "the merged Dump holds both branches' frames -- one from "
              "branchA, one from branchB, concatenated in link order -- "
              "not one (only the first parent) and not more than two");
        const QJsonArray energies =
            verified.value(QStringLiteral("energies")).toArray();
        bool allHaveEnergy = energies.size() == 2;
        for (const QJsonValue& value : energies)
            allHaveEnergy = allHaveEnergy && !value.isNull();
        check(allHaveEnergy,
              "and both merged frames carry a real computed energy, not a "
              "placeholder for whichever parent was silently dropped");

        // The complementary half of "merge only where it is well-defined":
        // a second link into a SINGLE-geometry input is refused outright,
        // not silently accepted and then never read. NOT branchA (a
        // Single-point Calculation) any more -- that task is now, on
        // purpose, one of the three exempt from this cap (see the fan-in
        // test below) -- so a Supercell Builder stands in as a task that is
        // still genuinely capped at one parent. Attempted only now, after
        // the run has already finished, so a rejected (and therefore
        // orphaned, parentless) stray node cannot be picked up by a
        // scheduler pass that no longer runs.
        OrchestrationNodeItem* capped = window.addProcessNode(
            OrchestrationTask::Supercell, 0, calango::core::CalculatorKind::EMT);
        window.linkNodes(branchA, capped);
        OrchestrationNodeItem* stray = window.addProcessNode(
            OrchestrationTask::SinglePoint, 0,
            calango::core::CalculatorKind::EMT);
        window.linkNodes(stray, capped);
        // ONE call to links(), not one per iterator: window.links().cbegin()
        // and window.links().cend() would each construct a SEPARATE
        // temporary QList, making begin() of one and end() of another an
        // invalid range (undefined behaviour, not merely "less efficient")
        // rather than the pair of iterators into the SAME list this needs.
        const auto currentLinks = window.links();
        check(std::none_of(currentLinks.cbegin(), currentLinks.cend(),
                           [&](const auto& link) {
                               return link.second == capped
                                   && link.first == stray;
                           }),
              "a second link into a single-geometry input is rejected, not "
              "silently drawn and then never actually read");
    }

    // ---- Fan-in: a Single-point Calculation with several parents ----------
    //
    // Three independent branches (Cu, Au, Pt -- each its own Container ->
    // Single-point Calculation) linked, in that order, into ONE Single-point
    // Calculation node. It must run three times, once per parent, and pass i
    // must use parent i's own structure -- not whichever branch happens to
    // finish first.
    std::printf("Fan-in: Single-point Calculation with three parents, pass "
                "order matching link order:\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_fanin"));
    {
        using calango::core::Atom;
        using calango::core::Structure;
        using calango::core::UnitCell;

        const auto oneAtom = [](int z) {
            auto s = std::make_shared<Structure>();
            s->setCell(UnitCell({12.0, 0.0, 0.0}, {0.0, 12.0, 0.0},
                                {0.0, 0.0, 12.0}));
            Atom atom;
            atom.atomicNumber = z;
            atom.position = {0.0, 0.0, 0.0};
            s->addAtom(atom);
            return s;
        };
        const QList<QPair<int, QString>> elements = {
            {29, QStringLiteral("Cu")}, {79, QStringLiteral("Au")},
            {78, QStringLiteral("Pt")}};

        calango::core::CalculatorConfig singlePointConfig;
        singlePointConfig.task = calango::core::TaskKind::SinglePoint;
        singlePointConfig.calculator = calango::core::CalculatorKind::EMT;
        const QString script = QString::fromStdString(
            calango::core::AseScriptGenerator::generate(singlePointConfig,
                                                         "structure.extxyz"));

        OrchestrationWindow window({{QStringLiteral("Cu (rattled)"), copper}},
                                   pythonResolver);
        QStringList refusals;
        window.setRefusalHandler(
            [&refusals](const QString& message) { refusals << message; });

        QList<OrchestrationNodeItem*> branchPoints;
        for (const auto& [z, symbol] : elements) {
            OrchestrationNodeItem* container = window.addProcessNode(
                OrchestrationTask::Container, 0,
                calango::core::CalculatorKind::EMT);
            window.setNodeBatchItems(
                container, {{symbol,
                            std::shared_ptr<const Structure>(oneAtom(z))}});
            OrchestrationNodeItem* point = window.addProcessNode(
                OrchestrationTask::SinglePoint, 0,
                calango::core::CalculatorKind::EMT);
            window.linkNodes(container, point);
            window.configureNode(point, script, pythonExe, QString(),
                                 calango::core::CalculatorKind::EMT);
            branchPoints << point;
        }

        OrchestrationNodeItem* fanIn = window.addProcessNode(
            OrchestrationTask::SinglePoint, 0,
            calango::core::CalculatorKind::EMT);
        // Linked in element order: Cu, Au, Pt -- pass 0 must use Cu's
        // branch, pass 1 Au's, pass 2 Pt's.
        for (OrchestrationNodeItem* point : branchPoints)
            window.linkNodes(point, fanIn);
        window.configureNode(fanIn, script, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);

        calango::core::WorkflowReport delivered;
        int finishedSignals = 0;
        QObject::connect(&window, &OrchestrationWindow::runFinished,
                         [&](const calango::core::WorkflowReport& report) {
                             delivered = report;
                             ++finishedSignals;
                         });

        window.sendToProcesses();
        check(refusals.isEmpty(),
              "the fan-in graph -- three parents into one Single-point -- "
              "is accepted");

        settle(fanIn, fanIn);
        QElapsedTimer e2eTimer;
        e2eTimer.start();
        while (finishedSignals == 0 && e2eTimer.elapsed() < 30000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        check(finishedSignals == 1, "the run reports itself exactly once");
        check(delivered.allSucceeded(),
              "every branch and every fan-in pass succeeded");

        int fanInOutcomes = 0;
        for (const auto& outcome : delivered.outcomes)
            if (outcome.nodeId == fanIn->id())
                ++fanInOutcomes;
        check(fanInOutcomes == 3,
              "the fan-in node ran exactly three times -- once per parent, "
              "not once total and not nine times (3x3, as an accidental "
              "cross product with the branches' own single pass would give)");

        // Pass order: read back which element each pass actually staged as
        // its OWN structure.extxyz, independent of AseBridge or anything
        // this feature itself wrote, and check it matches link order.
        QStringList passElements(3, QString());
        for (const auto& outcome : delivered.outcomes) {
            if (outcome.nodeId != fanIn->id())
                continue;
            if (outcome.batchIndex < 0 || outcome.batchIndex >= 3)
                continue;
            const QString content = readAll(
                outcome.directory + QStringLiteral("/structure.extxyz"));
            for (const auto& [z, symbol] : elements)
                if (content.contains(symbol))
                    passElements[outcome.batchIndex] = symbol;
        }
        check(passElements
                  == (QStringList{QStringLiteral("Cu"), QStringLiteral("Au"),
                                  QStringLiteral("Pt")}),
              "pass 0 used Cu's branch, pass 1 Au's, pass 2 Pt's -- "
              "execution order matches connection order exactly, not "
              "whichever branch happened to finish first");

        // At most one fan-in node per graph: this odometer has no way to
        // track two independent per-node parent counts at once.
        refusals.clear();
        OrchestrationNodeItem* secondFanIn = window.addProcessNode(
            OrchestrationTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::EMT);
        window.linkNodes(branchPoints[0], secondFanIn);
        window.linkNodes(branchPoints[1], secondFanIn);
        window.sendToProcesses();
        check(!refusals.isEmpty()
                  && refusals.constLast().contains(
                         QStringLiteral("more than one parent")),
              "a second fan-in node in the same graph is refused, naming "
              "the ambiguity, rather than silently picking one");
    }

    // ---- Fan-in: a Geometry Optimization with several parents --------------
    //
    // The SAME mechanism, proven for the other task the user asked for
    // specifically -- a separate window so it is not blocked by the "one
    // fan-in node per graph" rule the test just above exercises.
    std::printf("Fan-in: Geometry Optimization with two parents, same "
                "mechanism as Single-point:\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_fanin_relax"));
    {
        using calango::core::Atom;
        using calango::core::Structure;
        using calango::core::UnitCell;

        const auto oneAtom = [](int z) {
            auto s = std::make_shared<Structure>();
            s->setCell(UnitCell({12.0, 0.0, 0.0}, {0.0, 12.0, 0.0},
                                {0.0, 0.0, 12.0}));
            Atom atom;
            atom.atomicNumber = z;
            atom.position = {0.0, 0.0, 0.0};
            s->addAtom(atom);
            return s;
        };
        const QList<QPair<int, QString>> elements = {
            {29, QStringLiteral("Cu")}, {79, QStringLiteral("Au")}};

        calango::core::CalculatorConfig pointConfig;
        pointConfig.task = calango::core::TaskKind::SinglePoint;
        pointConfig.calculator = calango::core::CalculatorKind::EMT;
        const QString pointScript = QString::fromStdString(
            calango::core::AseScriptGenerator::generate(pointConfig,
                                                         "structure.extxyz"));
        const QString relaxScript = QString::fromStdString(
            calango::core::AseScriptGenerator::generate(relaxConfig(),
                                                         "structure.extxyz"));

        OrchestrationWindow window({{QStringLiteral("Cu (rattled)"), copper}},
                                   pythonResolver);
        QStringList refusals;
        window.setRefusalHandler(
            [&refusals](const QString& message) { refusals << message; });

        QList<OrchestrationNodeItem*> branchPoints;
        for (const auto& [z, symbol] : elements) {
            OrchestrationNodeItem* container = window.addProcessNode(
                OrchestrationTask::Container, 0,
                calango::core::CalculatorKind::EMT);
            window.setNodeBatchItems(
                container, {{symbol,
                            std::shared_ptr<const Structure>(oneAtom(z))}});
            OrchestrationNodeItem* point = window.addProcessNode(
                OrchestrationTask::SinglePoint, 0,
                calango::core::CalculatorKind::EMT);
            window.linkNodes(container, point);
            window.configureNode(point, pointScript, pythonExe, QString(),
                                 calango::core::CalculatorKind::EMT);
            branchPoints << point;
        }

        OrchestrationNodeItem* fanIn = window.addProcessNode(
            OrchestrationTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::EMT);
        for (OrchestrationNodeItem* point : branchPoints)
            window.linkNodes(point, fanIn);
        window.configureNode(fanIn, relaxScript, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);

        calango::core::WorkflowReport delivered;
        int finishedSignals = 0;
        QObject::connect(&window, &OrchestrationWindow::runFinished,
                         [&](const calango::core::WorkflowReport& report) {
                             delivered = report;
                             ++finishedSignals;
                         });

        window.sendToProcesses();
        check(refusals.isEmpty(),
              "the fan-in graph is accepted for Geometry Optimization too");

        settle(fanIn, fanIn);
        QElapsedTimer e2eTimer;
        e2eTimer.start();
        while (finishedSignals == 0 && e2eTimer.elapsed() < 60000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        check(finishedSignals == 1, "the run reports itself exactly once");
        check(delivered.allSucceeded(), "every branch and both relaxations "
                                        "succeeded");
        int fanInOutcomes = 0;
        for (const auto& outcome : delivered.outcomes)
            if (outcome.nodeId == fanIn->id())
                ++fanInOutcomes;
        check(fanInOutcomes == 2,
              "the fan-in Geometry Optimization ran exactly twice -- once "
              "per parent, the same mechanism as Single-point Calculation");
    }

    // ---- Abort: stopping a run in flight, and resuming it ------------------
    //
    // The interesting failure is not "the process kept running" — killing a
    // QProcess is the easy half. It is that a killed job reports the same
    // nonzero exit as a genuinely failed one, and the ordinary response to a
    // failed node is to START THE NEXT ONE. An abort that does not say so
    // stops one job and launches its successor a moment later, which from the
    // user's side is a Stop button that does not stop anything.
    std::printf("Abort a running orchestration:\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_abort"));
    {
        OrchestrationWindow window({{QStringLiteral("Cu (rattled)"), copper}},
                              pythonResolver);
        // Nobody is there to dismiss a modal box in a test, and the abort
        // asks for confirmation.
        window.setConfirmHandler([](const QString&) { return true; });
        OrchestrationNodeItem* parent = window.addProcessNode(
            OrchestrationTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::EMT);
        OrchestrationNodeItem* child = window.addProcessNode(
            OrchestrationTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::EMT);
        window.linkNodes(parent, child);
        window.configureNode(parent, script, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        window.configureNode(child, script, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);

        window.sendToProcesses();
        check(parent->status() == OrchestrationNodeItem::Status::Running,
              "the pipeline is running before the abort");

        window.abortOrchestration();
        // The kill is asynchronous — SIGTERM, then SIGKILL after three
        // seconds — so the unwind lands in onJobFinished, not in the call.
        QElapsedTimer timer;
        timer.start();
        while (parent->status() == OrchestrationNodeItem::Status::Running
               && timer.elapsed() < 60000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

        check(parent->status() == OrchestrationNodeItem::Status::Failed,
              "the node in flight is marked failed, not done");
        check(child->status() == OrchestrationNodeItem::Status::Skipped,
              "the node still queued is marked skipped, not left waiting");

        // The real assertion: nothing starts up again. Give the event loop a
        // generous window to launch a successor if it were going to.
        timer.restart();
        bool restarted = false;
        while (timer.elapsed() < 3000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            restarted = restarted
                || parent->status() == OrchestrationNodeItem::Status::Running
                || child->status() == OrchestrationNodeItem::Status::Running;
        }
        check(!restarted, "no node is started after the abort");
        check(window.canResume(), "an aborted run can be resumed");

        // And resuming it actually finishes the pipeline — an abort that left
        // the canvas in a state Resume could not recover would be a trap.
        window.resumeFromFailure();
        settle(parent, child);
        check(parent->status() == OrchestrationNodeItem::Status::Done
                  && child->status() == OrchestrationNodeItem::Status::Done,
              "resuming after an abort completes the pipeline");
    }

    // Declining the confirmation must leave the run alone — a Stop button that
    // stops the pipeline whatever you answer is worse than no dialog at all.
    std::printf("Abort, declined:\n");
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations_abort_declined"));
    {
        OrchestrationWindow window({{QStringLiteral("Cu (rattled)"), copper}},
                              pythonResolver);
        window.setConfirmHandler([](const QString&) { return false; });
        OrchestrationNodeItem* only = window.addProcessNode(
            OrchestrationTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::EMT);
        window.configureNode(only, script, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        window.sendToProcesses();
        check(only->status() == OrchestrationNodeItem::Status::Running,
              "running before the declined abort");
        window.abortOrchestration();
        check(only->status() == OrchestrationNodeItem::Status::Running,
              "declining the confirmation leaves the run untouched");
        settle(only, only);
        check(only->status() == OrchestrationNodeItem::Status::Done,
              "and it goes on to finish normally");
    }

    // ---- Processes panel: abort one process --------------------------------
    //
    // The button is only as good as its enablement: an Abort offered on a run
    // that finished ten minutes ago is a control that does nothing, and one
    // NOT offered on a queued job leaves the only way to cancel it being
    // Delete, which destroys the folder as well.
    std::printf("Processes panel abort button:\n");
    {
        calango::gui::ProcessManagerPanel panel;
        auto* abort = panel.findChild<QPushButton*>(
            QStringLiteral("abortProcessButton"));
        check(abort != nullptr, "the panel has an abort button");

        int aborted = -1;
        QObject::connect(&panel, &calango::gui::ProcessManagerPanel::abortRequested,
                         [&aborted](int id) { aborted = id; });

        const int id = panel.registerTask(QStringLiteral("job"), QString());
        check(abort && abort->isEnabled(),
              "a freshly queued process can be aborted");
        panel.setTaskStatus(id, calango::gui::ProcessManagerPanel::Status::Running);
        check(abort && abort->isEnabled(), "a running process can be aborted");

        if (abort)
            abort->click();
        check(aborted == id, "clicking it names the selected process");

        panel.setTaskStatus(id,
                            calango::gui::ProcessManagerPanel::Status::Completed);
        check(abort && !abort->isEnabled(),
              "a completed process offers nothing to abort");
        panel.setTaskStatus(id, calango::gui::ProcessManagerPanel::Status::Failed);
        check(abort && !abort->isEnabled(),
              "nor does a failed one");
    }

    if (failures)
        std::printf("\n%d check(s) FAILED.\n", failures);
    else
        std::printf("\nAll checks passed.\n");
    return failures ? 1 : 0;
}
