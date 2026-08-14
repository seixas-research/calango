#include "core/ClusterVariation.hpp"
#include "core/PhysicalConstants.hpp"

#include <algorithm>
#include <cmath>

namespace calango::core {

namespace {

/// x ln x, continuous at zero.
///
/// Written out because 0*log(0) is NaN in IEEE arithmetic and a single NaN
/// anywhere in these sums poisons the entropy, the free energy and every
/// subsequent temperature. Empty cluster states are the norm, not the
/// exception: a strongly ordered alloy drives most tetrahedron probabilities
/// to zero.
double xlnx(double x)
{
    return x > 0.0 ? x * std::log(x) : 0.0;
}

} // namespace

double idealConfigurationalEntropy(const std::vector<double>& composition)
{
    double total = 0.0;
    for (const double x : composition)
        total += x;
    if (total <= 0.0)
        return 0.0;
    double entropy = 0.0;
    for (const double x : composition)
        entropy -= xlnx(x / total);
    return entropy;
}

int cvmCoordination(CvmLattice lattice)
{
    switch (lattice) {
    case CvmLattice::Fcc:
        return 12;
    case CvmLattice::Bcc:
        return 8;
    case CvmLattice::Chain:
        return 2;
    }
    return 12;
}

std::vector<double> pairEnergiesFromEci(double pairEci, bool* ok)
{
    // The +/-1 correlation basis writes the pair energy as J * s_i * s_j with
    // s = +1 for A and -1 for B, so
    //     e_AA = e_BB = +J,  e_AB = e_BA = -J.
    // The sign convention that follows: J > 0 makes LIKE neighbours cost
    // energy, which drives ORDERING. Spelled out because the opposite
    // convention is equally common in the literature and the two produce
    // qualitatively different alloys from the same number.
    if (ok)
        *ok = true;
    return {pairEci, -pairEci, -pairEci, pairEci};
}

std::vector<double> tripletEnergiesFromEci(double tripletEci, bool* ok)
{
    if (ok)
        *ok = true;
    // s = +1 for species 0 (A), -1 for species 1 (B).
    std::vector<double> energies(8, 0.0);
    const double sign[2] = {1.0, -1.0};
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k)
                energies[static_cast<std::size_t>((i * 2 + j) * 2 + k)] =
                    tripletEci * sign[i] * sign[j] * sign[k];
    return energies;
}

namespace {

/// Pair probabilities at one temperature, by the quasi-chemical form that
/// stationarity of the BPG functional implies exactly.
///
/// Minimizing F = (z/2) sum y e - T S with
///     S/k_B = -(z/2) sum y ln y + (z-1) sum x ln x
/// subject to sum_j y_ij = x_i gives
///     y_ij = a_i a_j exp(-beta e_ij),
/// with the a_i fixed by the marginals. The coordination z drops out of the
/// stationarity condition entirely — it scales the whole pair term — so the
/// pair probabilities depend on the lattice ONLY through z's appearance in
/// the entropy prefactor, which is why this same routine serves every
/// lattice.
///
/// Solved by the natural iteration a_i <- x_i / sum_j a_j exp(-beta e_ij),
/// which is the standard quasi-chemical fixed point and decreases the
/// functional monotonically.
bool solvePairProbabilities(int species, const std::vector<double>& x,
                            const std::vector<double>& energies, double beta,
                            int maxIterations, double tolerance,
                            std::vector<double>& y, int* iterations)
{
    std::vector<double> boltzmann(static_cast<std::size_t>(species) * species);
    for (int i = 0; i < species; ++i)
        for (int j = 0; j < species; ++j) {
            const double e = 0.5
                * (energies[static_cast<std::size_t>(i) * species + j]
                   + energies[static_cast<std::size_t>(j) * species + i]);
            boltzmann[static_cast<std::size_t>(i) * species + j] =
                std::exp(-beta * e);
        }

    std::vector<double> a(species, 1.0);
    std::vector<double> next(species, 0.0);
    int used = 0;
    bool converged = false;
    for (; used < maxIterations; ++used) {
        double change = 0.0;
        for (int i = 0; i < species; ++i) {
            double sum = 0.0;
            for (int j = 0; j < species; ++j)
                sum += a[j] * boltzmann[static_cast<std::size_t>(i) * species + j];
            next[i] = sum > 0.0 ? x[i] / sum : 0.0;
        }
        // Under-relaxed. The bare fixed point oscillates without converging
        // for strongly ordering interactions at low temperature — the very
        // regime the module exists to describe — because the update
        // overshoots between the two sublattices it is trying to average
        // over. Halving the step removes the oscillation at the cost of
        // roughly twice the iterations.
        for (int i = 0; i < species; ++i) {
            const double updated = std::sqrt(std::max(1e-300, a[i] * next[i]));
            change = std::max(change, std::abs(updated - a[i]));
            a[i] = updated;
        }
        if (change < tolerance) {
            converged = true;
            ++used;
            break;
        }
    }
    if (iterations)
        *iterations = used;

    y.assign(static_cast<std::size_t>(species) * species, 0.0);
    double total = 0.0;
    for (int i = 0; i < species; ++i)
        for (int j = 0; j < species; ++j) {
            const double value = a[i] * a[j]
                * boltzmann[static_cast<std::size_t>(i) * species + j];
            y[static_cast<std::size_t>(i) * species + j] = value;
            total += value;
        }
    if (total <= 0.0)
        return false;
    for (double& value : y)
        value /= total;
    return converged;
}


/// The six vertex-pairs of a tetrahedron, and its four vertices.
constexpr int kTetPairs[6][2] = {{0, 1}, {0, 2}, {0, 3},
                                 {1, 2}, {1, 3}, {2, 3}};
/// The four triangular faces of the tetrahedron.
///
/// Counting, since it decides a factor: FCC has 8 nearest-neighbour triangles
/// per site and 2 tetrahedra per site, and each tetrahedron carries 4 faces —
/// 2 x 4 = 8, so every triangle is counted exactly once per site by summing
/// each tetrahedron's own faces and weighting by the 2 tetrahedra. That is the
/// same bookkeeping that gives the pairs a factor z/2 = 6.
constexpr int kTetTriangles[4][3] = {{0, 1, 2}, {0, 1, 3},
                                     {0, 2, 3}, {1, 2, 3}};

/// Kikuchi tetrahedron approximation on FCC, by natural iteration.
///
/// The entropy is
///
///     S/k_B = -2 sum_4 w ln w + 6 sum_2 y ln y - 5 sum_1 x ln x
///
/// with the Kikuchi-Barker coefficients (a4, a3, a2, a1) = (2, 0, -6, 5) for
/// FCC. They are not free: Moebius inversion over the subcluster lattice with
/// 2 tetrahedra, 8 triangles, 6 pairs and 1 point per site gives
///     a4 = 2,  a3 = 8 - 4*2 = 0,  a2 = 6 - 6*2 = -6,  a1 = 1 - (4*2 - 2*6) = 5,
/// and the triangles genuinely vanish. The check that they are right is that
/// the random limit w = prod(x) collapses S to -sum x ln x EXACTLY:
///     -2*4 + 6*2 - 5*1 = -1.
///
/// Stationarity, with multipliers for normalization AND for the fixed
/// composition, gives
///
///     w_ijkl  ~  exp(-beta/2 * sum_6 eps_pq) * prod_6 y_pq^(1/2) * prod_4 c_p
///
/// where c_p carries the composition multiplier. **The composition multiplier
/// is load-bearing and is what an earlier version of this derivation left
/// out.** Without it the ideal limit comes out as w ~ (prod x)^(7/8) instead
/// of prod x — which is invisible for an equiatomic alloy, because there
/// every tuple carries the same factor and normalization hides the error, and
/// wrong for every other composition. With it, the product form is stationary
/// exactly when nu_p = ln x_p, i.e. c_p = x_p^(-1/2).
///
/// Iterated with the PREVIOUS y on the right-hand side — Kikuchi's natural
/// iteration, which decreases the functional monotonically, rather than a
/// bare fixed point which oscillates between the sublattices it is trying to
/// average over.
bool solveTetrahedron(int species, const std::vector<double>& x,
                      const std::vector<double>& energies,
                      const std::vector<double>& triplets, double beta,
                      int maxIterations, double tolerance,
                      std::vector<double>& w, std::vector<double>& y,
                      int* iterations)
{
    const int k = species;
    std::size_t states = 1;
    for (int i = 0; i < 4; ++i)
        states *= static_cast<std::size_t>(k);

    // Decode helper: tuple index -> four species indices.
    const auto decode = [k](std::size_t index, int* out) {
        for (int position = 3; position >= 0; --position) {
            out[position] = static_cast<int>(index % k);
            index /= k;
        }
    };

    // exp(-beta/2 * sum over the six pairs), fixed for the whole solve.
    std::vector<double> bond(states, 0.0);
    for (std::size_t index = 0; index < states; ++index) {
        int t[4];
        decode(index, t);
        double sum = 0.0;
        for (const auto& pair : kTetPairs) {
            const int a = t[pair[0]];
            const int b = t[pair[1]];
            sum += 0.5
                * (energies[static_cast<std::size_t>(a) * k + b]
                   + energies[static_cast<std::size_t>(b) * k + a]);
        }
        // The four faces. Symmetrized over the six permutations of each
        // triangle's vertices, so an asymmetric tensor supplied by a caller
        // cannot make the energy depend on which vertex was written first.
        if (!triplets.empty()) {
            for (const auto& tri : kTetTriangles) {
                const int a = t[tri[0]];
                const int b = t[tri[1]];
                const int c = t[tri[2]];
                const int perm[6][3] = {{a, b, c}, {a, c, b}, {b, a, c},
                                        {b, c, a}, {c, a, b}, {c, b, a}};
                double face = 0.0;
                for (const auto& p : perm)
                    face += triplets[static_cast<std::size_t>(
                        (p[0] * k + p[1]) * k + p[2])];
                sum += face / 6.0;
            }
        }
        bond[index] = std::exp(-0.5 * beta * sum);
    }

    // Start from the random alloy: the correct answer at infinite temperature
    // and a good start at any temperature.
    w.assign(states, 0.0);
    for (std::size_t index = 0; index < states; ++index) {
        int t[4];
        decode(index, t);
        w[index] = x[t[0]] * x[t[1]] * x[t[2]] * x[t[3]];
    }

    const std::size_t pairCount = static_cast<std::size_t>(k) * k;
    y.assign(pairCount, 0.0);
    std::vector<double> c(k, 1.0);
    std::vector<double> current(k, 0.0);
    std::vector<double> next(states, 0.0);

    // Marginals, averaged over all six position-pairs and all four positions
    // rather than read off positions (0,1) and (0). For a symmetric w they
    // agree; averaging keeps them right if rounding ever breaks the symmetry.
    const auto marginals = [&](const std::vector<double>& dist,
                               std::vector<double>* pairOut,
                               std::vector<double>* pointOut) {
        if (pairOut)
            pairOut->assign(pairCount, 0.0);
        if (pointOut)
            pointOut->assign(k, 0.0);
        for (std::size_t index = 0; index < dist.size(); ++index) {
            const double value = dist[index];
            if (value == 0.0)
                continue;
            int t[4];
            decode(index, t);
            if (pairOut)
                for (const auto& pair : kTetPairs)
                    (*pairOut)[static_cast<std::size_t>(t[pair[0]]) * k
                               + t[pair[1]]] += value / 6.0;
            if (pointOut)
                for (int position = 0; position < 4; ++position)
                    (*pointOut)[t[position]] += value / 4.0;
        }
    };

    int used = 0;
    bool converged = false;
    for (; used < maxIterations; ++used) {
        marginals(w, &y, nullptr);

        // Symmetrize the pair marginal. y_pq and y_qp are the same physical
        // quantity on a homogeneous lattice, and letting them drift apart
        // makes the update asymmetric in a way that shows up as spurious
        // order.
        for (int i = 0; i < k; ++i)
            for (int j = i + 1; j < k; ++j) {
                const std::size_t ab = static_cast<std::size_t>(i) * k + j;
                const std::size_t ba = static_cast<std::size_t>(j) * k + i;
                const double mean = 0.5 * (y[ab] + y[ba]);
                y[ab] = mean;
                y[ba] = mean;
            }

        // Base of the update: bond term times the product of pair marginals.
        for (std::size_t index = 0; index < states; ++index) {
            int t[4];
            decode(index, t);
            double product = bond[index];
            for (const auto& pair : kTetPairs) {
                const double value =
                    y[static_cast<std::size_t>(t[pair[0]]) * k + t[pair[1]]];
                product *= std::sqrt(std::max(value, 1e-300));
            }
            next[index] = product;
        }

        // Iterative proportional fitting for the composition multipliers c_p.
        // Scaling c_p by r multiplies every tuple by r^(count of p), so the
        // update is damped by the exponent rather than applied raw.
        std::fill(c.begin(), c.end(), 1.0);
        for (int fit = 0; fit < 500; ++fit) {
            double total = 0.0;
            for (std::size_t index = 0; index < states; ++index) {
                int t[4];
                decode(index, t);
                total += next[index] * c[t[0]] * c[t[1]] * c[t[2]] * c[t[3]];
            }
            if (!(total > 0.0))
                return false;
            std::fill(current.begin(), current.end(), 0.0);
            for (std::size_t index = 0; index < states; ++index) {
                int t[4];
                decode(index, t);
                const double value =
                    next[index] * c[t[0]] * c[t[1]] * c[t[2]] * c[t[3]] / total;
                for (int position = 0; position < 4; ++position)
                    current[t[position]] += value / 4.0;
            }
            double error = 0.0;
            for (int p = 0; p < k; ++p)
                error = std::max(error, std::abs(current[p] - x[p]));
            if (error < 1e-14)
                break;
            for (int p = 0; p < k; ++p)
                if (current[p] > 0.0 && x[p] > 0.0)
                    c[p] *= std::pow(x[p] / current[p], 0.5);
        }

        double total = 0.0;
        for (std::size_t index = 0; index < states; ++index) {
            int t[4];
            decode(index, t);
            next[index] *= c[t[0]] * c[t[1]] * c[t[2]] * c[t[3]];
            total += next[index];
        }
        if (!(total > 0.0))
            return false;

        double change = 0.0;
        for (std::size_t index = 0; index < states; ++index) {
            const double updated = next[index] / total;
            change = std::max(change, std::abs(updated - w[index]));
            w[index] = updated;
        }
        if (change < tolerance) {
            converged = true;
            ++used;
            break;
        }
    }
    if (iterations)
        *iterations = used;
    marginals(w, &y, nullptr);
    return converged;
}

/// Warren-Cowley alpha from pair probabilities.
///
/// alpha_ij = 1 - P(j|i)/x_j with P(j|i) = y_ij/x_i. Zero is random; negative
/// means i prefers unlike neighbour j (ordering), positive means it prefers
/// like (clustering). Reported alongside the entropy because S alone says how
/// FAR from random the alloy is but not in which direction.
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
            const double conditional = y[index] / x[i];
            alpha[index] = 1.0 - conditional / x[j];
        }
    return alpha;
}

} // namespace

CvmResult solveClusterVariation(const CvmInput& input)
{
    CvmResult result;

    const int species = static_cast<int>(input.composition.size());
    if (species < 2) {
        result.warnings.push_back(
            "Configurational entropy needs at least two species: a pure "
            "element has exactly one arrangement and no configurational "
            "entropy at all.");
        return result;
    }
    if (!input.species.empty()
        && static_cast<int>(input.species.size()) != species) {
        result.warnings.push_back(
            "The species list and the composition have different lengths.");
        return result;
    }

    std::vector<double> x = input.composition;
    double total = 0.0;
    for (const double value : x) {
        if (value < 0.0) {
            result.warnings.push_back("Negative mole fraction.");
            return result;
        }
        total += value;
    }
    if (total <= 0.0) {
        result.warnings.push_back("The composition sums to zero.");
        return result;
    }
    for (double& value : x)
        value /= total;

    const std::size_t pairCount = static_cast<std::size_t>(species) * species;
    std::vector<double> energies = input.pairEnergiesEv;
    if (energies.empty())
        energies.assign(pairCount, 0.0);
    if (energies.size() != pairCount) {
        result.warnings.push_back(
            "The pair energy matrix is not species x species.");
        return result;
    }

    // The triplet tensor, when one was supplied. Only the tetrahedron can use
    // it — a triangle is not a subcluster of a pair — so asking for triplets
    // with the pair approximation is a contradiction rather than a detail,
    // and is reported instead of being ignored.
    std::vector<double> triplets = input.tripletEnergiesEv;
    const std::size_t tripletCount =
        static_cast<std::size_t>(species) * species * species;
    if (!triplets.empty() && triplets.size() != tripletCount) {
        result.warnings.push_back(
            "The triplet energy tensor is not species x species x species.");
        return result;
    }
    if (!triplets.empty()
        && input.approximation != CvmApproximation::Tetrahedron) {
        result.warnings.push_back(
            "Triplet interactions were supplied but the approximation is not "
            "the tetrahedron, which is the only one with triangles in it. "
            "They are IGNORED here — the entropy and the energy below "
            "describe a pair-only model, so do not read them as the cluster "
            "expansion that was fitted.");
        triplets.clear();
    }

    result.idealEntropyKb = idealConfigurationalEntropy(x);

    const int z = cvmCoordination(input.lattice);
    if (input.approximation == CvmApproximation::Tetrahedron
        && input.lattice == CvmLattice::Chain) {
        result.warnings.push_back(
            "A chain has no tetrahedra, so the tetrahedron approximation is "
            "not defined on it; the pair approximation is used instead — and "
            "on a chain the pair approximation is EXACT, so nothing is lost.");
    }
    const bool tetrahedron =
        input.approximation == CvmApproximation::Tetrahedron
        && input.lattice != CvmLattice::Chain;

    const int steps = std::max(1, input.temperatureSteps);
    result.points.reserve(steps);

    double highestOrdered = 0.0;
    bool sroVanishedInRange = false;
    constexpr double kOrderThreshold = 0.01;

    for (int step = 0; step < steps; ++step) {
        const double t = steps == 1
            ? input.minTemperatureK
            : input.minTemperatureK
                + (input.maxTemperatureK - input.minTemperatureK) * step
                    / (steps - 1);
        CvmPoint point;
        point.temperatureK = t;

        if (input.approximation == CvmApproximation::Point) {
            // Bragg-Williams: sites independent, so the pair probabilities are
            // products of concentrations regardless of the interaction, and
            // the entropy is the ideal one at every temperature. The energy
            // still responds to the interaction — that is exactly what makes
            // this approximation wrong rather than merely coarse: it lets the
            // alloy lower its energy without paying any entropy for the order
            // that requires.
            point.pairProbabilities.assign(pairCount, 0.0);
            for (int i = 0; i < species; ++i)
                for (int j = 0; j < species; ++j)
                    point.pairProbabilities[static_cast<std::size_t>(i) * species
                                            + j] = x[i] * x[j];
            point.entropyPerSiteKb = result.idealEntropyKb;
            point.warrenCowley.assign(pairCount, 0.0);
            point.converged = true;
            point.iterations = 0;
        } else {
            const double beta = t > 0.0 ? 1.0 / (kBoltzmannEvPerK * t) : 1e30;
            std::vector<double> y;
            std::vector<double> w;
            int iterations = 0;
            if (tetrahedron) {
                point.converged = solveTetrahedron(
                    species, x, energies, triplets, beta, input.maxIterations,
                    input.tolerance, w, y, &iterations);
            } else {
                point.converged = solvePairProbabilities(
                    species, x, energies, beta, input.maxIterations,
                    input.tolerance, y, &iterations);
            }
            point.iterations = iterations;
            if (y.empty()) {
                result.warnings.push_back(
                    "The pair probabilities failed to solve at "
                    + std::to_string(static_cast<int>(t)) + " K.");
                return result;
            }
            point.pairProbabilities = y;
            point.tetrahedronProbabilities = w;

            // S/k_B = -(z/2) sum y ln y + (z-1) sum x ln x.
            //
            // The random limit y = x_i x_j collapses this to -sum x ln x
            // exactly, for every z — which is the identity that makes the
            // prefactors checkable rather than asserted.
            double pairTerm = 0.0;
            for (const double value : y)
                pairTerm += xlnx(value);
            double pointTerm = 0.0;
            for (const double value : x)
                pointTerm += xlnx(value);
            if (tetrahedron) {
                // S/k_B = -2 sum_4 w ln w + 6 sum_2 y ln y - 5 sum_1 x ln x.
                double tetTerm = 0.0;
                for (const double value : w)
                    tetTerm += xlnx(value);
                point.entropyPerSiteKb =
                    -2.0 * tetTerm + 6.0 * pairTerm - 5.0 * pointTerm;
            } else {
                point.entropyPerSiteKb =
                    -0.5 * z * pairTerm + (z - 1.0) * pointTerm;
            }

            point.warrenCowley = warrenCowleyFromPairs(species, x, y);
        }

        // E per site = (z/2) sum_ij y_ij e_ij. Every bond is shared by two
        // sites, which is the whole content of the z/2.
        double energy = 0.0;
        for (int i = 0; i < species; ++i)
            for (int j = 0; j < species; ++j)
                energy += point.pairProbabilities[static_cast<std::size_t>(i)
                                                      * species
                                                  + j]
                    * energies[static_cast<std::size_t>(i) * species + j];
        point.energyPerSiteEv = 0.5 * z * energy;
        // Plus the triangles: 2 tetrahedra per site, each carrying 4 faces,
        // which is exactly the 8 nearest-neighbour triangles a site has.
        if (!triplets.empty() && !point.tetrahedronProbabilities.empty()) {
            double triple = 0.0;
            const int kk = species;
            for (std::size_t index = 0;
                 index < point.tetrahedronProbabilities.size(); ++index) {
                const double p = point.tetrahedronProbabilities[index];
                if (p == 0.0)
                    continue;
                std::size_t rest = index;
                int t[4];
                for (int position = 3; position >= 0; --position) {
                    t[position] = static_cast<int>(rest % kk);
                    rest /= kk;
                }
                for (const auto& tri : kTetTriangles)
                    triple += p
                        * triplets[static_cast<std::size_t>(
                            (t[tri[0]] * kk + t[tri[1]]) * kk + t[tri[2]])];
            }
            point.energyPerSiteEv += 2.0 * triple;
        }
        point.freeEnergyPerSiteEv = point.energyPerSiteEv
            - kBoltzmannEvPerK * t * point.entropyPerSiteKb;

        // Track the highest temperature at which SRO is still measurable AND
        // whether it ever falls away inside the scanned range. Both are
        // needed: SRO that is still strong at the top of the range means the
        // range ended before the physics did, not that a transition sits
        // there.
        bool ordered = false;
        for (const double value : point.warrenCowley)
            if (std::abs(value) > kOrderThreshold)
                ordered = true;
        if (ordered)
            highestOrdered = std::max(highestOrdered, t);
        else
            sroVanishedInRange = true;

        result.points.push_back(std::move(point));
    }

    if (tetrahedron && input.lattice == CvmLattice::Bcc)
        result.warnings.push_back(
            "The tetrahedron geometry used here is the FCC regular "
            "nearest-neighbour tetrahedron, whose Kikuchi-Barker coefficients "
            "(2, 0, -6, 5) follow from 2 tetrahedra and 6 pairs per site. BCC "
            "needs the IRREGULAR tetrahedron built from first and second "
            "neighbours, with different coefficients; the numbers reported "
            "here are the FCC counting applied to a BCC coordination and are "
            "not quantitative.");

    // Reported ONLY when the short-range order actually dies away inside the
    // scanned range. Otherwise the highest scanned temperature is exactly
    // that — the end of the scan — and returning it as a temperature invites
    // it to be read as a transition, which it is not.
    result.sroVanishingTemperatureK = sroVanishedInRange ? highestOrdered : 0.0;
    if (!sroVanishedInRange && highestOrdered > 0.0)
        result.warnings.push_back(
            "Short-range order is still present at the highest temperature "
            "scanned, so no temperature is reported for its disappearance. "
            "Raise the range if you want one.");

    // The limitation that decides whether these numbers can be compared with
    // the alloy literature at all, stated with every result rather than in
    // documentation somewhere.
    result.warnings.push_back(
        "This is the HOMOGENEOUS (single-sublattice) CVM: it describes "
        "short-range order in a DISORDERED solid solution. It cannot produce "
        "a long-range-ordered phase or an order-disorder transition "
        "temperature, because those require the lattice to be split into "
        "sublattices that can differ (four for L1_2/L1_0 on FCC). Do not "
        "compare the entropy here against a published T_c; the "
        "Warren-Cowley alpha in the disordered phase IS comparable against "
        "diffuse-scattering measurements.");

    result.ok = true;
    return result;
}

} // namespace calango::core
