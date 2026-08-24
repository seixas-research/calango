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

// ---------------------------------------------------------------------------
// pi percolation — the conjugated carbon network, without the ring criterion
// ---------------------------------------------------------------------------
//
// A SECOND, WEAKER criterion for the same physical question, and the
// difference between the two is the point of having both.
//
// analyzeRingPercolation() above asks whether intact BENZENE RINGS connect:
// every one of a hexagon's six carbons must be free of oxygen before the ring
// counts at all, and domains are rings sharing an edge. That is the right
// question for aromaticity, and it is a strict one — a single epoxide breaks
// three hexagons at once, so a lightly oxidized sheet can lose its intact-ring
// network long before it loses the ability to carry a current.
//
// analyzePiPercolation() asks the question conduction actually turns on:
// whether a CONNECTED PATH OF sp2 CARBONS crosses the cell. A carbon keeps its
// out-of-plane p_z orbital — and so contributes to the pi system — whenever it
// is still three-coordinate and carries no oxygen; two such carbons that are
// bonded have adjacent, overlapping p_z orbitals and are conjugated. No ring
// is required. A polyene chain threading between oxidized islands conducts and
// contains no intact hexagon at all, which is exactly the case the ring
// criterion scores as zero and this one does not.
//
// Both read the SAME classification (GrapheneOxideBuilder::
// functionalGroupLabels()); neither re-derives sp2 vs sp3 by a second method.

/// One connected component of the conjugated carbon network: sp2 carbons
/// joined by C-C bonds, with no ring requirement.
struct PiDomain {
    /// Atom indices, ascending.
    std::vector<int> atoms;
    /// True along axis a/b/c when the domain reaches around the periodic cell
    /// in that direction — the same winding-number test the ring analysis
    /// uses, applied to this graph. Always false along a non-periodic axis.
    std::array<bool, 3> percolates{false, false, false};
};

struct PiPercolationResult {
    /// Indices of the carbons carrying a pi orbital, ascending. See
    /// analyzePiPercolation() for the rule.
    std::vector<int> piCarbons;
    /// Per-atom domain index, -1 for an atom that is not a pi carbon.
    /// Index-aligned with the structure, so a caller colouring atoms does not
    /// have to build its own lookup.
    std::vector<int> atomDomain;
    std::vector<PiDomain> domains;

    /// pi carbons / all carbons. The direct analogue of
    /// RingPercolationResult::sp2CarbonFraction, and equal to it whenever
    /// every unoxidized carbon is three-coordinate — which is the normal
    /// case, and not the only one (a CH2 in a hydrogenated defect is
    /// unoxidized, four-coordinate and carries no pi orbital).
    double piCarbonFraction = 0.0;
    /// Atoms in the largest domain / all pi carbons. 1.0 means the whole
    /// conjugated network is one piece; a small value means it has been cut
    /// into islands, which is what oxidation does to conduction.
    double largestDomainFraction = 0.0;

    /// Index into domains with the most atoms, or -1 when there are none.
    int largestDomain = -1;
    /// True along an axis if ANY domain percolates it.
    std::array<bool, 3> percolatesAxis{false, false, false};
    /// Which axes the structure is periodic along, reported so callers do
    /// not re-derive it — a non-periodic axis can never percolate.
    std::array<bool, 3> periodicAxis{false, false, false};
};

/// The conjugated (pi) carbon network and whether it crosses the cell.
///
/// A carbon carries a pi orbital when BOTH hold:
///
///   * it carries no oxygen functional group — GrapheneOxideBuilder::
///     functionalGroupLabels() reports -1 for it. An epoxide, hydroxyl,
///     carbonyl or carboxyl carbon has rehybridized to sp3 and its p_z is
///     now a sigma bond to oxygen.
///   * it has at most three sigma neighbours, counting every bonded atom of
///     any element. Three is sp2 (the honeycomb interior, and an edge carbon
///     with a terminating hydrogen); four is sp3 and has no p_z left,
///     oxygen or no oxygen.
///
/// Two bonded pi carbons are conjugated, and the domains are the connected
/// components of that relation. Deliberately a CONSERVATIVE rule: it makes no
/// claim about bond alternation, planarity or aromaticity, only about which
/// carbons still have an orbital to conjugate with — which is what a
/// percolation question needs and all a bonding graph can honestly support.
PiPercolationResult analyzePiPercolation(const Structure& structure);

/// analyzePiPercolation() applied to each frame of a trajectory, in order.
std::vector<PiPercolationResult>
analyzePiPercolationTrajectory(const std::vector<Structure>& frames);

} // namespace calango::core
