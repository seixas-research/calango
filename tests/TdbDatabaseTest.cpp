// Thermodynamic-database (.tdb) parsing.
//
// The parser's job is to answer two questions the CALPHAD UI cannot open
// without: which elements does this database contain, and which phases. It is
// deliberately independent of pycalphad — the solver is not installed in any
// Calango environment by default, and a module that could not even list its
// own checkboxes without one would be unusable on most machines.
//
// Tested against a hand-written database exercising the syntax that actually
// varies in the wild (multi-line statements, '$' comments, sublattice models,
// interaction parameters), and against the real SGTE files shipped with ATAT
// when they are present.

#include "core/TdbDatabase.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

bool contains(const std::vector<std::string>& list, const std::string& value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

/// A small Fe-Cr database in standard syntax: comments, a statement broken
/// across lines, a two-sublattice solution phase, a vacancy, and an
/// interaction parameter.
const char* kFeCr = R"TDB(
$ A minimal Fe-Cr database for testing.
$ Comments run to end of line and must not eat the '!' of a later statement.

 ELEMENT /-   ELECTRON_GAS       0.0000E+00  0.0000E+00  0.0000E+00 !
 ELEMENT VA   VACUUM             0.0000E+00  0.0000E+00  0.0000E+00 !
 ELEMENT FE   BCC_A2             5.5847E+01  4.4890E+03  2.7280E+01 !
 ELEMENT CR   BCC_A2             5.1996E+01  4.0500E+03  2.3543E+01 !

 FUNCTION GHSERFE 298.15
   +1225.7+124.134*T-23.5143*T*LN(T)-0.00439752*T**2; 1811.00 Y
   -25383.581+299.31255*T-46*T*LN(T); 6000.00 N !

 PHASE LIQUID % 1 1.0 !
 CONSTITUENT LIQUID : FE,CR : !

 PHASE BCC_A2 %  2 1  3 !
 CONSTITUENT BCC_A2 : CR,FE : VA : !

 PHASE SIGMA % 3 8 4 18 !
 CONSTITUENT SIGMA : FE : CR : CR,FE : !

 PARAMETER G(BCC_A2,FE:VA;0) 298.15 +GHSERFE; 6000 N !
 PARAMETER L(BCC_A2,CR,FE:VA;0) 298.15 +20500-9.68*T; 6000 N !
 PARAMETER L(BCC_A2,CR,FE:VA;1) 298.15 +1000; 6000 N !
 PARAMETER TC(BCC_A2,FE:VA;0) 298.15 1043; 6000 N !
)TDB";

std::string readFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file)
        return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace

int main()
{
    using calango::core::TdbDatabase;

    std::printf("Hand-written Fe-Cr database:\n");
    TdbDatabase db;
    std::string error;
    check(db.parse(kFeCr, &error), "parses");
    if (!error.empty())
        std::printf("    error: %s\n", error.c_str());

    // -- Elements ----------------------------------------------------------
    const auto elements = db.selectableElements();
    check(elements.size() == 2, "offers exactly the two chemical elements");
    check(contains(elements, "FE") && contains(elements, "CR"),
          "which are Fe and Cr");
    // The two entries every database carries that are not elements. Offering
    // them as checkboxes would invite a user to build a system out of
    // vacancies.
    check(!contains(elements, "VA"),
          "the vacancy is excluded from the selectable set");
    check(!contains(elements, "/-") && !contains(elements, "-"),
          "and so is the electron");
    check(db.elements.size() == 4,
          "though all four ELEMENT records are retained");

    // Reference data survives, since the SER reference phase is what an
    // ab-initio parameter has to be quoted against.
    const auto fe = std::find_if(
        db.elements.begin(), db.elements.end(),
        [](const calango::core::TdbElement& e) { return e.name == "FE"; });
    check(fe != db.elements.end() && fe->referencePhase == "BCC_A2",
          "iron's SER reference phase is read");
    check(fe != db.elements.end() && fe->massKgPerMol > 55.0
              && fe->massKgPerMol < 56.0,
          "and its mass");

    // -- Phases and sublattice models --------------------------------------
    const auto phaseList = db.phaseNames();
    check(phaseList.size() == 3, "three phases");
    check(contains(phaseList, "LIQUID") && contains(phaseList, "BCC_A2")
              && contains(phaseList, "SIGMA"),
          "named LIQUID, BCC_A2 and SIGMA");

    const auto bcc = std::find_if(
        db.phases.begin(), db.phases.end(),
        [](const calango::core::TdbPhase& p) { return p.name == "BCC_A2"; });
    check(bcc != db.phases.end() && bcc->sublatticeCount() == 2,
          "BCC_A2 has two sublattices");
    check(bcc != db.phases.end() && bcc->siteRatios.size() == 2
              && bcc->siteRatios[0] == 1.0 && bcc->siteRatios[1] == 3.0,
          "with site ratios 1:3 — the interstitial model");
    check(bcc != db.phases.end() && bcc->constituents.size() == 2
              && contains(bcc->constituents[0], "CR")
              && contains(bcc->constituents[0], "FE")
              && contains(bcc->constituents[1], "VA"),
          "and (Cr,Fe)(Va) constituents");

    const auto sigma = std::find_if(
        db.phases.begin(), db.phases.end(),
        [](const calango::core::TdbPhase& p) { return p.name == "SIGMA"; });
    check(sigma != db.phases.end() && sigma->sublatticeCount() == 3
              && sigma->siteRatios[2] == 18.0,
          "the three-sublattice sigma phase keeps its 8:4:18 ratios");

    // -- Parameters --------------------------------------------------------
    check(db.parameters.size() == 4, "four parameters");
    const auto l1 = std::find_if(
        db.parameters.begin(), db.parameters.end(),
        [](const calango::core::TdbParameter& p) {
            return p.symbol == "L" && p.order == 1;
        });
    check(l1 != db.parameters.end(),
          "the first-order Redlich-Kister term is distinguished from the "
          "zeroth by its ;N order, not by its position");
    check(l1 != db.parameters.end() && l1->phase == "BCC_A2",
          "and belongs to BCC_A2");
    check(l1 != db.parameters.end() && contains(l1->constituents, "CR")
              && contains(l1->constituents, "FE"),
          "with Cr and Fe as the interacting pair");
    const auto tc = std::find_if(
        db.parameters.begin(), db.parameters.end(),
        [](const calango::core::TdbParameter& p) { return p.symbol == "TC"; });
    check(tc != db.parameters.end(),
          "the magnetic Curie-temperature parameter is kept apart from G");

    // A FUNCTION broken across three lines is one statement, not three.
    check(db.functionNames.size() == 1
              && db.functionNames[0] == "GHSERFE",
          "a multi-line FUNCTION is a single declaration");

    // -- Phase filtering by selected elements ------------------------------
    std::printf("Phase availability by element selection:\n");
    const auto feOnly = db.phasesForElements({"FE"});
    check(contains(feOnly, "LIQUID") && contains(feOnly, "BCC_A2"),
          "pure Fe keeps LIQUID and BCC_A2 — a sublattice needs ANY of its "
          "constituents, not all of them");
    check(!contains(feOnly, "SIGMA"),
          "but drops SIGMA, whose second sublattice is Cr only");
    const auto both = db.phasesForElements({"FE", "CR"});
    check(both.size() == 3, "Fe-Cr admits all three phases");
    const auto crOnly = db.phasesForElements({"CR"});
    check(!contains(crOnly, "SIGMA"),
          "and pure Cr drops SIGMA too, whose first sublattice is Fe only");

    // -- Rejection ---------------------------------------------------------
    std::printf("Rejection:\n");
    TdbDatabase notADatabase;
    std::string why;
    check(!notADatabase.parse("This is a POSCAR, not a database.\n", &why),
          "a file with no database statements is refused");
    check(!why.empty(), "with a reason that says what a .tdb contains");

    // -- The real SGTE files, when present ---------------------------------
    std::printf("Real SGTE databases (ATAT distribution):\n");
    const std::string sgtePath =
        std::string(std::getenv("HOME") ? std::getenv("HOME") : "")
        + "/Codes/atat/data/sgte_elements.tdb";
    const std::string sgte = readFile(sgtePath);
    if (sgte.empty()) {
        std::printf("    (not present here — skipped)\n");
    } else {
        TdbDatabase real;
        std::string realError;
        check(real.parse(sgte, &realError), "the SGTE element list parses");
        const auto realElements = real.selectableElements();
        check(realElements.size() > 50,
              "and yields the full unary element set ("
                  + std::to_string(realElements.size()) + " elements)");
        // The ATAT dialect prefixes every element with ELEM_. A UI showing
        // "ELEM_FE" as a checkbox would be a parser detail leaking into the
        // user's vocabulary.
        check(contains(realElements, "FE") && contains(realElements, "CR")
                  && contains(realElements, "NI"),
              "with the ELEM_ prefix stripped, so Fe/Cr/Ni read as themselves");
        check(!contains(realElements, "ELEM_FE"),
              "and never in their prefixed form");
    }

    const std::string freePath =
        std::string(std::getenv("HOME") ? std::getenv("HOME") : "")
        + "/Codes/atat/data/sgte_freee.tdb";
    const std::string freee = readFile(freePath);
    if (freee.empty()) {
        std::printf("    (free-energy file not present — skipped)\n");
    } else {
        TdbDatabase real;
        std::string realError;
        check(real.parse(freee, &realError),
              "the SGTE free-energy file parses");
        check(real.functionNames.size() > 100,
              "with its "
                  + std::to_string(real.functionNames.size())
                  + " FUNCTION declarations");
        check(!real.parameters.empty(),
              "and its PARAMETER declarations");
        // This file is the ATAT dialect: <NL> stands in for a newline inside a
        // statement. Left as a token it welds onto the neighbouring number and
        // the whole file becomes one unparseable statement.
        const bool noted = std::any_of(
            real.warnings.begin(), real.warnings.end(),
            [](const std::string& w) { return w.find("<NL>") != std::string::npos; });
        check(noted, "and the <NL> dialect is handled and reported");
        // Multi-range parameters are the norm for SGTE unaries.
        const bool multiRange = std::any_of(
            real.parameters.begin(), real.parameters.end(),
            [](const calango::core::TdbParameter& p) {
                return p.temperatureRanges > 1;
            });
        check(multiRange,
              "piecewise temperature ranges are counted, not flattened");
    }

    if (failures == 0) {
        std::printf("\nAll .tdb parsing checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d .tdb parsing check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
