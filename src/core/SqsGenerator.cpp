#include "core/SqsGenerator.hpp"

#include "core/Element.hpp"
#include "core/PeriodicImages.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <stdexcept>

namespace calango::core {

namespace {

/// One pair interaction on the substitutional sublattice: the two sublattice
/// site ranks and which coordination shell it falls in. Building this list
/// once turns each objective evaluation into a walk over pairs rather than a
/// neighbor search.
struct ShellPair {
    int a = 0;
    int b = 0;
    int shell = 0;
};

/// Largest-remainder rounding of fractional composition onto whole sites, so
/// the counts sum to `sites` exactly (plain rounding does not).
std::vector<int> siteCounts(const std::vector<double>& fractions, int sites)
{
    const double total = std::accumulate(fractions.begin(), fractions.end(), 0.0);
    std::vector<double> ideal(fractions.size());
    std::vector<int> counts(fractions.size());
    int assigned = 0;
    for (std::size_t i = 0; i < fractions.size(); ++i) {
        ideal[i] = fractions[i] / total * sites;
        counts[i] = static_cast<int>(std::floor(ideal[i]));
        assigned += counts[i];
    }
    // Hand the leftover sites to the largest fractional remainders.
    std::vector<std::size_t> order(fractions.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t l, std::size_t r) {
        return (ideal[l] - counts[l]) > (ideal[r] - counts[r]);
    });
    for (int k = 0; assigned < sites; ++k, ++assigned)
        ++counts[order[static_cast<std::size_t>(k) % order.size()]];
    return counts;
}

} // namespace

Structure SqsGenerator::makeSupercell(const Structure& base, int nx, int ny,
                                      int nz)
{
    Structure super;
    const auto& v = base.cell().vectors();
    super.setCell(UnitCell(v[0] * static_cast<double>(nx),
                           v[1] * static_cast<double>(ny),
                           v[2] * static_cast<double>(nz)));
    // Image-major ordering (all atoms of image 000, then 001, …) so a site's
    // index maps back to (image, basis atom) by simple division.
    for (int ia = 0; ia < nx; ++ia) {
        for (int ib = 0; ib < ny; ++ib) {
            for (int ic = 0; ic < nz; ++ic) {
                const Vec3 shift = v[0] * static_cast<double>(ia)
                    + v[1] * static_cast<double>(ib)
                    + v[2] * static_cast<double>(ic);
                for (const Atom& atom : base.atoms()) {
                    Atom copy = atom;
                    copy.position = atom.position + shift;
                    super.addAtom(copy);
                }
            }
        }
    }
    return super;
}

SqsGenerator::Result SqsGenerator::generate(const Structure& base,
                                            const Params& params)
{
    if (params.composition.size() < 2)
        throw std::invalid_argument(
            "SQS needs at least two species in the target composition");
    if (!base.cell().isDefined())
        throw std::invalid_argument(
            "SQS needs a periodic cell — the sublattice and its coordination "
            "shells are only defined with one");

    // -- Species table ------------------------------------------------------
    std::vector<int> speciesZ;
    std::vector<double> fractions;
    for (const auto& [symbol, fraction] : params.composition) {
        const int z = Elements::atomicNumber(symbol);
        if (z == 0)
            throw std::invalid_argument("unknown chemical symbol: " + symbol);
        if (fraction <= 0.0)
            throw std::invalid_argument("composition fractions must be > 0");
        speciesZ.push_back(z);
        fractions.push_back(fraction);
    }
    const int nSpecies = static_cast<int>(speciesZ.size());

    // -- Supercell + substitutional sublattice ------------------------------
    Structure super = makeSupercell(base, std::max(1, params.nx),
                                    std::max(1, params.ny),
                                    std::max(1, params.nz));
    const int replaceZ = Elements::atomicNumber(params.replaceElement);
    if (replaceZ == 0)
        throw std::invalid_argument("unknown element to replace: "
                                    + params.replaceElement);

    std::vector<int> sites; // indices into super.atoms()
    for (std::size_t i = 0; i < super.atoms().size(); ++i)
        if (super.atoms()[i].atomicNumber == replaceZ)
            sites.push_back(static_cast<int>(i));
    if (sites.empty())
        throw std::invalid_argument("no " + params.replaceElement
                                    + " sites in the supercell");
    const int nSites = static_cast<int>(sites.size());
    if (nSites < nSpecies)
        throw std::invalid_argument(
            "the sublattice has fewer sites than species to place");

    // -- Shell cutoffs ------------------------------------------------------
    std::vector<double> cutoffs;
    for (const double cutoff : {params.shell1, params.shell2})
        if (cutoff > 0.0 && (cutoffs.empty() || cutoff > cutoffs.back()))
            cutoffs.push_back(cutoff);
    if (cutoffs.empty())
        throw std::invalid_argument("no positive coordination-shell cutoff");
    const double rmax = cutoffs.back();

    // -- Pair list over the sublattice, binned by shell ---------------------
    // Minimum-image is not enough here: a supercell can be thinner than the
    // outer cutoff along one axis, so the same pair is a neighbor through
    // several images and every one of them contributes.
    const auto& cellVectors = super.cell().vectors();
    const auto range = imageRange(super.cell(), rmax);
    std::vector<ShellPair> pairs;
    std::vector<int> shellPairCount(cutoffs.size(), 0);
    for (int a = 0; a < nSites; ++a) {
        const Vec3 pa = super.atoms()[sites[a]].position;
        for (int b = a + 1; b < nSites; ++b) {
            const Vec3 pb = super.atoms()[sites[b]].position;
            for (int ia = -range[0]; ia <= range[0]; ++ia) {
                for (int ib = -range[1]; ib <= range[1]; ++ib) {
                    for (int ic = -range[2]; ic <= range[2]; ++ic) {
                        const Vec3 shift =
                            cellVectors[0] * static_cast<double>(ia)
                            + cellVectors[1] * static_cast<double>(ib)
                            + cellVectors[2] * static_cast<double>(ic);
                        const double d = (pb + shift - pa).norm();
                        if (d < 1e-6 || d > rmax)
                            continue;
                        // First cutoff the distance falls under wins.
                        for (std::size_t s = 0; s < cutoffs.size(); ++s) {
                            if (d <= cutoffs[s]) {
                                pairs.push_back({a, b, static_cast<int>(s)});
                                ++shellPairCount[s];
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    if (pairs.empty())
        throw std::invalid_argument(
            "no sublattice pairs within the shell cutoffs — raise the first "
            "shell cutoff or enlarge the supercell");

    int populatedShells = 0;
    for (const int count : shellPairCount)
        if (count > 0)
            ++populatedShells;

    // -- Target correlations for the ideal random alloy ---------------------
    // For a random solid solution the probability of finding species (p, q) on
    // the two ends of a bond is just c_p · c_q — no correlation between sites.
    // The counts are exact, so the concentrations come from them rather than
    // from the requested fractions (which the rounding may have nudged).
    const std::vector<int> counts = siteCounts(fractions, nSites);
    std::vector<double> concentration(nSpecies);
    for (int s = 0; s < nSpecies; ++s)
        concentration[s] = static_cast<double>(counts[s]) / nSites;

    std::vector<double> weights(cutoffs.size());
    for (std::size_t s = 0; s < cutoffs.size(); ++s)
        weights[s] = s < params.shellWeights.size()
            ? params.shellWeights[s]
            : 1.0 / static_cast<double>(s + 1);

    // -- Objective ----------------------------------------------------------
    // Ordered pair counts per shell, normalized to probabilities, compared
    // against c_p·c_q. `labels[site]` is the species index on that site.
    const int pairStride = nSpecies * nSpecies;
    std::vector<double> pairProbability(cutoffs.size() * pairStride);

    const auto tally = [&](const std::vector<int>& labels) {
        std::fill(pairProbability.begin(), pairProbability.end(), 0.0);
        for (const ShellPair& p : pairs) {
            const int la = labels[static_cast<std::size_t>(p.a)];
            const int lb = labels[static_cast<std::size_t>(p.b)];
            const std::size_t base =
                static_cast<std::size_t>(p.shell) * pairStride;
            // Both orderings: an unordered pair contributes to (p,q) and (q,p).
            pairProbability[base + la * nSpecies + lb] += 1.0;
            pairProbability[base + lb * nSpecies + la] += 1.0;
        }
    };

    const auto objectiveOf = [&](const std::vector<int>& labels) {
        tally(labels);
        double total = 0.0;
        for (std::size_t s = 0; s < cutoffs.size(); ++s) {
            if (shellPairCount[s] == 0)
                continue;
            const double norm = 2.0 * shellPairCount[s];
            const std::size_t base = s * pairStride;
            for (int p = 0; p < nSpecies; ++p) {
                for (int q = 0; q < nSpecies; ++q) {
                    const double observed =
                        pairProbability[base + p * nSpecies + q] / norm;
                    const double target = concentration[p] * concentration[q];
                    total += weights[s] * std::abs(observed - target);
                }
            }
        }
        return total;
    };

    // -- Initial decoration: a plain random shuffle at the exact composition -
    std::vector<int> labels;
    labels.reserve(nSites);
    for (int s = 0; s < nSpecies; ++s)
        labels.insert(labels.end(), counts[s], s);
    std::mt19937 rng(params.seed);
    std::shuffle(labels.begin(), labels.end(), rng);

    double current = objectiveOf(labels);
    const double initial = current;
    std::vector<int> best = labels;
    double bestObjective = current;

    // -- Metropolis annealing over species swaps ----------------------------
    // Swapping two sites conserves the composition exactly, so the search
    // never has to reject a move for being off-stoichiometry.
    const int steps = std::max(1, params.steps);
    const double t0 = std::max(params.startTemperature, 1e-12);
    const double t1 = std::max(params.endTemperature, 1e-12);
    std::uniform_int_distribution<int> site(0, nSites - 1);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    int accepted = 0;

    for (int step = 0; step < steps; ++step) {
        const int a = site(rng);
        const int b = site(rng);
        if (labels[static_cast<std::size_t>(a)]
            == labels[static_cast<std::size_t>(b)])
            continue; // same species — the swap is a no-op
        std::swap(labels[static_cast<std::size_t>(a)],
                  labels[static_cast<std::size_t>(b)]);
        const double proposal = objectiveOf(labels);
        const double temperature =
            t0 * std::pow(t1 / t0, static_cast<double>(step) / (steps - 1));
        if (proposal <= current
            || unit(rng) < std::exp(-(proposal - current) / temperature)) {
            current = proposal;
            ++accepted;
            if (current < bestObjective) {
                bestObjective = current;
                best = labels;
            }
        } else {
            std::swap(labels[static_cast<std::size_t>(a)],
                      labels[static_cast<std::size_t>(b)]);
        }
    }

    // -- Decorate ------------------------------------------------------------
    for (int s = 0; s < nSites; ++s)
        super.atoms()[static_cast<std::size_t>(sites[s])].atomicNumber =
            speciesZ[static_cast<std::size_t>(best[static_cast<std::size_t>(s)])];

    Result result;
    result.structure = std::move(super);
    result.objective = bestObjective;
    result.initialObjective = initial;
    result.shells = populatedShells;
    result.sublatticeSites = nSites;
    result.steps = steps;
    result.accepted = accepted;
    result.method = "Monte Carlo simulated annealing (native)";
    return result;
}

} // namespace calango::core
