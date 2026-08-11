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

    /// The structured-logging preamble shared by every generated script: a
    /// plain dict plus the three functions
    /// `_calango_metric(step, energy=..., ...)`, `_calango_progress(step,
    /// total)` and `_calango_event(level, message)`. They write metrics.json
    /// and log.json (which Calango's Results panel polls) and route Python
    /// warnings to warnings.log. Other generators (Monte Carlo, NEB, MACE
    /// trainer) prepend this and call those functions instead of printing.
    ///
    /// It is EMBEDDED, not imported. Nothing in a generated script refers to
    /// Calango, so the file can be copied to a cluster and run as-is wherever
    /// ASE and the calculator are installed — which is the whole point of
    /// generating a script rather than driving the calculation in-process. It
    /// used to be `from calango_log import CalangoLog`, which quietly made
    /// every script a two-file bundle and failed on line 5 anywhere the helper
    /// module had not been copied along with it.
    static std::string jsonLoggerPreamble();

    /// The live-geometry streaming helper: defines `_stream_frame()`, which
    /// writes one CALANGO_CELL / CALANGO_FRAME block that JobRunner parses into
    /// a trajectory frame for the viewport.
    ///
    /// Exposed so a generator that drives its own loop (the graphene oxide
    /// MDMC sampler) streams through the SAME code path as a relaxation or an
    /// MD run. A second implementation of the block format would be a second
    /// thing to keep in step with the parser, and the parser is one regex.
    ///
    /// It reads a global named `atoms`, which is the convention every
    /// generated script already follows.
    static std::string streamFrameHelper();

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
