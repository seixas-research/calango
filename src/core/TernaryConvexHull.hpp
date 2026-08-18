#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace calango::core {

/// One relaxed configuration on a ternary formation-energy diagram — the
/// 2D-composition generalization of core::HullPoint (see ConvexHull.hpp).
struct TernaryHullPoint {
    /// Composition coordinates of species B and C on the composition
    /// triangle (species A is implicit: x_A = 1 - x_B - x_C). Same
    /// barycentric convention core::TernaryIsothermalSection and
    /// gui::TernarySectionWidget already use for CALPHAD sections.
    double xB = 0.0;
    double xC = 0.0;
    /// Formation energy per atom (eV/atom), relative to the three pure
    /// endpoints. Zero at the three corners by construction.
    double formationEnergy = 0.0;
    std::string label;
    int frameIndex = -1; ///< index in the source config/trajectory list

    // -- Filled in by computeTernaryConvexHull -------------------------------
    bool onHull = false;
    /// Vertical distance above the hull (eV/atom); 0 for hull vertices, and
    /// left at 0 for a point whose composition falls outside every found
    /// facet (see computeTernaryConvexHull's doc comment) rather than a
    /// value that would misreport confidence the geometry does not have.
    double energyAboveHull = 0.0;
};

struct TernaryConvexHullResult {
    /// Every input point, in the order given, annotated with onHull and
    /// energyAboveHull.
    std::vector<TernaryHullPoint> points;
    /// Lower-hull FACETS, each three indices into `points` (a triangle on
    /// the composition simplex). A ground state is any point index that
    /// appears in at least one facet — draw these as the wireframe over the
    /// formation-energy surface.
    std::vector<std::array<std::size_t, 3>> facets;
};

/// Annotate `points` with lower-hull membership on the composition triangle.
///
/// Exact ground-state search over a full enumeration is a 3D incremental
/// convex hull — real computational-geometry machinery this module
/// deliberately does not implement. Instead: points are first reduced to the
/// single lowest-energy representative per composition BIN (any other point
/// near the same composition is provably not a ground state once a strictly
/// lower one exists there), then every triple of the reduced candidate set
/// is tested directly against the definition of a lower-hull facet — it
/// qualifies iff every OTHER candidate lies on or above the plane through
/// it. This is exact with respect to the reduced candidate set (a witness
/// too far inside its own bin to be tested against a near-boundary facet is
/// the one approximation, and shrinks with the bin resolution) and costs
/// O(candidates^4) in the worst case, which is why `maxCandidates` bounds
/// it — bins are coarsened automatically to stay under the cap.
///
/// NaN/infinite energies are dropped from hull construction but preserved in
/// the returned points (flagged off-hull), matching computeConvexHull()'s
/// convention for the binary case this generalizes.
TernaryConvexHullResult computeTernaryConvexHull(
    std::vector<TernaryHullPoint> points, int maxCandidates = 120);

/// Ternary formation energy per atom, given the three pure-endpoint per-atom
/// reference energies. Generalizes formationEnergyPerAtom() (ConvexHull.hpp):
///
///   E_form = E/N − [(1 − x_B − x_C)·μ_A + x_B·μ_B + x_C·μ_C]
double ternaryFormationEnergyPerAtom(double energyPerAtom, double xB, double xC,
                                     double referenceA, double referenceB,
                                     double referenceC);

} // namespace calango::core
