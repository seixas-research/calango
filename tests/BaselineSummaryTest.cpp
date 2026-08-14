// What a Wannier run can learn about the ground state it inherits.
//
// WHY THIS MATTERS. ASE's Wannier builds overlaps between neighbouring
// k-points across the WHOLE Brillouin zone. A single point that folded its
// k-set into the irreducible wedge has no state to offer at most of them, so
// the localization cannot run at all — and until the wizard showed this, the
// only way to find out was to wait for the job and read the traceback.
//
// The check therefore has to be right in three ways, and each is asserted
// below: it must say "on" when the zone was folded, "off" when it was not, and
// "unknown" when the directory does not settle it. The third is the one worth
// having a test for. Reading a missing `symmetry_off` key as `false` would
// report "on" for a baseline that may well have been correct, and defaulting
// the other way would wave through the calculations this exists to catch.

#include "core/BaselineSummary.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using calango::core::BaselineSummary;
using calango::core::readBaselineSummary;
using calango::core::SymmetryState;

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

void checkEqual(int got, int want, const std::string& what)
{
    const bool ok = got == want;
    std::printf("  %-4s %s (got %d, want %d)\n", ok ? "ok" : "FAIL",
                what.c_str(), got, want);
    if (!ok)
        ++failures;
}

const char* name(SymmetryState s)
{
    switch (s) {
    case SymmetryState::Off:
        return "off";
    case SymmetryState::On:
        return "on";
    case SymmetryState::Unknown:
        return "unknown";
    }
    return "?";
}

void checkSymmetry(SymmetryState got, SymmetryState want,
                   const std::string& what)
{
    const bool ok = got == want;
    std::printf("  %-4s %s (got %s, want %s)\n", ok ? "ok" : "FAIL",
                what.c_str(), name(got), name(want));
    if (!ok)
        ++failures;
}

/// One throwaway run directory per case.
class RunDir {
public:
    explicit RunDir(const std::string& tag)
    {
        path_ = std::filesystem::temp_directory_path()
            / ("calango_baseline_" + tag);
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~RunDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    RunDir(const RunDir&) = delete;
    RunDir& operator=(const RunDir&) = delete;

    void write(const std::string& name, const std::string& text) const
    {
        std::ofstream out(path_ / name, std::ios::binary);
        out << text;
    }
    std::string str() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

/// The shape GPAW's text log actually has, trimmed to the blocks that are
/// read. Kept verbatim from a real gpaw.out rather than idealized, because the
/// parser's whole job is to survive that formatting — the counts sit behind
/// two spaces and a colon, and the mesh inside a bracketed list.
std::string gpawLog(int bz, int ibz, int bands, int mesh)
{
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer),
                  "Number of symmetries: %d\n"
                  "\n"
                  "BZ-sampling:\n"
                  "  Number of BZ points: %d\n"
                  "  Number of IBZ points: %d\n"
                  "\n"
                  "  Monkhorst-Pack size: [%d, %d, %d]\n"
                  "  Monkhorst-Pack shift: [0.03125, 0.03125, 0.03125]\n"
                  "\n"
                  "Plane wave coefficients: 1944\n"
                  "Cutoff: 600.0 eV\n"
                  "\n"
                  "Spin-components: 1 (collinear spins)\n"
                  "Bands:           %d\n"
                  "Projectors:      16\n"
                  "Number of atoms: 3\n",
                  bz == ibz ? 1 : 6, bz, ibz, mesh, mesh, mesh, bands);
    return buffer;
}

/// calculator.json as Calango writes it: compact, flat, keys sorted.
std::string calculatorJson(const std::string& symmetryClause)
{
    return R"({"conda_env":"/env","cutoff_ev":600,"engine":"GPAW",)"
           R"("engine_kind":5,"grid_spacing":0.2,"kpts":[16,16,16],"mode":"PW",)"
        + symmetryClause + R"("xc":"PBE"})";
}

void testFoldedZoneIsDetected()
{
    std::printf("A folded Brillouin zone is reported as symmetry ON:\n");
    const RunDir dir("folded");
    dir.write("gpaw.out", gpawLog(4096, 417, 19, 16));
    dir.write("calculator.json", calculatorJson(R"("symmetry_off":false,)"));

    const BaselineSummary s = readBaselineSummary(dir.str());
    checkSymmetry(s.symmetry, SymmetryState::On,
                  "417 of 4096 k-points kept is a folded zone");
    checkEqual(s.bands, 19, "the band count is read from the log");
    checkEqual(s.kpts[0], 16, "and so is the Monkhorst-Pack mesh");
    checkEqual(s.bzPoints, 4096, "with the full-zone count");
    checkEqual(s.ibzPoints, 417, "and the wedge count");
    check(!s.fullZoneConfirmed(), "the full zone is NOT confirmed");
    check(s.evidence.find("gpaw.out") != std::string::npos,
          "and the evidence names the file it came from");
}

void testFullZoneIsDetected()
{
    std::printf("An unfolded zone is reported as symmetry OFF:\n");
    const RunDir dir("full");
    // symmetry="off" leaves every BZ point in the k-set.
    dir.write("gpaw.out", gpawLog(4096, 4096, 19, 16));
    dir.write("calculator.json", calculatorJson(R"("symmetry_off":true,)"));

    const BaselineSummary s = readBaselineSummary(dir.str());
    checkSymmetry(s.symmetry, SymmetryState::Off,
                  "4096 of 4096 k-points kept is the full zone");
    check(s.fullZoneConfirmed(), "and the full zone is confirmed");
}

void testTheLogOutranksTheRequest()
{
    std::printf("What the run DID beats what it was asked for:\n");
    // The disagreement is the point. calculator.json records the request; a
    // job re-run by hand, edited before launch, or restarted from another
    // directory can have done something else. The k-set that was stored is the
    // thing the localization will meet, so it wins.
    const RunDir dir("disagree");
    dir.write("gpaw.out", gpawLog(4096, 417, 19, 16));
    dir.write("calculator.json", calculatorJson(R"("symmetry_off":true,)"));

    const BaselineSummary s = readBaselineSummary(dir.str());
    checkSymmetry(s.symmetry, SymmetryState::On,
                  "the log says folded, the sidecar says off — the log wins");
}

void testRequestIsUsedWithoutALog()
{
    std::printf("Without a log, the recorded request is used:\n");
    {
        const RunDir dir("req_off");
        dir.write("calculator.json", calculatorJson(R"("symmetry_off":true,)"));
        const BaselineSummary s = readBaselineSummary(dir.str());
        checkSymmetry(s.symmetry, SymmetryState::Off,
                      "symmetry_off = true is a determination");
        checkEqual(s.kpts[1], 16, "and the mesh still comes from the sidecar");
    }
    {
        const RunDir dir("req_on");
        dir.write("calculator.json", calculatorJson(R"("symmetry_off":false,)"));
        const BaselineSummary s = readBaselineSummary(dir.str());
        checkSymmetry(s.symmetry, SymmetryState::On,
                      "and so is symmetry_off = false");
    }
}

void testAbsentKeyIsUnknownNotFalse()
{
    std::printf("A missing flag is UNKNOWN, never a silent \"on\":\n");
    // The case this test exists for. A baseline written by an older Calango
    // has no symmetry_off key at all; reading that absence as `false` would
    // put a red "re-run this" warning on a calculation that may be perfectly
    // good, and would make the check untrustworthy exactly where the user
    // cannot check it themselves.
    const RunDir dir("legacy");
    dir.write("calculator.json", calculatorJson(""));

    const BaselineSummary s = readBaselineSummary(dir.str());
    checkSymmetry(s.symmetry, SymmetryState::Unknown,
                  "no symmetry_off key means undetermined");
    checkEqual(s.kpts[2], 16, "though the rest of the sidecar is still read");
    checkEqual(s.bands, 0, "and an unrecorded band count stays 0, not a guess");
}

void testScriptIsTheLastResort()
{
    std::printf("The generated script settles it when nothing else does:\n");
    const RunDir dir("script");
    dir.write("calculator.json", calculatorJson(""));
    dir.write("run.py",
              "from gpaw import GPAW, PW\n"
              "calc = GPAW(\n"
              "    mode=PW(600.0),\n"
              "    symmetry=\"off\",  # no point-group reduction\n"
              ")\n");

    const BaselineSummary s = readBaselineSummary(dir.str());
    checkSymmetry(s.symmetry, SymmetryState::Off,
                  "symmetry=\"off\" in run.py is a determination");
    check(s.evidence.find("run.py") != std::string::npos,
          "and the evidence says so");

    // A script WITHOUT the keyword is not evidence of the opposite: GPAW's
    // default is symmetry on, but the script may simply not be the one that
    // ran. Staying at Unknown is the honest answer.
    const RunDir plain("script_plain");
    plain.write("calculator.json", calculatorJson(""));
    plain.write("run.py", "from gpaw import GPAW, PW\ncalc = GPAW(mode=PW(600))\n");
    checkSymmetry(readBaselineSummary(plain.str()).symmetry,
                  SymmetryState::Unknown,
                  "but its absence is not evidence of symmetry being on");
}

void testNonGpawEngineIsUnknown()
{
    std::printf("A non-GPAW baseline is not judged by GPAW's flag:\n");
    // symmetry_off is gpawSymmetryOff — a GPAW keyword. Quantum ESPRESSO turns
    // symmetry off with nosym/noinv and SIESTA differently again, and Calango
    // drives neither of those from this field, so its value says nothing about
    // them. Claiming otherwise would be a fabricated verdict.
    const RunDir dir("qe");
    dir.write("calculator.json",
              R"({"engine":"Quantum ESPRESSO","engine_kind":3,)"
              R"("kpts":[8,8,8],"symmetry_off":false,"xc":"PBE"})");

    const BaselineSummary s = readBaselineSummary(dir.str());
    checkSymmetry(s.symmetry, SymmetryState::Unknown,
                  "a QE baseline's symmetry is not read off a GPAW keyword");
    checkEqual(s.kpts[0], 8, "though its k-mesh is still reported");
    check(s.engine == "Quantum ESPRESSO", "and the engine is named");
}

void testMissingDirectoryIsSafe()
{
    std::printf("A directory that is not there does not throw:\n");
    const BaselineSummary s =
        readBaselineSummary("/no/such/calango/run/directory");
    checkSymmetry(s.symmetry, SymmetryState::Unknown, "it reports unknown");
    checkEqual(s.bands, 0, "with no bands");
    check(!s.evidence.empty(), "and says why rather than going quiet");
    checkSymmetry(readBaselineSummary("").symmetry, SymmetryState::Unknown,
                  "and so does an empty path");
}

void testEmptyDirectoryIsUnknown()
{
    std::printf("An empty run directory is unknown, not off:\n");
    const RunDir dir("empty");
    const BaselineSummary s = readBaselineSummary(dir.str());
    checkSymmetry(s.symmetry, SymmetryState::Unknown,
                  "nothing to read means nothing is claimed");
    check(!s.fullZoneConfirmed(),
          "and the full zone is certainly not confirmed");
}

void testBandsComeFromTheSidecarWhenRecorded()
{
    std::printf("An explicitly recorded band count is used:\n");
    const RunDir dir("nbands");
    dir.write("calculator.json",
              R"({"engine":"GPAW","engine_kind":5,"kpts":[4,4,4],)"
              R"("nbands":24,"symmetry_off":true,"xc":"PBE"})");
    const BaselineSummary s = readBaselineSummary(dir.str());
    checkEqual(s.bands, 24, "nbands is read from calculator.json");
    checkSymmetry(s.symmetry, SymmetryState::Off, "alongside the flag");
}

} // namespace

int main()
{
    std::printf("Baseline summary: bands, k-points and the symmetry check\n\n");
    testFoldedZoneIsDetected();
    testFullZoneIsDetected();
    testTheLogOutranksTheRequest();
    testRequestIsUsedWithoutALog();
    testAbsentKeyIsUnknownNotFalse();
    testScriptIsTheLastResort();
    testNonGpawEngineIsUnknown();
    testMissingDirectoryIsSafe();
    testEmptyDirectoryIsUnknown();
    testBandsComeFromTheSidecarWhenRecorded();

    std::printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
