#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>
#include <string>
#include <vector>

#include "core/ClusterVariation.hpp"

namespace calango::core {

/// Sublattice-resolved (four-sublattice) Cluster Variation Method for FCC
/// order-disorder transitions.
///
/// WHY THIS EXISTS SEPARATELY FROM `ClusterVariation`. That module solves the
/// HOMOGENEOUS CVM: one sublattice, every site statistically identical. It
/// describes short-range order in a disordered solid solution and it is very
/// good at that — but it structurally CANNOT produce a long-range-ordered
/// phase or an order-disorder transition, because a single sublattice has no
/// symmetry left to break. Run Cu3Au through it and you get a perfectly smooth
/// S(T) and alpha(T) through 663 K, which is not a bug in the solver but the
/// approximation stating its own limits.
///
/// THE GEOMETRY THAT MAKES THIS WORK. The FCC nearest-neighbour tetrahedron
/// has exactly one vertex on each of the four simple-cubic sublattices that
/// FCC decomposes into. So the tetrahedron distribution can be written
///
///     w[i][j][k][l],  POSITION = sublattice (0..3),  VALUE = species there
///
/// and — unlike the homogeneous case — `w` is NOT symmetric under permuting
/// the positions. That asymmetry IS the long-range order. Nothing else in the
/// formalism changes; the same Kikuchi-Barker counting applies, resolved per
/// sublattice.
///
/// Entropy per site, with x^p the point marginal on sublattice p and y^{pq}
/// the pair marginal on the sublattice pair (p,q) — there are six such pairs,
/// one per bond per site:
///
///     S/k_B = -2 sum_{ijkl} w ln w
///             + sum_{6 pairs} sum_{ij} y^{pq}_{ij} ln y^{pq}_{ij}
///             - (5/4) sum_{4 sublattices} sum_i x^p_i ln x^p_i
///
/// Setting all four sublattices equal collapses this to the homogeneous
/// -2 sum w ln w + 6 sum y ln y - 5 sum x ln x exactly, which is the identity
/// the "reduction" test checks numerically.
///
/// Energy per site — FCC has z = 12 neighbours, hence 6 bonds per site, and
/// each of the 6 sublattice pairs supplies exactly one of them:
///
///     E = sum_{6 pairs} sum_{ij} y^{pq}_{ij} eps_ij
///
/// WHAT IS CONSTRAINED. The OVERALL composition, (1/4) sum_p x^p_i = x_i, and
/// NOT the per-sublattice composition. That freedom is the entire point: it is
/// what lets L1_2 form at x_B = 0.25 with three A-rich sublattices and one
/// B-rich one.
///
/// WHAT THIS DOES NOT DO, stated here rather than buried:
///   * Nearest-neighbour PAIR interactions only. No multi-body (triplet,
///     quadruplet) effective cluster interactions are wired in, and for real
///     alloys those are not negligible — a NN-pair-only CVM is a model of the
///     ordering tendency, not a quantitative reproduction of a phase diagram.
///   * No second-neighbour interactions, which on FCC are what select between
///     competing ordered structures in several systems.
///   * Ordered structures are searched only among the trial patterns supplied
///     (L1_2 and L1_0), at FIXED overall composition. This is a competition
///     between homogeneous phases, NOT a phase diagram: there is no common
///     tangent construction, so two-phase equilibria and the solvus lines that
///     bound the ordered fields are absent.
///   * FCC only. BCC ordering (B2/D0_3) needs the irregular tetrahedron and
///     different Kikuchi-Barker coefficients.
///   * No vibrational, electronic or magnetic entropy — configurational only.
///
/// TWO PATHOLOGIES OF THE TETRAHEDRON CVM ON FRUSTRATED FCC, both found here
/// and both reported as warnings rather than hidden, because a caller who does
/// not know about them will read the numbers as physics:
///
///   1. NEGATIVE ENTROPY. Well below the transition the DISORDERED branch
///      returns S < 0 — at x = 1/2 it tends to -0.929 k_B, the entropy of the
///      uniform distribution over the two-A-two-B tetrahedra. The Kikuchi
///      expansion is not a probability and nothing forces its truncation to
///      stay positive on a lattice this frustrated. It is not a bug in the
///      iteration and it does not affect the ordered branch, which is the one
///      that is stable there; it does mean the disordered free energy is
///      meaningless deep in the ordered field.
///   2. A DOMAIN-MIXED BOUNDARY SOLUTION. At x = 1/2, below roughly
///      0.35 T_c, the L1_0 start converges onto w = 1/2 (AABB) + 1/2 (BBAA):
///      an equal mixture of the two symmetry-related L1_0 domains. It has
///      exactly the ground-state energy, exactly S = ln 2, and ZERO
///      long-range order, because the point marginals of the two domains
///      average away while their pair correlations do not. It is a fixed point
///      of the natural iteration only because zeros in the pair marginals are
///      absorbing under a multiplicative update, and it is not a physical
///      homogeneous phase — a real crystal would pay a domain wall. Solutions
///      with vanishing order parameter are therefore never accepted as an
///      ordered phase, and hitting one is reported. The practical consequence
///      is that an L1_0 transition must be bracketed from inside the ordered
///      field (a few hundred K, not 0 K). The L1_2 branch at x = 1/4 does not
///      suffer from this at any temperature.

/// A trial ordered structure, i.e. a pattern of sublattice occupation used as
/// the starting point of the minimization.
///
/// These are STARTING GUESSES, not constraints. The iteration is free to walk
/// away from the pattern it started in — and above the transition it does,
/// collapsing back onto the disordered solution, which is exactly how the
/// transition is detected.
enum class SublatticeOrder {
    /// All four sublattices identical. Always a stationary point; it is the
    /// homogeneous CVM solution, and this branch is symmetrized explicitly so
    /// that it stays that way.
    Disordered,
    /// Cu3Au-type: one sublattice distinct, the other three equal. The
    /// stoichiometric composition is x = 1/4.
    L12,
    /// CuAu-type: two pairs of sublattices. The stoichiometric composition is
    /// x = 1/2.
    L10,
    /// Converged to a pattern that is none of the above. Only ever produced by
    /// `classifySublatticeOrder`, never used as a starting guess.
    Other,
};

const char* sublatticeOrderName(SublatticeOrder order);

struct SublatticeCvmInput {
    /// `Tetrahedron` is the Kikuchi four-sublattice CVM; `Point` is the
    /// four-sublattice Bragg-Williams (mean-field) theory, present because
    /// CVM T_c < BW T_c is a provable inequality and therefore a real check on
    /// the entropy functional. `Pair` is not implemented here and is treated
    /// as `Tetrahedron` with a warning.
    CvmApproximation approximation = CvmApproximation::Tetrahedron;

    /// Species labels, for reporting. Size must match `composition`.
    std::vector<std::string> species;
    /// OVERALL mole fractions. The per-sublattice compositions are free.
    std::vector<double> composition;

    /// Nearest-neighbour pair interaction matrix, eV, [i * species + j],
    /// symmetrized on entry. `pairEnergiesFromEci` builds it from a pair ECI.
    std::vector<double> pairEnergiesEv;

    double minTemperatureK = 100.0;
    double maxTemperatureK = 1500.0;
    int temperatureSteps = 60;

    int maxIterations = 20000;
    double tolerance = 1e-12;

    /// Damping of the Kikuchi natural iteration: w_new = w_NI^r * w_old^(1-r),
    /// applied BEFORE the composition multipliers are refitted so the
    /// constraint is still satisfied exactly. r = 1 is undamped NI. The fixed
    /// points are identical for any r in (0, 1].
    double relaxation = 1.0;

    /// Which species' sublattice segregation defines the trial ordered
    /// patterns. For a binary listed as {A, B} at x_B = 0.25 this is 1 (B).
    int orderingSpecies = 1;

    /// How far the trial ordered start is pushed towards full order, in [0, 1]
    /// of the maximum the composition allows. Only a starting guess.
    double initialOrder = 0.95;

    /// Optional explicit starting point, [p * species + i]. Overrides the
    /// pattern implied by the trial. Used by the tests to start from a
    /// DELIBERATELY UNEQUAL guess on sublattices 1..3 and check that the
    /// converged L1_2 state brings them back together — without that, "three
    /// sublattices are equal" would be true by construction rather than by
    /// physics.
    std::vector<double> initialSublatticeComposition;

    /// Ordered structures to try at each temperature, in addition to the
    /// disordered one. The lowest free energy wins.
    std::vector<SublatticeOrder> trials = {SublatticeOrder::L12,
                                           SublatticeOrder::L10};
};

/// One converged branch at one temperature.
struct SublatticeCvmPoint {
    double temperatureK = 0.0;
    /// The pattern this branch was STARTED from.
    SublatticeOrder startOrder = SublatticeOrder::Disordered;
    /// The pattern it CONVERGED to, by `classifySublatticeOrder`.
    SublatticeOrder order = SublatticeOrder::Disordered;

    double entropyPerSiteKb = 0.0;
    double energyPerSiteEv = 0.0;
    double freeEnergyPerSiteEv = 0.0;

    /// Long-range order parameter: the largest spread of any species'
    /// concentration across the four sublattices,
    ///     eta = max_i ( max_p x^p_i - min_p x^p_i ).
    /// Zero in the disordered phase, 1 for a fully ordered stoichiometric
    /// compound. For a binary at L1_2 stoichiometry this is |x^0_B - x^1_B|,
    /// the conventional eta.
    double longRangeOrder = 0.0;

    /// Per-sublattice composition, [p * species + i].
    std::vector<double> sublatticeComposition;
    /// Nearest-neighbour pair probabilities averaged over the six sublattice
    /// pairs and symmetrized in (i, j), [i * species + j]. This is the
    /// shell-averaged quantity a diffuse-scattering experiment sees, and in an
    /// ordered phase it carries the long-range order as well as the
    /// short-range part.
    std::vector<double> pairProbabilities;
    /// Warren-Cowley alpha built from `pairProbabilities` and the OVERALL
    /// composition, [i * species + j].
    std::vector<double> warrenCowley;

    bool converged = false;
    int iterations = 0;
};

struct SublatticeCvmResult {
    bool ok = false;
    /// The stable (lowest free energy) branch at each scanned temperature.
    std::vector<SublatticeCvmPoint> points;
    /// The disordered branch at each scanned temperature, whether or not it
    /// was stable. Kept because the free-energy DIFFERENCE between the two is
    /// the evidence for the transition and for its order.
    std::vector<SublatticeCvmPoint> disorderedPoints;

    double idealEntropyKb = 0.0;

    /// Order-disorder transition temperature, K, located by bisection on which
    /// branch has the lower free energy. Zero when no ordered phase is stable
    /// anywhere in the scanned range, or when the ordered phase is still
    /// stable at the top of it (a warning says which).
    double transitionTemperatureK = 0.0;
    /// The ordered structure that is stable just below the transition.
    SublatticeOrder orderedPhase = SublatticeOrder::Disordered;
    /// eta of the STABLE phase immediately below and immediately above the
    /// transition. For a FIRST ORDER transition — which the FCC L1_2
    /// transition is — the first is finite and the second is zero: the order
    /// parameter JUMPS rather than decaying to zero.
    double orderParameterBelowTc = 0.0;
    double orderParameterAboveTc = 0.0;
    /// eta of the ordered branch just ABOVE the transition, where it is
    /// metastable rather than stable. The second, independent signature of a
    /// first-order transition: the ordered solution does not cease to exist at
    /// T_c, it merely stops being the lower free energy, and it still carries
    /// almost the same order parameter it had below. A second-order transition
    /// would give zero here too, because there the ordered branch and the
    /// disordered branch merge.
    double metastableOrderParameterAboveTc = 0.0;
    /// True when the jump in the stable-phase order parameter is resolvable,
    /// i.e. the transition is first order.
    bool firstOrder = false;

    std::vector<std::string> warnings;
};

/// Which sublattices ended up equal: 4 equal is Disordered, 3+1 is L1_2,
/// 2+2 is L1_0, anything else is Other. Compares the `orderingSpecies` column
/// of a `[p * species + i]` table with the given absolute tolerance.
SublatticeOrder classifySublatticeOrder(
    const std::vector<double>& sublatticeComposition, int species,
    int orderingSpecies, double tolerance);

/// Minimize the four-sublattice free energy at ONE temperature, starting from
/// one trial pattern. Exposed because the individual branches — not just the
/// winner — are what the tests compare: the disordered branch of this solver
/// must reproduce `solveClusterVariation` exactly, and the free-energy
/// difference between branches is the evidence for a first-order transition.
SublatticeCvmPoint solveSublatticeCvmPoint(const SublatticeCvmInput& input,
                                           double temperatureK,
                                           SublatticeOrder start, bool* ok);

/// Locate the order-disorder temperature for one ordered structure by
/// bisection on WHICH BRANCH HAS THE LOWER FREE ENERGY.
///
/// Deliberately not a search for the order parameter going continuously to
/// zero: the FCC L1_2 transition is FIRST ORDER, the order parameter jumps,
/// and a continuous-vanishing search would either find nothing or find the
/// spinodal where the ordered branch stops existing — which is above T_c and
/// is not the transition.
double sublatticeOrderDisorderTemperature(const SublatticeCvmInput& input,
                                          SublatticeOrder ordering,
                                          double lowK, double highK,
                                          double toleranceK, bool* ok);

/// Scan the temperature range, taking the lowest-free-energy branch at each
/// temperature, and locate the transition.
SublatticeCvmResult solveSublatticeClusterVariation(
    const SublatticeCvmInput& input);

} // namespace calango::core
