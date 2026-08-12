#include "core/SublatticeClusterVariation.hpp"

// <cstddef> for std::size_t: libstdc++ does not supply it transitively the way
// libc++ does, and the header's <cstdint> is not a substitute. Neither may be
// removed on a clangd "unused" hint — the Linux .deb build needs both.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace calango::core {

namespace {

constexpr double kBoltzmannEvPerK = 8.617333262e-5;

/// The six vertex-pairs of a tetrahedron. Here the vertex index IS the
/// sublattice index, so these are the six SUBLATTICE pairs, and each of them
/// supplies exactly one of the six bonds per site on FCC (z = 12 neighbours,
/// four of them on each of the three other sublattices, 12/2 = 6 bonds).
constexpr int kTetPairs[6][2] = {{0, 1}, {0, 2}, {0, 3},
                                 {1, 2}, {1, 3}, {2, 3}};

/// x ln x, continuous at zero — see ClusterVariation.cpp. An ordered phase
/// drives most tetrahedron probabilities to exactly zero, so this is the
/// common case here, not the edge case.
double xlnx(double x)
{
    return x > 0.0 ? x * std::log(x) : 0.0;
}

/// Logarithm floored well below any physically meaningful probability.
///
/// The natural-iteration exponent contains +0.5*sum_6 ln y and
/// -0.625*sum_4 ln x^p. In a strongly ordered state BOTH go to -infinity and
/// they very nearly cancel; taking them literally gives inf - inf = NaN, and a
/// single NaN destroys every subsequent temperature. Flooring both at ln(1e-300)
/// keeps the difference finite, and the exponent is evaluated relative to its
/// maximum before exponentiation so the floor never overflows either way.
constexpr double kLogFloor = -690.7755278982137; // ln(1e-300)

double safeLog(double value)
{
    return value > 1e-300 ? std::log(value) : kLogFloor;
}

/// Static tables that depend only on the number of species: the decoded
/// species tuple of every state index, and the orbit of every state under the
/// 24 permutations of the four sublattices.
struct Geometry {
    int species = 0;
    std::size_t states = 0;
    std::vector<std::array<int, 4>> tuple;
    /// permuted[index][m] = the state index of `tuple[index]` with its
    /// positions relabelled by permutation m. Used to symmetrize the
    /// disordered branch; see `solveTetrahedronBranch`.
    std::vector<std::array<std::size_t, 24>> permuted;
};

Geometry makeGeometry(int species)
{
    Geometry g;
    g.species = species;
    g.states = 1;
    for (int i = 0; i < 4; ++i)
        g.states *= static_cast<std::size_t>(species);

    g.tuple.assign(g.states, {});
    for (std::size_t index = 0; index < g.states; ++index) {
        std::size_t rest = index;
        for (int position = 3; position >= 0; --position) {
            g.tuple[index][position] =
                static_cast<int>(rest % static_cast<std::size_t>(species));
            rest /= static_cast<std::size_t>(species);
        }
    }

    std::array<int, 4> perm{0, 1, 2, 3};
    std::vector<std::array<int, 4>> perms;
    do {
        perms.push_back(perm);
    } while (std::next_permutation(perm.begin(), perm.end()));

    g.permuted.assign(g.states, {});
    for (std::size_t index = 0; index < g.states; ++index)
        for (std::size_t m = 0; m < perms.size(); ++m) {
            std::size_t encoded = 0;
            for (int position = 0; position < 4; ++position)
                encoded = encoded * static_cast<std::size_t>(species)
                    + static_cast<std::size_t>(
                              g.tuple[index][perms[m][position]]);
            g.permuted[index][m] = encoded;
        }
    return g;
}

/// Point and pair marginals of the tetrahedron distribution.
///
/// `pair` is 6 blocks of species*species: block b holds y^{pq}_{ij} for the
/// sublattice pair kTetPairs[b], with i the species on p and j the species on
/// q. It is NOT symmetric in (i, j) when p and q carry different occupations —
/// that asymmetry is long-range order and must not be averaged away, which is
/// the one place this differs structurally from the homogeneous solver (which
/// symmetrizes y precisely because there it would be spurious).
void marginals(const Geometry& g, const std::vector<double>& w,
               std::vector<double>* pair, std::vector<double>* point)
{
    const int k = g.species;
    const std::size_t block = static_cast<std::size_t>(k) * k;
    if (pair)
        pair->assign(6 * block, 0.0);
    if (point)
        point->assign(static_cast<std::size_t>(4) * k, 0.0);
    for (std::size_t index = 0; index < g.states; ++index) {
        const double value = w[index];
        if (value == 0.0)
            continue;
        const auto& t = g.tuple[index];
        if (pair)
            for (int b = 0; b < 6; ++b)
                (*pair)[static_cast<std::size_t>(b) * block
                        + static_cast<std::size_t>(t[kTetPairs[b][0]]) * k
                        + t[kTetPairs[b][1]]] += value;
        if (point)
            for (int position = 0; position < 4; ++position)
                (*point)[static_cast<std::size_t>(position) * k + t[position]]
                    += value;
    }
}

/// S/k_B = -2 sum_4 w ln w + sum_{6 pairs} y ln y - (5/4) sum_{4 subl} x ln x.
///
/// The coefficients are the FCC Kikuchi-Barker set (2, 0, -6, 5) resolved per
/// sublattice: the single -6 on the pair term becomes one unit on each of the
/// six sublattice pairs, and the single +5 on the point term becomes 5/4 on
/// each of the four sublattices. Forcing all sublattices equal reproduces the
/// homogeneous expression term by term, which is what makes the two solvers
/// comparable to machine precision rather than merely "similar".
double tetrahedronEntropy(const Geometry& g, const std::vector<double>& w,
                          const std::vector<double>& pair,
                          const std::vector<double>& point)
{
    double tet = 0.0;
    for (const double value : w)
        tet += xlnx(value);
    double pairTerm = 0.0;
    for (const double value : pair)
        pairTerm += xlnx(value);
    double pointTerm = 0.0;
    for (const double value : point)
        pointTerm += xlnx(value);
    (void)g;
    return -2.0 * tet + pairTerm - 1.25 * pointTerm;
}

/// E per site = sum over the six sublattice pairs of sum_ij y^{pq}_ij eps_ij.
///
/// No z/2 factor: it is already there, as the fact that there are exactly six
/// sublattice pairs and six bonds per site. With all sublattices equal this
/// becomes 6 sum_ij y_ij eps_ij = (z/2) sum y eps, the homogeneous form.
double pairEnergyFromMarginals(int species, const std::vector<double>& pair,
                               const std::vector<double>& eps)
{
    const std::size_t block = static_cast<std::size_t>(species) * species;
    double energy = 0.0;
    for (int b = 0; b < 6; ++b)
        for (std::size_t n = 0; n < block; ++n)
            energy += pair[static_cast<std::size_t>(b) * block + n] * eps[n];
    return energy;
}

/// Shell-averaged, (i,j)-symmetrized pair probability — what a diffuse
/// scattering measurement sees, which averages over the six sublattice pairs
/// and cannot tell y_ij from y_ji.
std::vector<double> shellAveragedPairs(int species,
                                       const std::vector<double>& pair)
{
    const std::size_t block = static_cast<std::size_t>(species) * species;
    std::vector<double> y(block, 0.0);
    for (int b = 0; b < 6; ++b)
        for (int i = 0; i < species; ++i)
            for (int j = 0; j < species; ++j) {
                const double value = pair[static_cast<std::size_t>(b) * block
                                          + static_cast<std::size_t>(i) * species
                                          + j];
                y[static_cast<std::size_t>(i) * species + j] += value / 12.0;
                y[static_cast<std::size_t>(j) * species + i] += value / 12.0;
            }
    return y;
}

std::vector<double> warrenCowleyFromPairs(int species,
                                          const std::vector<double>& x,
                                          const std::vector<double>& y)
{
    std::vector<double> alpha(static_cast<std::size_t>(species) * species, 0.0);
    for (int i = 0; i < species; ++i)
        for (int j = 0; j < species; ++j) {
            const std::size_t index = static_cast<std::size_t>(i) * species + j;
            if (x[i] <= 0.0 || x[j] <= 0.0)
                continue;
            alpha[index] = 1.0 - (y[index] / x[i]) / x[j];
        }
    return alpha;
}

/// Build the starting per-sublattice composition for a trial pattern.
///
/// The patterns are chosen so that the OVERALL composition is exact by
/// construction — (1/4) sum_p x^p_i = x_i — because that is the only
/// constraint the minimization imposes, and starting off it merely wastes the
/// first few iterative-proportional-fitting passes.
///
///   L1_2: x^0_s = f + 3d, x^1..3_s = f - d       (average f)
///   L1_0: x^0,1_s = f + d, x^2,3_s = f - d       (average f)
///
/// with s the ordering species, f = x_s, and d scaled by `initialOrder` to the
/// largest value that keeps every concentration inside [0, 1]. The remaining
/// species fill each sublattice in proportion to their overall fractions,
/// which likewise preserves the average.
std::vector<double> startingSublatticeComposition(
    const std::vector<double>& x, int species, int orderingSpecies,
    double initialOrder, SublatticeOrder order)
{
    const int k = species;
    std::vector<double> xp(static_cast<std::size_t>(4) * k, 0.0);
    for (int p = 0; p < 4; ++p)
        for (int i = 0; i < k; ++i)
            xp[static_cast<std::size_t>(p) * k + i] = x[i];
    if (order == SublatticeOrder::Disordered || order == SublatticeOrder::Other)
        return xp;

    const int s = (orderingSpecies >= 0 && orderingSpecies < k)
        ? orderingSpecies
        : (k - 1);
    const double f = x[s];
    // A pure sublattice-forming species has no room to segregate further and a
    // vanishing one has nothing to segregate; either way the ordered start
    // degenerates to the disordered one and the branch simply reproduces it.
    if (f <= 1e-12 || f >= 1.0 - 1e-12)
        return xp;

    double maximum = 0.0;
    std::array<double, 4> target{};
    if (order == SublatticeOrder::L12) {
        maximum = std::min(f, (1.0 - f) / 3.0);
        const double d = std::clamp(initialOrder, 0.0, 1.0) * maximum;
        target = {f + 3.0 * d, f - d, f - d, f - d};
    } else {
        maximum = std::min(f, 1.0 - f);
        const double d = std::clamp(initialOrder, 0.0, 1.0) * maximum;
        target = {f + d, f + d, f - d, f - d};
    }

    for (int p = 0; p < 4; ++p) {
        const double occupied = std::clamp(target[p], 0.0, 1.0);
        xp[static_cast<std::size_t>(p) * k + s] = occupied;
        for (int i = 0; i < k; ++i) {
            if (i == s)
                continue;
            xp[static_cast<std::size_t>(p) * k + i] =
                (1.0 - occupied) * x[i] / (1.0 - f);
        }
    }
    return xp;
}

/// Iterative proportional fitting of the COMPOSITION multipliers.
///
/// **This is the piece that is easy to leave out and impossible to notice at
/// x = 0.5.** Stationarity of the four-sublattice functional carries a
/// multiplier for normalization AND one per species for the fixed OVERALL
/// composition. In the zero-interaction limit, dropping the composition
/// multiplier gives w ~ (prod_p x^p)^(7/8) instead of prod_p x^p: at x = 0.5
/// every tuple picks up the same factor and normalization hides it completely,
/// and at every other composition the answer is simply wrong. With the
/// multiplier in place the product form is stationary exactly when
/// nu_p = ln x^p. The ideal-limit test therefore runs at x = 0.25, never 0.5.
///
/// Note there is ONE multiplier per SPECIES, not one per sublattice: only the
/// overall composition is constrained, and constraining it per sublattice
/// would forbid the very segregation the module exists to find.
///
/// `logWeight` is the un-normalized log-probability of every state; `logLambda`
/// is warm-started from the previous outer iteration, which cuts the inner loop
/// from tens of passes to a handful.
bool fitCompositionMultipliers(const Geometry& g,
                               const std::vector<double>& logWeight,
                               const std::vector<double>& target,
                               std::vector<double>& logLambda,
                               std::vector<double>& out)
{
    const int k = g.species;
    std::vector<double> current(k, 0.0);
    out.assign(g.states, 0.0);
    for (int fit = 0; fit < 400; ++fit) {
        double maximum = -1e300;
        for (std::size_t index = 0; index < g.states; ++index) {
            const auto& t = g.tuple[index];
            double value = logWeight[index];
            for (int position = 0; position < 4; ++position)
                value += logLambda[t[position]];
            out[index] = value;
            maximum = std::max(maximum, value);
        }
        if (!(maximum > -1e299))
            return false;
        double total = 0.0;
        for (std::size_t index = 0; index < g.states; ++index) {
            out[index] = std::exp(out[index] - maximum);
            total += out[index];
        }
        if (!(total > 0.0))
            return false;
        std::fill(current.begin(), current.end(), 0.0);
        for (std::size_t index = 0; index < g.states; ++index) {
            const double value = out[index] / total;
            out[index] = value;
            const auto& t = g.tuple[index];
            for (int position = 0; position < 4; ++position)
                current[t[position]] += 0.25 * value;
        }
        double error = 0.0;
        for (int i = 0; i < k; ++i)
            error = std::max(error, std::abs(current[i] - target[i]));
        if (error < 1e-15)
            return true;
        // Step size, and it matters for runtime by nearly an order of
        // magnitude. Scaling lambda_i by r multiplies a state by r^(number of
        // times species i appears in it), so a raw step of log(ratio) is
        // wrong by the response of the marginal to the multiplier. That
        // response is d ln x_i / d ln lambda_i = Var(n_i)/<n_i>, and for four
        // near-independent sites Var(n_i) = 4 x_i (1 - x_i) with <n_i> = 4 x_i,
        // i.e. simply (1 - x_i). Dividing by it makes every species converge
        // at the same rate; the flat 0.5 that was here first left the MAJORITY
        // species converging at 0.5 * (1 - x) = 0.125 per pass, which meant
        // hundreds of passes to reach the tolerance and dominated the whole
        // solve. The 0.5 that remains is honest damping, and the floor on the
        // denominator keeps a nearly pure component from taking a wild step.
        for (int i = 0; i < k; ++i)
            if (current[i] > 0.0 && target[i] > 0.0)
                logLambda[i] += 0.5 * std::log(target[i] / current[i])
                    / std::max(0.1, 1.0 - target[i]);
    }
    return true;
}

/// Kikuchi natural iteration for the four-sublattice tetrahedron.
///
///     w_ijkl  ~  exp(-beta E_t/2)
///                * prod_{6 sublattice pairs} (y^{pq})^(1/2)
///                * prod_{4 sublattices}      (x^p)^(-5/8)
///                * prod_{4 sublattices}      lambda_{species there}
///
/// with E_t the sum of eps over the tetrahedron's six pairs and lambda the
/// composition multipliers. The right-hand side uses the PREVIOUS iterate's
/// marginals — that is what makes it Kikuchi's natural iteration, which
/// decreases the functional monotonically, rather than a bare fixed point,
/// which oscillates between the very sublattices it is trying to average over.
///
/// Everything is done on logarithms and shifted by the maximum before
/// exponentiating. That is not tidiness: in an ordered phase the y^(1/2) and
/// x^(-5/8) factors are individually 1e-150 and 1e+180 and only their product
/// is representable.
///
/// `symmetric` symmetrizes w over all 24 sublattice permutations every
/// iteration. It is set for the disordered branch, where it is not an
/// approximation but the definition: the disordered branch IS the
/// permutation-symmetric stationary point, i.e. the homogeneous CVM solution.
/// Without it, floating-point noise in the six pair marginals can seed a drift
/// into the ordered basin below T_c and the branch stops being the thing the
/// free energies are being compared against.
bool solveTetrahedronBranch(const Geometry& g, const std::vector<double>& x,
                            const std::vector<double>& eps, double beta,
                            const std::vector<double>& startXp, bool symmetric,
                            int maxIterations, double tolerance,
                            double relaxation, std::vector<double>& w,
                            std::vector<double>& pair,
                            std::vector<double>& point, int* iterations)
{
    const int k = g.species;
    const std::size_t block = static_cast<std::size_t>(k) * k;

    // exp(-beta/2 * sum over the six pairs), as a logarithm; fixed for the
    // whole solve.
    std::vector<double> logBond(g.states, 0.0);
    for (std::size_t index = 0; index < g.states; ++index) {
        const auto& t = g.tuple[index];
        double sum = 0.0;
        for (const auto& p : kTetPairs)
            sum += eps[static_cast<std::size_t>(t[p[0]]) * k + t[p[1]]];
        logBond[index] = -0.5 * beta * sum;
    }

    w.assign(g.states, 0.0);
    for (std::size_t index = 0; index < g.states; ++index) {
        const auto& t = g.tuple[index];
        double value = 1.0;
        for (int position = 0; position < 4; ++position)
            value *= startXp[static_cast<std::size_t>(position) * k
                             + t[position]];
        w[index] = value;
    }

    std::vector<double> logWeight(g.states, 0.0);
    std::vector<double> candidate(g.states, 0.0);
    std::vector<double> logLambda(k, 0.0);
    std::vector<double> symmetrized(g.states, 0.0);
    const double relax = std::clamp(relaxation, 1e-3, 1.0);

    int used = 0;
    bool converged = false;
    for (; used < maxIterations; ++used) {
        if (symmetric) {
            for (std::size_t index = 0; index < g.states; ++index) {
                double sum = 0.0;
                for (int m = 0; m < 24; ++m)
                    sum += w[g.permuted[index][m]];
                symmetrized[index] = sum / 24.0;
            }
            w.swap(symmetrized);
        }
        marginals(g, w, &pair, &point);

        for (std::size_t index = 0; index < g.states; ++index) {
            const auto& t = g.tuple[index];
            double value = logBond[index];
            for (int b = 0; b < 6; ++b)
                value += 0.5
                    * safeLog(pair[static_cast<std::size_t>(b) * block
                                   + static_cast<std::size_t>(
                                         t[kTetPairs[b][0]])
                                       * k
                                   + t[kTetPairs[b][1]]]);
            for (int position = 0; position < 4; ++position)
                value -= 0.625
                    * safeLog(point[static_cast<std::size_t>(position) * k
                                    + t[position]]);
            // Damping applied to the log BEFORE the multipliers are refitted,
            // so the composition constraint still holds exactly afterwards. At
            // a fixed point the mixture reduces to the same w (the refit
            // absorbs the change into lambda^relax), so damping cannot move
            // the answer, only the path to it.
            logWeight[index] =
                relax * value + (1.0 - relax) * safeLog(w[index]);
        }

        if (!fitCompositionMultipliers(g, logWeight, x, logLambda, candidate))
            return false;

        double change = 0.0;
        for (std::size_t index = 0; index < g.states; ++index) {
            change = std::max(change, std::abs(candidate[index] - w[index]));
            w[index] = candidate[index];
        }
        if (change < tolerance) {
            converged = true;
            ++used;
            break;
        }
    }

    if (symmetric) {
        for (std::size_t index = 0; index < g.states; ++index) {
            double sum = 0.0;
            for (int m = 0; m < 24; ++m)
                sum += w[g.permuted[index][m]];
            symmetrized[index] = sum / 24.0;
        }
        w.swap(symmetrized);
    }
    marginals(g, w, &pair, &point);
    if (iterations)
        *iterations = used;
    return converged;
}

/// Four-sublattice Bragg-Williams (point) theory.
///
/// Sites independent within each sublattice:
///     E = sum_{p<q} sum_ij x^p_i x^q_j eps_ij
///     S/k_B = -(1/4) sum_p sum_i x^p_i ln x^p_i
/// Stationarity with the overall-composition multiplier gives
///     x^p_i  ~  exp(-4 beta h^p_i) * lambda_i,   h^p_i = sum_{q!=p} sum_j x^q_j eps_ij
/// The factor 4 comes from the 1/4 in front of the entropy, and it is what
/// makes the closed-form L1_0 result come out at k_B T_c = 4J rather than J —
/// which is the check that this routine is normalized correctly.
///
/// Present because CVM T_c < BW T_c is a provable inequality: the tetrahedron
/// entropy counts correlations that mean-field theory does not, correlations
/// let the disordered phase hold some of the ordered phase's energy gain
/// without paying long-range order for it, and so ordering survives to lower
/// temperature. A CVM T_c above the mean-field one would mean the entropy
/// functional is wrong.
bool solvePointBranch(int species, const std::vector<double>& x,
                      const std::vector<double>& eps, double beta,
                      int maxIterations, double tolerance,
                      std::vector<double>& xp, int* iterations)
{
    const int k = species;
    std::vector<double> field(static_cast<std::size_t>(4) * k, 0.0);
    std::vector<double> candidate(static_cast<std::size_t>(4) * k, 0.0);
    std::vector<double> logLambda(k, 0.0);
    std::vector<double> current(k, 0.0);

    // The undamped mean-field map for an ORDERING (antiferromagnetic-like)
    // interaction flips the sublattices past each other and never settles; a
    // quarter step removes that without moving the fixed point.
    constexpr double kMix = 0.25;

    int used = 0;
    bool converged = false;
    for (; used < maxIterations; ++used) {
        for (int p = 0; p < 4; ++p)
            for (int i = 0; i < k; ++i) {
                double sum = 0.0;
                for (int q = 0; q < 4; ++q) {
                    if (q == p)
                        continue;
                    for (int j = 0; j < k; ++j)
                        sum += xp[static_cast<std::size_t>(q) * k + j]
                            * eps[static_cast<std::size_t>(i) * k + j];
                }
                field[static_cast<std::size_t>(p) * k + i] = sum;
            }

        for (int fit = 0; fit < 400; ++fit) {
            for (int p = 0; p < 4; ++p) {
                double maximum = -1e300;
                for (int i = 0; i < k; ++i) {
                    const double value =
                        -4.0 * beta * field[static_cast<std::size_t>(p) * k + i]
                        + logLambda[i];
                    candidate[static_cast<std::size_t>(p) * k + i] = value;
                    maximum = std::max(maximum, value);
                }
                double total = 0.0;
                for (int i = 0; i < k; ++i) {
                    const std::size_t n = static_cast<std::size_t>(p) * k + i;
                    candidate[n] = std::exp(candidate[n] - maximum);
                    total += candidate[n];
                }
                if (!(total > 0.0))
                    return false;
                for (int i = 0; i < k; ++i)
                    candidate[static_cast<std::size_t>(p) * k + i] /= total;
            }
            std::fill(current.begin(), current.end(), 0.0);
            for (int p = 0; p < 4; ++p)
                for (int i = 0; i < k; ++i)
                    current[i] +=
                        0.25 * candidate[static_cast<std::size_t>(p) * k + i];
            double error = 0.0;
            for (int i = 0; i < k; ++i)
                error = std::max(error, std::abs(current[i] - x[i]));
            if (error < 1e-15)
                break;
            // Same Jacobian scaling as the tetrahedron fit; see there.
            for (int i = 0; i < k; ++i)
                if (current[i] > 0.0 && x[i] > 0.0)
                    logLambda[i] += 0.5 * std::log(x[i] / current[i])
                        / std::max(0.1, 1.0 - x[i]);
        }

        double change = 0.0;
        for (std::size_t n = 0; n < xp.size(); ++n) {
            // Linear mixing, deliberately: both the old and the new iterate
            // satisfy the overall composition, and a linear combination of
            // them still does. A geometric mix would not.
            const double updated = (1.0 - kMix) * xp[n] + kMix * candidate[n];
            change = std::max(change, std::abs(updated - xp[n]));
            xp[n] = updated;
        }
        if (change < tolerance) {
            converged = true;
            ++used;
            break;
        }
    }
    if (iterations)
        *iterations = used;
    return converged;
}

/// Normalize the composition and symmetrize the interaction matrix, or report
/// why the input cannot be used.
bool prepareInput(const SublatticeCvmInput& input, std::vector<double>& x,
                  std::vector<double>& eps, std::vector<std::string>* warnings)
{
    const int species = static_cast<int>(input.composition.size());
    if (species < 2) {
        if (warnings)
            warnings->push_back(
                "Configurational ordering needs at least two species.");
        return false;
    }
    if (!input.species.empty()
        && static_cast<int>(input.species.size()) != species) {
        if (warnings)
            warnings->push_back(
                "The species list and the composition have different lengths.");
        return false;
    }
    x = input.composition;
    double total = 0.0;
    for (const double value : x) {
        if (value < 0.0) {
            if (warnings)
                warnings->push_back("Negative mole fraction.");
            return false;
        }
        total += value;
    }
    if (total <= 0.0) {
        if (warnings)
            warnings->push_back("The composition sums to zero.");
        return false;
    }
    for (double& value : x)
        value /= total;

    const std::size_t block = static_cast<std::size_t>(species) * species;
    eps = input.pairEnergiesEv;
    if (eps.empty())
        eps.assign(block, 0.0);
    if (eps.size() != block) {
        if (warnings)
            warnings->push_back(
                "The pair energy matrix is not species x species.");
        return false;
    }
    for (int i = 0; i < species; ++i)
        for (int j = i + 1; j < species; ++j) {
            const std::size_t ab = static_cast<std::size_t>(i) * species + j;
            const std::size_t ba = static_cast<std::size_t>(j) * species + i;
            const double mean = 0.5 * (eps[ab] + eps[ba]);
            eps[ab] = mean;
            eps[ba] = mean;
        }
    return true;
}

double longRangeOrderParameter(const std::vector<double>& xp, int species)
{
    double eta = 0.0;
    for (int i = 0; i < species; ++i) {
        double lowest = 1e300;
        double highest = -1e300;
        for (int p = 0; p < 4; ++p) {
            const double value = xp[static_cast<std::size_t>(p) * species + i];
            lowest = std::min(lowest, value);
            highest = std::max(highest, value);
        }
        eta = std::max(eta, highest - lowest);
    }
    return eta;
}

} // namespace

const char* sublatticeOrderName(SublatticeOrder order)
{
    switch (order) {
    case SublatticeOrder::Disordered:
        return "A1 (disordered)";
    case SublatticeOrder::L12:
        return "L1_2";
    case SublatticeOrder::L10:
        return "L1_0";
    case SublatticeOrder::Other:
        return "other";
    }
    return "other";
}

SublatticeOrder classifySublatticeOrder(
    const std::vector<double>& sublatticeComposition, int species,
    int orderingSpecies, double tolerance)
{
    if (species <= 0
        || sublatticeComposition.size()
            < static_cast<std::size_t>(4) * species)
        return SublatticeOrder::Other;
    const int s = (orderingSpecies >= 0 && orderingSpecies < species)
        ? orderingSpecies
        : (species - 1);
    std::array<double, 4> c{};
    for (int p = 0; p < 4; ++p)
        c[static_cast<std::size_t>(p)] =
            sublatticeComposition[static_cast<std::size_t>(p) * species + s];

    // Group the four sublattice concentrations by equality, then read the
    // partition: 4 = disordered, 3+1 = L1_2, 2+2 = L1_0.
    std::array<int, 4> group{-1, -1, -1, -1};
    int groups = 0;
    for (int p = 0; p < 4; ++p) {
        for (int q = 0; q < p; ++q)
            if (std::abs(c[static_cast<std::size_t>(p)]
                         - c[static_cast<std::size_t>(q)])
                <= tolerance) {
                group[static_cast<std::size_t>(p)] =
                    group[static_cast<std::size_t>(q)];
                break;
            }
        if (group[static_cast<std::size_t>(p)] < 0)
            group[static_cast<std::size_t>(p)] = groups++;
    }
    if (groups == 1)
        return SublatticeOrder::Disordered;
    if (groups != 2)
        return SublatticeOrder::Other;
    int inFirst = 0;
    for (int p = 0; p < 4; ++p)
        if (group[static_cast<std::size_t>(p)] == 0)
            ++inFirst;
    if (inFirst == 1 || inFirst == 3)
        return SublatticeOrder::L12;
    if (inFirst == 2)
        return SublatticeOrder::L10;
    return SublatticeOrder::Other;
}

SublatticeCvmPoint solveSublatticeCvmPoint(const SublatticeCvmInput& input,
                                           double temperatureK,
                                           SublatticeOrder start, bool* ok)
{
    SublatticeCvmPoint result;
    result.temperatureK = temperatureK;
    result.startOrder = start;
    if (ok)
        *ok = false;

    std::vector<double> x;
    std::vector<double> eps;
    if (!prepareInput(input, x, eps, nullptr))
        return result;
    const int species = static_cast<int>(x.size());

    std::vector<double> startXp;
    if (input.initialSublatticeComposition.size()
        == static_cast<std::size_t>(4) * species) {
        startXp = input.initialSublatticeComposition;
        // Renormalize each sublattice; an override that does not sum to one
        // per sublattice would otherwise silently change the total site count.
        for (int p = 0; p < 4; ++p) {
            double total = 0.0;
            for (int i = 0; i < species; ++i)
                total += startXp[static_cast<std::size_t>(p) * species + i];
            if (total > 0.0)
                for (int i = 0; i < species; ++i)
                    startXp[static_cast<std::size_t>(p) * species + i] /= total;
        }
    } else {
        startXp = startingSublatticeComposition(x, species,
                                                input.orderingSpecies,
                                                input.initialOrder, start);
    }

    const double beta =
        temperatureK > 0.0 ? 1.0 / (kBoltzmannEvPerK * temperatureK) : 1e30;
    const bool symmetric = (start == SublatticeOrder::Disordered);

    if (input.approximation == CvmApproximation::Point) {
        std::vector<double> xp = startXp;
        int iterations = 0;
        result.converged =
            solvePointBranch(species, x, eps, beta, input.maxIterations,
                             input.tolerance, xp, &iterations);
        result.iterations = iterations;
        result.sublatticeComposition = xp;

        double energy = 0.0;
        for (const auto& p : kTetPairs)
            for (int i = 0; i < species; ++i)
                for (int j = 0; j < species; ++j)
                    energy += xp[static_cast<std::size_t>(p[0]) * species + i]
                        * xp[static_cast<std::size_t>(p[1]) * species + j]
                        * eps[static_cast<std::size_t>(i) * species + j];
        double entropy = 0.0;
        for (const double value : xp)
            entropy -= 0.25 * xlnx(value);
        result.energyPerSiteEv = energy;
        result.entropyPerSiteKb = entropy;

        std::vector<double> y(static_cast<std::size_t>(species) * species, 0.0);
        for (const auto& p : kTetPairs)
            for (int i = 0; i < species; ++i)
                for (int j = 0; j < species; ++j) {
                    const double value =
                        xp[static_cast<std::size_t>(p[0]) * species + i]
                        * xp[static_cast<std::size_t>(p[1]) * species + j];
                    y[static_cast<std::size_t>(i) * species + j] += value / 12.0;
                    y[static_cast<std::size_t>(j) * species + i] += value / 12.0;
                }
        result.pairProbabilities = y;
    } else {
        const Geometry g = makeGeometry(species);
        std::vector<double> w;
        std::vector<double> pair;
        std::vector<double> point;
        int iterations = 0;
        result.converged = solveTetrahedronBranch(
            g, x, eps, beta, startXp, symmetric, input.maxIterations,
            input.tolerance, input.relaxation, w, pair, point, &iterations);
        if (w.empty())
            return result;
        result.iterations = iterations;
        result.sublatticeComposition = point;
        result.entropyPerSiteKb = tetrahedronEntropy(g, w, pair, point);
        result.energyPerSiteEv = pairEnergyFromMarginals(species, pair, eps);
        result.pairProbabilities = shellAveragedPairs(species, pair);
    }

    result.freeEnergyPerSiteEv = result.energyPerSiteEv
        - kBoltzmannEvPerK * temperatureK * result.entropyPerSiteKb;
    result.longRangeOrder =
        longRangeOrderParameter(result.sublatticeComposition, species);
    result.order = classifySublatticeOrder(result.sublatticeComposition,
                                           species, input.orderingSpecies,
                                           1e-6);
    result.warrenCowley =
        warrenCowleyFromPairs(species, x, result.pairProbabilities);
    if (ok)
        *ok = true;
    return result;
}

namespace {

/// True when the ordered branch is strictly and resolvably below the
/// disordered one at this temperature.
///
/// The threshold is not cosmetic. Above the transition the ordered start
/// collapses onto the disordered solution, so the free-energy difference goes
/// to ZERO rather than changing sign — a plain sign test would then bisect on
/// rounding noise. Requiring a definite margin AND a finite order parameter
/// makes the predicate monotone in temperature, which is what the bisection
/// needs.
bool orderedIsStable(const SublatticeCvmInput& input, double temperatureK,
                     SublatticeOrder ordering, double* etaOut)
{
    bool okOrdered = false;
    bool okDisordered = false;
    const SublatticeCvmPoint ordered =
        solveSublatticeCvmPoint(input, temperatureK, ordering, &okOrdered);
    const SublatticeCvmPoint disordered = solveSublatticeCvmPoint(
        input, temperatureK, SublatticeOrder::Disordered, &okDisordered);
    if (etaOut)
        *etaOut = ordered.longRangeOrder;
    if (!okOrdered || !okDisordered)
        return false;
    return ordered.freeEnergyPerSiteEv
        < disordered.freeEnergyPerSiteEv - 1e-12
        && ordered.longRangeOrder > 1e-4;
}

} // namespace

double sublatticeOrderDisorderTemperature(const SublatticeCvmInput& input,
                                          SublatticeOrder ordering,
                                          double lowK, double highK,
                                          double toleranceK, bool* ok)
{
    if (ok)
        *ok = false;
    if (ordering == SublatticeOrder::Disordered
        || ordering == SublatticeOrder::Other || !(highK > lowK))
        return 0.0;
    if (!orderedIsStable(input, lowK, ordering, nullptr))
        return 0.0; // No ordered phase even at the bottom of the bracket.
    if (orderedIsStable(input, highK, ordering, nullptr))
        return 0.0; // Still ordered at the top: the bracket ended, not the phase.

    double low = lowK;
    double high = highK;
    const double stop = toleranceK > 0.0 ? toleranceK : 0.01;
    while (high - low > stop) {
        const double mid = 0.5 * (low + high);
        if (orderedIsStable(input, mid, ordering, nullptr))
            low = mid;
        else
            high = mid;
    }
    if (ok)
        *ok = true;
    return 0.5 * (low + high);
}

SublatticeCvmResult solveSublatticeClusterVariation(
    const SublatticeCvmInput& input)
{
    SublatticeCvmResult result;
    std::vector<double> x;
    std::vector<double> eps;
    if (!prepareInput(input, x, eps, &result.warnings))
        return result;
    result.idealEntropyKb = idealConfigurationalEntropy(x);

    if (input.approximation == CvmApproximation::Pair)
        result.warnings.push_back(
            "The pair (Bethe-Peierls-Guggenheim) approximation is not "
            "implemented on four sublattices; the Kikuchi tetrahedron is used "
            "instead. It is the better approximation, so nothing is lost "
            "except the ability to compare the two.");

    // FCC has 12 nearest neighbours, hence 6 bonds per site, and the four
    // sublattices supply exactly one bond per site to each of the 6 sublattice
    // pairs. Asserted against the shared coordination table rather than
    // written as a literal 6, so that the two cannot drift apart.
    if (cvmCoordination(CvmLattice::Fcc) / 2 != 6) {
        result.warnings.push_back(
            "The FCC coordination number no longer implies six bonds per "
            "site; the sublattice pair counting is invalid.");
        return result;
    }

    const int steps = std::max(1, input.temperatureSteps);
    result.points.reserve(static_cast<std::size_t>(steps));
    result.disorderedPoints.reserve(static_cast<std::size_t>(steps));

    std::vector<SublatticeOrder> trials;
    for (const SublatticeOrder order : input.trials)
        if (order == SublatticeOrder::L12 || order == SublatticeOrder::L10)
            trials.push_back(order);

    SublatticeOrder lowestOrdered = SublatticeOrder::Disordered;
    double highestOrderedT = 0.0;
    double lowestDisorderedT = 0.0;
    bool sawOrdered = false;
    bool sawDisordered = false;
    bool domainMixWarned = false;
    bool negativeEntropy = false;

    for (int step = 0; step < steps; ++step) {
        const double t = steps == 1
            ? input.minTemperatureK
            : input.minTemperatureK
                + (input.maxTemperatureK - input.minTemperatureK) * step
                    / (steps - 1);
        bool ok = false;
        SublatticeCvmPoint disordered =
            solveSublatticeCvmPoint(input, t, SublatticeOrder::Disordered, &ok);
        if (!ok) {
            result.warnings.push_back(
                "The disordered branch failed to solve at "
                + std::to_string(static_cast<int>(t)) + " K.");
            return result;
        }
        if (disordered.entropyPerSiteKb < -1e-9)
            negativeEntropy = true;
        SublatticeCvmPoint best = disordered;
        for (const SublatticeOrder order : trials) {
            bool okOrdered = false;
            SublatticeCvmPoint candidate =
                solveSublatticeCvmPoint(input, t, order, &okOrdered);
            if (!okOrdered)
                continue;
            const bool lower = candidate.freeEnergyPerSiteEv
                < disordered.freeEnergyPerSiteEv - 1e-12;
            // A branch that ends up below the disordered one while carrying NO
            // long-range order is the domain-mixed boundary solution described
            // in the header, not a phase. Refusing it silently would leave the
            // caller wondering why an obviously ordered alloy reports A1, so
            // it is named.
            if (lower && candidate.longRangeOrder <= 1e-4 && !domainMixWarned) {
                result.warnings.push_back(
                    "At " + std::to_string(static_cast<int>(t))
                    + " K the ordered trial converged onto a state with zero "
                      "long-range order but a free energy below the "
                      "disordered branch. That is the equal mixture of "
                      "symmetry-related ordered domains, a fixed point of the "
                      "natural iteration on the boundary of the simplex rather "
                      "than a homogeneous phase, and it is discarded. Bracket "
                      "the transition from inside the ordered field rather "
                      "than from 0 K.");
                domainMixWarned = true;
            }
            // A strict margin, so that an ordered branch which merely
            // reproduced the disordered solution to rounding does not get
            // reported as an ordered phase.
            if (candidate.freeEnergyPerSiteEv
                    < best.freeEnergyPerSiteEv - 1e-12
                && candidate.longRangeOrder > 1e-4)
                best = candidate;
        }
        if (best.entropyPerSiteKb < -1e-9)
            negativeEntropy = true;

        if (best.longRangeOrder > 1e-4) {
            sawOrdered = true;
            if (t > highestOrderedT) {
                highestOrderedT = t;
                lowestOrdered = best.startOrder;
            }
        } else {
            if (!sawDisordered || t < lowestDisorderedT)
                lowestDisorderedT = t;
            sawDisordered = true;
        }

        result.points.push_back(std::move(best));
        result.disorderedPoints.push_back(std::move(disordered));
    }

    if (sawOrdered && sawDisordered && lowestDisorderedT > highestOrderedT) {
        bool ok = false;
        const double tc = sublatticeOrderDisorderTemperature(
            input, lowestOrdered, highestOrderedT, lowestDisorderedT, 0.05,
            &ok);
        if (ok) {
            result.transitionTemperatureK = tc;
            result.orderedPhase = lowestOrdered;
            // Evaluated a hair either side rather than AT T_c: at the
            // transition itself the two branches are degenerate by
            // construction and which one a solver lands on is arbitrary.
            const double delta = std::max(0.1, 0.001 * tc);
            double etaBelow = 0.0;
            orderedIsStable(input, tc - delta, lowestOrdered, &etaBelow);
            result.orderParameterBelowTc = etaBelow;
            // Above T_c the STABLE phase is the disordered one, so the stable
            // order parameter is zero — while the ordered branch is still
            // there, still strongly ordered, merely metastable. Those two
            // numbers together are the first-order signature; reporting only
            // the metastable branch (which barely changes across T_c) would
            // make a first-order transition look like nothing happened.
            bool okAbove = false;
            const SublatticeCvmPoint above = solveSublatticeCvmPoint(
                input, tc + delta, lowestOrdered, &okAbove);
            result.metastableOrderParameterAboveTc =
                okAbove ? above.longRangeOrder : 0.0;
            result.orderParameterAboveTc = 0.0;
            result.firstOrder =
                (result.orderParameterBelowTc - result.orderParameterAboveTc)
                > 0.05;
        }
    } else if (sawOrdered && !sawDisordered) {
        result.warnings.push_back(
            "The ordered phase is still stable at the highest temperature "
            "scanned, so no transition temperature is reported. Raise the "
            "range if you want one.");
    }

    if (negativeEntropy)
        result.warnings.push_back(
            "The configurational entropy came out NEGATIVE at some "
            "temperature. That is a known failure of the truncated Kikuchi "
            "expansion on the frustrated FCC lattice — the expansion is not a "
            "probability and nothing forces it to stay positive — and it "
            "happens to the disordered branch well below the transition, "
            "where that branch is not the stable one anyway. The ordered "
            "branch and the transition temperature are unaffected, but the "
            "disordered free energy at those temperatures is meaningless.");

    result.warnings.push_back(
        "Nearest-neighbour PAIR interactions only: no triplet or quadruplet "
        "effective cluster interactions and no second-neighbour pairs are "
        "included, and for real alloys those are not negligible. Treat the "
        "transition temperature as the ordering tendency of this interaction, "
        "not as a reproduction of a measured phase diagram.");
    result.warnings.push_back(
        "Ordered structures are searched only among the trial patterns "
        "supplied, at FIXED overall composition. This compares homogeneous "
        "phases; there is no common-tangent construction, so two-phase "
        "equilibria and the solvus boundaries of the ordered fields are "
        "absent. It is a transition temperature at one composition, not a "
        "phase diagram.");

    result.ok = true;
    return result;
}

} // namespace calango::core
