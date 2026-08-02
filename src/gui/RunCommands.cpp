#include "gui/RunCommands.hpp"

#include "gui/EnginePresets.hpp"
#include "gui/SettingsManager.hpp"

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
    case core::CalculatorKind::Siesta:
        return QStringLiteral("mpirun -np {cores} siesta < {input} > {output}");
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
    if (text.contains(QLatin1String("{script}"))) {
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
