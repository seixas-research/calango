#pragma once

#include <string>

namespace calango::core {

/// Which invariant a topology run reports.
enum class TopologicalInvariant {
    /// Chern number C — the winding of the summed hybrid Wannier centres
    /// across a full reciprocal period. Defined WITHOUT time-reversal
    /// symmetry, and non-zero only when T is broken (a magnetic system, or
    /// one with spin-orbit coupling and magnetic order).
    Chern,
    /// Z₂ index ν — the Kane-Mele invariant, from how the hybrid Wannier
    /// centres of a Kramers pair switch partners across HALF the zone.
    /// Requires time-reversal symmetry; it is not defined without it.
    Z2,
    /// Both, on the same transported states.
    Both,
};

/// Parameters for a topological-invariant calculation built on the hybrid
/// Wannier centre (Wilson loop) flow.
///
/// Both invariants come from the same object: the Berry phases of the occupied
/// manifold accumulated along one reciprocal direction, resolved as a function
/// of the perpendicular k. Those phases ARE the hybrid Wannier centres, and
/// their flow across the zone is what is topological — an integer that no
/// smooth deformation of the Hamiltonian can change without closing the gap.
///
/// The distinction between the two invariants is what is counted:
///   • Chern counts the NET winding of the summed centres over a full period;
///   • Z₂ counts, modulo 2, how many times the largest gap between centres is
///     crossed over half a period — the partner-switching of Kramers pairs.
struct TopologyConfig {
    /// ABSOLUTE path to the MLWF job directory, for the wavefunctions.
    std::string mlwfDir;

    TopologicalInvariant invariant = TopologicalInvariant::Both;

    /// Reciprocal direction the Berry phase is accumulated ALONG (0/1/2). The
    /// flow is then resolved against the remaining two.
    int direction = 2;

    /// Number of occupied bands entering the manifold. 0 derives it from the
    /// electron count, which is right whenever the gap is at the nominal
    /// filling — and wrong, loudly, when it is not.
    int occupiedBands = 0;

    /// Spin-orbit coupling. Z₂ is meaningless without it for most candidate
    /// materials: SOC is what opens the inverted gap that makes the phase
    /// non-trivial in the first place.
    bool spinOrbit = true;

    /// Samples along the flow coordinate.
    int loopSamples = 51;
};

/// Turns a TopologyConfig into a standalone GPAW script that computes the
/// hybrid Wannier centre flow by parallel transport, derives the requested
/// invariants from it, and writes `topology.json` (the flow itself plus the
/// integers) for the viewer.
std::string generateTopologyScript(const TopologyConfig& cfg);

} // namespace calango::core
