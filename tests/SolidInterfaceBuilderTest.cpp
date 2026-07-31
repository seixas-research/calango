// Solid-interface builder test.
//
// Every construction here joins two pieces of crystal, and every way it can go
// wrong looks fine in a viewer:
//
//   - a stacking fault applied to the wrong half (or to none)
//   - a twin whose mirror lost half the atoms, or duplicated the plane
//   - a polycrystal whose grains stop at the cell face instead of wrapping
//     through it, leaving a slab of one orientation glued to every boundary
//   - a seam where two grains' atoms sit on top of each other at 0.2 Å
//
// So the checks below are structural, re-derived from the returned
// coordinates: atom counts against what the geometry demands, periodicity by
// wrapping atoms through the cell and looking, and closest approach under the
// minimum-image convention.
//
// GUI-free, Python-free.

#include "core/SolidInterfaceBuilder.hpp"

#include "core/Element.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
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

void checkClose(double actual, double expected, double tolerance,
                const std::string& what)
{
    const bool ok = std::abs(actual - expected) <= tolerance;
    std::printf("  %s %s  (got %.4f, expected %.4f)\n", ok ? "ok  " : "FAIL",
                what.c_str(), actual, expected);
    if (!ok)
        ++failures;
}

/// Minimum-image separation, scanning a full 3x3x3 image block so the test
/// does not inherit whatever shortcut the builder took.
double minimumImage(const Structure& s, const Vec3& a, const Vec3& b)
{
    const auto& cell = s.cell().vectors();
    double best = std::numeric_limits<double>::max();
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j)
            for (int k = -1; k <= 1; ++k)
                best = std::min(
                    best,
                    (a - b - (cell[0] * i + cell[1] * j + cell[2] * k)).norm());
    return best;
}

double closestPair(const Structure& s)
{
    double best = std::numeric_limits<double>::max();
    const auto& atoms = s.atoms();
    for (std::size_t i = 0; i + 1 < atoms.size(); ++i)
        for (std::size_t j = i + 1; j < atoms.size(); ++j)
            best = std::min(best, minimumImage(s, atoms[i].position,
                                               atoms[j].position));
    return best == std::numeric_limits<double>::max() ? 0.0 : best;
}

/// Simple cubic, n x n x n cells of side `a`, one species.
Structure cubic(int n, double a, int z)
{
    Structure s;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            for (int k = 0; k < n; ++k) {
                Atom atom;
                atom.atomicNumber = z;
                atom.position = {i * a, j * a, k * a};
                s.addAtom(atom);
            }
    s.setCell(UnitCell({n * a, 0, 0}, {0, n * a, 0}, {0, 0, n * a},
                       {true, true, true}));
    return s;
}

void testStackingFault()
{
    std::printf("stacking fault\n");
    const Structure parent = cubic(4, 3.0, 13);

    SolidInterfaceBuilder::Params params;
    params.kind = SolidInterfaceBuilder::Kind::StackingFault;
    params.axis = SolidInterfaceBuilder::Axis::C;
    params.boundaryPosition = 0.5;
    params.faultVector = {1.0 / 3.0, 0.0};
    params.mergeTolerance = 0.0;

    const auto fault = SolidInterfaceBuilder::generate({parent}, params);
    check(fault.structure.size() == parent.size(),
          "a rigid shift creates and destroys nothing");
    check(fault.interfaceCount == 2,
          "and the cell honestly reports TWO faults, not one — the periodic "
          "boundary carries the second");
    check(!fault.warnings.empty(), "which is stated as a warning too");

    // Exactly the atoms above the plane moved, by exactly the fault vector.
    const double shift = 12.0 / 3.0; // 1/3 of the 12 A a-vector
    int moved = 0;
    int still = 0;
    bool allExact = true;
    for (std::size_t i = 0; i < parent.size(); ++i) {
        const Vec3 delta = fault.structure.atoms()[i].position
            - parent.atoms()[i].position;
        if (delta.norm() < 1e-12) {
            ++still;
            continue;
        }
        ++moved;
        allExact = allExact && std::abs(delta.x - shift) < 1e-9
            && std::abs(delta.y) < 1e-9 && std::abs(delta.z) < 1e-9;
    }
    check(moved == 32 && still == 32,
          "half the atoms move and half do not (the plane is at c = 1/2)");
    check(allExact,
          "and every one that moves moves by exactly b/3 along a");

    // Opening a gap grows the cell by exactly that much and nothing else.
    params.gap = 1.5;
    const auto gapped = SolidInterfaceBuilder::generate({parent}, params);
    checkClose(gapped.structure.cell().vectors()[2].z, 12.0 + 1.5, 1e-9,
               "a requested gap grows the cell along the stacking direction");
    check(gapped.structure.size() == parent.size(),
          "without changing the atom count");
}

void testTwinBoundary()
{
    std::printf("twin boundary\n");
    // A layer sequence with NO mirror symmetry of its own, so twinning it is
    // detectable. With one species — or with any centrosymmetric stacking — a
    // twin of simple cubic is indistinguishable from the perfect crystal, and
    // the test would pass on a builder that did nothing at all.
    //
    // Layers along c: O, Al, Al, Al, Mg, Al.
    Structure parent;
    const double a = 3.0;
    const int layerSpecies[6] = {8, 13, 13, 13, 12, 13};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 6; ++k) {
                Atom atom;
                atom.atomicNumber = layerSpecies[k];
                atom.position = {i * a, j * a, k * a};
                parent.addAtom(atom);
            }
    parent.setCell(UnitCell({3 * a, 0, 0}, {0, 3 * a, 0}, {0, 0, 6 * a},
                            {true, true, true}));

    SolidInterfaceBuilder::Params params;
    params.kind = SolidInterfaceBuilder::Kind::TwinBoundary;
    params.axis = SolidInterfaceBuilder::Axis::C;
    params.boundaryPosition = 0.5;
    params.mergeTolerance = 0.0;

    const auto twin = SolidInterfaceBuilder::generate({parent}, params);
    check(twin.structure.size() == parent.size(),
          "the mirrored half exactly replaces the half it displaced");
    check(twin.interfaceCount == 2,
          "and a mirror in a periodic cell is two twin boundaries");

    // Mirror symmetry is the definition: for every atom there is a partner at
    // the reflected height with the same species and the same in-plane
    // position.
    const double plane = 0.5 * 18.0;
    int matched = 0;
    for (const Atom& atom : twin.structure.atoms()) {
        double reflected = 2.0 * plane - atom.position.z;
        reflected -= 18.0 * std::floor(reflected / 18.0);
        for (const Atom& other : twin.structure.atoms()) {
            if (other.atomicNumber != atom.atomicNumber)
                continue;
            double dz = std::abs(other.position.z - reflected);
            dz = std::min(dz, 18.0 - dz);
            if (std::abs(other.position.x - atom.position.x) < 1e-6
                && std::abs(other.position.y - atom.position.y) < 1e-6
                && dz < 1e-6) {
                ++matched;
                break;
            }
        }
    }
    check(matched == static_cast<int>(twin.structure.size()),
          "every atom has its mirror partner — which is what makes it a twin");

    // Every layer is still fully occupied — no gap at the twin plane, and no
    // doubled layer either. Atoms lying exactly ON a mirror plane belong to
    // both halves and must be kept exactly once.
    std::map<int, int> occupancy;
    for (const Atom& atom : twin.structure.atoms())
        ++occupancy[static_cast<int>(std::lround(atom.position.z))];
    bool everyLayerFull = occupancy.size() == 6;
    for (const auto& [height, count] : occupancy)
        everyLayerFull = everyLayerFull && count == 9;
    check(everyLayerFull,
          "all six layers survive with nine atoms each — the twin plane is "
          "shared, not doubled and not empty");

    // The construction REPLACES the half above the plane with the mirror of
    // the half below it, so whatever was only in that half is gone. The Mg
    // layer sat there; its disappearance is the proof that the replacement
    // happened rather than the crystal being handed back unchanged.
    int magnesium = 0;
    for (const Atom& atom : twin.structure.atoms())
        if (atom.atomicNumber == 12)
            ++magnesium;
    check(magnesium == 0,
          "the layer that existed only in the replaced half is gone");
    check(closestPair(twin.structure) > 2.9,
          "and no two atoms were stacked on top of each other at the seam");

    // A tilted cell is refused rather than silently reflected into a
    // half-crystal that is periodic with a different cell.
    Structure tilted = parent;
    tilted.setCell(UnitCell({3 * a, 0, 0}, {0, 3 * a, 0}, {2.0, 0, 6 * a},
                            {true, true, true}));
    bool threw = false;
    try {
        SolidInterfaceBuilder::generate({tilted}, params);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw,
          "a twin plane that is not perpendicular to its lattice vector is "
          "refused");
}

void testBicrystal()
{
    std::printf("bicrystal\n");
    const Structure parent = cubic(1, 3.0, 13);

    SolidInterfaceBuilder::Params params;
    params.kind = SolidInterfaceBuilder::Kind::Bicrystal;
    params.axis = SolidInterfaceBuilder::Axis::C;
    params.boundaryPosition = 0.5;
    params.repeat = {6, 6, 6};
    params.rotationA = 0.0;
    params.rotationB = 36.87; // the Sigma-5 twist
    params.mergeTolerance = 1.2;

    const auto bicrystal = SolidInterfaceBuilder::generate({parent}, params);
    check(bicrystal.grains.size() == 2, "two grains");
    check(bicrystal.grains[0].atomCount > 0 && bicrystal.grains[1].atomCount > 0,
          "and both of them are actually filled");
    check(bicrystal.interfaceCount == 2,
          "two boundaries: the chosen plane and the cell face");

    // The box is what was asked for, and every atom is inside it.
    const auto& cell = bicrystal.structure.cell();
    checkClose(cell.vectors()[0].x, 18.0, 1e-9, "the box is 6 parent cells wide");
    bool inside = true;
    for (const Atom& atom : bicrystal.structure.atoms()) {
        const Vec3 f = cell.cartesianToFractional(atom.position);
        inside = inside && f.x > -1e-6 && f.x < 1.0 + 1e-6 && f.y > -1e-6
            && f.y < 1.0 + 1e-6 && f.z > -1e-6 && f.z < 1.0 + 1e-6;
    }
    check(inside, "and every atom lands inside it");

    // The two halves really do carry different orientations: grain 1's atoms
    // must NOT sit on the parent's simple-cubic sublattice.
    int offLattice = 0;
    for (std::size_t i = 0; i < bicrystal.structure.size(); ++i) {
        const auto& grain = bicrystal.structure.scalarFields().at("grain");
        if (grain[i] < 0.5)
            continue;
        const Vec3& p = bicrystal.structure.atoms()[i].position;
        const double dx = std::abs(p.x / 3.0 - std::round(p.x / 3.0));
        const double dy = std::abs(p.y / 3.0 - std::round(p.y / 3.0));
        if (dx > 0.05 || dy > 0.05)
            ++offLattice;
    }
    check(offLattice > 0,
          "the second grain is genuinely rotated off the first one's lattice");

    // No pile-up at the seam.
    check(closestPair(bicrystal.structure) >= 1.2 - 1e-6,
          "and no two atoms are closer than the merge tolerance");

    // A twist of 36.87 degrees is the Sigma-5 CSL relationship, but a 6x6 box
    // is not a multiple of the sqrt(5) CSL period — so the rotated crystal
    // meets the periodic boundary out of register, and the builder says so
    // instead of leaving the user to discover it in a relaxation.
    check(bicrystal.commensurabilityResidual > 0.3,
          "the box/lattice mismatch of an arbitrary box is measured");
    bool reported = false;
    for (const std::string& warning : bicrystal.warnings)
        reported = reported || warning.find("coincidence-site") != std::string::npos;
    check(reported, "and reported as a warning, not swallowed");
}

void testPolycrystal()
{
    std::printf("polycrystal\n");
    const Structure parent = cubic(1, 3.0, 13);

    SolidInterfaceBuilder::Params params;
    params.kind = SolidInterfaceBuilder::Kind::Polycrystal;
    params.repeat = {6, 6, 6};
    params.grainCount = 5;
    params.seed = 7;
    params.mergeTolerance = 1.5;

    const auto poly = SolidInterfaceBuilder::generate({parent}, params);
    check(poly.grains.size() == 5, "five grains were requested and five exist");
    int populated = 0;
    int total = 0;
    for (const auto& grain : poly.grains) {
        if (grain.atomCount > 0)
            ++populated;
        total += grain.atomCount;
    }
    check(populated == 5, "and every one of them got atoms");
    check(total == static_cast<int>(poly.structure.size()),
          "the per-grain counts add up to the structure — every atom belongs "
          "to exactly one grain");

    check(poly.structure.scalarFields().count("grain") == 1
              && poly.structure.scalarFields().count("phase") == 1,
          "each atom carries its grain and phase id, so the tessellation can "
          "be seen rather than trusted");

    check(closestPair(poly.structure) >= 1.5 - 1e-6,
          "no seam pile-up anywhere in the cell");
    check(poly.mergedAtoms > 0,
          "which took deleting atoms — grains meeting at arbitrary angles "
          "always overlap somewhere along the boundary");

    // Density within a factor of the parent's. A tessellation that filled only
    // part of the box, or double-filled it, shows up here and nowhere else.
    Structure reference = parent;
    const double parentDensity =
        1.0 * 26.9815385 / 27.0 * 1.6605390666; // one Al per 3^3 A^3
    checkClose(poly.density, parentDensity, 0.35 * parentDensity,
               "and the density is within a third of the parent crystal's");
    (void)reference;

    // The grains WRAP through the periodic boundary. If they did not, atoms
    // near a face would all belong to whichever grain happens to be nearest
    // without the minimum image — and the cell would have a slab of a single
    // orientation glued to every face. Test it by counting how many distinct
    // grains own atoms in the outermost 10% of the cell along a.
    std::map<int, int> edgeGrains;
    const auto& grainField = poly.structure.scalarFields().at("grain");
    for (std::size_t i = 0; i < poly.structure.size(); ++i) {
        const Vec3 f = poly.structure.cell().cartesianToFractional(
            poly.structure.atoms()[i].position);
        if (f.x < 0.1 || f.x > 0.9)
            ++edgeGrains[static_cast<int>(grainField[i])];
    }
    check(edgeGrains.size() >= 2,
          "more than one grain reaches the cell face, so the tessellation "
          "wrapped through the boundary instead of stopping at it");

    // Determinism: the same seed must give the same cell, or a published
    // structure cannot be reproduced.
    const auto again = SolidInterfaceBuilder::generate({parent}, params);
    check(again.structure.size() == poly.structure.size(),
          "the same seed rebuilds the same cell");

    params.seed = 8;
    const auto different = SolidInterfaceBuilder::generate({parent}, params);
    check(different.structure.size() != poly.structure.size()
              || different.grains[0].atomCount != poly.grains[0].atomCount,
          "and a different seed does not");
}

void testMultiPhase()
{
    std::printf("multi-phase polycrystal\n");
    // Two phases that are distinguishable by species AND by lattice constant.
    const Structure aluminium = cubic(1, 3.0, 13);
    Structure magnesium = cubic(1, 3.6, 12);

    SolidInterfaceBuilder::Params params;
    params.kind = SolidInterfaceBuilder::Kind::MultiPhasePolycrystal;
    params.repeat = {6, 6, 6};
    params.grainCount = 6;
    params.seed = 3;
    params.mergeTolerance = 1.5;
    params.phaseWeights = {1.0, 1.0};

    const auto multi =
        SolidInterfaceBuilder::generate({aluminium, magnesium}, params);

    std::map<int, int> phaseGrains;
    for (const auto& grain : multi.grains)
        ++phaseGrains[grain.phase];
    check(phaseGrains.size() == 2,
          "both phases were drawn — a mixture that produced one phase is not "
          "a mixture");

    // The species present must be exactly the two phases' species, and each
    // grain must be single-phase.
    std::map<int, int> census;
    for (const Atom& atom : multi.structure.atoms())
        ++census[atom.atomicNumber];
    check(census.count(13) == 1 && census.count(12) == 1 && census.size() == 2,
          "and both species are present, with nothing else");

    const auto& grainField = multi.structure.scalarFields().at("grain");
    const auto& phaseField = multi.structure.scalarFields().at("phase");
    bool consistent = true;
    for (std::size_t i = 0; i < multi.structure.size(); ++i) {
        const int expected =
            static_cast<int>(phaseField[i]) == 0 ? 13 : 12;
        consistent = consistent
            && multi.structure.atoms()[i].atomicNumber == expected
            && multi.grains[static_cast<std::size_t>(grainField[i])].phase
                == static_cast<int>(phaseField[i]);
    }
    check(consistent,
          "every atom's species matches its grain's phase — no grain is a "
          "blend of the two lattices");

    check(closestPair(multi.structure) >= 1.5 - 1e-6,
          "and the phase boundaries carry no pile-up either");

    // A weight of zero really excludes a phase.
    params.phaseWeights = {1.0, 0.0};
    const auto single =
        SolidInterfaceBuilder::generate({aluminium, magnesium}, params);
    bool onlyAluminium = true;
    for (const Atom& atom : single.structure.atoms())
        onlyAluminium = onlyAluminium && atom.atomicNumber == 13;
    check(onlyAluminium, "a zero weight excludes its phase entirely");
}

void testRefusals()
{
    std::printf("refusals\n");
    const auto refuses = [](const std::vector<Structure>& lattices,
                            const SolidInterfaceBuilder::Params& p) {
        try {
            SolidInterfaceBuilder::generate(lattices, p);
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };

    SolidInterfaceBuilder::Params params;
    check(refuses({}, params), "no parent lattice is refused");

    Structure noCell = cubic(2, 3.0, 13);
    noCell.setCell(UnitCell{});
    check(refuses({noCell}, params), "a parent without a cell is refused");

    SolidInterfaceBuilder::Params outside = params;
    outside.boundaryPosition = 1.0;
    check(refuses({cubic(2, 3.0, 13)}, outside),
          "a boundary on the cell face is refused — that is not inside the "
          "cell");

    SolidInterfaceBuilder::Params noGrains = params;
    noGrains.kind = SolidInterfaceBuilder::Kind::Polycrystal;
    noGrains.grainCount = 0;
    check(refuses({cubic(2, 3.0, 13)}, noGrains),
          "a polycrystal with no grains is refused");

    SolidInterfaceBuilder::Params huge = params;
    huge.kind = SolidInterfaceBuilder::Kind::Polycrystal;
    huge.repeat = {40, 40, 40};
    huge.atomBudget = 1000;
    check(refuses({cubic(2, 3.0, 13)}, huge),
          "and a cell past the atom budget is refused rather than allocated");
}

} // namespace

int main()
{
    testStackingFault();
    testTwinBoundary();
    testBicrystal();
    testPolycrystal();
    testMultiPhase();
    testRefusals();

    if (failures == 0) {
        std::printf("\nAll solid-interface checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d solid-interface check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
