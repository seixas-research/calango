#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Turns a CalculatorConfig into a standalone, human-editable ASE Python
/// script. The script is the "source of truth" a job runs — users can save
/// it, edit it, version it, or run it on a cluster untouched by Calango.
///
/// The generated script emits machine-readable markers on stdout
/// (CALANGO_PROGRESS / CALANGO_RESULT / CALANGO_DONE) that JobRunner parses
/// to drive the progress bar and result display.
class AseScriptGenerator {
public:
    /// `structureFile` is the path the script will read (usually the
    /// relative name "structure.extxyz" inside the job directory).
    static std::string generate(const CalculatorConfig& config,
                                const std::string& structureFile);

    /// Just the calculator-construction block (imports + `atoms.calc = ...`),
    /// for embedding in other generated scripts (e.g. the phonon builder).
    static std::string calculatorSnippet(const CalculatorConfig& config);

    /// The structured-logging preamble shared by every generated script:
    /// imports CalangoLog from the staged `calango_log.py` module and installs
    /// it as `_calango_log`, a thread-safe JSON logger writing step metrics to
    /// metrics.json and events to log.json (and routing Python warnings to
    /// warnings.log). Other generators (Monte Carlo, NEB, MACE trainer)
    /// prepend this and call `_calango_log.metric(step, energy=..., ...)`
    /// instead of printing.
    ///
    /// Every caller that *writes a script somewhere* must also write
    /// loggerModuleSource() as loggerModuleFileName() beside it, or the
    /// import fails at run time.
    static std::string jsonLoggerPreamble();

    /// Full text of the `calango_log.py` helper module (the single copy lives
    /// at assets/.internal/scripts/calango_log.py and is baked in at build
    /// time). Staged next to run.py by the job launcher and next to an
    /// exported script by the wizards' Export action.
    static std::string loggerModuleSource();

    /// GPAW import line for the configured mode/mixer, and the keyword
    /// arguments of a GPAW(...) call (mode, xc, k-points, eigensolver, mixer,
    /// convergence, smearing), each line prefixed with `indent`.
    ///
    /// Shared so the Electronic Structure generator builds its SCF calculator
    /// from exactly the same parameter set as Geometry Optimization and
    /// Single-point — the two used to diverge, with the bands path hardcoding
    /// PW/PBE and ignoring every other GPAW setting the wizard collected.
    static std::string gpawImports(const CalculatorConfig& config);
    static std::string gpawKeywordArguments(const CalculatorConfig& config,
                                            const std::string& indent);

    /// File name the module must be written under for the generated
    /// `from calango_log import CalangoLog` to resolve.
    static const char* loggerModuleFileName();

    /// Standalone script that restarts GPAW from the `*.gpw` in `gpwDir` and
    /// writes the charge density to `density.cube` (all-electron when
    /// `allElectron`, else the pseudo density). Used by the Single-Point
    /// Viewer's "Get Volumetric Data" action to export a density on demand from
    /// a completed run that did not already write one. `gpwDir` is baked in as
    /// an absolute path so the script can run from a fresh job directory.
    static std::string densityCubeScript(const std::string& gpwDir,
                                         bool allElectron);
};

} // namespace calango::core
