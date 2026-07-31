// Solid-liquid / solid-gas interface builder test.
//
// Everything below is re-derived from the RETURNED COORDINATES rather than
// read back out of the builder's own bookkeeping. A packer that reports "64
// water molecules at 0.997 g/cm³" while actually having fused two of them into
// each other, dropped half the chloride ions, or left the cell 5 Å short
// produces a structure that looks entirely normal in a viewer and fails only
// when a calculation is run on it — hours later, on someone else's cluster.
//
// GUI-free, Python-free.

#include "core/SolvationBuilder.hpp"

#include "core/Element.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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

/// Minimum-image separation under the structure's cell, scanning enough images
/// to be right for a tilted cell (the test must not repeat the builder's own
/// shortcut, or a bug in that shortcut would cancel out).
double minimumImage(const Structure& structure, const Vec3& a, const Vec3& b)
{
    const auto& cell = structure.cell().vectors();
    double best = std::numeric_limits<double>::max();
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j)
            for (int k = -1; k <= 1; ++k) {
                const Vec3 delta =
                    a - b - (cell[0] * i + cell[1] * j + cell[2] * k);
                best = std::min(best, delta.norm());
            }
    return best;
}

/// A 3-layer fcc(100)-like Al slab: an ordinary substrate with a cell, some
/// pre-existing vacuum, and a well-defined top face.
Structure aluminiumSlab()
{
    Structure slab;
    const double a = 4.05;
    slab.setCell(UnitCell({a, 0, 0}, {0, a, 0}, {0, 0, 3.0 * a},
                          {true, true, true}));
    for (int layer = 0; layer < 3; ++layer) {
        const double z = layer * a * 0.5;
        const double offset = (layer % 2) ? 0.5 * a : 0.0;
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) {
                Atom atom;
                atom.atomicNumber = 13;
                atom.position = {offset + i * a * 0.5, offset + j * a * 0.5, z};
                slab.addAtom(atom);
            }
    }
    return slab;
}

/// Element counts of a structure, by symbol.
std::map<std::string, int> composition(const Structure& structure)
{
    std::map<std::string, int> counts;
    for (const Atom& atom : structure.atoms())
        ++counts[Elements::data(atom.atomicNumber).symbol];
    return counts;
}

/// Perpendicular height of the cell along lattice vector `axis`.
double perpendicularHeight(const Structure& structure, int axis)
{
    const auto& cell = structure.cell().vectors();
    const Vec3 normal =
        cell[(axis + 1) % 3].cross(cell[(axis + 2) % 3]).normalized();
    return std::abs(cell[axis].dot(normal));
}

} // namespace

int main()
{
    std::printf("Species library:\n");
    {
        // The geometries are the one thing here that is not a run-time result:
        // they are fixed by symmetry plus a bond length, so they can be
        // checked exactly. A transposed coordinate or a degrees/radians slip
        // makes a molecule that is subtly the wrong shape everywhere it is
        // used.
        const auto* water = SolvationBuilder::find("water");
        check(water != nullptr, "water is in the library");
        if (water) {
            check(water->numbers == std::vector<int>({8, 1, 1}),
                  "with oxygen first, as the bent-triatomic builder emits");
            const double oh = (water->positions[1] - water->positions[0]).norm();
            checkClose(oh, 0.9572, 1e-6, "O-H bond length");
            const Vec3 u = (water->positions[1] - water->positions[0]).normalized();
            const Vec3 v = (water->positions[2] - water->positions[0]).normalized();
            checkClose(std::acos(u.dot(v)) * 180.0 / 3.14159265358979323846,
                       104.52, 1e-4, "H-O-H angle");
            checkClose(water->molarMassU(), 18.015, 0.01, "molar mass of H2O");
        }

        const auto* ammonia = SolvationBuilder::find("ammonia");
        check(ammonia != nullptr, "ammonia is in the library");
        if (ammonia) {
            // The pyramidal builder inverts cos θ = 3/2 cos²β − 1/2. If that
            // inversion were wrong the molecule would still look like NH3 —
            // three N-H bonds of the right length — with the wrong apex angle.
            checkClose((ammonia->positions[1] - ammonia->positions[0]).norm(),
                       1.012, 1e-6, "N-H bond length");
            const Vec3 u =
                (ammonia->positions[1] - ammonia->positions[0]).normalized();
            const Vec3 v =
                (ammonia->positions[2] - ammonia->positions[0]).normalized();
            checkClose(std::acos(u.dot(v)) * 180.0 / 3.14159265358979323846,
                       106.67, 1e-3, "H-N-H angle");
        }

        const auto* sulfate = SolvationBuilder::find("so4-2");
        check(sulfate != nullptr && std::abs(sulfate->charge + 2.0) < 1e-9,
              "sulfate carries its −2 formal charge");
        if (sulfate) {
            bool tetrahedral = true;
            for (std::size_t i = 1; i < sulfate->positions.size(); ++i)
                tetrahedral = tetrahedral
                    && std::abs((sulfate->positions[i] - sulfate->positions[0])
                                    .norm()
                                - 1.49)
                        < 1e-6;
            check(tetrahedral, "with four equal S-O bonds");
        }

        const auto* ammoniumSulfate = SolvationBuilder::find("nh42so4");
        check(ammoniumSulfate != nullptr
                  && ammoniumSulfate->expandsTo
                      == std::vector<std::string>({"nh4+", "nh4+", "so4-2"}),
              "(NH4)2SO4 expands into two ammonium and one sulfate");

        // Every salt has to expand into ions that exist and that sum to a
        // neutral formula unit. A salt that does not is a charged cell the
        // user never asked for.
        bool saltsNeutral = true;
        bool saltsResolve = true;
        for (const auto& species : SolvationBuilder::library()) {
            if (species.category != SolvationBuilder::Category::Salt)
                continue;
            double charge = 0.0;
            for (const std::string& part : species.expandsTo) {
                const auto* ion = SolvationBuilder::find(part);
                if (!ion)
                    saltsResolve = false;
                else
                    charge += ion->charge;
            }
            if (std::abs(charge) > 1e-9)
                saltsNeutral = false;
        }
        check(saltsResolve, "every salt expands into ions that exist");
        check(saltsNeutral, "and every formula unit is charge neutral");
    }

    std::printf("Water on an aluminium slab:\n");
    {
        const Structure slab = aluminiumSlab();
        SolvationBuilder::Params params;
        params.axis = SolvationBuilder::Axis::C;
        params.regionThickness = 18.0;
        params.lateral[0] = 2;
        params.lateral[1] = 2;
        params.targetDensity = 0.997;
        params.components = {{"water", 1.0}};
        const auto result = SolvationBuilder::generate(slab, params);

        // -- The lateral supercell -----------------------------------------
        const auto counts = composition(result.structure);
        check(counts.at("Al") == 12 * 4,
              "the substrate is replicated 2x2 in the lateral directions");
        check(counts.count("O") == 1 && counts.at("H") == 2 * counts.at("O"),
              "and the fluid is intact water molecules, two H per O");
        check(result.totalMolecules == counts.at("O"),
              "the reported molecule count matches the coordinates");

        // -- The region ----------------------------------------------------
        // The gap between the substrate's top face and its own periodic image
        // must be EXACTLY what was asked for, whatever vacuum the input had.
        // The input slab is 4.05 Å thick in a 12.15 Å cell, so the builder has
        // to shorten the pre-existing vacuum, not add to it.
        const double height = perpendicularHeight(result.structure, 2);
        checkClose(height, 4.05 + 18.0, 1e-6,
                   "the cell height is the slab thickness plus the region");

        double topOfSlab = 0.0;
        double lowestFluid = 1e9;
        double highestFluid = -1e9;
        for (const Atom& atom : result.structure.atoms()) {
            if (atom.atomicNumber == 13)
                topOfSlab = std::max(topOfSlab, atom.position.z);
            else {
                lowestFluid = std::min(lowestFluid, atom.position.z);
                highestFluid = std::max(highestFluid, atom.position.z);
            }
        }
        check(lowestFluid > topOfSlab,
              "every fluid atom sits above the substrate, none inside it");
        check(highestFluid < height,
              "and none pokes out through the top of the cell");

        // -- No overlaps ---------------------------------------------------
        //
        // The contract the packer actually has to keep. Checked against the
        // substrate at the requested clearance, and between fluid molecules at
        // a distance no smaller than a real O-H contact, since a random
        // packing that overlapped two waters would be unusable as a starting
        // geometry however good the density looked.
        double closestToSurface = 1e9;
        double closestFluidPair = 1e9;
        std::vector<std::size_t> fluid;
        std::vector<std::size_t> metal;
        for (std::size_t i = 0; i < result.structure.size(); ++i)
            (result.structure.atoms()[i].atomicNumber == 13 ? metal : fluid)
                .push_back(i);
        for (std::size_t i : fluid)
            for (std::size_t j : metal)
                closestToSurface = std::min(
                    closestToSurface,
                    minimumImage(result.structure,
                                 result.structure.atoms()[i].position,
                                 result.structure.atoms()[j].position));
        // Molecule index of each fluid atom: they were emitted molecule by
        // molecule, three atoms at a time.
        for (std::size_t a = 0; a < fluid.size(); ++a)
            for (std::size_t b = a + 1; b < fluid.size(); ++b) {
                if (a / 3 == b / 3)
                    continue; // same molecule — bonded, not a contact
                closestFluidPair = std::min(
                    closestFluidPair,
                    minimumImage(result.structure,
                                 result.structure.atoms()[fluid[a]].position,
                                 result.structure.atoms()[fluid[b]].position));
            }
        check(closestToSurface >= params.surfaceClearance - 1e-6,
              "no fluid atom is closer to the substrate than the clearance");
        std::printf("       closest surface contact %.2f Å, closest fluid "
                    "contact %.2f Å\n",
                    closestToSurface, closestFluidPair);
        // 1.70 Å is the builder's floor for a pair involving hydrogen — just
        // under a real hydrogen bond, so the packing may set one up without
        // ever producing an overlap.
        check(closestFluidPair >= 1.70 - 1e-6,
              "and no two molecules are fused into each other");

        // -- The density ---------------------------------------------------
        //
        // Recomputed from the coordinates and the filled volume rather than
        // taken from the result: this is the number a user reads off the
        // status bar and believes.
        double mass = 0.0;
        for (std::size_t i : fluid)
            mass += Elements::atomicMass(
                result.structure.atoms()[i].atomicNumber);
        const double area =
            result.structure.cell().vectors()[0]
                .cross(result.structure.cell().vectors()[1])
                .norm();
        const double filled = area * (18.0 - 2.0 * params.surfaceClearance);
        checkClose(mass * 1.6605390666 / filled, result.density, 1e-6,
                   "the reported density is the one in the coordinates");
        // Random sequential packing cannot reach the equilibrium density of a
        // hydrogen-bonded liquid; what it must not do is silently return a
        // near-empty cell. Anything above ~2/3 of the target is a usable
        // starting geometry.
        check(result.density > 0.6 * params.targetDensity,
              "and it reaches a usable fraction of the target");
        std::printf("       %d molecules, %.3f g/cm³ (target %.3f)\n",
                    result.totalMolecules, result.density,
                    params.targetDensity);
    }

    std::printf("Ionic solution:\n");
    {
        // Ammonium sulfate — the salt named in the request — plus sodium
        // chloride, in water. The stoichiometry and the neutrality are the
        // point: an ion dropped by a saturated packing changes the chemistry.
        const Structure slab = aluminiumSlab();
        SolvationBuilder::Params params;
        params.regionThickness = 22.0;
        params.lateral[0] = 3;
        params.lateral[1] = 3;
        params.components = {{"water", 1.0}};
        params.ions = {{"nh42so4", 2}, {"nacl", 3}};
        const auto result = SolvationBuilder::generate(slab, params);

        const auto counts = composition(result.structure);
        check(counts.count("S") == 1 && counts.at("S") == 2,
              "two sulfate ions, one per formula unit of (NH4)2SO4");
        check(counts.count("N") == 1 && counts.at("N") == 4,
              "and four ammonium ions, two per formula unit");
        check(counts.count("Na") == 1 && counts.at("Na") == 3
                  && counts.count("Cl") == 1 && counts.at("Cl") == 3,
              "three Na+ and three Cl− from the sodium chloride");
        checkClose(result.netCharge, 0.0, 1e-9,
                   "the cell is charge neutral, as the salts are");

        bool everyIonPlaced = true;
        for (const auto& placement : result.placements)
            if (placement.key != "water"
                && placement.placed != placement.requested)
                everyIonPlaced = false;
        check(everyIonPlaced,
              "every requested ion is placed — ions go in before the solvent, "
              "so a saturated packing cannot change the stoichiometry");

        // The ions displace solvent rather than being added on top of it: the
        // density target covers the whole region, ions included.
        check(result.density < 1.2,
              "the ions count against the density target, not on top of it");
    }

    std::printf("Gas region, non-default axis:\n");
    {
        // N2 along a, by explicit count — the mode a gas actually needs, since
        // at its 1 bar density a cell this size holds a fraction of a
        // molecule. Also exercises an axis other than c, which is where an
        // index slip between "the axis" and "the lateral pair" would show.
        const Structure slab = aluminiumSlab();
        SolvationBuilder::Params params;
        params.axis = SolvationBuilder::Axis::A;
        params.regionThickness = 25.0;
        params.lateral[0] = 2; // b
        params.lateral[1] = 2; // c
        params.amount = SolvationBuilder::Amount::Count;
        params.moleculeCount = 12;
        params.components = {{"nitrogen", 1.0}};
        const auto result = SolvationBuilder::generate(slab, params);

        const auto counts = composition(result.structure);
        check(counts.count("N") == 1 && counts.at("N") == 24,
              "twelve N2 molecules were placed, by count rather than density");
        check(result.warnings.empty(),
              "with room to spare, so nothing is reported as saturated");

        // The slab is 4.05 Å thick along a (two atoms per 4.05 Å cell edge at
        // x = 0 and x = 2.025), so the a-height must come out at that plus the
        // region.
        double lowest = 1e9;
        double highest = -1e9;
        for (const Atom& atom : slab.atoms()) {
            lowest = std::min(lowest, atom.position.x);
            highest = std::max(highest, atom.position.x);
        }
        checkClose(perpendicularHeight(result.structure, 0),
                   (highest - lowest) + 25.0, 1e-6,
                   "the region is opened along a, not along c");
        checkClose(perpendicularHeight(result.structure, 1), 2 * 4.05, 1e-6,
                   "and b is the one that got replicated");
    }

    std::printf("Mixtures and refusals:\n");
    {
        const Structure slab = aluminiumSlab();
        SolvationBuilder::Params params;
        params.regionThickness = 20.0;
        params.lateral[0] = 3;
        params.lateral[1] = 3;
        params.amount = SolvationBuilder::Amount::Count;
        params.moleculeCount = 40;
        // 3:1 by mole fraction — the apportionment must land exactly on the
        // requested total, not drift by a molecule per component.
        params.components = {{"water", 3.0}, {"ammonia", 1.0}};
        const auto result = SolvationBuilder::generate(slab, params);
        int water = 0;
        int ammonia = 0;
        for (const auto& placement : result.placements) {
            if (placement.key == "water")
                water = placement.requested;
            if (placement.key == "ammonia")
                ammonia = placement.requested;
        }
        check(water == 30 && ammonia == 10,
              "a 3:1 mixture of 40 molecules is 30 water and 10 ammonia");
        check(water + ammonia == params.moleculeCount,
              "and the parts sum to the requested total exactly");
    }
    {
        // The refusals. Each of these would otherwise produce a structure
        // that is wrong rather than an error the user can act on.
        Structure noCell;
        Atom atom;
        atom.atomicNumber = 6;
        noCell.addAtom(atom);
        SolvationBuilder::Params params;
        bool threw = false;
        try {
            SolvationBuilder::generate(noCell, params);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "a structure with no cell is refused, not guessed at");

        threw = false;
        params = SolvationBuilder::Params{};
        params.regionThickness = 3.0;
        params.surfaceClearance = 2.2; // 2 x 2.2 > 3.0
        try {
            SolvationBuilder::generate(aluminiumSlab(), params);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw,
              "a clearance that consumes the whole region is refused");

        threw = false;
        params = SolvationBuilder::Params{};
        params.components = {{"nacl", 1.0}};
        try {
            SolvationBuilder::generate(aluminiumSlab(), params);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw,
              "a salt offered as a solvent is refused — salts are inserted by "
              "formula unit");
    }

    if (failures == 0)
        std::printf("\nAll solvation-builder checks passed.\n");
    else
        std::printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
