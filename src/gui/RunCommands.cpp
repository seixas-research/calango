#include "gui/RunCommands.hpp"

#include "gui/CondaEnvs.hpp"
#include "gui/EnginePresets.hpp"
#include "gui/SettingsManager.hpp"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QThread>

#include <algorithm>

namespace calango::gui {

namespace RunCommands {

namespace {

QJsonObject readMap()
{
    const QString raw =
        QSettings().value(QLatin1String(SettingsManager::kRunCommands)).toString();
    return QJsonDocument::fromJson(raw.toUtf8()).object();
}

void writeMap(const QJsonObject& map)
{
    QSettings().setValue(
        QLatin1String(SettingsManager::kRunCommands),
        QString::fromUtf8(QJsonDocument(map).toJson(QJsonDocument::Compact)));
    SettingsManager::save(); // mirror to ~/.calango/settings.json immediately
}

/// Quote a path for the /bin/sh (or cmd.exe) line the job is launched with.
/// Conda environments live under the user's home directory, and "Application
/// Support" or a space in a username is enough to split the command in two.
QString shellQuoted(const QString& path)
{
    if (path.isEmpty() || !path.contains(QLatin1Char(' ')))
        return path;
    return QLatin1Char('"') + path + QLatin1Char('"');
}

/// The launch line for a SIESTA that Conda installed, or "" when no
/// environment provides one.
///
/// Two things have to be resolved here rather than left to $PATH:
///
///   • WHICH siesta. `conda create -n siesta -c conda-forge siesta` produces an
///     environment with a solver and no Python in it, so it is not the
///     environment the engine's interpreter comes from and its bin/ is not on
///     the job's PATH. Naming the binary absolutely is what makes that layout
///     work at all.
///
///   • WHETHER to launch it under MPI. conda-forge ships `nompi`, `openmpi`
///     and `mpich` builds of SIESTA under the same package name. Running a
///     serial build under `mpirun -np 8` does not fail: it starts eight
///     independent copies of the same calculation in one directory, all
///     writing the same files, and the result looks like a corrupted run
///     rather than a misconfiguration. The MPI builds pull their launcher into
///     the same environment, so an environment with no mpirun beside its
///     siesta is taken as serial and launched directly. When there IS one, it
///     is named absolutely too — a conda-built solver started by the system's
///     mpirun is the other classic way this fails.
QString condaSiestaCommand()
{
    const QString env = CondaEnvs::environmentProviding(
        QStringLiteral("siesta"),
        EnginePresets::envFor(core::CalculatorKind::Siesta));
    if (env.isEmpty())
        return {};
    const QString siesta =
        shellQuoted(CondaEnvs::executableIn(env, QStringLiteral("siesta")));
    if (siesta.isEmpty())
        return {};

    for (const auto* launcher : {"mpirun", "mpiexec"}) {
        const QString mpi =
            CondaEnvs::executableIn(env, QLatin1String(launcher));
        if (!mpi.isEmpty()) {
            return QStringLiteral("%1 -np {cores} %2 < {input} > {output}")
                .arg(shellQuoted(mpi), siesta);
        }
    }
    return QStringLiteral("%1 < {input} > {output}").arg(siesta);
}

} // namespace

QString defaultTemplate(core::CalculatorKind kind)
{
    switch (kind) {
    case core::CalculatorKind::Gpaw:
        // GPAW parallelizes by running the SCRIPT under MPI. mpirun directly
        // rather than `gpaw -P n python`, its own launcher wrapper: the
        // wrapper has to find and re-exec the right mpirun itself, which is
        // where it fails on a machine with more than one MPI installed or with
        // the scheduler's launcher first on PATH. Calling mpirun by name uses
        // whichever one the environment already resolved.
        //
        // OMP_NUM_THREADS=1 is deliberate: GPAW's own MPI decomposition and a
        // threaded BLAS underneath it fight for the same cores and the run
        // gets slower, not faster.
        return QStringLiteral(
            "OMP_NUM_THREADS=1 mpirun -n {cores} gpaw python {script}");
    case core::CalculatorKind::QuantumEspresso:
        return QStringLiteral("mpirun -np {cores} pw.x -in {input} > {output}");
    case core::CalculatorKind::Siesta: {
        // A Conda-installed solver is named absolutely and launched by its own
        // environment's MPI, or serially when that environment has none — see
        // condaSiestaCommand(). Falls back to the bare name, which is right for
        // a module-loaded or system-packaged SIESTA already on $PATH.
        const QString detected = condaSiestaCommand();
        return detected.isEmpty()
            ? QStringLiteral("mpirun -np {cores} siesta < {input} > {output}")
            : detected;
    }
    case core::CalculatorKind::Vasp:
        return QStringLiteral("mpirun -np {cores} vasp_std");
    case core::CalculatorKind::Orca:
        // ORCA reads its own %pal block for parallelism and must be invoked
        // with a full path, so the rank count does not belong on this line.
        return QStringLiteral("orca {input} > {output}");
    case core::CalculatorKind::DftbPlus:
        // No {input}/{output}: ASE appends its own '> PREFIX.out' to
        // $DFTB_COMMAND, so a redirection here would nest two of them. The
        // template is just the binary (prefix it with OMP_NUM_THREADS=n or
        // mpirun for a parallel build); without {script} it is exported as
        // the solver command rather than run as the job line.
        return QStringLiteral("dftb+");
    case core::CalculatorKind::Abinit:
        // ABINIT reads its input from stdin in the ASE-driven form.
        return QStringLiteral("mpirun -np {cores} abinit < {input} > {output}");
    case core::CalculatorKind::FhiAims:
        // aims writes to stdout and takes no input argument at all: it reads
        // control.in / geometry.in from the working directory.
        return QStringLiteral("mpirun -np {cores} aims.x > {output}");
    case core::CalculatorKind::NwChem:
        return QStringLiteral("mpirun -np {cores} nwchem {input} > {output}");
    case core::CalculatorKind::OpenMx:
        // OpenMX takes the .dat path as an argument and OpenMP threads with
        // -nt; the MPI ranks and the threads multiply, so the thread count is
        // left at 1 here rather than silently oversubscribing.
        return QStringLiteral("mpirun -np {cores} openmx {input} -nt 1 > {output}");
    case core::CalculatorKind::Fleur:
        // The SCF binary. inpgen (the input generator ASE calls first) is
        // resolved separately, through $ASE_FLEUR_INPGEN.
        return QStringLiteral("mpirun -np {cores} fleur_MPI");
    case core::CalculatorKind::Cp2k:
        // NOT an input/output pair: ASE speaks to a PERSISTENT cp2k_shell
        // process over a pipe, so this is the shell command itself, and the
        // '-s' that puts it in that mode is part of it.
        return QStringLiteral("mpirun -np {cores} cp2k_shell.psmp -s");
    case core::CalculatorKind::Amber:
        // sander's own flags carry the file names (ASE fills -i/-o/-p/-c), so
        // the template is the launcher plus the binary. -O overwrites, which
        // is what makes a re-run of the same job directory work.
        return QStringLiteral("sander -O ");
    default:
        break;
    }
    // Everything else — the ML potentials, xTB (in-process through its
    // Python API), GROMACS (whose gmx binary is a calculator setting, not a
    // launch command), EMT/LJ, ASAP — is a single Python process; threading
    // is controlled by OMP_NUM_THREADS (Preferences → General) and by the
    // model's own device selection.
    return QStringLiteral("{python} {script}");
}

QString templateFor(core::CalculatorKind kind)
{
    const QString stored =
        readMap().value(EnginePresets::presetName(kind)).toString();
    return stored.trimmed().isEmpty() ? defaultTemplate(kind) : stored;
}

void setTemplateFor(core::CalculatorKind kind, const QString& commandTemplate)
{
    QJsonObject map = readMap();
    const QString key = EnginePresets::presetName(kind);
    // Storing a value identical to the default would freeze this engine at
    // today's default; clearing lets it track future releases.
    if (commandTemplate.trimmed().isEmpty()
        || commandTemplate.trimmed() == defaultTemplate(kind).trimmed())
        map.remove(key);
    else
        map[key] = commandTemplate.trimmed();
    writeMap(map);
}

int cores()
{
    return QSettings()
        .value(QLatin1String(SettingsManager::kRunCores),
               std::max(1, QThread::idealThreadCount() / 2))
        .toInt();
}

void setCores(int coreCount)
{
    QSettings().setValue(QLatin1String(SettingsManager::kRunCores),
                         std::max(1, coreCount));
    SettingsManager::save();
}

QString solverCommandVariable(core::CalculatorKind kind)
{
    switch (kind) {
    case core::CalculatorKind::QuantumEspresso:
        return QStringLiteral("ASE_ESPRESSO_COMMAND");
    case core::CalculatorKind::Siesta:
        return QStringLiteral("ASE_SIESTA_COMMAND");
    case core::CalculatorKind::Vasp:
        return QStringLiteral("ASE_VASP_COMMAND");
    case core::CalculatorKind::Orca:
        return QStringLiteral("ASE_ORCA_COMMAND");
    case core::CalculatorKind::Lammps:
        // Only the lammpsrun interface spawns a binary at all; the library
        // interface runs LAMMPS in-process and ignores this entirely. Naming
        // the variable here is still right — the resolver only exports it when
        // the user has actually configured a command.
        return QStringLiteral("ASE_LAMMPSRUN_COMMAND");
    case core::CalculatorKind::DftbPlus:
        // DFTB_COMMAND, not ASE_DFTB_COMMAND: the Dftb constructor resolves
        // its command itself (DFTB_COMMAND + ' > PREFIX.out', then the [dftb]
        // config section, then literally 'dftb+ > PREFIX.out'), so the
        // generic ASE_*_COMMAND fallback is never consulted.
        return QStringLiteral("DFTB_COMMAND");
    case core::CalculatorKind::Gromacs:
        // Read only when the generated script passes no command= — the wizard
        // normally bakes the gmx path into the calculator, and this is the
        // fallback ASE's FileIOCalculator machinery checks.
        return QStringLiteral("ASE_GROMACS_COMMAND");
    case core::CalculatorKind::Abinit:
        return QStringLiteral("ASE_ABINIT_COMMAND");
    case core::CalculatorKind::FhiAims:
        return QStringLiteral("ASE_AIMS_COMMAND");
    case core::CalculatorKind::NwChem:
        return QStringLiteral("ASE_NWCHEM_COMMAND");
    case core::CalculatorKind::OpenMx:
        return QStringLiteral("ASE_OPENMX_COMMAND");
    case core::CalculatorKind::Fleur:
        // The SCF binary. ase-fleur resolves the input generator separately
        // through ASE_FLEUR_INPGEN, which the generated script sets up.
        return QStringLiteral("ASE_FLEUR_COMMAND");
    case core::CalculatorKind::Cp2k:
        // Not a solver command in the usual sense — it is the persistent
        // cp2k_shell ASE keeps a pipe open to — but it travels the same way,
        // and the generated script reads exactly this variable.
        return QStringLiteral("ASE_CP2K_COMMAND");
    case core::CalculatorKind::Amber:
        // ASE's Amber calculator takes `amber_exe` as a constructor argument
        // rather than from the environment, and the wizard bakes it in; this
        // is the fallback for a hand-edited script that drops it.
        return QStringLiteral("ASE_AMBER_COMMAND");
    default:
        break;
    }
    return QString();
}

Resolved resolve(core::CalculatorKind kind, const Context& context,
                 const QString& commandTemplate)
{
    const QString text = commandTemplate.trimmed().isEmpty()
        ? templateFor(kind)
        : commandTemplate.trimmed();

    const auto substitute = [&context](QString value) {
        value.replace(QLatin1String("{cores}"), QString::number(std::max(1, context.cores)));
        value.replace(QLatin1String("{script}"), context.scriptFile);
        value.replace(QLatin1String("{python}"),
                      context.pythonExecutable.isEmpty()
                          ? QStringLiteral("python")
                          : context.pythonExecutable);
        return value;
    };

    Resolved resolved;
    // VASP's three build flavors (Preferences → External Files): exported
    // UNCONDITIONALLY here, whichever command-line path this run takes
    // below, because it is the GENERATED SCRIPT (AseScriptGenerator.cpp's
    // emitVasp()) that reads them to pick ASE_VASP_COMMAND per run — the
    // same "resolved wherever the script executes" design VASP_PP_PATH
    // already uses (see SettingsManager.hpp's own comment on
    // kVaspExecutableStd). A custom "{script}" launcher template still
    // needs them for the same reason a template override does not disable
    // VASP_PP_PATH resolution either.
    if (kind == core::CalculatorKind::Vasp) {
        static constexpr std::pair<const char*, const char*> kFlavors[] = {
            {SettingsManager::kVaspExecutableStd, "CALANGO_VASP_STD"},
            {SettingsManager::kVaspExecutableGam, "CALANGO_VASP_GAM"},
            {SettingsManager::kVaspExecutableNcl, "CALANGO_VASP_NCL"},
        };
        for (const auto& [key, variable] : kFlavors) {
            const QString path =
                QSettings().value(QLatin1String(key)).toString().trimmed();
            if (!path.isEmpty())
                resolved.environment.insert(QLatin1String(variable), path);
        }
    }

    // Script-launcher vs. solver-command is a property of the ENGINE
    // (kind), not of whether the literal "{script}" placeholder still
    // appears in this particular string — checking the string was the bug
    // behind "GPAW runs on 1 core despite cores=4" surviving the previous
    // fix (Task 1, 2026-08-22): displayCommand() below deliberately
    // pre-substitutes every placeholder (including {script} -> "run.py")
    // so the wizard's editable "Running:" field shows a ready-to-read
    // command rather than raw template syntax. That already-substituted
    // text is exactly what a caller who left the field untouched hands
    // back here as `commandTemplate` (SimulationWizardBase::runCommand(),
    // OrchestrationNodeItem::configuredRunCommand()) — with no literal
    // "{script}" left in it, `text.contains("{script}")` was always FALSE
    // for it, so a fully-correct GPAW "OMP_NUM_THREADS=1 mpirun -n 4 gpaw
    // python run.py" preview was misclassified as a solver command,
    // GPAW has no ASE_*_COMMAND (solverCommandVariable() returns empty for
    // it), and the whole mpirun wrapper was silently discarded in favour
    // of the generic solver-command fallback "{python} {script}" — a bare
    // serial `python run.py`, with cores=4 lost with no diagnostic at all.
    if (defaultTemplate(kind).contains(QLatin1String("{script}"))) {
        // A script launcher: this line starts the job process itself.
        resolved.commandLine = substitute(text);
        return resolved;
    }

    // A solver command. {input}/{output} are NOT substituted here — ASE fills
    // them in per calculation (PREFIX.in / PREFIX.out), so they must survive
    // into the environment variable verbatim.
    const QString variable = solverCommandVariable(kind);
    if (!variable.isEmpty())
        resolved.environment.insert(variable, substitute(text));
    // The job itself is still one Python process driving that solver.
    resolved.commandLine = substitute(QStringLiteral("{python} {script}"));
    return resolved;
}

QString displayCommand(core::CalculatorKind kind, const Context& context)
{
    // The field shows (and the user edits) the TEMPLATE with the launch-time
    // values filled in — not the derived process line. For a solver command
    // the interesting part is the mpirun invocation, and substituting it away
    // to "python run.py" would hide exactly what the user came to check.
    // {input}/{output} stay symbolic: ASE fills those per calculation.
    QString text = templateFor(kind);
    text.replace(QLatin1String("{cores}"),
                 QString::number(std::max(1, context.cores)));
    text.replace(QLatin1String("{script}"), context.scriptFile);
    text.replace(QLatin1String("{python}"),
                 context.pythonExecutable.isEmpty()
                     ? QStringLiteral("python")
                     : context.pythonExecutable);
    return text;
}

} // namespace RunCommands

} // namespace calango::gui
