// Native SQS generator test.
//
// The property that defines an SQS is physical, not structural: the pair
// correlations of the generated supercell must approach those of the ideal
// random alloy. So the checks here measure the Warren-Cowley short-range order
// of the RESULT rather than asserting on the algorithm's internals — an
// annealer that runs happily while leaving the alloy clustered would pass any
// structural test and fail these.
//
// GUI-free, Python-free.

#include "core/SqsGenerator.hpp"

#include "core/Element.hpp"
#include "core/PeriodicImages.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace calango::core;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

/// FCC conventional cell of `symbol` with lattice constant `a`.
Structure fccCell(const char* symbol, double a)
{
    Structure s;
    s.setCell(UnitCell({a, 0, 0}, {0, a, 0}, {0, 0, a}));
    const int z = Elements::atomicNumber(symbol);
    for (const Vec3 frac : {Vec3{0, 0, 0}, Vec3{0.5, 0.5, 0.0},
                            Vec3{0.5, 0.0, 0.5}, Vec3{0.0, 0.5, 0.5}}) {
        Atom atom;
        atom.atomicNumber = z;
        atom.position = {frac.x * a, frac.y * a, frac.z * a};
        s.addAtom(atom);
    }
    return s;
}

/// Warren-Cowley parameter of the first shell for the ordered pair (A, B):
///     alpha = 1 - P(B | A) / c_B
/// Zero is the ideal random solution; positive means A-A clustering, negative
/// means ordering (A preferring B neighbors).
double warrenCowley(const Structure& s, const std::string& a,
                    const std::string& b, double cutoff)
{
    const int za = Elements::atomicNumber(a);
    const int zb = Elements::atomicNumber(b);
    const auto range = imageRange(s.cell(), cutoff);
    const auto& v = s.cell().vectors();

    long neighborsOfA = 0;
    long aToB = 0;
    long countB = 0;
    long countAll = 0;
    for (const Atom& atom : s.atoms()) {
        if (atom.atomicNumber == za || atom.atomicNumber == zb)
            ++countAll;
        if (atom.atomicNumber == zb)
            ++countB;
    }
    for (const Atom& ai : s.atoms()) {
        if (ai.atomicNumber != za)
            continue;
        for (const Atom& aj : s.atoms()) {
            if (aj.atomicNumber != za && aj.atomicNumber != zb)
                continue;
            for (int ia = -range[0]; ia <= range[0]; ++ia)
                for (int ib = -range[1]; ib <= range[1]; ++ib)
                    for (int ic = -range[2]; ic <= range[2]; ++ic) {
                        const Vec3 shift = v[0] * ia + v[1] * ib + v[2] * ic;
                        const double d = (aj.position + shift - ai.position).norm();
                        if (d < 1e-6 || d > cutoff)
                            continue;
                        ++neighborsOfA;
                        if (aj.atomicNumber == zb)
                            ++aToB;
                    }
        }
    }
    if (neighborsOfA == 0 || countAll == 0)
        return 0.0;
    const double cb = static_cast<double>(countB) / countAll;
    const double pba = static_cast<double>(aToB) / neighborsOfA;
    return 1.0 - pba / cb;
}

std::map<std::string, int> countSpecies(const Structure& s)
{
    std::map<std::string, int> counts;
    for (const Atom& atom : s.atoms())
        ++counts[atom.symbol()];
    return counts;
}

} // namespace

int main()
{
    const double a = 3.6; // Cu-like fcc lattice constant
    // First fcc shell is a/sqrt(2) ~ 2.55 A; 3.0 brackets it, 3.7 adds the
    // second shell at a = 3.6.
    const double shell1 = 3.0;
    const double shell2 = 3.7;

    // -- Binary Cu3Au ------------------------------------------------------
    std::printf("Binary Cu-Au (75/25) on a 3x3x3 fcc supercell:\n");
    {
        SqsGenerator::Params p;
        p.nx = p.ny = p.nz = 3;
        p.replaceElement = "Cu";
        p.composition = {{"Cu", 0.75}, {"Au", 0.25}};
        p.shell1 = shell1;
        p.shell2 = shell2;
        p.steps = 20000;
        p.seed = 7;
        const auto r = SqsGenerator::generate(fccCell("Cu", a), p);

        check(r.sublatticeSites == 4 * 27, "every fcc site is substitutional");
        const auto counts = countSpecies(r.structure);
        // 108 sites at 75/25 is exactly 81/27 — the composition must be hit
        // exactly, not approximately: a rounding slip would change the alloy.
        check(counts.at("Cu") == 81 && counts.at("Au") == 27,
              "composition is exact (Cu81 Au27)");
        check(r.shells == 2, "both requested shells are populated");
        check(r.objective < r.initialObjective,
              "annealing improved on the random starting decoration");

        // The physics check: first-shell short-range order must be near zero.
        // A random shuffle of this cell typically lands around |alpha| ~ 0.05;
        // an SQS should be several times better than that.
        const double alpha = warrenCowley(r.structure, "Au", "Cu", shell1);
        std::printf("       first-shell Warren-Cowley alpha(Au-Cu) = %+.4f "
                    "(objective %.4g -> %.4g)\n",
                    alpha, r.initialObjective, r.objective);
        check(std::abs(alpha) < 0.02,
              "first-shell short-range order is essentially random");
    }

    // -- Ternary -----------------------------------------------------------
    // The correlation machinery is written for arbitrary species counts, so a
    // ternary must work without being a special case anywhere.
    std::printf("Ternary Cu-Au-Ag (1/3 each) on a 3x3x3 fcc supercell:\n");
    {
        SqsGenerator::Params p;
        p.nx = p.ny = p.nz = 3;
        p.replaceElement = "Cu";
        p.composition = {{"Cu", 1.0}, {"Au", 1.0}, {"Ag", 1.0}};
        p.shell1 = shell1;
        p.shell2 = shell2;
        p.steps = 20000;
        p.seed = 11;
        const auto r = SqsGenerator::generate(fccCell("Cu", a), p);
        const auto counts = countSpecies(r.structure);
        check(counts.at("Cu") == 36 && counts.at("Au") == 36
                  && counts.at("Ag") == 36,
              "equimolar ternary splits 108 sites exactly 36/36/36");
        const double alpha = warrenCowley(r.structure, "Au", "Ag", shell1);
        std::printf("       first-shell alpha(Au-Ag) = %+.4f\n", alpha);
        check(std::abs(alpha) < 0.05, "ternary first shell is near-random");
    }

    // -- Quaternary --------------------------------------------------------
    std::printf("Quaternary Cu-Au-Ag-Pd (equimolar):\n");
    {
        SqsGenerator::Params p;
        p.nx = p.ny = p.nz = 3;
        p.replaceElement = "Cu";
        p.composition = {{"Cu", 0.25}, {"Au", 0.25}, {"Ag", 0.25}, {"Pd", 0.25}};
        p.shell1 = shell1;
        p.shell2 = shell2;
        p.steps = 20000;
        p.seed = 3;
        const auto r = SqsGenerator::generate(fccCell("Cu", a), p);
        const auto counts = countSpecies(r.structure);
        check(counts.size() == 4, "all four species are present");
        check(counts.at("Cu") == 27 && counts.at("Pd") == 27,
              "equimolar quaternary splits 108 sites 27 each");
        check(r.objective < r.initialObjective, "annealing improved the objective");
    }

    // -- Determinism and error handling ------------------------------------
    std::printf("Contracts:\n");
    {
        SqsGenerator::Params p;
        p.nx = p.ny = p.nz = 2;
        p.replaceElement = "Cu";
        p.composition = {{"Cu", 0.5}, {"Au", 0.5}};
        p.shell1 = shell1;
        p.shell2 = 0.0; // second shell disabled
        p.steps = 2000;
        p.seed = 99;
        const auto first = SqsGenerator::generate(fccCell("Cu", a), p);
        const auto second = SqsGenerator::generate(fccCell("Cu", a), p);
        bool identical = first.structure.size() == second.structure.size();
        for (std::size_t i = 0; identical && i < first.structure.size(); ++i)
            identical = first.structure.atoms()[i].atomicNumber
                == second.structure.atoms()[i].atomicNumber;
        check(identical, "same seed reproduces the same decoration");
        check(first.shells == 1, "a zero second cutoff disables that shell");
    }
    {
        bool threw = false;
        try {
            SqsGenerator::Params p;
            p.replaceElement = "Cu";
            p.composition = {{"Cu", 1.0}};
            SqsGenerator::generate(fccCell("Cu", a), p);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "a single-species composition is rejected");
    }
    {
        bool threw = false;
        try {
            SqsGenerator::Params p;
            p.replaceElement = "Fe"; // not in the structure
            p.composition = {{"Cu", 0.5}, {"Au", 0.5}};
            SqsGenerator::generate(fccCell("Cu", a), p);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "an absent sublattice element is rejected");
    }
    {
        bool threw = false;
        try {
            Structure molecule; // no cell
            Atom atom;
            atom.atomicNumber = Elements::atomicNumber("Cu");
            molecule.addAtom(atom);
            SqsGenerator::Params p;
            p.replaceElement = "Cu";
            p.composition = {{"Cu", 0.5}, {"Au", 0.5}};
            SqsGenerator::generate(molecule, p);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "a structure with no periodic cell is rejected");
    }

    std::printf(failures == 0 ? "\nAll SQS checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
