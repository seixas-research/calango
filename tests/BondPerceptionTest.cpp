// Periodic bond-perception test.
//
// Exists because of a real, visible bug: a MoS2 monolayer rendered with two
// bonds instead of six. Its Mo sits in a trigonal prism of six sulfurs, three
// above and three below — but the cell lists only two S atoms, and the other
// four neighbours are LATTICE IMAGES of those same two. Bond detection used
// the minimum-image convention, which answers "where is the nearest copy of
// atom j": the right question for a distance and the wrong one for bonding,
// because it can only ever return one image per pair. Four of the six bonds
// were unreachable.
//
// The second half of the same bug: the pair scan started at j = i + 1, so an
// atom could never bond to its OWN images — which is every bond a
// one-atom-per-cell lattice has.
//
// GUI-free, Python-free.

#include "core/Structure.hpp"

#include "core/Element.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

using namespace calango::core;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

/// Bond counts keyed "A-B" with the symbols sorted, so the expectation does
/// not depend on which atom the detector happened to emit first.
std::map<std::string, int> bondCensus(const Structure& s, double tolerance)
{
    std::map<std::string, int> census;
    for (const Bond& bond : s.detectBonds(tolerance, true)) {
        std::string a = s.atoms()[static_cast<std::size_t>(bond.i)].symbol();
        std::string b = s.atoms()[static_cast<std::size_t>(bond.j)].symbol();
        if (b < a)
            std::swap(a, b);
        ++census[a + "-" + b];
    }
    return census;
}

Structure monolayerMoS2()
{
    // The experimental 1H-MoS2 cell: a = b = 3.16 A, gamma = 120, with 15 A of
    // vacuum along c. Mo at z = 1/2, the two S directly above and below it.
    const double a = 3.160039;
    const double gamma = 120.000008 * M_PI / 180.0;
    UnitCell cell(Vec3{a, 0.0, 0.0},
                  Vec3{a * std::cos(gamma), a * std::sin(gamma), 0.0},
                  Vec3{0.0, 0.0, 15.0});
    Structure s;
    s.setCell(cell);
    const struct { const char* symbol; double frac[3]; } sites[] = {
        {"Mo", {0.055555, 0.277778, 0.500000}},
        {"S", {0.722222, 0.611111, 0.606094}},
        {"S", {0.722222, 0.611111, 0.393906}},
    };
    for (const auto& site : sites) {
        Atom atom;
        atom.atomicNumber = Elements::atomicNumber(site.symbol);
        atom.position = cell.fractionalToCartesian(
            Vec3{site.frac[0], site.frac[1], site.frac[2]});
        s.addAtom(atom);
    }
    return s;
}

/// The case from the bug report. Mo-S is 2.421 A against a 2.59 A radius sum
/// (ratio 0.935), so a tolerance of 1.0 admits all six and excludes the 3.16 A
/// Mo-Mo lattice contact (ratio 1.026) that is not a bond.
void testMoS2Coordination()
{
    std::printf("MoS2 monolayer: Mo in a trigonal prism of six S\n");
    const Structure s = monolayerMoS2();
    const auto census = bondCensus(s, 1.0);

    check(census.count("Mo-S") && census.at("Mo-S") == 6,
          "six Mo-S bonds, not the two the same-cell pair scan found");
    check(!census.count("S-S"), "the S atoms are 3.18 A apart and do not bond");

    // Each S must carry three of them: the six are 3 + 3 across the two
    // sulfur sites, not six hung off one.
    std::map<int, int> perAtom;
    for (const Bond& bond : s.detectBonds(1.0, true)) {
        ++perAtom[bond.i];
        ++perAtom[bond.j];
    }
    check(perAtom[0] == 6, "the Mo carries all six");
    check(perAtom[1] == 3, "the upper S carries three");
    check(perAtom[2] == 3, "the lower S carries three");

    // Four of the six leave the cell; a detector that only reported in-cell
    // contacts would pass the count above by accident.
    int wrapped = 0;
    for (const Bond& bond : s.detectBonds(1.0, true))
        if (bond.crossesBoundary())
            ++wrapped;
    check(wrapped == 4, "four of the six reach into neighbouring cells");
}

/// Every bond must land at the real Mo-S distance. A wrong image offset would
/// keep the COUNT right while drawing sticks to the wrong place.
void testImageOffsetsAreGeometric()
{
    std::printf("image offsets place the bonds correctly\n");
    const Structure s = monolayerMoS2();
    bool allCorrect = true;
    for (const Bond& bond : s.detectBonds(1.0, true)) {
        const Vec3 d = s.atoms()[static_cast<std::size_t>(bond.j)].position
            + bond.imageOffset
            - s.atoms()[static_cast<std::size_t>(bond.i)].position;
        if (std::fabs(d.norm() - 2.421) > 0.01)
            allCorrect = false;
    }
    check(allCorrect, "every bond is 2.42 A long, images included");
}

/// A one-atom-per-cell lattice: every bond it has is to an image of the single
/// atom it contains. The old scan started at j = i + 1 and so found none at
/// all — a simple-cubic metal rendered as one unbonded sphere.
void testSelfImageBonds()
{
    std::printf("one-atom cell bonds to its own images\n");
    Structure s;
    const double a = 2.80; // Po-like simple cubic; Po covalent radius 1.40
    s.setCell(UnitCell(Vec3{a, 0, 0}, Vec3{0, a, 0}, Vec3{0, 0, a}));
    Atom atom;
    atom.atomicNumber = Elements::atomicNumber("Po");
    s.addAtom(atom);

    const auto bonds = s.detectBonds(1.15, true);
    // Three bonds, not six: a self-image bond is emitted once per AXIS, since
    // the renderer already draws a wrapped bond as a stub at each end, which
    // covers +x and -x together. Crediting each bond to both of its endpoints
    // then gives the correct coordination of six.
    check(bonds.size() == 3, "one bond per axis, not zero");
    bool allSelf = true;
    for (const Bond& bond : bonds)
        allSelf = allSelf && bond.i == 0 && bond.j == 0 && bond.crossesBoundary();
    check(allSelf, "all of them are self-images crossing the boundary");

    int coordination = 0;
    for (const Bond& bond : bonds) {
        coordination += 2; // both endpoints are this atom
        (void)bond;
    }
    check(coordination == 6, "simple cubic is six-coordinate");
}

/// Close-packed metal from a primitive cell: the textbook 12 nearest
/// neighbours, every one of them a self-image. Counting a bond once per
/// endpoint has to reproduce that exactly — the check that caught self-image
/// bonds being emitted for both +t and -t and so counted twice.
void testFccCoordination()
{
    std::printf("fcc primitive cell is twelve-coordinate\n");
    Structure s;
    const double a = 3.615; // Cu
    s.setCell(UnitCell(Vec3{0, a / 2, a / 2}, Vec3{a / 2, 0, a / 2},
                       Vec3{a / 2, a / 2, 0}));
    Atom atom;
    atom.atomicNumber = Elements::atomicNumber("Cu");
    s.addAtom(atom);

    const auto bonds = s.detectBonds(1.15, true);
    check(bonds.size() == 6, "six bonds spanning the twelve contacts");
    check(bonds.size() * 2 == 12, "coordination number is 12");
}

/// A molecule in a vacuum box must be unaffected: no cell, no images, and the
/// same bonds a non-periodic detector would find.
void testNonPeriodicUnchanged()
{
    std::printf("non-periodic structure\n");
    Structure s;
    const auto add = [&s](const char* symbol, double x, double y, double z) {
        Atom atom;
        atom.atomicNumber = Elements::atomicNumber(symbol);
        atom.position = {x, y, z};
        s.addAtom(atom);
    };
    add("O", 0.0, 0.0, 0.0);
    add("H", 0.96, 0.0, 0.0);
    add("H", -0.24, 0.93, 0.0);

    const auto bonds = s.detectBonds(1.15, true);
    check(bonds.size() == 2, "water has two O-H bonds");
    bool noneWrapped = true;
    for (const Bond& bond : bonds)
        noneWrapped = noneWrapped && !bond.crossesBoundary();
    check(noneWrapped, "no bond claims to cross a boundary");
}

/// Manual add/remove overrides still win over perception, per image too: a
/// removed pair must take ALL of its images with it, or the user would delete
/// a bond and watch five identical ones remain.
void testOverridesStillApply()
{
    std::printf("manual overrides\n");
    Structure s = monolayerMoS2();
    check(bondCensus(s, 1.0)["Mo-S"] == 6, "six to begin with");

    s.removeBondOverride(0, 1); // Mo - upper S
    const auto after = bondCensus(s, 1.0);
    check(after.count("Mo-S") && after.at("Mo-S") == 3,
          "removing the pair drops all three of its images");

    s.addBondOverride(0, 1);
    check(bondCensus(s, 1.0)["Mo-S"] == 4,
          "re-adding it draws the pair once, at the nearest image");
}

} // namespace

int main()
{
    testMoS2Coordination();
    testImageOffsetsAreGeometric();
    testSelfImageBonds();
    testFccCoordination();
    testNonPeriodicUnchanged();
    testOverridesStillApply();

    if (failures == 0) {
        std::printf("\nAll bond-perception checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d bond-perception check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
