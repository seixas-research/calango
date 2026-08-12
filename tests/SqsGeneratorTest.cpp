// Native SQS generator test.
//
// The property that defines an SQS is physical, not structural: the cluster
// correlations of the generated supercell must approach those of the ideal
// random alloy. So the checks here measure the Warren-Cowley short-range order
// of the RESULT rather than asserting on the algorithm's internals — an
// annealer that runs happily while leaving the alloy clustered would pass any
// structural test and fail these.
//
// The multi-body section is pinned against CLOSED FORMS instead, on a periodic
// 1D chain where every cluster can be counted by hand: an eight-site ring has
// exactly 8 nearest-neighbour pairs, 8 consecutive triplets and 8 consecutive
// quadruplets, and the deviation of a perfectly alternating decoration from
// the random-alloy targets is an exact rational number at every order.
//
// GUI-free, Python-free.

#include "core/SqsGenerator.hpp"

#include "core/Element.hpp"
#include "core/PeriodicImages.hpp"

#include <chrono>
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

/// A periodic chain of `n` sites, `spacing` apart along x, in a box with
/// 20 Å of vacuum along y and z so nothing but the chain is ever a neighbour.
///
/// The whole point of the fixture: on a ring every cluster is a run of
/// consecutive sites, so "how many triplets are there" is a number a reader
/// can check without running anything.
Structure chain(const std::vector<const char*>& symbols, double spacing)
{
    Structure s;
    const auto n = static_cast<double>(symbols.size());
    s.setCell(UnitCell({spacing * n, 0, 0}, {0, 20.0, 0}, {0, 0, 20.0}));
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        Atom atom;
        atom.atomicNumber = Elements::atomicNumber(symbols[i]);
        atom.position = {spacing * static_cast<double>(i), 0.0, 0.0};
        s.addAtom(atom);
    }
    return s;
}

/// Π₃ in the ±1 spin convention over the consecutive triples of a chain:
/// the mean of σ_{i-1} σ_i σ_{i+1} with σ = +1 for `up` and −1 otherwise.
///
/// The random-alloy value is (c_up − c_down)³, which is exactly 0 for an
/// equiatomic binary — the closed form this convention is pinned against.
double chainSpinTriplet(const Structure& s, const char* up)
{
    const int zUp = Elements::atomicNumber(up);
    const auto n = static_cast<int>(s.size());
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        double product = 1.0;
        for (const int offset : {-1, 0, 1})
            product *= s.atoms()[static_cast<std::size_t>((i + offset + n) % n)]
                               .atomicNumber
                    == zUp
                ? 1.0
                : -1.0;
        sum += product;
    }
    return sum / n;
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

        // The α the generator REPORTS must be the α the structure has. Two
        // independent implementations of the same quantity (this file's local
        // counter and core::ChemicalOrder, reached through the result) have to
        // agree, or the number shown to the user is decorative.
        check(r.shortRangeOrder.shells.size() == 2,
              "the result reports α for both requested shells");
        // Row/column order follows sorted atomic number: Cu (29) then Au (79).
        const auto& alphaMatrix = r.shortRangeOrder.shells.front().alpha;
        const bool agrees = r.shortRangeOrder.species.size() == 2
            && r.shortRangeOrder.species[0] == Elements::atomicNumber("Cu")
            && std::abs(alphaMatrix[1][0] - alpha) < 1e-9;
        check(agrees,
              "and it is the same α measured independently here (Au→Cu)");
        // fcc has 12 nearest neighbours, and the SQS must not have lost any:
        // an α averaged over the wrong shell population is an α about a
        // different structure.
        check(std::abs(r.shortRangeOrder.shells.front().meanNeighbors - 12.0)
                  < 1e-9,
              "over the 12 nearest neighbours fcc actually has");
        check(r.deviation.triplet == 0.0 && r.deviation.quadruplet == 0.0
                  && r.triplets == 0 && r.quadruplets == 0,
              "and no multi-body term entered: they are off unless asked for");
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

    // -- Multi-body correlations against closed forms ----------------------
    //
    // An eight-site periodic chain at 2 Å spacing. Every cluster is a run of
    // consecutive sites, so the enumeration is countable by hand: 8 pairs
    // (each site to its two neighbours, halved), 8 triples (one per centre)
    // and 8 quadruplets (one per starting site). Anything else is out of
    // range — the next-nearest pair is 4 Å, and a triple spanning it is 8 Å
    // across, past the 4.5 Å triplet cutoff.
    std::printf("Multi-body closed forms on an 8-site chain:\n");
    {
        SqsGenerator::Params p;
        p.composition = {{"Cu", 0.5}, {"Au", 0.5}};
        p.shell1 = 2.5;   // nearest neighbours only
        p.shell2 = 0.0;
        p.shellWeights = {1.0};
        p.tripletCutoff = 4.5;
        p.quadrupletCutoff = 6.5;
        p.tripletWeight = 1.0;
        p.quadrupletWeight = 1.0;

        // Perfectly alternating: every bond is unlike, every triple is A-B-A
        // or B-A-B, every quadruplet is A-B-A-B.
        const Structure alternating =
            chain({"Cu", "Au", "Cu", "Au", "Cu", "Au", "Cu", "Au"}, 2.0);
        const SqsGenerator::Deviation ordered =
            SqsGenerator::evaluate(alternating, p);

        // PAIRS. Observed ordered-pair probabilities are (AA, AB, BA, BB) =
        // (0, ½, ½, 0) against a target of ¼ each, so ΔΠ_pair = 4 × ¼ = 1.
        check(std::abs(ordered.pair - 1.0) < 1e-12,
              "alternating chain: ΔΠ_pair = 1 exactly (4 × ¼)");

        // TRIPLETS. Four A-B-A and four B-A-B, so the observed sorted-tuple
        // probabilities are (AAA, AAB, ABB, BBB) = (0, ½, ½, 0). The random
        // targets are the multiplicities times c³ = (1, 3, 3, 1)/8, so every
        // one of the four bins is off by exactly ⅛ and ΔΠ_triplet = ½.
        check(std::abs(ordered.triplet - 0.5) < 1e-12,
              "alternating chain: ΔΠ_triplet = ½ exactly (4 × ⅛)");

        // QUADRUPLETS. Every one is A-B-A-B, i.e. the {A,A,B,B} bin holds all
        // of them. Targets are (1, 4, 6, 4, 1)/16, so the deviation is
        // 1/16 + 4/16 + (1 − 6/16) + 4/16 + 1/16 = 20/16 = 1.25.
        check(std::abs(ordered.quadruplet - 1.25) < 1e-12,
              "alternating chain: ΔΠ_quad = 1¼ exactly");
        check(std::abs(ordered.total
                       - (ordered.pair + ordered.triplet + ordered.quadruplet))
                  < 1e-12,
              "and the total is the sum of the three orders");

        // Why the histogram convention rather than the ±1 one: the ±1 triplet
        // correlation of this PERFECTLY ORDERED chain is exactly 0, which is
        // precisely the random-alloy value — the four A-B-A triples contribute
        // −1 and the four B-A-B contribute +1. An objective built on ±1
        // triplets alone would score this structure as ideally random while
        // the sorted-tuple histogram above is off by ½.
        check(std::abs(chainSpinTriplet(alternating, "Cu")) < 1e-12,
              "±1 triplet correlation of the same chain is exactly 0 — the "
              "random-alloy value, for a perfectly ordered structure");

        // A different ordered decoration whose PAIR statistics are exactly
        // random (2 A-A, 2 B-B, 4 A-B bonds out of 8) while its triplets are
        // not. This is the failure a pair-only objective cannot see, in its
        // smallest possible form.
        const Structure blocks =
            chain({"Cu", "Cu", "Au", "Au", "Cu", "Cu", "Au", "Au"}, 2.0);
        const SqsGenerator::Deviation mixed =
            SqsGenerator::evaluate(blocks, p);
        check(std::abs(mixed.pair) < 1e-12,
              "A-A-B-B blocks: ΔΠ_pair = 0 — pair-perfect");
        check(std::abs(mixed.triplet - 0.5) < 1e-12,
              "but ΔΠ_triplet = ½ — which is the whole reason for this feature");
    }

    // -- Multi-body annealing ----------------------------------------------
    // Turning the triplet term on has to actually buy triplet quality. Both
    // runs are scored with the SAME triplet-aware parameters, so the
    // comparison is of two decorations under one measure rather than of two
    // measures.
    std::printf("Triplet-aware annealing on fcc Cu-Au (50/50, 3x3x3):\n");
    {
        SqsGenerator::Params p;
        p.nx = p.ny = p.nz = 3;
        p.replaceElement = "Cu";
        p.composition = {{"Cu", 0.5}, {"Au", 0.5}};
        p.shell1 = shell1;
        p.shell2 = shell2;
        p.steps = 20000;
        p.seed = 5;

        SqsGenerator::Params scored = p;
        scored.tripletCutoff = shell1; // nearest-neighbour triangles
        scored.tripletWeight = 1.0;

        const auto pairOnly = SqsGenerator::generate(fccCell("Cu", a), p);
        const auto start = std::chrono::steady_clock::now();
        const auto withTriplets = SqsGenerator::generate(fccCell("Cu", a), scored);
        const auto elapsed = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();

        check(withTriplets.triplets > 0, "triangles were enumerated");
        // fcc: every site sits in 24 nearest-neighbour triangles, and each
        // triangle has three vertices — 108 × 24 / 3 = 864. A closed form, and
        // the one number that says the enumeration counted each cluster once.
        check(withTriplets.triplets == 864,
              "and there are exactly 864 of them (108 sites x 24 / 3)");

        const double before = SqsGenerator::evaluate(pairOnly.structure, scored)
                                  .triplet;
        const double after =
            SqsGenerator::evaluate(withTriplets.structure, scored).triplet;
        std::printf("       ΔΠ_triplet  pair-only %.5f -> triplet-aware %.5f\n"
                    "       %d steps with triplets in %.0f ms (%.1f us/step)\n",
                    before, after, withTriplets.steps, elapsed,
                    1000.0 * elapsed / withTriplets.steps);
        check(after < before,
              "optimizing triplets improves them over a pair-only SQS");
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
