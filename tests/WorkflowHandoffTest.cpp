// Workflow data-handoff validation: what a LINK between two nodes means,
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
#include "core/UnitCell.hpp"
#include "gui/SettingsManager.hpp"
#include "gui/WorkflowWindow.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
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
        QStringLiteral("WorkflowHandoffTest"));

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
    using calango::gui::WorkflowNodeItem;
    using calango::gui::WorkflowTask;
    using calango::gui::WorkflowWindow;

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
    const auto terminal = [](const WorkflowNodeItem* node) {
        return node->status() == WorkflowNodeItem::Status::Done
            || node->status() == WorkflowNodeItem::Status::Failed
            || node->status() == WorkflowNodeItem::Status::Skipped;
    };
    const auto settle = [&terminal](WorkflowNodeItem* first,
                                    WorkflowNodeItem* second) {
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
        WorkflowWindow window({{QStringLiteral("Cu (rattled)"), copper}},
                              pythonResolver);
        WorkflowNodeItem* first = window.addProcessNode(
            WorkflowTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::EMT);
        WorkflowNodeItem* second = window.addProcessNode(
            WorkflowTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::EMT);
        check(first && second, "two process nodes on the canvas");
        window.configureNode(first, script, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        window.configureNode(second, script, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);

        window.sendToProcesses();
        settle(first, second);
        check(first->status() == WorkflowNodeItem::Status::Done
                  && second->status() == WorkflowNodeItem::Status::Done,
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
        WorkflowWindow window({{QStringLiteral("Cu (rattled)"), copper}},
                              pythonResolver);
        WorkflowNodeItem* parent = window.addProcessNode(
            WorkflowTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::EMT);
        WorkflowNodeItem* child = window.addProcessNode(
            WorkflowTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::EMT);
        window.linkNodes(parent, child);
        window.configureNode(parent, script, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);
        window.configureNode(child, script, pythonExe, QString(),
                             calango::core::CalculatorKind::EMT);

        window.sendToProcesses();
        check(parent->status() == WorkflowNodeItem::Status::Running,
              "parent starts running on send");
        check(child->status() == WorkflowNodeItem::Status::Waiting,
              "child queues as waiting");
        settle(parent, child);
        check(parent->status() == WorkflowNodeItem::Status::Done
                  && child->status() == WorkflowNodeItem::Status::Done,
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

    if (failures)
        std::printf("\n%d check(s) FAILED.\n", failures);
    else
        std::printf("\nAll checks passed.\n");
    return failures ? 1 : 0;
}
