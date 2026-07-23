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

    /// File name the module must be written under for the generated
    /// `from calango_log import CalangoLog` to resolve.
    static const char* loggerModuleFileName();
};

} // namespace calango::core
