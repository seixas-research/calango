// Regression test for the proc_26 failure mode.
//
// WHAT HAPPENED. A Wannier run asked for 10 Wannier functions against a ground
// state that had converged only 9 bands. With no frozen window ASE fixes
// exactly one state per Wannier function, so it tried to take 10 bands out of
// 9 and died inside rotation_from_projection with
//
//     ValueError: could not broadcast input array from shape (9,10)
//                 into shape (10,10)
//
// which names neither the Wannier count nor the band count. The generated
// script already guarded the OPPOSITE inequality (more frozen states than
// Wannier functions) and so said nothing.
//
// Two things are asserted here: that the generated script now carries the
// nwannier-vs-nbands pre-flight, and that the failure extractor turns that
// real captured traceback into a line worth showing.

#include "core/JobFailureReason.hpp"
#include "core/WannierScriptGenerator.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using calango::core::extractFailureReason;

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

void checkContains(const std::string& haystack, const std::string& needle,
                   const std::string& what)
{
    check(haystack.find(needle) != std::string::npos, what);
}

/// The real log.txt from ~/My Simulations/proc_26, trimmed to the frames that
/// matter. Kept verbatim so the extractor is tested against what Python
/// actually produced rather than against a tidied-up imitation.
const char* kProc26Log = R"(Traceback (most recent call last):
  File "/Users/leseixas/My Simulations/proc_26/run.py", line 113, in <module>
    wan = Wannier(nwannier=nwannier, calc=calc,
                  fixedenergy=_fixedenergy, fixedstates=_fixedstates,
                  initialwannier='orbitals')
  File "/Users/leseixas/miniconda3/envs/gpaw_fast/lib/python3.14/site-packages/ase/dft/wannier.py", line 621, in __init__
    self.initialize(file=file, initialwannier=initialwannier, rng=rng)
  File "/Users/leseixas/miniconda3/envs/gpaw_fast/lib/python3.14/site-packages/ase/dft/wannier.py", line 178, in rotation_from_projection
    U_ww[:M] = proj_nw[:M]
ValueError: could not broadcast input array from shape (9,10) into shape (10,10)
)";

void testFailureExtraction()
{
    std::printf("Failure reason out of the real proc_26 traceback:\n");
    const std::string reason = extractFailureReason(kProc26Log);
    check(!reason.empty(), "a reason is extracted at all");
    checkContains(reason, "could not broadcast",
                  "and it is the exception message, not an interpreter frame");
    checkContains(reason, "(9,10)", "carrying the shapes that disagree");
    check(reason.find("File \"") == std::string::npos,
          "with no traceback frames dragged along");
    std::printf("       -> \"%s\"\n", reason.c_str());
}

void testMultiLineRuntimeError()
{
    std::printf("A wrapped RuntimeError keeps its whole message:\n");
    // The pre-flight below raises exactly this shape: a long sentence Python
    // prints across several lines with no continuation marker. Cutting it at
    // the first newline would drop the half that says what to do.
    const std::string log =
        "Traceback (most recent call last):\n"
        "  File \"run.py\", line 99, in <module>\n"
        "    raise RuntimeError(...)\n"
        "RuntimeError: Requested 10 Wannier functions, but the ground state in\n"
        "single_point.gpw has only 9 bands. Lower the Wannier count to 9 or\n"
        "fewer, or re-run the Single-Point Calculation with more bands.\n";
    const std::string reason = extractFailureReason(log);
    checkContains(reason, "Requested 10 Wannier functions",
                  "the first line is kept");
    checkContains(reason, "re-run the Single-Point",
                  "and so is the actionable continuation");
    check(reason.find("RuntimeError:") == std::string::npos,
          "with the exception type stripped from the message");
}

void testNoFalsePositives()
{
    std::printf("Ordinary output is not mistaken for a failure:\n");
    check(extractFailureReason("").empty(), "empty log yields nothing");
    check(extractFailureReason("Total energy: -12.5 eV\nDone.\n").empty(),
          "prose containing a colon is not read as an exception");
    check(extractFailureReason("converged in 12 steps\n").empty(),
          "and neither is a plain progress line");
}

void testGeneratedScriptCarriesThePreflight()
{
    std::printf("The generated Wannier script pre-flights nwannier:\n");
    calango::core::WannierConfig config;
    config.nWannier = 10;
    const std::string script = calango::core::generateWannierScript(config);

    checkContains(script, "get_number_of_bands()",
                  "it asks the ground state how many bands it has");
    checkContains(script, "if nwannier > _nbands:",
                  "and refuses when more Wannier functions were asked for");
    checkContains(script, "Lower the Wannier count",
                  "with an actionable instruction, not just a diagnosis");
    // The check has to happen BEFORE the constructor that used to blow up,
    // or it costs the user the wait and reports nothing new.
    const std::size_t preflight = script.find("if nwannier > _nbands:");
    const std::size_t construct = script.find("wan = Wannier(");
    check(preflight != std::string::npos && construct != std::string::npos
              && preflight < construct,
          "and it runs before Wannier() is constructed");

    // The pre-existing guard covers the opposite inequality and must survive:
    // both failures are real, and they are different.
    checkContains(script, "_fixed > nwannier",
                  "the frozen-window guard is still there too");
}

/// A non-GPAW, non-VASP engine (Quantum ESPRESSO here, but the check is
/// engine-name-agnostic) with NO baseline: WannierWizard::calculatorAllowed()
/// keeps this unreachable from the wizard's own UI (only GPAW and VASP are
/// allowed there), but a hand-built or saved-workflow WannierConfig is not
/// bound by that, so the generator has to refuse on its own. Verified
/// against the installed ASE source (ase/dft/wannier.py, mace_env 0.3.15 /
/// gpaw_fast environments): new_Z() calls
/// calc.get_wannier_localization_matrix() unconditionally, a method only
/// GPAW implements — Quantum ESPRESSO and SIESTA calculators do not have
/// it, and (unlike VASP) neither has a native Wannier90 interface this app
/// drives instead.
void testFreshScfRefusesUnsupportedEngine()
{
    std::printf("A fresh SCF with an unsupported engine is refused before "
                "it runs:\n");
    calango::core::WannierConfig config;
    config.calculator.calculator = calango::core::CalculatorKind::QuantumEspresso;
    const std::string script = calango::core::generateWannierScript(config);

    checkContains(script, "cannot run a fresh SCF with Quantum ESPRESSO",
                  "names the engine that cannot work");
    checkContains(script, "get_wannier_localization_matrix",
                  "and the GPAW-only method that is actually missing");
    // The whole point: this has to fail in milliseconds, not after a real
    // (possibly expensive) SCF has already run.
    check(script.find("raise RuntimeError(\n"
                      "    'Wannier Functions cannot run a fresh SCF with "
                      "Quantum ESPRESSO")
              != std::string::npos,
          "raised BEFORE any SCF setup, not after atoms.get_potential_energy()");
    check(script.find("Espresso(") == std::string::npos,
          "and no Espresso calculator is even constructed");
}

/// VASP, unlike Quantum ESPRESSO/SIESTA, is NOT refused: it has its own
/// native Wannier90 interface (LWANNIER90/LWANNIER90_RUN, verified against
/// the VASP wiki this session), routed through a completely separate
/// generator (generateVaspWannier90Script()) that never touches
/// ase.dft.wannier at all.
void testVaspRoutesThroughItsOwnWannier90Library()
{
    std::printf("VASP is routed through its own native Wannier90 library:\n");
    calango::core::WannierConfig config;
    config.calculator.calculator = calango::core::CalculatorKind::Vasp;
    config.nWannier = 6;
    const std::string script = calango::core::generateWannierScript(config);

    check(script.find("get_wannier_localization_matrix") == std::string::npos,
          "never mentions the GPAW-only method — it is not on this path "
          "at all");
    check(script.find("from ase.dft.wannier import Wannier")
              == std::string::npos,
          "and never imports ase.dft.wannier either");
    checkContains(script, "from ase.calculators.vasp import Vasp",
                  "imports the VASP calculator instead");
    checkContains(script, "from ase.io.wannier90 import read_wout_all",
                  "and ASE's own Wannier90 .wout reader");

    // The win file: only num_wann/write_hr/projections, deliberately no
    // mp_grid or kpoints block (see the generator's own doc comment for why
    // — VASP fills those in from its own KPOINTS, which the interface does
    // NOT cross-check against a hand-written one).
    checkContains(script, "num_wann = 6", "num_wann matches the requested count");
    checkContains(script, "write_hr = .true.",
                  "asks wannier90 to write the H(R) file");
    checkContains(script, "begin projections\\nrandom\\nend projections",
                  "and supplies a projections block (random)");
    // A bare substring search for "mp_grid"/".gpw" would also match this
    // generator's OWN explanatory comments about their absence — checking
    // for the actual win-file WRITE call is what the test means.
    check(script.find("_fh.write('mp_grid") == std::string::npos,
          "does not hand-write mp_grid — left for VASP to fill in correctly");
    check(script.find("begin kpoints") == std::string::npos,
          "nor a kpoints block, for the same reason");

    // The two INCAR tags that make this work at all.
    checkContains(script, "lwannier90=True",
                  "switches the VASP<->Wannier90 interface on");
    checkContains(script, "lwannier90_run=True",
                  "and asks VASP to run the library to completion, not "
                  "just write .amn/.mmn/.eig for an external binary");
    checkContains(script, "isym=0",
                  "forces the full Brillouin zone on this node's OWN pass "
                  "(charge-density reuse does not need the baseline itself "
                  "to have used it)");

    // The result has to reach wannier.json in the SAME shape the GPAW path
    // writes, or the viewer and the three downstream H(R) consumers
    // (Boltzmann Transport, Berry Phase, cRPA) would need engine-specific
    // reader code.
    checkContains(script, "'nwannier': 6,", "records the requested count");
    checkContains(script, "'hr': hr_file,",
                  "and the same 'hr' key the GPAW path uses");
    checkContains(script, "'cell':", "and the cell, needed to turn R into a "
                                     "distance");
    checkContains(script, "CALANGO_RESULT wannier=wannier.json",
                  "emitting the same result marker regardless of engine");

    // A wout with no localized state is a failure, not an empty success —
    // matches the "no silent wrong answer" refusal style used elsewhere in
    // this generator.
    checkContains(script, "if centers.shape[0] == 0:",
                  "refuses rather than reporting zero Wannier functions as "
                  "a successful run");
}

/// The baseline-inheriting VASP path reuses CHGCAR (charge density only),
/// not a restart from saved wavefunctions — so, unlike the GPAW .gpw
/// restart, there is no "was the baseline itself run with symmetry off"
/// question: isym=0 on THIS node's own pass is the whole story.
void testVaspBaselineReusesChgcarNotGpw()
{
    std::printf("A VASP baseline reuses CHGCAR, not a restart file:\n");
    calango::core::WannierConfig config;
    config.calculator.calculator = calango::core::CalculatorKind::Vasp;
    config.baselineDir = "/tmp/some_vasp_baseline";
    const std::string script = calango::core::generateWannierScript(config);

    checkContains(script, "/tmp/some_vasp_baseline",
                  "names the baseline directory");
    checkContains(script, "os.path.join(_base, 'CHGCAR')",
                  "looks for its CHGCAR specifically");
    checkContains(script, "icharg=11",
                  "and reuses it non-self-consistently (ICHARG = 11)");
    // A bare ".gpw" search would also match this generator's own
    // explanatory comment contrasting the two paths — the actual claim is
    // that no .gpw file is ever globbed for or opened.
    check(script.find("glob.glob") == std::string::npos
              && script.find("GPAW(") == std::string::npos,
          "never globs for or opens a .gpw — this path does not restart "
          "from one");
    checkContains(script, "LCHARG = .TRUE.",
                  "and points at the flag that produces CHGCAR when it is "
                  "missing");
}

/// A disentanglement window (EnergyWindow/BandCount) is requested but not
/// mapped on this path yet — see the generator's own doc comment. Verifies
/// the fallback is a logged warning and a script that still runs with no
/// window, never an unverified numeric guess.
void testVaspWarnsWhenWindowRequestIsNotMapped()
{
    std::printf("An unmapped disentanglement window falls back with a "
                "warning:\n");
    calango::core::WannierConfig config;
    config.calculator.calculator = calango::core::CalculatorKind::Vasp;
    config.fixedMode =
        calango::core::WannierConfig::FixedStatesMode::EnergyWindow;
    config.energyWindowEv = 2.0;
    const std::string script = calango::core::generateWannierScript(config);

    checkContains(script, "not applied on the VASP/Wannier90 path yet",
                  "says plainly that the window was not applied");
    check(script.find("dis_froz") == std::string::npos,
          "and writes no dis_froz_* keyword it has not verified the "
          "reference level of");
}

/// The baseline-inheriting path: a directory whose calculator.json declares
/// a non-GPAW engine. Checked ahead of the bare ".gpw not found" glob miss,
/// which is technically also a refusal but does not say WHY — a VASP user
/// reading "no GPAW wavefunction found" could easily read it as "you forgot
/// calc.write()", which is not the actual problem.
void testBaselineRefusesNonGpawByCalculatorJson()
{
    std::printf("A non-GPAW baseline is refused by its own calculator.json:\n");
    calango::core::WannierConfig config;
    config.baselineDir = "/tmp/some_vasp_run";
    const std::string script = calango::core::generateWannierScript(config);

    checkContains(script, "calculator.json",
                  "reads the baseline's own provenance file");
    checkContains(script, "_engine.upper() != 'GPAW'",
                  "and checks the recorded engine name");
    checkContains(script, "cannot be driven from a ' + _engine",
                  "naming the actual engine in the refusal, not a generic one");
    checkContains(script, "get_wannier_localization_matrix",
                  "citing the missing GPAW-only method here too");
    // Positioned before the .gpw glob: the clearer, engine-aware refusal has
    // to win over the older, vaguer one for a baseline that has both.
    const std::size_t provenanceCheck = script.find("calculator.json");
    const std::size_t gpwGlob = script.find("glob.glob(os.path.join(_base, '*.gpw'))");
    check(provenanceCheck != std::string::npos && gpwGlob != std::string::npos
              && provenanceCheck < gpwGlob,
          "checked before the .gpw glob, not after");
    // A baseline with NO calculator.json at all (predates this check, or was
    // never staged by the Orchestration canvas) must still fall through to
    // the existing glob-miss refusal rather than raising a confusing "no
    // engine key" error of its own.
    checkContains(script, "except (OSError, ValueError):\n    pass",
                  "and silently falls through when there is no "
                  "calculator.json to read at all");
}

} // namespace

int main()
{
    std::printf("Wannier pre-flight and failure surfacing\n\n");
    testFailureExtraction();
    testMultiLineRuntimeError();
    testNoFalsePositives();
    testGeneratedScriptCarriesThePreflight();
    testFreshScfRefusesUnsupportedEngine();
    testBaselineRefusesNonGpawByCalculatorJson();
    testVaspRoutesThroughItsOwnWannier90Library();
    testVaspBaselineReusesChgcarNotGpw();
    testVaspWarnsWhenWindowRequestIsNotMapped();

    std::printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
