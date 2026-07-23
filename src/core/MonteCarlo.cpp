#include "core/MonteCarlo.hpp"
#include "core/PeriodicImages.hpp"

#include "core/UnitCell.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace calango::core {

namespace {

constexpr double kBoltzmann = 8.617333262e-5; // eV/K

} // namespace

SwapMonteCarloResult runSwapMonteCarlo(const Structure& start,
                                       const SwapMonteCarloOptions& options)
{
    SwapMonteCarloResult result;
    const int n = static_cast<int>(start.size());
    if (n < 2) {
        result.note = "need at least two atoms";
        return result;
    }

    // Distinct species must be >= 2, else there is nothing to swap.
    std::vector<int> species(static_cast<std::size_t>(n));
    {
        std::vector<int> distinct;
        for (int i = 0; i < n; ++i) {
            species[static_cast<std::size_t>(i)] =
                start.atoms()[static_cast<std::size_t>(i)].atomicNumber;
            if (std::find(distinct.begin(), distinct.end(), species[i])
                == distinct.end())
                distinct.push_back(species[i]);
        }
        if (distinct.size() < 2) {
            result.note = "structure has a single species — nothing to swap";
            return result;
        }
    }

    // --- Neighbor list (directed contacts, periodic-image aware) -----------
    const auto& atoms = start.atoms();
    const double rc = options.neighborCutoff;
    const double rc2 = rc * rc;
    std::vector<Vec3> translations{{0, 0, 0}};
    const bool pbc = start.cell().isDefined()
        && (start.cell().pbc()[0] || start.cell().pbc()[1]
            || start.cell().pbc()[2]);
    if (pbc) {
        translations.clear();
        const auto range = imageRange(start.cell(), rc);
        const auto& v = start.cell().vectors();
        for (int i = -range[0]; i <= range[0]; ++i)
            for (int j = -range[1]; j <= range[1]; ++j)
                for (int k = -range[2]; k <= range[2]; ++k)
                    translations.push_back(v[0] * i + v[1] * j + v[2] * k);
    }

    std::vector<std::vector<int>> neighbors(static_cast<std::size_t>(n));
    long totalBonds = 0; // directed
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const Vec3 base = atoms[static_cast<std::size_t>(j)].position
                - atoms[static_cast<std::size_t>(i)].position;
            for (const Vec3& t : translations) {
                if (i == j && t.dot(t) < 1e-12)
                    continue;
                const Vec3 d = base + t;
                const double d2 = d.dot(d);
                if (d2 < rc2 && d2 > 1e-8) {
                    neighbors[static_cast<std::size_t>(i)].push_back(j);
                    ++totalBonds;
                }
            }
        }
    }
    if (totalBonds == 0) {
        result.note = "no neighbor bonds within the cutoff — increase it";
        return result;
    }

    const double V = options.interactionEv;
    auto unlike = [&](int a, int b) { return species[a] != species[b] ? 1 : 0; };

    // Total energy = V * (number of unlike bonds). Directed contacts are
    // counted then halved so each bond is counted once.
    auto totalEnergy = [&]() {
        long unlikeDirected = 0;
        for (int i = 0; i < n; ++i)
            for (int j : neighbors[static_cast<std::size_t>(i)])
                unlikeDirected += unlike(i, j);
        return 0.5 * static_cast<double>(unlikeDirected) * V;
    };

    // Energy change when swapping the species of sites p and q (p != q,
    // different species). Only bonds incident to p or q change; the p–q bond
    // itself keeps the same unordered species pair, so it is excluded.
    auto swapDeltaE = [&](int p, int q) {
        const int sp = species[p], sq = species[q];
        int deltaUnlike = 0;
        for (int j : neighbors[static_cast<std::size_t>(p)]) {
            if (j == q)
                continue;
            deltaUnlike += (sq != species[j] ? 1 : 0) - (sp != species[j] ? 1 : 0);
        }
        for (int j : neighbors[static_cast<std::size_t>(q)]) {
            if (j == p)
                continue;
            deltaUnlike += (sp != species[j] ? 1 : 0) - (sq != species[j] ? 1 : 0);
        }
        return static_cast<double>(deltaUnlike) * V;
    };

    std::mt19937 rng(options.seed);
    std::uniform_int_distribution<int> pickSite(0, n - 1);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    const double kT = std::max(kBoltzmann * options.temperatureK, 1e-12);

    double energy = totalEnergy();
    result.initialEnergy = energy;
    double bestEnergy = energy;
    std::vector<int> bestSpecies = species;

    auto recordSnapshot = [&]() {
        Structure snap = start;
        for (int i = 0; i < n; ++i)
            snap.atoms()[static_cast<std::size_t>(i)].atomicNumber = species[i];
        result.snapshots.push_back(std::move(snap));
    };
    result.stepTrace.push_back(0);
    result.energyTrace.push_back(energy);
    recordSnapshot();

    long accepted = 0;
    for (long step = 1; step <= options.steps; ++step) {
        int p = pickSite(rng);
        int q = pickSite(rng);
        // Require two different species; a few retries keep it cheap.
        for (int tries = 0; tries < 8 && species[p] == species[q]; ++tries)
            q = pickSite(rng);
        if (species[p] == species[q])
            continue;

        const double dE = swapDeltaE(p, q);
        if (dE <= 0.0 || uniform(rng) < std::exp(-dE / kT)) {
            std::swap(species[p], species[q]);
            energy += dE;
            ++accepted;
            if (energy < bestEnergy) {
                bestEnergy = energy;
                bestSpecies = species;
            }
        }

        if (options.snapshotInterval > 0 && step % options.snapshotInterval == 0) {
            result.stepTrace.push_back(static_cast<int>(step));
            result.energyTrace.push_back(energy);
            recordSnapshot();
        }
    }

    // Final and best structures.
    result.finalStructure = start;
    for (int i = 0; i < n; ++i)
        result.finalStructure.atoms()[static_cast<std::size_t>(i)].atomicNumber =
            species[i];
    result.bestStructure = start;
    for (int i = 0; i < n; ++i)
        result.bestStructure.atoms()[static_cast<std::size_t>(i)].atomicNumber =
            bestSpecies[i];

    result.finalEnergy = energy;
    result.bestEnergy = bestEnergy;
    result.acceptedMoves = accepted;
    result.acceptanceRatio = options.steps > 0
        ? static_cast<double>(accepted) / static_cast<double>(options.steps)
        : 0.0;

    long unlikeDirected = 0;
    for (int i = 0; i < n; ++i)
        for (int j : neighbors[static_cast<std::size_t>(i)])
            unlikeDirected += unlike(i, j);
    result.finalUnlikeFraction =
        static_cast<double>(unlikeDirected) / static_cast<double>(totalBonds);

    return result;
}

} // namespace calango::core
