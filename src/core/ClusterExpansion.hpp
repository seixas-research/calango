#pragma once

#include "core/Structure.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Inputs for the native cluster-expansion configuration generator.
struct ClusterExpansionOptions {
    /// Atomic number of the parent sublattice whose sites are substituted
    /// (the "active" Wyckoff sites — every atom of this species is active).
    int activeZ = 0;
    /// Substitution species (>= 2 atomic numbers) placed on the active sites.
    std::vector<int> speciesZ;

    int supercell[3] = {2, 2, 2};

    /// Cluster cutoff radii (Å). An order is enabled only when its cutoff is
    /// > 0; pairs/triplets/quadruplets are enumerated among active sites whose
    /// mutual minimum-image distances all fall within the cutoff.
    double pairCutoff = 0.0;
    double tripletCutoff = 0.0;
    double quadCutoff = 0.0;

    /// Cap on the number of symmetry-inequivalent configurations kept.
    int maxConfigs = 500;
    /// Cap on the number of decorations examined (the occupation space is
    /// exhaustively enumerated when it is smaller than this, else randomly
    /// sampled up to this many).
    long long maxEnumeration = 200000;
    /// Safety cap on the number of clusters enumerated per order.
    int maxClusters = 200000;

    /// Restrict to decorations with an exact per-species count (site-fraction
    /// fixed). When false, all compositions are explored.
    bool fixedComposition = false;
    std::vector<int> composition; ///< per-species counts (fixedComposition)

    unsigned seed = 42;
    double distanceTolerance = 0.05; ///< Å; groups clusters into orbits
};

/// Summary of one geometric cluster orbit (a set of symmetry/geometry-
/// equivalent clusters sharing the same distance signature).
struct ClusterOrbitSummary {
    int order = 2;          ///< 2 = pair, 3 = triplet, 4 = quadruplet
    double radius = 0.0;    ///< largest pairwise distance in the cluster (Å)
    int multiplicity = 0;   ///< clusters belonging to this orbit
};

/// One generated, symmetry-inequivalent decorated configuration.
struct ClusterExpansionConfig {
    Structure structure;
    std::vector<int> speciesCounts;   ///< per species (aligned to speciesZ)
    std::vector<double> correlation;  ///< cluster-correlation fingerprint
    /// How many raw decorations of the active sublattice collapsed onto
    /// this one canonical fingerprint before dedup — g_j in the sense
    /// EGQCA (core::EgqcaCluster::degeneracy) and Eq. 6-7 of the working
    /// paper use it: the multiplicity of this configuration CLASS, not a
    /// geometric cluster-orbit multiplicity (that is
    /// ClusterOrbitSummary::multiplicity, a different quantity this
    /// enumerator also produces). Always >= 1.
    int degeneracy = 1;
};

struct ClusterExpansionResult {
    std::vector<ClusterExpansionConfig> configs; ///< inequivalent configurations
    int activeSites = 0;
    long long enumerated = 0; ///< decorations examined
    bool sampled = false;     ///< true if the occupation space was subsampled
    std::vector<ClusterOrbitSummary> orbits;
    std::string note;
};

/// Generate symmetry-inequivalent occupational configurations of an alloy by
/// the cluster-expansion methodology (as in ICET), reimplemented natively in
/// C++ with no external cluster-expansion dependency.
///
/// The active sublattice of a supercell is decorated with the substitution
/// species; each decoration is reduced to a canonical cluster-correlation
/// fingerprint — per-orbit histograms of the species tuples on every pair,
/// triplet and quadruplet cluster within the cutoff radii — and two
/// decorations are treated as equivalent when their fingerprints match. The
/// deduplicated set is the returned ensemble, ready for ML/DFT training.
/// Column labels for the correlation fingerprint, one per entry of
/// `ClusterExpansionConfig::correlation`.
///
/// The fingerprint is NOT one number per orbit — it is K point terms followed
/// by a species-tuple HISTOGRAM per orbit (K(K+1)/2 buckets for a pair, K^3
/// for a triplet, K^4 for a quadruplet), laid out pairs then triplets then
/// quadruplets, which is the same order `orbits` is filled in. So a caller
/// that labelled columns one-per-orbit would mislabel everything after the
/// first pair, and an ECI attributed to the wrong cluster is worse than an
/// unlabelled one.
///
/// Built here rather than in the GUI because this file owns that layout and
/// is the only place that can be wrong about it in one edit.
std::vector<std::string> clusterCorrelationLabels(
    const ClusterExpansionResult& result, int speciesCount);

/// The pair-orbit histogram bucket a species pair (si, sj) falls into — the
/// upper-triangular (order-independent) index generateClusterExpansion()
/// itself fills the fingerprint with. Exported so a caller translating a
/// fitted pair ECI back into a per-species-pair energy (ClusterVariation's
/// pairEnergiesFromEci()) uses the exact same bucket numbering rather than a
/// second, hand-derived copy of the formula.
int clusterExpansionPairBucket(int speciesCount, int speciesI, int speciesJ);

/// The triplet-orbit histogram bucket a species triple (si, sj, sk) falls
/// into — the canonical (sorted) mixed-radix index generateClusterExpansion()
/// fills the fingerprint with. See clusterExpansionPairBucket().
int clusterExpansionTripletBucket(int speciesCount, int speciesI, int speciesJ,
                                  int speciesK);

ClusterExpansionResult generateClusterExpansion(
    const Structure& parent, const ClusterExpansionOptions& options);

} // namespace calango::core
