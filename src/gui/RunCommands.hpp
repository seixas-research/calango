#pragma once

#include "core/CalculatorConfig.hpp"

#include <QMap>
#include <QString>

namespace calango::gui {

/// Per-engine shell command templates for launching a calculation
/// (Preferences → "Run"), persisted through QSettings into
/// `~/.calango/settings.json`.
///
/// There are two genuinely different kinds of template here, and the
/// distinction is what makes this work rather than being a cosmetic string:
///
///   * **Script launchers** contain `{script}`. The template IS the process
///     command line — the ASE script itself runs under it. This is how a
///     parallel GPAW job is started: `mpirun -n 4 gpaw python run.py`
///     launches four MPI ranks that each execute the script. NOT `gpaw -P 4
///     python run.py` — GPAW's own `-P`/`--parallel` flag is deprecated as
///     of the version this was verified against (26.7.1b1) and prints a
///     runtime warning recommending exactly the `mpirun`/`mpiexec` form
///     used here (gpaw/cli/main.py's own `hook()`).
///
///   * **Solver commands** contain `{input}` / `{output}`. These are NOT the
///     process command line: for Quantum ESPRESSO or SIESTA the ASE script
///     runs under a plain interpreter and ASE shells out to the solver binary
///     itself. Putting `mpirun -np 4 pw.x` on the job command line would run
///     four copies of the whole Python script instead of one script driving a
///     four-rank pw.x. So these templates are exported into the environment
///     variable the ASE calculator reads (ASE_ESPRESSO_COMMAND,
///     ASE_SIESTA_COMMAND), and the job runs `{python} {script}`.
///
/// resolve() applies that rule, so callers get one launch command line plus
/// whatever environment the engine needs. The classification is by ENGINE
/// (`kind`, via defaultTemplate()'s own placeholders), never by scanning the
/// literal command-line text for "{script}": a caller's `commandTemplate`
/// argument is routinely an ALREADY-SUBSTITUTED string (displayCommand()
/// below deliberately bakes every placeholder in, including {script}, so a
/// wizard's editable "Running:" field shows something a user can read) —
/// checking that string for a placeholder that has already been replaced
/// away is always false, which is exactly the bug ("GPAW runs on 1 core
/// despite cores=4" surviving its own fix, Task 1 2026-08-22) resolve()'s
/// own comment documents in full.
namespace RunCommands {

/// Placeholders a template may use:
///   {cores}   MPI rank count (the "Cores" setting, or the wizard's override)
///   {script}  the staged script file name (run.py)
///   {python}  the resolved interpreter for the engine
///   {input}   solver input file  (ASE substitutes PREFIX.in / PREFIX.fdf)
///   {output}  solver output file (ASE substitutes PREFIX.out)
struct Context {
    QString pythonExecutable; ///< resolved interpreter (Preferences → Python)
    QString scriptFile = QStringLiteral("run.py");
    int cores = 1;
};

/// What to actually launch, after applying the script-launcher vs
/// solver-command rule above.
struct Resolved {
    /// The full shell command line for the job process.
    QString commandLine;
    /// Extra environment variables the job needs (the ASE solver command for
    /// the template kinds that are not script launchers). Empty for the rest.
    QMap<QString, QString> environment;
};

/// The shipped default template for an engine — what the "Restore Defaults"
/// button writes back, and what an unconfigured engine runs.
QString defaultTemplate(core::CalculatorKind kind);

/// The user's template for `kind`, falling back to defaultTemplate().
QString templateFor(core::CalculatorKind kind);
/// Persist a template; an empty/whitespace string clears the override so the
/// engine falls back to the default again.
void setTemplateFor(core::CalculatorKind kind, const QString& commandTemplate);

/// MPI rank count substituted for `{cores}`. Defaults to half the machine's
/// cores — parallel enough to be useful out of the box, while a template that
/// silently claimed every core would oversubscribe any node already running
/// something else.
int cores();
void setCores(int cores);

/// The environment variable an engine's solver command belongs in
/// (ASE_ESPRESSO_COMMAND, ASE_SIESTA_COMMAND, …), or an empty string when the
/// engine has no such hand-off.
QString solverCommandVariable(core::CalculatorKind kind);

/// Substitute the placeholders in `commandTemplate` and split it into a launch
/// command line plus environment. Passing an empty template uses templateFor().
Resolved resolve(core::CalculatorKind kind, const Context& context,
                 const QString& commandTemplate = QString());

/// The command line a wizard shows in its editable "Running:" field: exactly
/// what resolve() would launch, so what the user reads is what runs.
QString displayCommand(core::CalculatorKind kind, const Context& context);

} // namespace RunCommands

} // namespace calango::gui
