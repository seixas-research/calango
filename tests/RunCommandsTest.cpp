// RunCommands::resolve()/displayCommand() round-trip (Task 1, 2026-08-22).
//
// The regression this guards: displayCommand() deliberately returns an
// ALREADY-SUBSTITUTED preview ({script} replaced with the literal script
// file name) so a wizard's editable "Running:" field shows something a user
// can read rather than raw template syntax. When that same text is fed back
// into resolve() as `commandTemplate` -- exactly what happens when a user
// leaves the field untouched and clicks Run (Local) -- resolve() used to
// decide "script launcher vs. solver command" by checking whether the text
// STILL contained the literal "{script}" placeholder. It never does, for
// text that already came out of displayCommand(), so a fully-correct GPAW
// preview ("OMP_NUM_THREADS=1 mpirun -n 4 gpaw python run.py") was
// misclassified as a solver command every time, and — GPAW having no
// ASE_*_COMMAND env var to export it into — the whole mpirun wrapper was
// silently discarded in favour of a bare serial "python run.py". This is
// the real root cause of "GPAW runs on 1 core despite cores=4" surviving
// the previous session's fix: that fix made resolve()'s GPAW template
// itself correct, but every ordinary Run (Local) click routed the ALREADY-
// RESOLVED preview text back through resolve(), which reclassified it.

#include "gui/RunCommands.hpp"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

using namespace calango::gui;

namespace {

int failures = 0;

void check(bool ok, const char* label)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", label);
    if (!ok)
        ++failures;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Calango Test"));
    QCoreApplication::setApplicationName(
        QStringLiteral("RunCommandsTest"));

    // A private QSettings scope so this never touches the real
    // ~/.calango/settings.json -- RunCommands::cores()/templateFor() read
    // straight through QSettings.
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) {
        std::printf("SKIP: no temporary directory available\n");
        return 0;
    }
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       sandbox.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    std::printf("GPAW: the wizard's untouched \"Running:\" preview still "
               "parallelizes (Task 1 regression):\n");
    {
        RunCommands::setCores(4);
        RunCommands::Context context;
        context.pythonExecutable =
            QStringLiteral("/opt/gpaw_env/bin/python");
        context.scriptFile = QStringLiteral("run.py");
        context.cores = RunCommands::cores();

        // Exactly what SimulationWizardBase::refreshRunCommand() writes
        // into runCommandEdit_, and exactly what runCommand() returns when
        // the user never edits it.
        const QString preview =
            RunCommands::displayCommand(calango::core::CalculatorKind::Gpaw, context);
        check(preview.contains(QStringLiteral("mpirun -n 4"))
                  && !preview.contains(QStringLiteral("{cores}")),
              "the preview itself is already fully substituted (cores=4, "
              "no placeholders left)");
        check(!preview.contains(QStringLiteral("{script}")),
              "...and {script} in particular is already gone -- this is "
              "exactly the string shape that broke the old string-sniffing "
              "dispatch");

        // The bug: feeding that preview straight back into resolve(), as
        // MainWindow::runScript()/OrchestrationWindow::startNode() do with
        // wizard.runCommand()/node->configuredRunCommand().
        const RunCommands::Resolved resolved = RunCommands::resolve(
            calango::core::CalculatorKind::Gpaw, context, preview);
        check(resolved.commandLine.contains(QStringLiteral("mpirun -n 4"))
                  && resolved.commandLine.contains(
                         QStringLiteral("gpaw python run.py")),
              "resolve() still launches under mpirun with 4 ranks -- NOT "
              "silently downgraded to a bare serial \"python run.py\"");
        check(resolved.environment.isEmpty(),
              "and nothing was exported as a solver-command environment "
              "variable (GPAW is a script launcher, not a solver command)");
    }

    std::printf("GPAW: the SAME round-trip at cores=1 stays serial "
               "correctly (not a false mpirun wrap):\n");
    {
        RunCommands::setCores(1);
        RunCommands::Context context;
        context.pythonExecutable =
            QStringLiteral("/opt/gpaw_env/bin/python");
        context.scriptFile = QStringLiteral("run.py");
        context.cores = RunCommands::cores();
        const QString preview =
            RunCommands::displayCommand(calango::core::CalculatorKind::Gpaw, context);
        const RunCommands::Resolved resolved = RunCommands::resolve(
            calango::core::CalculatorKind::Gpaw, context, preview);
        check(resolved.commandLine.contains(QStringLiteral("mpirun -n 1")),
              "cores=1 still round-trips through mpirun -n 1 (not skipped "
              "entirely) -- the template does not special-case 1 rank, and "
              "neither should resolve()");
    }

    std::printf("VASP (a solver-command engine): the same round-trip still "
               "goes to ASE_VASP_COMMAND, not the job's own command "
               "line:\n");
    {
        RunCommands::setCores(8);
        RunCommands::Context context;
        context.pythonExecutable = QStringLiteral("/usr/bin/python3");
        context.scriptFile = QStringLiteral("run.py");
        context.cores = RunCommands::cores();
        const QString preview =
            RunCommands::displayCommand(calango::core::CalculatorKind::Vasp, context);
        check(preview.contains(QStringLiteral("mpirun -np 8 vasp_std")),
              "the VASP preview is the mpirun-wrapped solver line itself, "
              "not a {script} launcher line");
        const RunCommands::Resolved resolved = RunCommands::resolve(
            calango::core::CalculatorKind::Vasp, context, preview);
        check(!resolved.commandLine.contains(QStringLiteral("mpirun")),
              "resolve()'s job command line stays a plain interpreter "
              "invocation for VASP, exactly as before this fix -- the "
              "kind-based dispatch does not change solver-command engines "
              "at all");
        check(resolved.environment.value(QStringLiteral("ASE_VASP_COMMAND"))
                  == QStringLiteral("mpirun -np 8 vasp_std"),
              "and the rank count reaches ASE_VASP_COMMAND unchanged");
    }

    std::printf("Empty commandTemplate still falls back to templateFor() "
               "for every kind (no regression from the dispatch change):\n");
    {
        RunCommands::setCores(2);
        RunCommands::Context context;
        context.pythonExecutable = QStringLiteral("/usr/bin/python3");
        context.scriptFile = QStringLiteral("run.py");
        context.cores = RunCommands::cores();
        const RunCommands::Resolved gpaw = RunCommands::resolve(
            calango::core::CalculatorKind::Gpaw, context, QString());
        check(gpaw.commandLine.contains(QStringLiteral("mpirun -n 2 gpaw "
                                                       "python run.py")),
              "GPAW with no override still resolves via templateFor()");
        const RunCommands::Resolved qe = RunCommands::resolve(
            calango::core::CalculatorKind::QuantumEspresso, context, QString());
        check(qe.environment.value(QStringLiteral("ASE_ESPRESSO_COMMAND"))
                  .contains(QStringLiteral("mpirun -np 2 pw.x")),
              "Quantum ESPRESSO with no override still resolves via "
              "templateFor()");
    }

    if (failures == 0) {
        std::printf("\nAll RunCommands resolve()/displayCommand() checks "
                   "passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d RunCommands check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
