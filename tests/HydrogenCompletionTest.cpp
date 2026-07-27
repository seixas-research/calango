// Hydrogen-completion test.
//
// Two things have to hold for "Complete with hydrogens" to be trustworthy:
// the COUNT must follow the standard valence (a bare carbon gets four, an
// aromatic carbon gets one, a saturated atom gets none, a metal gets nothing
// at all), and the GEOMETRY must fall out of the coordination — methane
// tetrahedral, water bent, a methyl carbon pointing its new hydrogens away
// from the bond it already has. The placement is a relaxation rather than a
// lookup table, so the angles are checked numerically instead of assumed.
//
// GUI-free, Python-free.

#include "core/HydrogenCompletion.hpp"

#include "core/Element.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

void checkNear(double value, double expected, double tolerance,
               const std::string& what)
{
    const bool ok = std::fabs(value - expected) <= tolerance;
    std::printf("  %s %s (got %.3f, expected %.3f +- %.3f)\n",
                ok ? "ok  " : "FAIL", what.c_str(), value, expected, tolerance);
    if (!ok)
        ++failures;
}

void add(Structure& s, const char* symbol, double x, double y, double z)
{
    Atom atom;
    atom.atomicNumber = Elements::atomicNumber(symbol);
    atom.position = {x, y, z};
    s.addAtom(atom);
}

/// Angles in degrees between every pair of bonds from `center` to the atoms
/// listed in `neighbours`.
std::vector<double> bondAngles(const Structure& s, std::size_t center,
                               const std::vector<std::size_t>& neighbours)
{
    std::vector<double> angles;
    const Vec3 c = s.atoms()[center].position;
    for (std::size_t a = 0; a < neighbours.size(); ++a) {
        for (std::size_t b = a + 1; b < neighbours.size(); ++b) {
            const Vec3 u = (s.atoms()[neighbours[a]].position - c).normalized();
            const Vec3 v = (s.atoms()[neighbours[b]].position - c).normalized();
            angles.push_back(std::acos(std::clamp(u.dot(v), -1.0, 1.0))
                             * 180.0 / M_PI);
        }
    }
    return angles;
}

/// Indices of every hydrogen added after the original `from` atoms.
std::vector<std::size_t> addedIndices(const Structure& s, std::size_t from)
{
    std::vector<std::size_t> out;
    for (std::size_t i = from; i < s.size(); ++i)
        out.push_back(i);
    return out;
}

// -- Counts ---------------------------------------------------------------

/// A lone carbon has no bonds at all, so all four hydrogens are new and the
/// relaxation has nothing fixed to work around: pure tetrahedral is the only
/// minimum, and every H-C-H angle must come out at 109.47 degrees.
void testMethaneFromBareCarbon()
{
    std::printf("bare carbon -> methane\n");
    Structure s;
    add(s, "C", 0.0, 0.0, 0.0);

    const auto result = completeWithHydrogens(s);
    check(result.added == 4, "four hydrogens added");
    check(result.completedAtoms == 1, "one atom completed");
    check(s.size() == 5, "structure has 5 atoms");

    const auto hydrogens = addedIndices(s, 1);
    for (const double angle : bondAngles(s, 0, hydrogens))
        checkNear(angle, 109.47, 2.0, "H-C-H angle is tetrahedral");

    // The bond length comes from the covalent radii: 0.76 (C) + 0.31 (H).
    for (const std::size_t h : hydrogens) {
        checkNear((s.atoms()[h].position - s.atoms()[0].position).norm(), 1.07,
                  0.02, "C-H bond length");
    }
}

/// Oxygen's valence is 2, and one bond is already there, so exactly one
/// hydrogen is added — and it must go on the far side of the oxygen from the
/// existing one, not on top of it.
void testWaterFromHydroxyl()
{
    std::printf("O-H -> water\n");
    Structure s;
    add(s, "O", 0.0, 0.0, 0.0);
    add(s, "H", 0.97, 0.0, 0.0);

    const auto result = completeWithHydrogens(s);
    check(result.added == 1, "one hydrogen added");
    check(s.size() == 3, "structure has 3 atoms");

    // With a single fixed neighbour the repulsion minimum is the antipode:
    // the relaxation cannot know the 104.5 degrees of real water, only that
    // the two bonds should be as far apart as possible. What matters is that
    // the new hydrogen is not stacked on the existing one.
    const auto angles = bondAngles(s, 0, {1, 2});
    check(!angles.empty() && angles[0] > 150.0,
          "the new hydrogen points away from the existing one");
}

/// Benzene: each carbon carries two AROMATIC bonds, which count 1.5 each for
/// a total of 3, leaving exactly one hydrogen per carbon. A rule that counted
/// aromatic bonds as single would add two per carbon and double the formula.
void testBenzeneAromatic()
{
    std::printf("benzene ring (aromatic bond orders)\n");
    Structure s;
    const double r = 1.39; // C-C in benzene
    for (int k = 0; k < 6; ++k) {
        const double t = 2.0 * M_PI * k / 6.0;
        add(s, "C", r * std::cos(t), r * std::sin(t), 0.0);
    }
    for (int k = 0; k < 6; ++k)
        s.setBondOrder(k, (k + 1) % 6, 4); // 4 = aromatic

    const auto result = completeWithHydrogens(s);
    check(result.added == 6, "one hydrogen per aromatic carbon");
    check(s.size() == 12, "C6H6");

    // Every hydrogen sits radially outward, in the ring plane.
    for (const std::size_t h : addedIndices(s, 6)) {
        checkNear(s.atoms()[h].position.z, 0.0, 0.05,
                  "aromatic hydrogen stays in the ring plane");
        check(s.atoms()[h].position.norm() > r,
              "aromatic hydrogen points out of the ring");
    }
}

/// A structure that is already saturated must be left exactly as it is —
/// running the command twice cannot keep growing the molecule.
void testIdempotent()
{
    std::printf("already-saturated structure\n");
    Structure s;
    add(s, "C", 0.0, 0.0, 0.0);
    completeWithHydrogens(s);
    const std::size_t after = s.size();

    const auto again = completeWithHydrogens(s);
    check(again.added == 0, "second pass adds nothing");
    check(s.size() == after, "atom count unchanged");
}

/// Metals have no tabulated organic valence, so they are reported as skipped
/// rather than decorated with hydrides nobody asked for.
void testMetalsUntouched()
{
    std::printf("metal slab\n");
    Structure s;
    add(s, "Pt", 0.0, 0.0, 0.0);
    add(s, "Pt", 2.77, 0.0, 0.0);
    add(s, "Fe", 0.0, 2.77, 0.0);

    const auto result = completeWithHydrogens(s);
    check(result.added == 0, "no hydrogens on a metal");
    check(result.skippedAtoms == 3, "all three reported as skipped");
    check(s.size() == 3, "structure untouched");

    check(standardValence(78) == 0, "Pt has no standard valence");
    check(standardValence(6) == 4, "C has valence 4");
    check(standardValence(8) == 2, "O has valence 2");
}

/// A methyl-like carbon: one existing heavy bond, three hydrogens to add.
/// The new bonds must relax AWAY from the fixed one, which is the whole point
/// of holding the existing directions during the relaxation.
void testExistingBondIsRespected()
{
    std::printf("C-C fragment -> ethane\n");
    Structure s;
    add(s, "C", 0.0, 0.0, 0.0);
    add(s, "C", 1.54, 0.0, 0.0);

    const auto result = completeWithHydrogens(s);
    check(result.added == 6, "three hydrogens per carbon");
    check(s.size() == 8, "C2H6");

    // Every hydrogen on carbon 0 must sit on the -x side, i.e. more than 90
    // degrees from the C-C bond that was already there.
    const Vec3 cc = (s.atoms()[1].position - s.atoms()[0].position).normalized();
    int away = 0;
    for (const std::size_t h : addedIndices(s, 2)) {
        const Vec3 v = (s.atoms()[h].position - s.atoms()[0].position);
        if (v.norm() < 1.5 && v.normalized().dot(cc) < 0.0)
            ++away;
    }
    check(away == 3, "carbon 0's three hydrogens all point away from the C-C bond");
}

/// Bond orders are never auto-perceived, so a file-loaded structure carries
/// none — and counting every bond as single would put a hydrogen on every
/// carbonyl carbon. The C=O here is 1.21 Å against a 1.42 Å single-bond radius
/// sum, which the length rule has to read as a double bond.
void testCarbonylFromGeometry()
{
    std::printf("formaldehyde skeleton (C=O inferred from length)\n");
    Structure s;
    add(s, "C", 0.0, 0.0, 0.0);
    add(s, "O", 1.21, 0.0, 0.0);

    const auto result = completeWithHydrogens(s);
    check(result.added == 2,
          "two hydrogens on the carbon, none on the doubly-bonded oxygen");
    check(s.size() == 4, "CH2O");

    // Both hydrogens on the carbon, none hanging off the oxygen.
    int onCarbon = 0;
    for (const std::size_t h : addedIndices(s, 2)) {
        if ((s.atoms()[h].position - s.atoms()[0].position).norm() < 1.2)
            ++onCarbon;
    }
    check(onCarbon == 2, "both hydrogens sit on the carbon");

    // sp2: the three bonds around the carbon open to ~120 degrees.
    for (const double angle : bondAngles(s, 0, {1, 2, 3}))
        checkNear(angle, 120.0, 5.0, "trigonal angle at the carbonyl carbon");
}

/// Ethylene: a C=C at 1.34 Å is 0.88 of the single-bond radius sum, so each
/// carbon must come out with two hydrogens, not three.
void testEthyleneFromGeometry()
{
    std::printf("ethylene skeleton (C=C inferred from length)\n");
    Structure s;
    add(s, "C", 0.0, 0.0, 0.0);
    add(s, "C", 1.34, 0.0, 0.0);

    const auto result = completeWithHydrogens(s);
    check(result.added == 4, "two hydrogens per sp2 carbon");
    check(s.size() == 6, "C2H4");
}

/// The peptide unit is the case that motivates the whole length rule. With
/// every bond counted as single, the carbonyl carbon gains a hydrogen it does
/// not have and the backbone nitrogen is left with the wrong count. The amide
/// C–N (1.33 Å) is partial-double, the C=O (1.23 Å) is double.
void testPeptideBackbone()
{
    std::printf("peptide unit -CA-C(=O)-N(-CA)-\n");
    Structure s;
    // Standard planar peptide geometry: C=O 1.23 Å at 121°, amide C–N 1.33 Å
    // at 116°, N–Cα 1.46 Å at 122°.
    add(s, "C", 0.000, 0.000, 0.0);   // 0: Cα
    add(s, "C", 1.520, 0.000, 0.0);   // 1: carbonyl C
    add(s, "O", 2.153, 1.054, 0.0);   // 2: carbonyl O
    add(s, "N", 2.103, -1.196, 0.0);  // 3: amide N
    add(s, "C", 3.554, -1.349, 0.0);  // 4: next Cα

    const std::size_t before = s.size();
    completeWithHydrogens(s);

    // Count what landed on each heavy atom.
    std::vector<int> attached(before, 0);
    for (const std::size_t h : addedIndices(s, before)) {
        for (std::size_t a = 0; a < before; ++a) {
            if ((s.atoms()[h].position - s.atoms()[a].position).norm() < 1.3)
                ++attached[a];
        }
    }
    check(attached[1] == 0, "carbonyl carbon gets no hydrogen");
    check(attached[2] == 0, "carbonyl oxygen gets no hydrogen");
    check(attached[3] == 1, "backbone nitrogen gets exactly one hydrogen");
    check(attached[0] == 3, "terminal Ca gets three (it has one heavy bond)");
    check(attached[4] == 3, "the other terminal Ca gets three");
}

/// An explicitly assigned order is a statement of chemistry and must beat the
/// geometry: a pair marked double stays double even at a single-bond distance.
void testExplicitOrderWins()
{
    std::printf("explicit double bond at a single-bond distance\n");
    Structure s;
    add(s, "C", 0.0, 0.0, 0.0);
    add(s, "C", 1.54, 0.0, 0.0); // looks single by length
    s.setBondOrder(0, 1, 2);     // ...but is declared double

    const auto result = completeWithHydrogens(s);
    check(result.added == 4, "the declared double bond wins over the length");
}

} // namespace

int main()
{
    testMethaneFromBareCarbon();
    testWaterFromHydroxyl();
    testBenzeneAromatic();
    testIdempotent();
    testMetalsUntouched();
    testExistingBondIsRespected();
    testCarbonylFromGeometry();
    testEthyleneFromGeometry();
    testPeptideBackbone();
    testExplicitOrderWins();

    if (failures == 0) {
        std::printf("\nAll hydrogen-completion checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d hydrogen-completion check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
