// Periodic boundary ghost duplication test.
//
// The rule is combinatorial: an atom on one cell face gets one duplicate, one
// on an edge gets three, one at the origin vertex gets seven (every non-empty
// subset of the axes it touches). Those counts are easy to get subtly wrong,
// and a wrong one shows up as a cell that looks almost right — a missing edge
// atom, or a doubled one at a corner.
//
// Also checks the property the whole feature rests on: duplication is a
// RENDERING concern and must never touch the structure.
//
// GUI-free, Python-free.

#include "render/StructureRenderer.hpp"

#include "core/Structure.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace calango;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

/// Cubic 10 A cell.
core::UnitCell cubicCell()
{
    return core::UnitCell({10, 0, 0}, {0, 10, 0}, {0, 0, 10});
}

std::size_t shiftCount(const core::Vec3& position, const core::UnitCell& cell)
{
    return render::StructureRenderer::boundaryGhostShifts(position, cell, 1e-3f)
        .size();
}

} // namespace

int main()
{
    const core::UnitCell cell = cubicCell();

    std::printf("Ghost counts by site type:\n");
    check(shiftCount({5.0, 5.0, 5.0}, cell) == 0,
          "an interior atom is not duplicated");
    check(shiftCount({0.0, 5.0, 5.0}, cell) == 1,
          "a face atom gets one duplicate");
    check(shiftCount({0.0, 0.0, 5.0}, cell) == 3,
          "an edge atom gets three (two faces + the edge itself)");
    check(shiftCount({0.0, 0.0, 0.0}, cell) == 7,
          "the origin vertex gets seven (all three faces, edges and the far "
          "corner)");
    check(shiftCount({10.0, 5.0, 5.0}, cell) == 0,
          "an atom already AT fractional 1 is its own duplicate and adds none");

    std::printf("Ghost placement:\n");
    {
        // A face atom's duplicate must land exactly one lattice vector away —
        // any drift and the two faces of the cell stop lining up.
        const auto shifts =
            render::StructureRenderer::boundaryGhostShifts({0.0, 3.0, 7.0}, cell, 1e-3f);
        check(shifts.size() == 1, "one shift for the x = 0 face");
        if (!shifts.empty()) {
            const core::Vec3& s = shifts.front();
            check(std::abs(s.x - 10.0) < 1e-9 && std::abs(s.y) < 1e-9
                      && std::abs(s.z) < 1e-9,
                  "the shift is exactly the a-axis lattice vector");
        }
    }
    {
        // Edge atom: the three shifts must be a, b and a+b — not three copies
        // of one axis, and not the body diagonal.
        const auto shifts =
            render::StructureRenderer::boundaryGhostShifts({0.0, 0.0, 4.0}, cell, 1e-3f);
        bool haveA = false, haveB = false, haveAB = false;
        for (const core::Vec3& s : shifts) {
            haveA = haveA || (std::abs(s.x - 10) < 1e-9 && std::abs(s.y) < 1e-9);
            haveB = haveB || (std::abs(s.y - 10) < 1e-9 && std::abs(s.x) < 1e-9);
            haveAB = haveAB
                || (std::abs(s.x - 10) < 1e-9 && std::abs(s.y - 10) < 1e-9);
            check(std::abs(s.z) < 1e-9, "no shift along the non-boundary axis");
        }
        check(haveA && haveB && haveAB, "the edge yields a, b and a+b");
    }

    std::printf("Non-orthogonal cell:\n");
    {
        // A sheared cell is where a per-axis-length shortcut would break: the
        // shift must be the LATTICE VECTOR, not a displacement along x.
        const core::UnitCell triclinic({10, 0, 0}, {3, 9, 0}, {2, 1, 8});
        const core::Vec3 origin{0, 0, 0};
        const auto shifts =
            render::StructureRenderer::boundaryGhostShifts(origin, triclinic, 1e-3f);
        check(shifts.size() == 7, "the origin still yields seven duplicates");
        bool foundB = false;
        for (const core::Vec3& s : shifts)
            if (std::abs(s.x - 3) < 1e-9 && std::abs(s.y - 9) < 1e-9
                && std::abs(s.z) < 1e-9)
                foundB = true;
        check(foundB, "the b-axis shift carries its shear component");
    }

    std::printf("Contracts:\n");
    {
        core::UnitCell none;
        check(shiftCount({0, 0, 0}, none) == 0,
              "a structure with no cell has no boundaries to duplicate onto");
    }
    {
        // The defining property: this is a rendering rule, so the structure it
        // is asked about must be untouched by asking.
        core::Structure s;
        s.setCell(cell);
        core::Atom atom;
        atom.atomicNumber = 6;
        atom.position = {0, 0, 0};
        s.addAtom(atom);
        const std::size_t before = s.size();
        const std::string formulaBefore = s.chemicalFormula();
        (void)render::StructureRenderer::boundaryGhostShifts(
            s.atoms()[0].position, s.cell(), 1e-3f);
        check(s.size() == before && s.chemicalFormula() == formulaBefore,
              "computing ghosts leaves the atom count and formula unchanged");
    }

    std::printf(failures == 0 ? "\nAll boundary ghost checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
