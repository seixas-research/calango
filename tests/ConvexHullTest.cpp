// Formation-energy convex hull used by the Cluster Expansion Calculation
// workflow. Pins the physics conventions (endpoints pinned at zero, only the
// LOWER hull is stable, energy-above-hull is the vertical tie-line distance)
// and the degenerate cases a real ensemble hits: duplicate concentrations,
// a single composition, and configurations whose relaxation failed.

#include "core/ConvexHull.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
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

void checkNear(double got, double want, double tol, const std::string& what)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  %s %s  (got %.6f, want %.6f)\n", ok ? "ok  " : "FAIL",
                what.c_str(), got, want);
    if (!ok)
        ++failures;
}

HullPoint at(double x, double eForm, int frame = -1)
{
    HullPoint p;
    p.concentration = x;
    p.formationEnergy = eForm;
    p.frameIndex = frame;
    return p;
}

} // namespace

int main()
{
    std::printf("Formation energy:\n");
    {
        // Endpoints must come out exactly zero, otherwise the whole diagram
        // is offset and "above hull" loses its meaning.
        checkNear(formationEnergyPerAtom(-3.0, 0.0, -3.0, -5.0), 0.0, 1e-12,
                  "pure A endpoint is zero");
        checkNear(formationEnergyPerAtom(-5.0, 1.0, -3.0, -5.0), 0.0, 1e-12,
                  "pure B endpoint is zero");
        // Ideal (non-interacting) mixing sits exactly on the tie-line.
        checkNear(formationEnergyPerAtom(-4.0, 0.5, -3.0, -5.0), 0.0, 1e-12,
                  "linear interpolation gives zero formation energy");
        // An ordered compound below the tie-line is negative (stable).
        checkNear(formationEnergyPerAtom(-4.2, 0.5, -3.0, -5.0), -0.2, 1e-12,
                  "compound below the tie-line is negative");
    }

    std::printf("Hull membership:\n");
    {
        // Classic pseudo-binary: two endpoints, one stable intermediate, one
        // metastable configuration above the hull.
        const auto result = computeConvexHull({
            at(0.00, 0.00, 0),   // endpoint
            at(0.25, -0.05, 1),  // stable
            at(0.50, -0.10, 2),  // stable (deepest)
            at(0.50, 0.05, 3),   // same composition, unstable polymorph
            at(0.75, 0.02, 4),   // above the 0.5 -> 1.0 tie-line
            at(1.00, 0.00, 5),   // endpoint
        });
        check(result.points[0].onHull, "x=0 endpoint on hull");
        check(result.points[1].onHull, "x=0.25 stable point on hull");
        check(result.points[2].onHull, "x=0.5 ground state on hull");
        check(!result.points[3].onHull,
              "higher-energy polymorph at the same x is NOT on hull");
        check(!result.points[4].onHull, "x=0.75 metastable point off hull");
        check(result.points[5].onHull, "x=1 endpoint on hull");
        check(result.hullIndices.size() == 4,
              "hull has 4 vertices (0, 0.25, 0.5, 1.0)");

        // Tie-line from (0.5, -0.10) to (1.0, 0.00): at x = 0.75 it sits at
        // -0.05, so a point at +0.02 is 0.07 eV/atom above the hull.
        checkNear(result.points[4].energyAboveHull, 0.07, 1e-9,
                  "energy above hull is the vertical tie-line distance");
        checkNear(result.points[3].energyAboveHull, 0.15, 1e-9,
                  "degenerate-x polymorph measured against the ground state");
        for (const auto& p : result.points) {
            if (p.onHull)
                checkNear(p.energyAboveHull, 0.0, 1e-12,
                          "hull vertices have zero energy above hull");
        }
    }

    std::printf("Only the LOWER hull is stable:\n");
    {
        // A point far ABOVE the endpoints must never be reported stable —
        // using a full (upper+lower) hull would wrongly include it.
        const auto result = computeConvexHull({
            at(0.0, 0.0, 0),
            at(0.5, 0.9, 1), // way above the tie-line
            at(1.0, 0.0, 2),
        });
        check(!result.points[1].onHull, "point above the tie-line is off hull");
        checkNear(result.points[1].energyAboveHull, 0.9, 1e-9,
                  "its distance above the hull is 0.9 eV/atom");
        check(result.hullIndices.size() == 2, "hull is just the two endpoints");
    }

    std::printf("Degenerate inputs:\n");
    {
        check(computeConvexHull({}).points.empty(), "empty input is handled");

        const auto single = computeConvexHull({at(0.4, -0.2, 0)});
        check(single.points[0].onHull, "a lone point is trivially on its hull");

        // One composition, several energies: only the minimum is stable.
        const auto sameX = computeConvexHull({
            at(0.5, -0.1, 0), at(0.5, 0.3, 1), at(0.5, -0.4, 2)});
        check(sameX.points[2].onHull, "lowest energy at a single x is on hull");
        check(!sameX.points[0].onHull && !sameX.points[1].onHull,
              "the others at that x are not");
        checkNear(sameX.points[1].energyAboveHull, 0.7, 1e-9,
                  "measured against that minimum");

        // A configuration whose relaxation diverged: keep it in the list but
        // never let a NaN corrupt the hull.
        auto withNan = computeConvexHull({
            at(0.0, 0.0, 0),
            at(0.5, std::numeric_limits<double>::quiet_NaN(), 1),
            at(1.0, 0.0, 2)});
        check(withNan.points.size() == 3, "failed configuration is preserved");
        check(!withNan.points[1].onHull, "NaN point is not on the hull");
        check(withNan.hullIndices.size() == 2, "hull ignores the NaN point");
        check(withNan.points[0].onHull && withNan.points[2].onHull,
              "the valid endpoints still form the hull");
    }

    std::printf(failures == 0 ? "\nAll convex-hull checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
