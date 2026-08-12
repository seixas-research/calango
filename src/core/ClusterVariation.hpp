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

    /// Optional nearest-neighbour triplet interaction, eV, for the
    /// tetrahedron approximation. Empty means zero.
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

/// Number of nearest neighbours for a lattice.
int cvmCoordination(CvmLattice lattice);

/// Solve the CVM free-energy minimization over the temperature range.
CvmResult solveClusterVariation(const CvmInput& input);

} // namespace calango::core
