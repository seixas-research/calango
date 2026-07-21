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
};

} // namespace calango::core
