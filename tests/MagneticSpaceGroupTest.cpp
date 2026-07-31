// Magnetic space group determination.
//
// One cell, three magnetic configurations, three different answers — which is
// the whole point of the module: the crystallography is identical in all three
// and only the moments distinguish them. The reference is the
// Belov-Neronova-Smirnova classification as laid out in Watanabe, Po &
// Vishwanath, Sci. Adv. 4, eaat8685 (2018).
//
// The cell is the CsCl-shaped cube: Fe at (0,0,0) and (1/2,1/2,1/2), a = 2.87 A.
// Geometrically that is bcc, space group Im-3m (229), body-centring translation
// (1/2,1/2,1/2) included.
//
//   moments 0, 0     -> the centring translation is an ordinary symmetry and
//                       time reversal is a symmetry on its own. Grey group,
//                       BNS TYPE II, parent 229.
//   moments +m, +m   -> time reversal is broken outright and nothing needs it.
//                       BNS TYPE I, parent 229 (still bcc: the two sites remain
//                       equivalent).
//   moments +m, -m   -> the centring translation now maps a moment onto its own
//                       reverse, so it survives ONLY combined with time
//                       reversal: an ANTI-TRANSLATION. BNS TYPE IV, and the
//                       unitary group drops to the simple cubic Pm-3m (221).
//
// The last case is the one worth having a test for: the type IV / type III
// distinction is exactly whether the halving operation is a pure translation,
// and getting it wrong would misreport a two-sublattice antiferromagnet as a
// symmetry-lowered ferromagnet.
//
// GUI-free apart from the embedded interpreter; requires ASE + spglib.

#include "core/Structure.hpp"
#include "core/UnitCell.hpp"
#include "python_bridge/MagneticSpaceGroup.hpp"
#include "python_bridge/PythonEngine.hpp"

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

core::Structure bccIron()
{
    constexpr double a = 2.87;
    core::Structure structure;
    core::UnitCell cell;
    cell.setVectors({core::Vec3{a, 0.0, 0.0}, core::Vec3{0.0, a, 0.0},
                     core::Vec3{0.0, 0.0, a}});
    cell.setPbc({true, true, true});
    structure.setCell(cell);

    core::Atom corner;
    corner.atomicNumber = 26; // Fe
    corner.position = {0.0, 0.0, 0.0};
    structure.addAtom(corner);

    core::Atom body = corner;
    body.position = {0.5 * a, 0.5 * a, 0.5 * a};
    structure.addAtom(body);
    return structure;
}

using MSG = pybridge::MagneticSpaceGroup;

MSG::Result run(const core::Structure& structure, double m0, double m1)
{
    const std::vector<core::Vec3> moments = {core::Vec3{0.0, 0.0, m0},
                                             core::Vec3{0.0, 0.0, m1}};
    return MSG::analyze(structure, moments, /*symprec=*/1e-4,
                        /*magSymprec=*/1e-3);
}

} // namespace

int main()
{
    pybridge::PythonEngine python;
    if (!python.aseAvailable()) {
        std::fprintf(stderr, "FAIL: ASE not importable\n");
        return EXIT_FAILURE;
    }

    const core::Structure structure = bccIron();

    // --- non-magnetic: the grey group ------------------------------------
    std::printf("Zero moments (grey group):\n");
    {
        const MSG::Result result = run(structure, 0.0, 0.0);
        if (!result.error.empty()) {
            // spglib without the magnetic tables is a skip, not a failure —
            // the same convention the network-dependent tests use.
            std::fprintf(stderr, "SKIP: %s\n", result.error.c_str());
            return 77;
        }
        check(result.type == 2, "BNS type II (time reversal alone survives)");
        check(result.parentNumber == 229,
              "parent space group Im-3m (229) — the bcc lattice");
        check(result.crystalSpaceGroupNumber == 229,
              "and the crystallography agrees, moments ignored");
        check(result.ordering == "Non-magnetic", "reported as non-magnetic");
        // A grey group is the parent doubled: every operation appears twice,
        // once plain and once with time reversal.
        check(result.antiunitaryOperations == result.unitaryOperations
                  && result.unitaryOperations > 0,
              "the group is the parent doubled by time reversal");
        check(!result.hasAntiTranslation,
              "no anti-translation: T is a symmetry on its own, not paired "
              "with a translation");
    }

    // --- ferromagnetic: type I -------------------------------------------
    std::printf("Parallel moments (ferromagnet):\n");
    {
        const MSG::Result result = run(structure, 2.2, 2.2);
        check(result.error.empty(), "determination succeeded");
        check(result.type == 1,
              "BNS type I — every operation is unitary, time reversal is "
              "broken outright");
        check(result.antiunitaryOperations == 0,
              "and there are no antiunitary operations at all");
        check(result.parentNumber == 229,
              "the two sites stay equivalent, so the parent is still Im-3m");
        check(result.ordering == "Ferromagnetic", "reported as ferromagnetic");
        check(std::abs(result.totalMoment - 4.4) < 1e-6,
              "net moment 4.4 muB per cell");
        check(result.uniqueSites == 1,
              "one magnetic sublattice");
        check(!result.hasAntiTranslation, "no anti-translation");
    }

    // --- antiferromagnetic: type IV, the anti-translation -----------------
    std::printf("Antiparallel moments (two-sublattice antiferromagnet):\n");
    {
        const MSG::Result result = run(structure, 2.2, -2.2);
        check(result.error.empty(), "determination succeeded");
        check(result.type == 4,
              "BNS type IV — the halving operation IS a pure translation");
        check(result.bnsNumber == "221.97",
              std::string("BNS label 221.97, got '") + result.bnsNumber + "'");
        // The centring translation is gone from the UNITARY group: what is
        // left is simple cubic. This is the number that says the magnetic cell
        // is a supercell of the crystallographic one.
        check(result.parentNumber == 221,
              "the unitary subgroup is the simple-cubic Pm-3m (221), not the "
              "bcc Im-3m the geometry alone gives");
        check(result.crystalSpaceGroupNumber == 229,
              "while the crystallography, moments ignored, is still Im-3m "
              "(229) — the magnetic order is what lowered it");
        check(result.hasAntiTranslation, "an anti-translation is reported");
        if (result.hasAntiTranslation) {
            bool bodyCentre = true;
            for (const double component : result.antiTranslation)
                bodyCentre = bodyCentre && std::abs(component - 0.5) < 1e-6;
            check(bodyCentre,
                  "and it is the body-centring translation (1/2, 1/2, 1/2)");
        }
        check(result.ordering == "Antiferromagnetic",
              "reported as antiferromagnetic");
        check(std::abs(result.totalMoment) < 1e-6, "with zero net moment");
        check(std::abs(result.absoluteMoment - 4.4) < 1e-6,
              "but 4.4 muB of local moment");
        check(result.antiunitaryOperations > 0
                  && result.unitaryOperations > 0,
              "the group has both unitary and antiunitary halves");
        check(result.uniqueSites == 1,
              "the two sites are still related — by an ANTIunitary operation");
    }

    // --- non-collinear: the moments are axial vectors ---------------------
    //
    // A canted pair is not the collinear problem with two extra zeros: spglib
    // treats rank-1 moments as axial vectors that rotate with the operations,
    // and feeding an (N, 3) array is what selects that treatment. This pins
    // that the bridge routes it there rather than silently flattening to z.
    std::printf("Canted moments (non-collinear):\n");
    {
        const std::vector<core::Vec3> canted = {core::Vec3{2.2, 0.0, 0.0},
                                                core::Vec3{0.0, 2.2, 0.0}};
        const MSG::Result result =
            MSG::analyze(structure, canted, 1e-4, 1e-3);
        check(result.error.empty(), "determination succeeded");
        check(!result.collinear,
              "recognized as non-collinear, not flattened onto z");
        // Two moments at 90 degrees: the net is sqrt(2) times one of them.
        check(std::abs(result.totalMoment - 2.2 * std::sqrt(2.0)) < 1e-6,
              "net moment is the vector sum, not the scalar one");
        check(result.unitaryOperations < 96,
              "and the cubic symmetry is broken well below the 96 operations "
              "of the grey bcc group");
    }

    std::printf(failures == 0
                    ? "\nAll magnetic space group checks passed.\n"
                    : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
