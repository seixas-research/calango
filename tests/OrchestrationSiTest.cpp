// Orchestration pipeline validation on silicon: Geometry Optimization (with unit
// cell relaxation) linked to a Phonon calculation.
//
// What this pins is the ORCHESTRATION, end to end and for real: two nodes on
// the canvas, an explicit link, "Send to Processes", the Waiting → Running →
// Done lifecycle, and — the load-bearing part — that node 2's input
// structure IS node 1's relaxed output, extracted and injected automatically
// along the link. The engine is Lennard-Jones so the pipeline exercises real
// ASE runs in seconds; the physics of default LJ silicon is irrelevant here
// and no result value is asserted, only the plumbing.
//
// Self-skips (exit 0) when the interpreter or ASE is unavailable, like the
// other integration benchmarks.

#include "core/AseScriptGenerator.hpp"
#include "core/PhononScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "core/UnitCell.hpp"
#include "gui/ProcessManagerPanel.hpp"
#include "gui/SettingsManager.hpp"
#include "gui/OrchestrationWindow.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeWidget>

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

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication::setOrganizationName(QStringLiteral("CalangoTest"));
    QCoreApplication::setApplicationName(QStringLiteral("OrchestrationSiTest"));

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
    QSettings().setValue(
        QLatin1String(calango::gui::SettingsManager::kSimulationsDir),
        sandbox.path() + QStringLiteral("/simulations"));

    // The interpreter the jobs run under — the embedded engine's own, which
    // is the build's .venv. Skip cleanly if it cannot import ASE.
    calango::pybridge::PythonEngine python;
    const QString pythonExe = QString::fromStdString(python.executable());
    // Guard against a mis-resolved interpreter (e.g. the engine falling back
    // to the application binary): probing THAT would fork this very test.
    if (pythonExe.isEmpty()
        || pythonExe == QCoreApplication::applicationFilePath()
        || !pythonExe.contains(QStringLiteral("python"))) {
        std::printf("SKIP: no usable interpreter (resolved to %s)\n",
                    qPrintable(pythonExe));
        return 0;
    }
    {
        QProcess probe;
        probe.start(pythonExe, {QStringLiteral("-c"),
                                QStringLiteral("import ase")});
        if (!probe.waitForFinished(30000) || probe.exitCode() != 0) {
            probe.kill();
            probe.waitForFinished(2000);
            std::printf("SKIP: %s cannot import ase\n",
                        qPrintable(pythonExe));
            return 0;
        }
    }

    using calango::core::Atom;
    using calango::core::Structure;
    using calango::core::UnitCell;
    using calango::gui::OrchestrationNodeItem;
    using calango::gui::OrchestrationTask;
    using calango::gui::OrchestrationWindow;

    // Diamond silicon: fcc cell, two-atom basis.
    auto silicon = std::make_shared<Structure>();
    const double a = 5.43;
    silicon->setCell(UnitCell({0.0, a / 2, a / 2}, {a / 2, 0.0, a / 2},
                              {a / 2, a / 2, 0.0}));
    for (const auto& fractional :
         {std::array<double, 3>{0.0, 0.0, 0.0},
          std::array<double, 3>{0.25, 0.25, 0.25}}) {
        Atom atom;
        atom.atomicNumber = 14;
        atom.position = {a * fractional[0], a * fractional[1],
                         a * fractional[2]};
        silicon->addAtom(atom);
    }

    OrchestrationWindow window({{QStringLiteral("Si"), silicon}},
                          [&pythonExe](calango::core::CalculatorKind) {
                              return pythonExe;
                          });

    std::printf("Pipeline construction:\n");
    OrchestrationNodeItem* relax = window.addProcessNode(
        OrchestrationTask::GeometryOptimization, 0,
        calango::core::CalculatorKind::LennardJones);
    OrchestrationNodeItem* phonon =
        window.addProcessNode(OrchestrationTask::Phonon, 0,
                              calango::core::CalculatorKind::LennardJones);
    check(relax && phonon, "two process nodes on the canvas");
    window.linkNodes(relax, phonon);

    // Node 1 configured the way the wizard's "Save process node" commits:
    // geometry optimization WITH unit cell relaxation.
    {
        calango::core::CalculatorConfig config;
        config.task = calango::core::TaskKind::GeometryOptimization;
        config.calculator = calango::core::CalculatorKind::LennardJones;
        config.relaxCell = true; // the requirement: cell relaxes too
        config.maxSteps = 60;    // LJ silicon needs few; cap keeps CI honest
        window.configureNode(
            relax,
            QString::fromStdString(calango::core::AseScriptGenerator::
                                       generate(config, "structure.extxyz")),
            pythonExe, QString(),
            calango::core::CalculatorKind::LennardJones);
        check(relax->isConfigured() &&
                  relax->configuredScript().contains(
                      QStringLiteral("optimized.extxyz")),
              "relaxation node carries its committed script");
        check(relax->configuredScript().contains(QStringLiteral("Filter")),
              "with a cell filter (unit cell relaxation)");
    }
    // Node 2: default phonon settings shrunk for speed — exactly what
    // configureNode would receive from the Phonon wizard.
    {
        calango::core::PhononConfig config;
        config.calculator.calculator =
            calango::core::CalculatorKind::LennardJones;
        config.supercell[0] = config.supercell[1] = config.supercell[2] = 2;
        config.bandPathPoints = 40;
        config.dosKptGrid[0] = config.dosKptGrid[1] = config.dosKptGrid[2] = 6;
        window.configureNode(
            phonon,
            QString::fromStdString(
                calango::core::PhononScriptGenerator::generate(
                    config, "structure.extxyz")),
            pythonExe, QString(),
            calango::core::CalculatorKind::LennardJones);
        check(phonon->isConfigured(), "phonon node carries its script");
    }

    std::printf("Execution:\n");
    window.sendToProcesses();
    check(relax->status() == OrchestrationNodeItem::Status::Running,
          "node 1 starts running on send");
    check(phonon->status() == OrchestrationNodeItem::Status::Waiting,
          "node 2 queues as waiting");

    // Drive the event loop until the pipeline settles (all nodes terminal).
    const auto terminal = [](const OrchestrationNodeItem* node) {
        return node->status() == OrchestrationNodeItem::Status::Done
            || node->status() == OrchestrationNodeItem::Status::Failed
            || node->status() == OrchestrationNodeItem::Status::Skipped;
    };
    QElapsedTimer timer;
    timer.start();
    while ((!terminal(relax) || !terminal(phonon))
           && timer.elapsed() < 600000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    check(relax->status() == OrchestrationNodeItem::Status::Done,
          "geometry optimization finished");
    const QString relaxed =
        relax->jobDirectory() + QStringLiteral("/optimized.extxyz");
    check(QFile::exists(relaxed), "and wrote optimized.extxyz");

    check(phonon->status() == OrchestrationNodeItem::Status::Done,
          "phonon calculation finished");
    const QString injected =
        phonon->jobDirectory() + QStringLiteral("/structure.extxyz");
    check(QFile::exists(injected), "node 2 received a structure.extxyz");
    // The heart of the data flow: node 2's input IS node 1's relaxed output,
    // byte for byte — not the original unrelaxed silicon.
    check(!readAll(relaxed).isEmpty()
              && readAll(injected) == readAll(relaxed),
          "and it is exactly node 1's relaxed structure");
    check(QFile::exists(phonon->jobDirectory()
                        + QStringLiteral("/phonon_band.json")),
          "phonon dispersion written");

    // ---- Scenario 2: DEFAULT-configured nodes + the Processes panel -------
    // No configureNode calls — the execution engine's fallback generators
    // carry the pipeline — and a real ProcessManagerPanel attached, pinning
    // that a dispatch registers, tracks and completes its rows there.
    std::printf("Default-configuration pipeline + Processes panel:\n");
    {
        calango::gui::ProcessManagerPanel panel;
        OrchestrationWindow window2(
            {{QStringLiteral("Si"), silicon}},
            [&pythonExe](calango::core::CalculatorKind) { return pythonExe; },
            &panel);
        OrchestrationNodeItem* relax2 = window2.addProcessNode(
            OrchestrationTask::GeometryOptimization, 0,
            calango::core::CalculatorKind::LennardJones);
        OrchestrationNodeItem* phonon2 = window2.addProcessNode(
            OrchestrationTask::Phonon, 0,
            calango::core::CalculatorKind::LennardJones);
        window2.linkNodes(relax2, phonon2);

        // The signals MainWindow's Results panel integration rides on: one
        // started (with a real directory) and one successful finish per node.
        int startedCount = 0;
        int finishedOk = 0;
        QObject::connect(
            &window2, &OrchestrationWindow::nodeStarted,
            [&startedCount](int id, const QString&, const QString& dir) {
                if (id >= 0 && !dir.isEmpty())
                    ++startedCount;
            });
        QObject::connect(&window2, &OrchestrationWindow::nodeFinished,
                         [&finishedOk](int, bool ok) { finishedOk += ok; });

        window2.sendToProcesses();

        auto* tree = panel.findChild<QTreeWidget*>();
        check(tree && tree->topLevelItemCount() == 2,
              "dispatch registers both nodes in the Processes panel");
        if (tree && tree->topLevelItemCount() == 2) {
            const QString first = tree->topLevelItem(0)->text(1);
            const QString second = tree->topLevelItem(1)->text(1);
            check((first == QLatin1String("running")
                   && second == QLatin1String("queued"))
                      || (first == QLatin1String("queued")
                          && second == QLatin1String("running")),
                  "panel shows one running and one queued row");
        }

        QElapsedTimer timer2;
        timer2.start();
        while ((!terminal(relax2) || !terminal(phonon2))
               && timer2.elapsed() < 600000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

        check(relax2->status() == OrchestrationNodeItem::Status::Done
                  && phonon2->status() == OrchestrationNodeItem::Status::Done,
              "default-configured pipeline runs to completion");
        const QString relaxed2 =
            relax2->jobDirectory() + QStringLiteral("/optimized.extxyz");
        const QString injected2 =
            phonon2->jobDirectory() + QStringLiteral("/structure.extxyz");
        check(!readAll(relaxed2).isEmpty()
                  && readAll(injected2) == readAll(relaxed2),
              "relaxed geometry (coordinates + cell) injected into node 2");
        if (tree && tree->topLevelItemCount() == 2)
            check(tree->topLevelItem(0)->text(1)
                          == QLatin1String("completed")
                      && tree->topLevelItem(1)->text(1)
                          == QLatin1String("completed"),
                  "both panel rows end completed");
        check(startedCount == 2 && finishedOk == 2,
              "node lifecycle signals fired for the Results integration");
    }

    if (failures)
        std::printf("\n%d check(s) FAILED.\n", failures);
    else
        std::printf("\nAll checks passed.\n");
    return failures ? 1 : 0;
}
