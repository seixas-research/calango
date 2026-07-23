#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace calango::core {

/// One relaxed configuration placed on a formation-energy diagram.
struct HullPoint {
    /// Composition coordinate, normally the fraction of the substituting
    /// species on the active sublattice. Any monotone scalar works.
    double concentration = 0.0;
    /// Formation energy per atom (eV/atom), relative to the endpoint
    /// references. Zero at the endpoints by construction.
    double formationEnergy = 0.0;
    /// Total energy per atom as computed (eV/atom) — kept so the diagram can
    /// show absolute numbers alongside the derived quantity.
    double energyPerAtom = 0.0;
    std::string label;      ///< e.g. "frame 12  Ag3Au1"
    int frameIndex = -1;    ///< index in the source trajectory
    std::size_t atomCount = 0;

    // -- Filled in by computeConvexHull ------------------------------------
    /// On the lower hull (thermodynamically stable against decomposition
    /// into the neighboring stable phases).
    bool onHull = false;
    /// Vertical distance above the hull (eV/atom); 0 for hull vertices.
    /// This is the standard "energy above hull" stability metric.
    double energyAboveHull = 0.0;
};

/// Lower convex hull of a formation-energy vs concentration diagram.
///
/// Only the *lower* hull is physically meaningful: a configuration is stable
/// exactly when no linear combination of two other compositions reaches the
/// same concentration at lower energy. Points above that envelope decompose
/// into the neighbouring stable phases, and their vertical distance to it
/// (energyAboveHull) is how far they are from stability.
struct ConvexHullResult {
    /// Every input point, in the order given, annotated with onHull and
    /// energyAboveHull.
    std::vector<HullPoint> points;
    /// Indices into `points` forming the hull, ordered by increasing
    /// concentration. Consecutive pairs are the tie-lines to draw.
    std::vector<std::size_t> hullIndices;
};

/// Annotate `points` with hull membership and energy above hull.
///
/// Degenerate inputs are handled rather than rejected: fewer than two
/// distinct concentrations yields every point flagged onHull only if it is
/// the minimum at its concentration (there is no tie-line to sit above), and
/// duplicate concentrations keep only the lowest-energy representative as a
/// hull vertex. NaN/infinite energies are dropped from hull construction but
/// preserved in the returned points (flagged off-hull) so the caller can
/// still show the failed configuration.
ConvexHullResult computeConvexHull(std::vector<HullPoint> points);

/// Formation energy per atom for a configuration, given the per-atom
/// reference energies of the pure endpoints:
///
///   E_form = E/N − [(1 − x)·μ_A + x·μ_B]
///
/// with x the concentration coordinate. Both references are per atom of the
/// corresponding pure phase, which is what makes E_form vanish at x = 0 and
/// x = 1 and keeps the diagram's endpoints pinned to zero.
double formationEnergyPerAtom(double energyPerAtom, double concentration,
                              double referenceA, double referenceB);

} // namespace calango::core
