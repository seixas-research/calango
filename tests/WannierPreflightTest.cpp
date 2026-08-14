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

} // namespace

int main()
{
    std::printf("Wannier pre-flight and failure surfacing\n\n");
    testFailureExtraction();
    testMultiLineRuntimeError();
    testNoFalsePositives();
    testGeneratedScriptCarriesThePreflight();

    std::printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
