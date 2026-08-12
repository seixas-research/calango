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
