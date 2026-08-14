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
#include "gui/GrainCasts.hpp"

#include <QColor>

#include "core/Element.hpp"

#include <algorithm>
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

    // The contract the viewport's grain casts are built on: every atom's id is
    // a valid grain index, the ids are CONTIGUOUS from 0, and re-counting them
    // reproduces grains[].atomCount. One cast is created per index and each
    // atom is assigned to cast (id + 1), so a gap or an out-of-range id would
    // put atoms in a cast that does not exist — or silently drop them into the
    // fallback, which reads as an unassigned region of the polycrystal.
    {
        const std::vector<double>& ids =
            poly.structure.scalarFields().at("grain");
        check(ids.size() == poly.structure.size(),
              "the grain field covers every atom");
        std::vector<int> recounted(poly.grains.size(), 0);
        bool inRange = true;
        bool integral = true;
        for (const double raw : ids) {
            const int id = static_cast<int>(raw);
            // Stored as a double because the scalar-field map is one type for
            // charges, forces and this; it still has to be an exact integer.
            integral = integral && std::abs(raw - id) < 1e-9;
            if (id < 0 || id >= static_cast<int>(poly.grains.size())) {
                inRange = false;
                continue;
            }
            ++recounted[static_cast<std::size_t>(id)];
        }
        check(inRange, "every id names one of the grains that exist");
        check(integral, "and is an exact integer, not a rounded one");
        bool matches = true;
        bool contiguous = true;
        for (std::size_t g = 0; g < poly.grains.size(); ++g) {
            matches = matches && recounted[g] == poly.grains[g].atomCount;
            contiguous = contiguous && recounted[g] > 0;
        }
        check(matches,
              "and re-counting the field reproduces every grain's atom count");
        check(contiguous,
              "with no empty index in between — the ids run 0..N-1 without a "
              "gap, which is what one-cast-per-grain assumes");
    }

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

/// A three-grain polycrystal, cast by cast.
///
/// The end of the feature, not the middle of it: a tessellation that tags its
/// atoms correctly and a viewport that colours them are one claim to a user,
/// and the half that used to be missing was the second one — the casts were
/// built and the scene was left on Element colouring, so a polycrystal opened
/// as a uniform block of aluminium and the grains were invisible.
///
/// Three grains because that is the smallest count where "distinct" is a real
/// constraint: two colours are trivially different, and at three the golden
/// -angle rotation has to actually work.
void testGrainCasts()
{
    std::printf("grain casts\n");
    const Structure parent = cubic(1, 3.0, 13);

    SolidInterfaceBuilder::Params params;
    params.kind = SolidInterfaceBuilder::Kind::Polycrystal;
    params.repeat = {6, 6, 6};
    params.grainCount = 3;
    params.seed = 11;
    params.mergeTolerance = 1.5;

    const auto poly = SolidInterfaceBuilder::generate({parent}, params);
    check(poly.grains.size() == 3, "a 3-grain polycrystal was generated");
    check(poly.structure.scalarFields().count("grain") == 1,
          "and every atom carries its grain id");
    if (poly.structure.scalarFields().count("grain") != 1)
        return;

    const auto assignment =
        calango::gui::grainCastsFor(poly.structure.scalarFields().at("grain"));

    // 1. Exactly three casts — not two, not four, and not one per atom.
    check(assignment.grainCount == 3,
          "exactly 3 casts are created, one per grain");
    check(assignment.colors.size() == 3, "each with a colour of its own");
    check(assignment.atomCasts.size() == poly.structure.size(),
          "and every atom is assigned to one");

    // 2. Every cast is actually USED. Three casts of which one is empty is
    //    three casts by count and two by anything a user would notice.
    std::vector<int> perCast(4, 0); // index 0 is the fallback
    bool inRange = true;
    for (const int cast : assignment.atomCasts) {
        if (cast < 0 || cast > 3) {
            inRange = false;
            continue;
        }
        ++perCast[static_cast<std::size_t>(cast)];
    }
    check(inRange, "no atom is assigned to a cast that does not exist");
    check(perCast[0] == 0,
          "no atom falls through to the fallback cast — every one was tagged");
    check(perCast[1] > 0 && perCast[2] > 0 && perCast[3] > 0,
          "all 3 casts are populated");
    check(perCast[1] + perCast[2] + perCast[3]
              == static_cast<int>(poly.structure.size()),
          "and between them they account for the whole structure");

    // 3. The colours are valid and mutually DISTINCT. "Coloured" is not the
    //    same claim as "differently coloured", and it is the second one that
    //    makes the grains readable.
    bool allValid = true;
    for (const QColor& color : assignment.colors)
        allValid = allValid && color.isValid();
    check(allValid, "every cast colour is a valid colour");

    bool distinct = true;
    for (std::size_t a = 0; a < assignment.colors.size(); ++a)
        for (std::size_t b = a + 1; b < assignment.colors.size(); ++b)
            distinct = distinct && assignment.colors[a] != assignment.colors[b];
    check(distinct, "the 3 cast colours are pairwise distinct");

    // Distinct as ARGB is a low bar — two colours one step apart in hue would
    // pass it and look identical. The golden angle puts consecutive grains
    // ~137.5 deg apart, so require real separation on the colour wheel.
    int minSeparation = 360;
    for (std::size_t a = 0; a < assignment.colors.size(); ++a) {
        for (std::size_t b = a + 1; b < assignment.colors.size(); ++b) {
            const int delta =
                std::abs(assignment.colors[a].hue() - assignment.colors[b].hue());
            minSeparation = std::min(minSeparation, std::min(delta, 360 - delta));
        }
    }
    check(minSeparation > 60,
          "and separated on the colour wheel, not merely unequal as pixels");
    std::printf("    3 casts, hues %d/%d/%d, min separation %d deg\n",
                assignment.colors[0].hue(), assignment.colors[1].hue(),
                assignment.colors[2].hue(), minSeparation);

    // 4. A single grain is NOT a polycrystal: one cast covering every atom is
    //    a control that does nothing, so the mapping declines to build it and
    //    the caller leaves the scene alone.
    const auto single = calango::gui::grainCastsFor(std::vector<double>(64, 0.0));
    check(single.grainCount == 0,
          "a single-grain field produces no casts at all");
    check(calango::gui::grainCastsFor({}).grainCount == 0,
          "and neither does an empty one");
}

// ---------------------------------------------------------------------------
// Real close-packed crystallography.
//
// Everything above builds from a SIMPLE CUBIC lattice, which is the right way
// to test the mechanics (which atoms moved, by how much, what the cell did)
// and cannot test the crystallography at all: a cubic lattice has no stacking
// sequence to fault. These build actual close-packed cells and read the
// stacking off the result.
// ---------------------------------------------------------------------------

/// FCC oriented with [111] along c, as the conventional hexagonal cell:
/// in-plane vectors at 60 degrees with the nearest-neighbour spacing, and one
/// close-packed layer per interlayer spacing, stacked A B C A B C.
Structure fcc111(int layers, double aFcc, int z)
{
    const double d = aFcc / std::sqrt(2.0);  // in-plane nearest neighbour
    const double h = aFcc / std::sqrt(3.0);  // {111} interlayer spacing
    const Vec3 a1{d, 0.0, 0.0};
    const Vec3 a2{0.5 * d, std::sqrt(3.0) / 2.0 * d, 0.0};

    Structure s;
    for (int layer = 0; layer < layers; ++layer) {
        // A at (0,0), B at (a1+a2)/3, C at 2(a1+a2)/3 — the two hollows of the
        // triangle the 60-degree cell spans.
        const double f = static_cast<double>(layer % 3) / 3.0;
        Atom atom;
        atom.atomicNumber = z;
        atom.position = a1 * f + a2 * f + Vec3{0.0, 0.0, layer * h};
        s.addAtom(atom);
    }
    s.setCell(UnitCell(a1, a2, {0.0, 0.0, layers * h}, {true, true, true}));
    return s;
}

/// HCP with c along the stacking axis: A B A B on the basal plane.
Structure hcpBasal(int layers, double a, double c, int z)
{
    const Vec3 a1{a, 0.0, 0.0};
    const Vec3 a2{0.5 * a, std::sqrt(3.0) / 2.0 * a, 0.0};
    const double h = 0.5 * c; // one basal layer per half-c

    Structure s;
    for (int layer = 0; layer < layers; ++layer) {
        const double f = (layer % 2 == 0) ? 0.0 : 1.0 / 3.0;
        Atom atom;
        atom.atomicNumber = z;
        atom.position = a1 * f + a2 * f + Vec3{0.0, 0.0, layer * h};
        s.addAtom(atom);
    }
    s.setCell(UnitCell(a1, a2, {0.0, 0.0, layers * h}, {true, true, true}));
    return s;
}

/// Read the stacking sequence off a built structure.
///
/// Each atom is reduced to its in-plane fractional coordinates; for a
/// close-packed layer the sum f1 + f2 is 0, 2/3 or 1/3 modulo 1, which labels
/// the site A, B or C. Derived from the coordinates the builder returned, not
/// from what the builder was asked to do.
std::string stackingSequence(const Structure& s, double tolerance = 0.02)
{
    struct Layer {
        double height;
        char label;
    };
    std::vector<Layer> layers;
    for (const Atom& atom : s.atoms()) {
        const Vec3 f = s.cell().cartesianToFractional(atom.position);
        const auto wrap = [](double v) { return v - std::floor(v); };
        const double sum = wrap(wrap(f.x) + wrap(f.y));
        char label = '?';
        if (sum < tolerance || sum > 1.0 - tolerance)
            label = 'A';
        else if (std::abs(sum - 1.0 / 3.0) < tolerance)
            label = 'C';
        else if (std::abs(sum - 2.0 / 3.0) < tolerance)
            label = 'B';

        const double height = wrap(f.z);
        bool merged = false;
        for (auto& existing : layers)
            if (std::abs(existing.height - height) < 1e-4) {
                merged = true;
                break;
            }
        if (!merged)
            layers.push_back({height, label});
    }
    std::sort(layers.begin(), layers.end(),
              [](const Layer& x, const Layer& y) { return x.height < y.height; });
    std::string out;
    for (const auto& layer : layers)
        out.push_back(layer.label);
    return out;
}

void testFccIntrinsicStackingFault()
{
    std::printf("FCC {111} intrinsic stacking fault\n");
    const double aCu = 3.615;
    const Structure parent = fcc111(6, aCu, 29);
    check(stackingSequence(parent) == "ABCABC",
          "the parent stacks ABCABC before anything is done to it");

    SolidInterfaceBuilder::Params params;
    params.kind = SolidInterfaceBuilder::Kind::StackingFault;
    params.axis = SolidInterfaceBuilder::Axis::C;
    params.boundaryPosition = 0.5;
    // The Shockley partial 1/6<112> IS the A -> B hollow displacement, which
    // in this 60-degree in-plane basis is (a1 + a2)/3. Expressed as fractions
    // of the two in-plane vectors that is {1/3, 1/3}.
    params.faultVector = {1.0 / 3.0, 1.0 / 3.0};
    params.mergeTolerance = 0.0;

    const auto fault = SolidInterfaceBuilder::generate({parent}, params);
    const std::string sequence = stackingSequence(fault.structure);
    check(sequence == "ABCBCA",
          std::string("a Shockley partial above the mid-plane gives the "
                      "intrinsic sequence ABC|BCA (got ")
              + sequence + ")");
    check(fault.structure.size() == parent.size(),
          "a rigid shift neither creates nor destroys atoms");

    // No layer may sit directly on the one below it ACROSS THE CHOSEN FAULT.
    // The seam is a different matter and is checked separately below.
    bool validAcrossFault = true;
    for (std::size_t i = 0; i + 1 < sequence.size(); ++i)
        validAcrossFault = validAcrossFault && sequence[i] != sequence[i + 1];
    check(validAcrossFault,
          "and every adjacent pair inside the cell is a real close-packed "
          "stack (no AA)");

    // Closest approach must still be the in-plane nearest-neighbour distance:
    // a shift parallel to the planes cannot bring atoms together.
    const double d = aCu / std::sqrt(2.0);
    check(closestPair(fault.structure) > 0.99 * std::min(d, aCu / std::sqrt(3.0)),
          "no atoms are brought into contact by the shift");

    // The compensating fault at the periodic seam is NOT the same defect: the
    // sequence wraps A -> A, which is an AA stack, not a second intrinsic
    // fault. This is exactly why the excess energy of this cell cannot simply
    // be halved, and the builder's warning is checked to say so.
    check(sequence.front() == sequence.back(),
          "the periodic seam carries an AA stack — a DIFFERENT fault from the "
          "intrinsic one, so the two energies are not equal");
    bool warnsAboutTwo = false;
    for (const auto& warning : fault.warnings)
        if (warning.find("TWO") != std::string::npos)
            warnsAboutTwo = true;
    check(warnsAboutTwo, "and the builder warns that there are two of them");
}

void testHcpBasalStackingFault()
{
    std::printf("HCP basal stacking fault\n");
    const Structure parent = hcpBasal(6, 3.209, 5.211, 12); // Mg
    check(stackingSequence(parent) == "ABABAB",
          "the parent stacks ABABAB");

    SolidInterfaceBuilder::Params params;
    params.kind = SolidInterfaceBuilder::Kind::StackingFault;
    params.axis = SolidInterfaceBuilder::Axis::C;
    params.boundaryPosition = 0.5;
    params.faultVector = {1.0 / 3.0, 1.0 / 3.0};
    params.mergeTolerance = 0.0;

    const auto fault = SolidInterfaceBuilder::generate({parent}, params);
    const std::string sequence = stackingSequence(fault.structure);
    check(fault.structure.size() == parent.size(),
          "atom count is preserved");
    // Layers 3,4,5 are B,A,B and each advances one hollow: B->C, A->B, B->C.
    // The cell therefore reads A B A | C B C — the I2-type basal fault, whose
    // signature is exactly the FCC-like ...ABAC... segment embedded in ABAB.
    check(sequence == "ABACBC",
          std::string("the shift introduces the C layer a basal fault is "
                      "defined by (got ")
              + sequence + ")");
    bool basalClosePacked = true;
    for (std::size_t i = 0; i < sequence.size(); ++i)
        basalClosePacked = basalClosePacked
            && sequence[i] != sequence[(i + 1) % sequence.size()];
    check(basalClosePacked,
          "and the faulted stack is close packed everywhere, seam included");
    check(closestPair(fault.structure) > 2.5,
          "and no atoms are brought into contact");
}

void testFccTwinIsAMirror()
{
    std::printf("FCC {111} twin boundary is a mirror\n");
    const double aCu = 3.615;
    const Structure parent = fcc111(6, aCu, 29);

    SolidInterfaceBuilder::Params params;
    params.kind = SolidInterfaceBuilder::Kind::TwinBoundary;
    params.axis = SolidInterfaceBuilder::Axis::C;
    params.boundaryPosition = 0.5;
    params.mergeTolerance = 0.0;

    const auto twin = SolidInterfaceBuilder::generate({parent}, params);
    check(twin.structure.size() == parent.size(),
          "a mirror preserves the atom count");

    // The defining property, checked on the COORDINATES rather than on the
    // sequence: for every atom there is another atom at the reflection of its
    // position through the twin plane. If the mirror were applied to the wrong
    // half, or the plane atoms duplicated, this fails.
    const auto& cell = twin.structure.cell();
    int mirrored = 0;
    for (const Atom& atom : twin.structure.atoms()) {
        const Vec3 f = cell.cartesianToFractional(atom.position);
        const auto wrap = [](double v) { return v - std::floor(v); };
        // Reflection through z = boundaryPosition, in fractional coordinates.
        const double reflected =
            wrap(2.0 * params.boundaryPosition - wrap(f.z));
        const Vec3 target =
            cell.fractionalToCartesian(Vec3{f.x, f.y, reflected});
        for (const Atom& other : twin.structure.atoms())
            if (minimumImage(twin.structure, other.position, target) < 1e-6) {
                ++mirrored;
                break;
            }
    }
    check(mirrored == static_cast<int>(twin.structure.size()),
          "every atom has a partner at its reflection through the twin plane");

    // A coherent twin is still close packed everywhere: no AA anywhere.
    const std::string sequence = stackingSequence(twin.structure);
    bool closePacked = true;
    for (std::size_t i = 0; i < sequence.size(); ++i)
        closePacked = closePacked
            && sequence[i] != sequence[(i + 1) % sequence.size()];
    check(closePacked,
          std::string("the twinned stack has no AA anywhere, including across "
                      "the periodic seam (got ")
              + sequence + ")");
    check(closestPair(twin.structure) > 0.99 * aCu / std::sqrt(2.0),
          "and the boundary layer is not doubled — closest approach is still "
          "the nearest-neighbour distance");
}

void testDiamondSiliconFault()
{
    std::printf("diamond silicon, {111} shift\n");
    // Diamond is FCC with a two-atom basis, so the {111} stack is AaBbCc: the
    // same close-packed sequence with a companion atom 1/4 of the interlayer
    // spacing above each site. Built explicitly so the test exercises a
    // multi-atom basis rather than a Bravais lattice.
    const double aSi = 5.431;
    const double d = aSi / std::sqrt(2.0);
    const double h = aSi / std::sqrt(3.0);
    const Vec3 a1{d, 0.0, 0.0};
    const Vec3 a2{0.5 * d, std::sqrt(3.0) / 2.0 * d, 0.0};

    Structure parent;
    for (int layer = 0; layer < 6; ++layer) {
        const double f = static_cast<double>(layer % 3) / 3.0;
        for (int basis = 0; basis < 2; ++basis) {
            Atom atom;
            atom.atomicNumber = 14;
            atom.position = a1 * f + a2 * f
                + Vec3{0.0, 0.0, layer * h + basis * 0.25 * h};
            parent.addAtom(atom);
        }
    }
    parent.setCell(UnitCell(a1, a2, {0.0, 0.0, 6 * h}, {true, true, true}));

    SolidInterfaceBuilder::Params params;
    params.kind = SolidInterfaceBuilder::Kind::StackingFault;
    params.axis = SolidInterfaceBuilder::Axis::C;
    // The cut goes through the WIDE gap between basis pairs, not through a
    // pair. A glide plane that splits the basis is not a stacking fault at
    // all, and placing it there would be testing the builder against a request
    // that has no crystallographic meaning.
    params.boundaryPosition = (3.0 + 0.6) / 6.0;
    params.faultVector = {1.0 / 3.0, 1.0 / 3.0};
    params.mergeTolerance = 0.0;

    const auto fault = SolidInterfaceBuilder::generate({parent}, params);
    check(fault.structure.size() == parent.size(),
          "the two-atom basis survives the shift intact");
    // The basis pair must stay together: a shift that split it would move one
    // sublattice and not the other.
    int pairs = 0;
    for (std::size_t i = 0; i < fault.structure.size(); i += 2) {
        const double separation =
            minimumImage(fault.structure, fault.structure.atoms()[i].position,
                         fault.structure.atoms()[i + 1].position);
        if (std::abs(separation - 0.25 * h) < 1e-6)
            ++pairs;
    }
    check(pairs == 6, "and every basis pair keeps its internal separation");
    check(closestPair(fault.structure) > 0.99 * 0.25 * h,
          "no atoms are brought into contact");
}

void testAtomExactlyOnFaultPlane()
{
    std::printf("an atom lying exactly on the fault plane\n");
    // Regression test for a real bug. The half-space test used a bare `<` on a
    // fractional coordinate, so an atom sitting exactly on the boundary was
    // assigned to whichever half the round trip through Cartesian rounded it
    // into. Harmless for a Bravais lattice; for a multi-atom basis it split
    // the basis, shifting one sublattice and not the other.
    //
    // Built so the FIRST atom of each pair lands exactly on the plane at 0.5.
    const double h = 3.0;
    const double d = 4.0;
    Structure parent;
    for (int layer = 0; layer < 6; ++layer)
        for (int basis = 0; basis < 2; ++basis) {
            Atom atom;
            atom.atomicNumber = 14;
            atom.position = {0.0, 0.0, layer * h + basis * 0.25 * h};
            parent.addAtom(atom);
        }
    parent.setCell(UnitCell({d, 0, 0}, {0, d, 0}, {0, 0, 6 * h},
                            {true, true, true}));

    SolidInterfaceBuilder::Params params;
    params.kind = SolidInterfaceBuilder::Kind::StackingFault;
    params.axis = SolidInterfaceBuilder::Axis::C;
    params.boundaryPosition = 0.5; // exactly the z of layer 3's first atom
    params.faultVector = {1.0 / 3.0, 0.0};
    params.mergeTolerance = 0.0;

    const auto fault = SolidInterfaceBuilder::generate({parent}, params);

    // Whichever side the on-plane atom is assigned to, its basis partner
    // 0.25h above it must be assigned to the SAME side, or the pair is torn
    // apart. That is the invariant; the choice of side is a convention.
    bool pairsIntact = true;
    for (std::size_t i = 0; i < fault.structure.size(); i += 2) {
        const double separation =
            minimumImage(fault.structure, fault.structure.atoms()[i].position,
                         fault.structure.atoms()[i + 1].position);
        pairsIntact = pairsIntact && std::abs(separation - 0.25 * h) < 1e-9;
    }
    check(pairsIntact,
          "the basis pair straddling the plane is not torn apart by rounding");

    // And the assignment is deterministic: an atom exactly on the plane joins
    // the half that moves, so layer 3's pair is shifted along with 4 and 5.
    const Vec3 delta = fault.structure.atoms()[6].position
        - parent.atoms()[6].position;
    check(delta.norm() > 1e-9,
          "and an atom exactly on the plane is deterministically shifted");
}

int main()
{
    testStackingFault();
    testTwinBoundary();
    testAtomExactlyOnFaultPlane();
    testFccIntrinsicStackingFault();
    testHcpBasalStackingFault();
    testFccTwinIsAMirror();
    testDiamondSiliconFault();
    testBicrystal();
    testPolycrystal();
    testGrainCasts();
    testMultiPhase();
    testRefusals();

    if (failures == 0) {
        std::printf("\nAll solid-interface checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d solid-interface check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
