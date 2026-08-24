#pragma once

#include "core/CalculatorConfig.hpp"

#include <sstream>
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

    /// Re-emit a generated block with `indent` prefixed to every non-empty
    /// line, so a module-level snippet (usually calculatorSnippet()) can live
    /// inside a Python function body. Blank lines stay blank rather than
    /// carrying trailing whitespace.
    static void emitIndented(std::ostringstream& out, const std::string& block,
                             const std::string& indent);

    /// The `_CellFilter` import shim for variable-cell relaxation: prefers
    /// ase.filters (ASE >= 3.23) and falls back to ase.constraints, so the
    /// same script runs on either ASE.
    static void emitCellFilterImport(std::ostringstream& out,
                                     CellFilter cellFilter);

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
    /// MCMD sampler) streams through the SAME code path as a relaxation or an
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

    /// The `convergence={...}` dict and `maxiter=`, factored out of
    /// gpawKeywordArguments() so a generator with its own mode/kpts/nbands
    /// handling (XasScriptGenerator's core-hole `setups=`/negative `nbands=`
    /// convention does not fit the shared one) can still emit the SAME SCF
    /// tolerance the wizard's Calculator & Convergence page collected,
    /// instead of silently building a calculator that ignores it.
    static std::string gpawConvergenceArguments(const CalculatorConfig& config,
                                                const std::string& indent);
    /// `spinpol=`/the non-collinear comment, then `occupations={...}` —
    /// factored out for the same reason as gpawConvergenceArguments(), and
    /// called separately from it (not merged into one function) because
    /// gpawKeywordArguments() itself emits the DFT+U `setups=` dict between
    /// the two, and this split keeps that call site's emitted order and
    /// content byte-for-byte unchanged.
    static std::string gpawSpinOccupationsArguments(const CalculatorConfig& config,
                                                     const std::string& indent);

    /// Standalone script that restarts GPAW from the `*.gpw` in `gpwDir` and
    /// writes the charge density to `density.cube` (all-electron when
    /// `allElectron`, else the pseudo density). Used by the Single-Point
    /// Viewer's "Get Volumetric Data" action to export a density on demand from
    /// a completed run that did not already write one. `gpwDir` is baked in as
    /// an absolute path so the script can run from a fresh job directory.
    static std::string densityCubeScript(const std::string& gpwDir,
                                         bool allElectron);

    /// Restarts GPAW from a completed baseline's saved wavefunctions,
    /// generalizing the glob-and-check block WannierScriptGenerator's
    /// `groundState()` introduced (that one keeps its own inline copy, with
    /// "MLWF localization" baked into its message, rather than being
    /// retrofitted onto this). Leaves `calc` (restarted GPAW calculator) and
    /// `atoms` defined; `_gpw_path` holds the absolute `.gpw` path actually
    /// used. Checks `calculator.json`'s recorded engine first — a clear
    /// refusal in milliseconds, before the `*.gpw` glob even runs — then
    /// raises a `RuntimeError` naming exactly what is missing if no `.gpw`
    /// is found.
    ///
    /// `featureName` names the calling module in the engine-mismatch message
    /// ("Local Density of States", "Wavefunctions"); `whatIsNeeded` is
    /// appended to the "no .gpw found" message to say how to fix it.
    ///
    /// Shared by the LDOS and Wavefunctions generators — the ONE
    /// implementation of "get a restarted GPAW calculator from a baseline
    /// directory" both are built on, per the project's baseline-inheriting
    /// wizard convention (see docs/sphinx/source/simulations/orchestration.md,
    /// "Inherited runs").
    static std::string gpawRestartFromBaselineScript(
        const std::string& baselineDir, const std::string& featureName,
        const std::string& whatIsNeeded);

    /// The wavefunction-access helper shared by LDOS and Wavefunctions:
    /// defines a Python function
    /// `_calango_wave_function(calc, band, kpt, spin, all_electron=False,
    /// grid_spacing=0.05)` returning the real-space array for one
    /// Kohn-Sham state (or `None` on an MPI rank that does not hold it —
    /// dead code today, since generated scripts never launch under MPI, but
    /// matching the underlying GPAW APIs' own contract rather than
    /// asserting something about them that is not true).
    ///
    /// Pseudo-wavefunctions (`all_electron=False`) go through
    /// `calc.get_pseudo_wave_function(periodic=True)` — the same call
    /// BandSymmetryScriptGenerator already uses — and work on any GPAW
    /// build. The PAW all-electron reconstruction
    /// (`calc.dft.ibzwfs.get_all_electron_wave_function(...)`) is new-GPAW
    /// internal state with no legacy-engine equivalent, so that branch
    /// raises a specific `RuntimeError` rather than the `AttributeError`
    /// `calc.dft` would otherwise fail with several frames down, on a
    /// restart under the legacy engine (`GPAW_NEW=0`, which nothing but
    /// XAS ever sets).
    static std::string gpawWaveFunctionHelperScript();

    /// The POTCAR resolution + flat-layout shim every VASP-emitting
    /// generator needs. ASE hardcodes `$VASP_PP_PATH/potpaw_PBE/<El>/
    /// POTCAR` with no way to override the subdirectory name; plenty of
    /// real installations — this session's own diagnosed failure
    /// (proc_4, a NiO band structure) among them — keep the element
    /// folders directly under the POTCAR root with no `potpaw_PBE` level
    /// at all. This detects that and builds a tiny symlink shim so ASE
    /// finds what is already there, exactly like emitVasp() has always
    /// done for the standard Single-point/Geometry Optimization page.
    ///
    /// Factored out (Task 3, 2026-08-22) because SIX other generators —
    /// Electronic Structure (the ICHARG=11 bands-from-CHGCAR chain, proc_4's
    /// own feature), 2D Bands, Born Charges, Raman/IR, Charge Density
    /// Difference, and point/extended Defects — each carried their own
    /// bare `os.environ['VASP_PP_PATH'] = r"..."` one-liner with NONE of
    /// this shim, so every one of them failed the exact same way for any
    /// flat-layout POTCAR library, not just the ICHARG=11 chain proc_4
    /// happened to exercise. HubbardScriptGenerator.cpp's VASP branch had
    /// the same bare one-liner and is switched to this too.
    ///
    /// MUST be emitted after `atoms` is already defined (the missing-
    /// element check reads `atoms.get_chemical_symbols()`) and after
    /// `import os`. Empty `potcarPath` emits nothing (ASE falls back to
    /// whatever VASP_PP_PATH the environment already carries — unchanged
    /// from every caller's own prior behavior for that case).
    static std::string vaspPotcarResolutionSnippet(
        const std::string& potcarPath);

    /// "Normal"/"Single"/"Accurate" — the literal PREC= value for a
    /// VASP() call, from the wizard's VaspPrecision choice. Defaults to
    /// "Accurate" (VASP's own most-conservative choice) exactly like
    /// emitVasp() always has.
    ///
    /// PREC directly sets VASP's FFT grid density for a given ENCUT — an
    /// ICHARG=11 non-self-consistent run that reads a CHGCAR written under
    /// a DIFFERENT PREC gets a DIFFERENT grid and VASP refuses it outright
    /// ("ERROR: charge density could not be read from file CHGCAR for
    /// ICHARG>10" — the second, distinct bug found alongside proc_4's
    /// POTCAR failure, Task 3, 2026-08-22): emitVasp() (the standard
    /// Single-point/Geometry Optimization page a CHGCAR baseline actually
    /// comes from) always emits an explicit PREC=, so any generator that
    /// restarts from its CHGCAR — ElectronicScriptGenerator.cpp's Vasp
    /// backend among them — MUST emit the SAME one, not rely on VASP's own
    /// default (Normal), or the grids silently stop matching the moment
    /// anyone picks anything but the default precision.
    /// The `ldau=…` keyword lines for a `Vasp(…)` call, or "" when the run
    /// asks for no Hubbard correction.
    ///
    /// Public because Electronic Structure (and anything else that builds its
    /// own Vasp() rather than going through the shared emitter) has to write
    /// the same block: a generator that forgets it produces a plain-PBE run
    /// from a wizard that collected a U, with nothing anywhere saying so.
    ///
    /// `withLmaxmix` false where the caller writes its own LMAXMIX line.
    /// The hybrid-functional INCAR tags as Python keyword arguments, one
    /// per line, each prefixed with `indent` — LHFCALC, GGA, AEXX, AGGAX,
    /// AGGAC, ALDAC and (screened hybrids only) HFSCREEN. Empty for a
    /// semilocal functional, so a caller can emit it unconditionally.
    ///
    /// Factored out so the ordinary calculator block and the hybrid
    /// band-structure route (ElectronicScriptGenerator's KPOINTS_OPT branch)
    /// write the SAME tags from the SAME transcription of
    /// https://vasp.at/wiki/List_of_hybrid_functionals — two emitters that
    /// drift apart would give a band structure computed with a different
    /// functional than the SCF that fed it, and nothing downstream could
    /// tell.
    ///
    /// ISTART is deliberately NOT included: whether this run restarts from a
    /// WAVECAR is the caller's question, not the functional's.
    static std::string vaspHybridKeywords(const CalculatorConfig& c,
                                          const std::string& indent);

    static std::string vaspHubbardKeywords(const CalculatorConfig& c,
                                           const std::string& indent,
                                           bool withLmaxmix = true);

    static std::string vaspPrecString(VaspPrecision prec);
};

} // namespace calango::core
