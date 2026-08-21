#pragma once

#include "core/Structure.hpp"

#include <array>
#include <vector>

namespace calango::core {

/// One detected six-membered carbon ring — a hexagonal face of the honeycomb
/// lattice, found from bond topology alone (chordless six-cycles in the
/// carbon-carbon bond graph, periodic images included). Classified against
/// GrapheneOxideBuilder's own per-carbon functional-group labelling
/// (GrapheneOxideBuilder::functionalGroupLabels()) — ring/domain analysis
/// does not re-derive sp2 vs sp3 by a second method.
struct CarbonRing {
    /// The six atom indices, in cyclic bonded order (consecutive entries —
    /// including index 5 back to index 0 — are bonded carbons).
    std::array<int, 6> atoms{};
    /// True when none of the six carbons carries an epoxide, hydroxyl,
    /// carbonyl or carboxyl group (GrapheneOxideBuilder::
    /// functionalGroupLabels() == -1 for all six).
    bool intact = false;
    /// Index into RingPercolationResult::domains, or -1 for a disrupted
    /// ring (disrupted rings are never assigned to a domain).
    int domain = -1;
};

/// One connected component of intact rings joined edge-to-edge by a shared
/// sp2 C-C bond — a contiguous sp2 domain.
struct SP2Domain {
    /// Indices into RingPercolationResult::rings.
    std::vector<int> rings;
    /// True along axis a/b/c when the domain reaches around the periodic
    /// cell in that direction: two ring-to-ring paths within the domain
    /// reach the same atom by a net non-zero lattice translation along
    /// that axis. Always false along a non-periodic axis.
    std::array<bool, 3> percolates{false, false, false};
};

struct RingPercolationResult {
    std::vector<CarbonRing> rings;
    std::vector<SP2Domain> domains;

    /// Intact rings / all six-membered rings found. 0 when no ring closes
    /// at all (e.g. no carbon framework, or a flake too small to complete
    /// one hexagon).
    double intactRingFraction = 0.0;
    /// Carbon atoms carrying no functional group / all carbon atoms in the
    /// structure. Computed independently of ring membership, so an edge
    /// carbon with a dangling valence but no attached group still counts
    /// as sp2.
    double sp2CarbonFraction = 0.0;

    /// Index into domains with the most rings, or -1 if domains is empty.
    int largestDomain = -1;
    /// True along an axis if ANY domain percolates it (usually, but not
    /// necessarily, the largest one — see domains[largestDomain].percolates
    /// for that domain specifically).
    std::array<bool, 3> percolatesAxis{false, false, false};
    /// Which axes the structure is actually periodic along
    /// (Structure::cell().pbc()), reported so callers don't re-derive it —
    /// an axis with no periodicity can never percolate.
    std::array<bool, 3> periodicAxis{false, false, false};
};

/// Finds every chordless six-membered ring in the carbon framework
/// (including rings that close through a periodic bond), classifies each
/// intact/disrupted from GrapheneOxideBuilder's own functional-group
/// labelling, and groups intact rings into periodicity-aware connected sp2
/// domains — the graph of intact rings joined by a shared C-C bond.
RingPercolationResult analyzeRingPercolation(const Structure& structure);

/// analyzeRingPercolation() applied to each frame of a trajectory, in
/// order — the oxidation-vs-conductivity time evolution (intact-ring
/// fraction, largest-domain span) a caller plots is just this result read
/// frame by frame.
std::vector<RingPercolationResult>
analyzeRingPercolationTrajectory(const std::vector<Structure>& frames);

} // namespace calango::core
