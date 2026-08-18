// Unit tests for the ternary composition-triangle lower convex hull
// (src/core/TernaryConvexHull).
//
// The test fixture is small enough to reason about by hand: three pure
// endpoints (A, B, C) at formation energy 0 by construction, one point deep
// in the interior that must pull the hull down around it, and one point
// well above the resulting surface that must NOT be a ground state. This is
// exactly the geometry a real ternary alloy with a stable ordered compound
// produces — a tetrahedron-like fan of three facets meeting at the compound
// — so the assertions below are closed-form (flat facets are trivially
// checked; the interior point's exact facet membership is not asserted,
// only that it is on-hull and every other interior point is not).
//
// Exit code 0 = pass.

#include "core/TernaryConvexHull.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

} // namespace

int main()
{
    using calango::core::computeTernaryConvexHull;
    using calango::core::TernaryHullPoint;
    using calango::core::ternaryFormationEnergyPerAtom;

    std::printf("Formation energy:\n");
    {
        // E_form = E/N - [xA*muA + xB*muB + xC*muC]; at a pure endpoint the
        // formula must give exactly 0 for the reference used AT that
        // endpoint, by construction.
        const double e = ternaryFormationEnergyPerAtom(
            /*energyPerAtom=*/-2.5, /*xB=*/0.0, /*xC=*/0.0,
            /*referenceA=*/-2.5, /*referenceB=*/-3.0, /*referenceC=*/-1.0);
        check(std::abs(e) < 1e-12, "pure A composition gives E_form = 0");
    }

    std::printf("A known small hull (3 corners + one deep interior point):\n");
    {
        std::vector<TernaryHullPoint> pts;
        // Corners, all at E_form = 0 by construction.
        pts.push_back({0.0, 0.0, 0.0, "A", 0});
        pts.push_back({1.0, 0.0, 0.0, "B", 1});
        pts.push_back({0.0, 1.0, 0.0, "C", 2});
        // Deep interior point: an ordered ABC compound, well below the
        // z=0 plane the three corners alone would define.
        pts.push_back({1.0 / 3.0, 1.0 / 3.0, -0.5, "ABC", 3});
        // Clearly above the resulting hull (positive formation energy: an
        // unstable, disordered-like point) — must not be a ground state.
        pts.push_back({0.5, 0.25, 0.2, "high", 4});
        // A second interior point that is ABOVE the tetrahedron-like fan
        // the deep point creates but would have been ON the flat z=0
        // hull had the deep point not existed — checks that adding one
        // deep point correctly excludes a point the flat-only hull would
        // have kept.
        pts.push_back({0.2, 0.2, 0.0, "would-be-flat", 5});

        const auto result = computeTernaryConvexHull(pts, /*maxCandidates=*/50);
        check(result.points.size() == pts.size(),
              "every input point is present in the output, annotated");

        check(result.points[0].onHull && result.points[1].onHull
                  && result.points[2].onHull,
              "the three pure endpoints are always ground states");
        check(result.points[3].onHull,
              "the deep interior point is a ground state");
        check(!result.points[4].onHull,
              "the clearly-high interior point is NOT a ground state");
        check(!result.points[5].onHull,
              "the point that would have been on a flat (corners-only) hull "
              "is excluded once the deep point pulls the surface down "
              "around it");

        check(result.points[0].energyAboveHull < 1e-9
                  && result.points[1].energyAboveHull < 1e-9
                  && result.points[2].energyAboveHull < 1e-9
                  && result.points[3].energyAboveHull < 1e-9,
              "every ground state has ~zero energy above its own hull");
        check(result.points[4].energyAboveHull > 0.05,
              "the clearly-high point has a substantial, positive energy "
              "above hull");
        check(result.points[5].energyAboveHull > 0.1,
              "the would-be-flat point sits well above the true (deep-point) "
              "hull, not at ~0 the way it would on the flat-only hull");

        check(!result.facets.empty(), "at least one lower-hull facet was found");
        for (const auto& facet : result.facets) {
            const bool touchesDeepPoint =
                facet[0] == 3 || facet[1] == 3 || facet[2] == 3;
            check(touchesDeepPoint,
                  "every facet touches the deep point — the tetrahedron-like "
                  "fan, not a flat triangulation of the three corners alone");
        }
    }

    std::printf("Degenerate inputs:\n");
    {
        // A single point: trivially its own ground state, no facet (a
        // facet needs three non-collinear points).
        std::vector<TernaryHullPoint> one = {{0.3, 0.3, -0.1, "solo", 0}};
        const auto result = computeTernaryConvexHull(one);
        check(result.facets.empty(),
              "a single point produces no facets (nothing to triangulate)");
        check(!result.points[0].onHull,
              "...and is therefore not flagged onHull either — a facet "
              "membership test with nothing to test against says nothing, "
              "not everything");

        // NaN energy: dropped from hull construction, preserved (off-hull)
        // in the output, matching the binary ConvexHull convention.
        std::vector<TernaryHullPoint> withNan = {
            {0.0, 0.0, 0.0, "A", 0},
            {1.0, 0.0, 0.0, "B", 1},
            {0.0, 1.0, 0.0, "C", 2},
            {0.3, 0.3, std::nan(""), "broken", 3},
        };
        const auto result2 = computeTernaryConvexHull(withNan);
        check(result2.points.size() == 4,
              "a NaN-energy point is preserved in the output...");
        check(!result2.points[3].onHull,
              "...but dropped from hull construction, not treated as -inf");
    }

    if (failures == 0) {
        std::printf("\nAll ternary convex hull checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d ternary convex hull check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
