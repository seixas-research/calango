#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>
#include <string>
#include <vector>

namespace calango::core {

/// Configurational thermodynamics of a substitutional alloy by the Cluster
/// Variation Method.
///
/// The question this answers: how much of an alloy's entropy is actually
/// k_B ln(number of arrangements)? The textbook answer,
/// S_ideal = -k_B sum_i x_i ln x_i, assumes the species are placed
/// INDEPENDENTLY on every site. They are not — if unlike neighbours are
/// favoured the alloy orders, if like neighbours are favoured it clusters, and
/// either way there are fewer distinguishable arrangements than the ideal
/// count. S_ideal is therefore an upper bound, and for a high-entropy alloy
/// quoted as "stabilized by configurational entropy" the gap between the bound
/// and the truth is the entire question.
///
/// Kikuchi's construction gets at that by writing the entropy as a sum over
/// clusters with alternating-sign corrections, so that the correlations a
/// cluster already accounts for are not counted again by its subclusters:
///
///     S/k_B = -sum_alpha  a_alpha  sum_{configs}  rho_alpha ln rho_alpha
///
/// with rho_alpha the probability of each decoration of cluster alpha and
/// a_alpha the Kikuchi-Barker coefficients, fixed by requiring that every
/// subcluster's contribution cancel to exactly one net counting.
///
/// Three approximations are provided, and they are a HIERARCHY rather than
/// alternatives — each is the previous one plus more correlation:
///
///   * Point (Bragg-Williams) = the ideal entropy. Sites independent.
///   * Pair (Bethe-Peierls-Guggenheim). Exact on a Bethe lattice / Cayley
///     tree, and therefore exact for a 1D chain, which is what pins it here.
///   * Tetrahedron (Kikuchi). The standard approximation for FCC, because the
///     nearest-neighbour tetrahedron is the smallest cluster that contains
///     the frustrated triangles an FCC lattice is built from.
///
/// The point of computing all three is the comparison: the spread between
/// them IS the size of the correlation correction, and a single number from
/// any one of them does not tell you whether it mattered.
enum class CvmApproximation {
    /// S = -k_B sum x_i ln x_i. Sites independent; no interaction enters.
    Point,
    /// Bethe-Peierls-Guggenheim: points plus nearest-neighbour pairs.
    Pair,
    /// Kikuchi tetrahedron: points, pairs, triangles, tetrahedra.
    Tetrahedron,
};

/// The lattice the cluster geometry is taken from.
///
/// This is not decoration: the Kikuchi coefficients depend on how many pairs,
/// triangles and tetrahedra share a site, which is a property of the lattice
/// and nothing else. Getting it wrong does not fail loudly — it produces a
/// smooth, plausible, wrong entropy.
enum class CvmLattice {
    Fcc, ///< z = 12, 8 tetrahedra per site
    Bcc, ///< z = 8 (nearest); the irregular tetrahedron approximation
    /// One-dimensional chain, z = 2. Present because the pair approximation is
    /// EXACT here, which makes it the one case with a closed-form answer to
    /// check against.
    Chain,
};

struct CvmInput {
    CvmLattice lattice = CvmLattice::Fcc;
    CvmApproximation approximation = CvmApproximation::Tetrahedron;

    /// Species labels, for reporting. Size sets the number of species.
    std::vector<std::string> species;
    /// Overall mole fractions, one per species. Normalized on entry.
    std::vector<double> composition;

    /// Nearest-neighbour pair interaction matrix, eV, indexed
    /// [i * species + j] and symmetrized on entry.
    ///
    /// In cluster-expansion terms this is the pair ECI expressed in the
    /// "bond energy" basis rather than the +/-1 correlation basis; the two are
    /// related by a linear transform, and `pairEnergiesFromEci` does it so
    /// that a fit from ClusterExpansionFit can be fed straight in.
    std::vector<double> pairEnergiesEv;

    /// Nearest-neighbour TRIPLET interaction, eV, indexed
    /// [(i * species + j) * species + k] and symmetrized over the three
    /// vertices. Empty means zero.
    ///
    /// Only the tetrahedron approximation can use it: a triangle is not a
    /// subcluster of a pair, so the pair approximation has nowhere to put it.
    /// Three-body terms are what distinguish an alloy that merely orders from
    /// one that picks a particular ordered structure, so a cluster expansion
    /// fitted with triplets and then evaluated with a pair-only CVM has
    /// thrown away the part of the fit that chose the phase.
    std::vector<double> tripletEnergiesEv;

    double minTemperatureK = 100.0;
    double maxTemperatureK = 2000.0;
    int temperatureSteps = 100;

    /// Natural-iteration convergence controls. The free energy is minimized by
    /// the Kikuchi-Sanchez natural iteration rather than by a general
    /// optimizer, because NI is guaranteed to decrease the functional and so
    /// cannot wander off a saddle the way a naive fixed point can.
    int maxIterations = 20000;
    double tolerance = 1e-12;
};

/// State of the alloy at one temperature.
struct CvmPoint {
    double temperatureK = 0.0;
    /// Configurational entropy per site, in k_B.
    double entropyPerSiteKb = 0.0;
    /// Internal energy per site, eV.
    double energyPerSiteEv = 0.0;
    /// F = E - TS per site, eV.
    double freeEnergyPerSiteEv = 0.0;
    /// Warren-Cowley alpha for the nearest-neighbour shell, [i*species + j].
    ///
    /// alpha = 1 - P(j|i)/x_j: zero is random, negative means unlike
    /// neighbours are preferred (ordering), positive means like (clustering).
    /// This is what makes the temperature dependence legible — S alone does
    /// not say WHICH way the alloy is departing from random.
    std::vector<double> warrenCowley;
    /// Nearest-neighbour pair probabilities, [i*species + j].
    std::vector<double> pairProbabilities;
    /// Tetrahedron probabilities, [((i*K + j)*K + k)*K + l]. Empty unless the
    /// tetrahedron approximation ran; kept because the triplet energy is a
    /// sum over the tetrahedron's faces and cannot be recovered from the pair
    /// marginals.
    std::vector<double> tetrahedronProbabilities;
    bool converged = false;
    int iterations = 0;
};

struct CvmResult {
    bool ok = false;
    std::vector<CvmPoint> points;
    /// S_ideal = -sum x ln x, per site in k_B. A constant, and the baseline
    /// every curve is compared against.
    double idealEntropyKb = 0.0;
    /// Temperature above which short-range order has fallen below the
    /// detection threshold, K — and ZERO when it never does inside the
    /// scanned range.
    ///
    /// **This is not an order-disorder transition temperature**, and it was
    /// named as though it were until a Cu3Au test showed the solver happily
    /// returning the top of the scan range as a "transition". A homogeneous
    /// CVM has one sublattice and therefore no symmetry to break: it can show
    /// short-range order dying away smoothly, never a phase transition. A
    /// T_c needs the sublattice-resolved CVM.
    double sroVanishingTemperatureK = 0.0;
    std::vector<std::string> warnings;
};

/// Ideal configurational entropy per site in k_B: -sum_i x_i ln x_i.
///
/// Separate and public because it is the baseline of every comparison, and
/// because it is the number people quote for high-entropy alloys (ln 5 = 1.61
/// k_B for an equiatomic quinary) without checking how far the real entropy
/// falls below it.
double idealConfigurationalEntropy(const std::vector<double>& composition);

/// Convert a pair ECI in the +/-1 correlation basis into the species-pair
/// energy matrix this module wants. Binary only; `ok` is set false otherwise.
///
/// Exists so that a ClusterExpansionFit result can drive the CVM without the
/// caller having to rediscover the basis transform, which is the sort of
/// factor-of-two/sign error that produces a clustering alloy where an ordering
/// one was meant.
std::vector<double> pairEnergiesFromEci(double pairEci, bool* ok);

/// Convert a nearest-neighbour TRIPLET ECI in the +/-1 correlation basis into
/// the species-triplet energy tensor. Binary only; `ok` is false otherwise.
///
/// eps3(i,j,k) = J3 * s_i s_j s_k with s = +1 for A and -1 for B, so the
/// tensor alternates in sign with the number of B atoms on the triangle. A
/// triplet term breaks the A<->B symmetry that a pair-only model has, which is
/// exactly why it can distinguish A3B from AB3.
std::vector<double> tripletEnergiesFromEci(double tripletEci, bool* ok);

/// Convert a nearest-neighbour pair orbit's fitted ECIs — one per
/// species-pair-type histogram bucket, in core::ClusterExpansion's own
/// upper-triangular bucket order (i <= j: bucket = i*K - i*(i-1)/2 + (j-i),
/// matching clusterExpansionPairBucket() in ClusterExpansion.hpp, duplicated
/// rather than shared — see the .cpp for why) — directly into the K x K
/// species-pair energy matrix this module wants. `bucketEci` must have
/// exactly speciesCount*(speciesCount+1)/2 entries; `ok` is set false
/// otherwise.
///
/// NOT the same transform as the single-scalar overload above, and
/// deliberately a separate overload rather than a replacement for it: that
/// one assumes the classical +/-1 Ising pair basis (which only has a
/// consistent meaning for two species), whereas ClusterExpansionFit's actual
/// design matrix for a pair orbit is a raw per-species-pair-type COUNT
/// (energy = sum_bucket ECI_bucket * count_bucket), so a bucket's fitted
/// coefficient already IS that species pair's bond energy — no basis
/// transform is needed, or even meaningful, once there are more than two
/// species. Existing binary callers keep using the scalar overload
/// unchanged; this one is for K >= 2 general use (K = 2 happens to still
/// work here, just as three independent numbers rather than one).
std::vector<double> pairEnergiesFromEci(const std::vector<double>& bucketEci,
                                        int speciesCount, bool* ok);

/// Same generalization for the nearest-neighbour TRIPLET orbit, using
/// core::ClusterExpansion's canonical (sorted) bucket order — sort (si, sj,
/// sk) ascending, then read it as a base-K digit string — matching
/// clusterExpansionTripletBucket() in ClusterExpansion.hpp.
/// `bucketEci` must have exactly speciesCount^3 entries (ClusterExpansion's
/// dense triplet histogram — most of them zero for K > 2, since only the
/// speciesCount*(speciesCount+1)*(speciesCount+2)/6 sorted-order buckets are
/// ever populated by a real configuration).
std::vector<double> tripletEnergiesFromEci(const std::vector<double>& bucketEci,
                                           int speciesCount, bool* ok);

/// Number of nearest neighbours for a lattice.
int cvmCoordination(CvmLattice lattice);

/// Solve the CVM free-energy minimization over the temperature range.
CvmResult solveClusterVariation(const CvmInput& input);

} // namespace calango::core
