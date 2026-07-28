// Whole-structure transformation test.
//
// Centring, vacuum padding and wrapping moved out of the Edit Structure dialog
// and onto the Structure panel, which means they now mutate the DOCUMENT
// through the undo stack instead of a dialog's throwaway working copy. A wrong
// result used to be discardable with Cancel; now it is a committed edit, so the
// arithmetic is worth pinning.
//
// Each of these is silently wrong-able: a vacuum layer added along the wrong
// lattice vector still produces a bigger cell, and a wrap that folds a
// non-periodic axis still puts every atom "inside the box" — it just drags the
// slab through its own vacuum on the way.
//
// GUI-free, Qt-free apart from nothing at all.

#include "core/StructureTransforms.hpp"

#include "core/Structure.hpp"
#include "core/UnitCell.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace calango;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

bool near(double a, double b, double tolerance = 1e-9)
{
    return std::abs(a - b) < tolerance;
}

core::Structure boxWithAtoms(const std::vector<core::Vec3>& positions,
                             double side = 10.0,
                             std::array<bool, 3> pbc = {true, true, true})
{
    core::Structure structure;
    structure.setCell(core::UnitCell({side, 0, 0}, {0, side, 0}, {0, 0, side},
                                     pbc));
    for (const core::Vec3& position : positions) {
        core::Atom atom;
        atom.atomicNumber = 6;
        atom.position = position;
        structure.addAtom(atom);
    }
    return structure;
}

} // namespace

int main()
{
    std::printf("Centring:\n");
    {
        // Two atoms with centroid at (1, 1, 1) in a 10 Å box: centring must
        // put the centroid at (5, 5, 5) and move both by the same vector.
        core::Structure s = boxWithAtoms({{0, 0, 0}, {2, 2, 2}});
        core::centerInCell(s);
        const core::Vec3 centroid = s.centroid();
        check(near(centroid.x, 5.0) && near(centroid.y, 5.0)
                  && near(centroid.z, 5.0),
              "the centroid lands at the centre of the cell");
        // A rigid translation: the separation must be untouched.
        const core::Vec3 d = s.atoms()[1].position - s.atoms()[0].position;
        check(near(d.x, 2.0) && near(d.y, 2.0) && near(d.z, 2.0),
              "and the atoms keep their separation");
    }
    {
        core::Structure molecule;
        core::Atom atom;
        atom.atomicNumber = 6;
        molecule.addAtom(atom);
        core::centerInCell(molecule); // no cell: must not move anything
        check(near(molecule.atoms()[0].position.x, 0.0),
              "a structure with no cell is left alone");
    }

    std::printf("Vacuum padding:\n");
    {
        // 10 Å box, atoms spanning z in [2, 4]; add 10 Å along c, split evenly.
        core::Structure s = boxWithAtoms({{5, 5, 2}, {5, 5, 4}});
        core::VacuumOptions options;
        options.thickness = 10.0;
        options.axes[0] = options.axes[1] = false;
        options.axes[2] = true;
        check(core::addVacuum(s, options), "reports success");

        const auto& v = s.cell().vectors();
        check(near(v[2].norm(), 20.0), "the c axis grew by the thickness");
        check(near(v[0].norm(), 10.0) && near(v[1].norm(), 10.0),
              "and the untouched axes did not");
        check(!s.cell().pbc()[2] && s.cell().pbc()[0] && s.cell().pbc()[1],
              "only the padded direction is marked non-periodic");

        // Split evenly: the 2 Å-thick slab should sit centred in 20 Å, i.e.
        // 9 Å of vacuum below it and 9 Å above.
        const double lo = std::min(s.atoms()[0].position.z, s.atoms()[1].position.z);
        const double hi = std::max(s.atoms()[0].position.z, s.atoms()[1].position.z);
        check(near(lo, 9.0) && near(hi, 11.0),
              "and the structure is centred in the enlarged cell");
    }
    {
        // One-sided: the cell grows but the atoms do not move.
        core::Structure s = boxWithAtoms({{5, 5, 2}, {5, 5, 4}});
        core::VacuumOptions options;
        options.thickness = 6.0;
        options.axes[0] = options.axes[1] = false;
        options.axes[2] = true;
        options.bothSides = false;
        core::addVacuum(s, options);
        check(near(s.atoms()[0].position.z, 2.0)
                  && near(s.atoms()[1].position.z, 4.0),
              "one-sided padding leaves the atoms where they were");
        check(near(s.cell().vectors()[2].norm(), 16.0),
              "and still grows the cell");
    }
    {
        core::Structure s = boxWithAtoms({{1, 1, 1}});
        core::VacuumOptions none;
        none.axes[0] = none.axes[1] = none.axes[2] = false;
        check(!core::addVacuum(s, none),
              "no direction selected is refused rather than silently ignored");
        core::VacuumOptions negative;
        negative.thickness = -5.0;
        check(!core::addVacuum(s, negative), "a non-positive thickness is refused");
    }

    std::printf("Wrapping:\n");
    {
        // Atoms outside the box on both sides, plus one already inside.
        core::Structure s = boxWithAtoms({{12.0, 3.0, 3.0},
                                          {-2.0, 3.0, 3.0},
                                          {5.0, 5.0, 5.0}});
        const int moved = core::wrapIntoCell(s, {});
        check(moved == 2, "only the atoms actually outside are counted as moved");
        check(near(s.atoms()[0].position.x, 2.0),
              "an atom past the far face folds back to the near side");
        check(near(s.atoms()[1].position.x, 8.0),
              "and one before the origin folds to the far side");
        check(near(s.atoms()[2].position.x, 5.0),
              "an atom already inside is untouched");
    }
    {
        // A slab: c is non-periodic, so wrapping must NOT fold the vacuum
        // direction — doing so would drag the slab through its own vacuum.
        core::Structure s = boxWithAtoms({{12.0, 3.0, 14.0}}, 10.0,
                                         {true, true, false});
        core::wrapIntoCell(s, {});
        check(near(s.atoms()[0].position.x, 2.0),
              "the periodic axis still wraps");
        check(near(s.atoms()[0].position.z, 14.0),
              "and the non-periodic axis is left alone");
    }
    {
        // Selection-limited: only the named atom moves.
        core::Structure s = boxWithAtoms({{12.0, 3.0, 3.0}, {13.0, 3.0, 3.0}});
        const int moved = core::wrapIntoCell(s, {1});
        check(moved == 1, "an explicit selection wraps only those atoms");
        check(near(s.atoms()[0].position.x, 12.0), "the unselected atom stays put");
        check(near(s.atoms()[1].position.x, 3.0), "the selected one wraps");
    }
    {
        core::Structure s = boxWithAtoms({{5, 5, 5}});
        check(core::wrapIntoCell(s, {}) == 0,
              "a structure already inside reports nothing moved");
        // Out-of-range indices must be skipped, not indexed into.
        check(core::wrapIntoCell(s, {99}) == 0,
              "an out-of-range index is skipped rather than dereferenced");
    }
    {
        core::Structure molecule;
        core::Atom atom;
        atom.atomicNumber = 6;
        atom.position = {50.0, 0.0, 0.0};
        molecule.addAtom(atom);
        check(core::wrapIntoCell(molecule, {}) == 0,
              "a structure with no cell has nothing to wrap into");
        check(near(molecule.atoms()[0].position.x, 50.0),
              "and its atom is not moved");
    }

    // Reordering: the Edit Structure sort renumbers the atoms, and everything
    // index-aligned has to travel with them. A sort that moved only the atoms
    // would attach the wrong forces to the wrong sites and re-point a double
    // bond at two other atoms, silently and in a form that looks fine.
    std::printf("Structure::reorder\n");
    {
        core::Structure s;
        for (const int z : {8, 1, 6, 7}) { // O, H, C, N
            core::Atom atom;
            atom.atomicNumber = z;
            atom.position = {static_cast<double>(z), 0.0, 0.0};
            s.addAtom(atom);
        }
        s.setScalarField("charge", {-0.8, 0.4, 0.1, 0.3});
        s.setVectorField("forces", {{1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 0}});
        s.setBondOrder(0, 2, 2); // O=C
        s.setBondOrder(1, 3, 3); // H#N (nonsense chemistry, useful key)

        // Sort by atomic number ascending: H(1) C(6) N(7) O(8),
        // i.e. old indices 1, 2, 3, 0.
        s.reorder({1, 2, 3, 0});

        check(s.atoms()[0].atomicNumber == 1 && s.atoms()[1].atomicNumber == 6
                  && s.atoms()[2].atomicNumber == 7
                  && s.atoms()[3].atomicNumber == 8,
              "atoms end up in the requested order");
        check(near(s.atoms()[0].position.x, 1.0)
                  && near(s.atoms()[3].position.x, 8.0),
              "positions travel with their atoms");
        const auto& charge = s.scalarFields().at("charge");
        check(near(charge[0], 0.4) && near(charge[3], -0.8),
              "scalar fields are permuted, not left behind");
        const auto& forces = s.vectorFields().at("forces");
        check(near(forces[0].x, 2.0) && near(forces[3].x, 1.0),
              "vector fields too");
        // O was 0 -> 3 and C was 2 -> 1, so the O=C double bond is now (1, 3).
        check(s.bondOrder(1, 3) == 2,
              "a bond order follows its two atoms to their new indices");
        // H was 1 -> 0 and N was 3 -> 2.
        check(s.bondOrder(0, 2) == 3, "and so does the other one");
        check(s.bondOrder(3, 1) == 2,
              "the key stays canonical, so lookups either way round find it");
    }
    {
        // A rejected permutation must change nothing at all: half-applying one
        // scrambles exactly the alignment this function exists to preserve.
        core::Structure s = boxWithAtoms({{1, 1, 1}, {2, 2, 2}});
        s.setScalarField("charge", {1.0, 2.0});
        s.reorder({1, 1});          // not a permutation — repeats an index
        check(near(s.atoms()[0].position.x, 1.0)
                  && near(s.scalarFields().at("charge")[0], 1.0),
              "a repeated index is rejected rather than applied");
        s.reorder({0});             // wrong length
        check(s.size() == 2 && near(s.atoms()[1].position.x, 2.0),
              "a short permutation is rejected too");
        s.reorder({0, 7});          // out of range
        check(near(s.atoms()[1].position.x, 2.0),
              "and an out-of-range one");
        s.reorder({1, 0});
        check(near(s.atoms()[0].position.x, 2.0)
                  && near(s.scalarFields().at("charge")[0], 2.0),
              "a valid swap still applies");
    }

    // Collinear magnetic moments arrive as ONE scalar per atom: the
    // calculation quantized the spin along z and reported only that
    // component. The vector overlay needs a direction, and testing only for a
    // vector field left "Magnetic moment" greyed out for every collinear
    // spin-polarized result.
    std::printf("Collinear moment promotion:\n");
    {
        calango::core::Structure s;
        s.addAtom({26, {0.0, 0.0, 0.0}});   // Fe
        s.addAtom({26, {1.0, 0.0, 0.0}});
        check(!s.hasVectorData("magmoms"), "no moments, no vector data");
        check(s.resolvedVectorField("magmoms").empty(), "and nothing to draw");

        // Antiferromagnetic: the sign is the whole point, and it must survive
        // as a direction rather than as a magnitude.
        s.setScalarField("magmoms", {2.2, -2.2});
        check(s.hasVectorData("magmoms"),
              "a scalar column IS drawable vector data");
        const auto v = s.resolvedVectorField("magmoms");
        check(v.size() == 2, "one vector per atom");
        check(near(v[0].x, 0.0) && near(v[0].y, 0.0) && near(v[0].z, 2.2),
              "spin up points along +z");
        check(near(v[1].z, -2.2), "spin down points along -z, not merely down");

        // An all-zero column is still data — the overlay must be offered, and
        // draw nothing, rather than report the frame as carrying no moments.
        calango::core::Structure zero;
        zero.addAtom({23, {0.0, 0.0, 0.0}});
        zero.setScalarField("magmoms", {0.0});
        check(zero.hasVectorData("magmoms"),
              "an all-zero moment column is still present");

        // Non-collinear supplies a real (N, 3) array, which must win.
        calango::core::Structure nc;
        nc.addAtom({26, {0.0, 0.0, 0.0}});
        nc.setScalarField("magmoms", {9.0});
        nc.setVectorField("magmoms", {{1.0, 0.0, 0.0}});
        const auto ncv = nc.resolvedVectorField("magmoms");
        check(ncv.size() == 1 && near(ncv[0].x, 1.0) && near(ncv[0].z, 0.0),
              "an explicit vector field is never overridden by the scalar");

        // A mismatched column is not per-atom data and must not be promoted.
        calango::core::Structure bad;
        bad.addAtom({26, {0.0, 0.0, 0.0}});
        bad.addAtom({26, {1.0, 0.0, 0.0}});
        check(!bad.hasVectorData("forces"), "an absent field stays absent");
    }

    std::printf(failures == 0 ? "\nAll structure-transform checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
