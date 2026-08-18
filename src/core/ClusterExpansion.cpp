#include "core/ClusterExpansion.hpp"
#include "core/PeriodicImages.hpp"

#include "core/UnitCell.hpp"

#include <algorithm>
#include <cstdio>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <random>

namespace calango::core {

namespace {

// -- Native supercell replication (self-contained; no ASE) ------------------
Structure repeat(const Structure& s, int nx, int ny, int nz)
{
    Structure out;
    const auto& v = s.cell().vectors();
    for (int a = 0; a < nx; ++a)
        for (int b = 0; b < ny; ++b)
            for (int c = 0; c < nz; ++c) {
                const Vec3 shift = v[0] * a + v[1] * b + v[2] * c;
                for (const Atom& at : s.atoms())
                    out.addAtom({at.atomicNumber, at.position + shift});
            }
    out.setCell(UnitCell(v[0] * nx, v[1] * ny, v[2] * nz, s.cell().pbc()));
    return out;
}

/// A cluster: the active-site indices it spans and its orbit id (within order).
struct Cluster {
    std::vector<int> sites;
    int orbit;
};

/// Groups clusters by a rounded distance signature into orbits and returns the
/// per-cluster orbit ids plus the count of distinct orbits.
long roundKey(double d, double tol)
{
    return static_cast<long>(std::llround(d / tol));
}

} // namespace

int clusterExpansionPairBucket(int speciesCount, int speciesI, int speciesJ)
{
    const int a = std::min(speciesI, speciesJ), b = std::max(speciesI, speciesJ);
    return a * speciesCount - a * (a - 1) / 2 + (b - a);
}

int clusterExpansionTripletBucket(int speciesCount, int speciesI, int speciesJ,
                                  int speciesK)
{
    std::array<int, 3> s{speciesI, speciesJ, speciesK};
    std::sort(s.begin(), s.end());
    int idx = 0;
    for (int v : s)
        idx = idx * speciesCount + v;
    return idx;
}

std::vector<std::string> clusterCorrelationLabels(
    const ClusterExpansionResult& result, int speciesCount)
{
    std::vector<std::string> labels;
    const int K = std::max(1, speciesCount);
    for (int i = 0; i < K; ++i)
        labels.push_back("point s" + std::to_string(i));

    const auto buckets = [K](int order) {
        if (order == 2)
            return K * (K + 1) / 2;
        int n = 1;
        for (int i = 0; i < order; ++i)
            n *= K;
        return n;
    };
    // The orbits are already ordered pairs -> triplets -> quadruplets, which
    // is the order the fingerprint concatenates them in.
    for (const ClusterOrbitSummary& orbit : result.orbits) {
        const int n = buckets(orbit.order);
        const char* kind = orbit.order == 2   ? "pair"
            : orbit.order == 3               ? "triplet"
                                             : "quad";
        char radius[32];
        std::snprintf(radius, sizeof(radius), "%.3f", orbit.radius);
        for (int b = 0; b < n; ++b)
            labels.push_back(std::string(kind) + " r=" + radius
                             + " m=" + std::to_string(orbit.multiplicity)
                             + " b" + std::to_string(b));
    }
    return labels;
}

ClusterExpansionResult generateClusterExpansion(
    const Structure& parent, const ClusterExpansionOptions& options)
{
    ClusterExpansionResult result;

    const int K = static_cast<int>(options.speciesZ.size());
    if (K < 2) {
        result.note = "need at least two substitution species";
        return result;
    }

    // --- Supercell + active sublattice -------------------------------------
    const Structure super = repeat(parent, options.supercell[0],
                                   options.supercell[1], options.supercell[2]);
    const auto& atoms = super.atoms();
    std::vector<int> active; // supercell atom indices that are substituted
    for (int i = 0; i < static_cast<int>(atoms.size()); ++i)
        if (atoms[static_cast<std::size_t>(i)].atomicNumber == options.activeZ)
            active.push_back(i);
    const int M = static_cast<int>(active.size());
    result.activeSites = M;
    if (M == 0) {
        result.note = "no active sites of the chosen element in the cell";
        return result;
    }

    // --- Minimum-image distance matrix among active sites ------------------
    const double rMax = std::max({options.pairCutoff, options.tripletCutoff,
                                  options.quadCutoff});
    std::vector<Vec3> translations{{0, 0, 0}};
    const bool pbc = super.cell().isDefined()
        && (super.cell().pbc()[0] || super.cell().pbc()[1]
            || super.cell().pbc()[2]);
    if (pbc && rMax > 0.0) {
        translations.clear();
        const auto range = imageRange(super.cell(), rMax);
        const auto& v = super.cell().vectors();
        for (int i = -range[0]; i <= range[0]; ++i)
            for (int j = -range[1]; j <= range[1]; ++j)
                for (int k = -range[2]; k <= range[2]; ++k)
                    translations.push_back(v[0] * i + v[1] * j + v[2] * k);
    }

    std::vector<std::vector<double>> dist(M, std::vector<double>(M, 0.0));
    for (int a = 0; a < M; ++a)
        for (int b = a + 1; b < M; ++b) {
            const Vec3 base = atoms[static_cast<std::size_t>(active[b])].position
                - atoms[static_cast<std::size_t>(active[a])].position;
            double best = std::numeric_limits<double>::max();
            for (const Vec3& t : translations)
                best = std::min(best, (base + t).norm());
            dist[a][b] = dist[b][a] = best;
        }

    // --- Cluster enumeration + orbit grouping ------------------------------
    std::vector<Cluster> pairs, triplets, quads;
    std::map<long, int> pairOrbit;
    std::map<std::array<long, 3>, int> tripOrbit;
    std::map<std::array<long, 6>, int> quadOrbit;
    const double tol = std::max(1e-6, options.distanceTolerance);

    if (options.pairCutoff > 0.0) {
        for (int a = 0; a < M && static_cast<int>(pairs.size()) < options.maxClusters; ++a)
            for (int b = a + 1; b < M; ++b)
                if (dist[a][b] <= options.pairCutoff) {
                    const long key = roundKey(dist[a][b], tol);
                    const int id = pairOrbit.emplace(key, static_cast<int>(pairOrbit.size()))
                                       .first->second;
                    pairs.push_back({{a, b}, id});
                }
    }
    if (options.tripletCutoff > 0.0) {
        for (int a = 0; a < M && static_cast<int>(triplets.size()) < options.maxClusters; ++a)
            for (int b = a + 1; b < M; ++b) {
                if (dist[a][b] > options.tripletCutoff)
                    continue;
                for (int c = b + 1; c < M; ++c) {
                    if (dist[a][c] > options.tripletCutoff
                        || dist[b][c] > options.tripletCutoff)
                        continue;
                    std::array<long, 3> sig{roundKey(dist[a][b], tol),
                                            roundKey(dist[a][c], tol),
                                            roundKey(dist[b][c], tol)};
                    std::sort(sig.begin(), sig.end());
                    const int id = tripOrbit.emplace(sig, static_cast<int>(tripOrbit.size()))
                                       .first->second;
                    triplets.push_back({{a, b, c}, id});
                }
            }
    }
    if (options.quadCutoff > 0.0) {
        for (int a = 0; a < M && static_cast<int>(quads.size()) < options.maxClusters; ++a)
            for (int b = a + 1; b < M; ++b) {
                if (dist[a][b] > options.quadCutoff)
                    continue;
                for (int c = b + 1; c < M; ++c) {
                    if (dist[a][c] > options.quadCutoff || dist[b][c] > options.quadCutoff)
                        continue;
                    for (int d = c + 1; d < M; ++d) {
                        if (dist[a][d] > options.quadCutoff
                            || dist[b][d] > options.quadCutoff
                            || dist[c][d] > options.quadCutoff)
                            continue;
                        std::array<long, 6> sig{
                            roundKey(dist[a][b], tol), roundKey(dist[a][c], tol),
                            roundKey(dist[a][d], tol), roundKey(dist[b][c], tol),
                            roundKey(dist[b][d], tol), roundKey(dist[c][d], tol)};
                        std::sort(sig.begin(), sig.end());
                        const int id = quadOrbit.emplace(sig, static_cast<int>(quadOrbit.size()))
                                           .first->second;
                        quads.push_back({{a, b, c, d}, id});
                    }
                }
            }
    }

    // Orbit summaries (representative radius = max pairwise distance).
    auto summarize = [&](const std::vector<Cluster>& cl, int order, int nOrbits) {
        std::vector<double> radius(nOrbits, 0.0);
        std::vector<int> mult(nOrbits, 0);
        for (const Cluster& c : cl) {
            double r = 0.0;
            for (std::size_t i = 0; i < c.sites.size(); ++i)
                for (std::size_t j = i + 1; j < c.sites.size(); ++j)
                    r = std::max(r, dist[c.sites[i]][c.sites[j]]);
            radius[c.orbit] = r;
            ++mult[c.orbit];
        }
        for (int o = 0; o < nOrbits; ++o)
            result.orbits.push_back({order, radius[o], mult[o]});
    };
    summarize(pairs, 2, static_cast<int>(pairOrbit.size()));
    summarize(triplets, 3, static_cast<int>(tripOrbit.size()));
    summarize(quads, 4, static_cast<int>(quadOrbit.size()));

    // --- Fingerprint of a decoration ---------------------------------------
    // Point term (composition) + per-orbit histograms of canonical species
    // tuples. The canonical tuple is the sorted species indices on the
    // cluster's sites, so translation/rotation/relabeling within an orbit
    // collapses — exactly the cluster-count correlation vector.
    const int nPairOrbits = static_cast<int>(pairOrbit.size());
    const int nTripOrbits = static_cast<int>(tripOrbit.size());
    const int nQuadOrbits = static_cast<int>(quadOrbit.size());
    // Histogram bucket per canonical (sorted) species tuple. We use a dense
    // mixed-radix index in [0, K^order): sorting first collapses equivalent
    // tuples to one bucket while keeping distinct tuples distinct (K is small,
    // so K^4 <= 256 buckets per orbit — cheap and exact).
    const int pairKeys = K * (K + 1) / 2;
    const int tripKeys = K * K * K;
    const int quadKeys = K * K * K * K;

    // Thin wrappers over the exported bucket functions (single source of
    // truth — see clusterExpansionPairBucket()'s own doc comment): the
    // triplet/quadruplet case also needs a variable-arity sorted index that
    // the exported function's fixed 3-argument signature doesn't cover, so
    // that one stays local.
    auto pairIndex = [K](int s0, int s1) {
        return clusterExpansionPairBucket(K, s0, s1);
    };
    auto sortedIndex = [K](std::vector<int> s) {
        std::sort(s.begin(), s.end());
        int idx = 0;
        for (int v : s)
            idx = idx * K + v;
        return idx;
    };

    auto fingerprint = [&](const std::vector<int>& occ) {
        std::vector<int> fp;
        fp.reserve(K + nPairOrbits * pairKeys + nTripOrbits * tripKeys
                   + nQuadOrbits * quadKeys);
        // Point term.
        std::vector<int> comp(K, 0);
        for (int o : occ)
            ++comp[o];
        fp.insert(fp.end(), comp.begin(), comp.end());
        // Pairs.
        std::vector<int> ph(static_cast<std::size_t>(nPairOrbits) * pairKeys, 0);
        for (const Cluster& c : pairs)
            ++ph[static_cast<std::size_t>(c.orbit) * pairKeys
                 + pairIndex(occ[c.sites[0]], occ[c.sites[1]])];
        fp.insert(fp.end(), ph.begin(), ph.end());
        // Triplets.
        std::vector<int> th(static_cast<std::size_t>(nTripOrbits) * tripKeys, 0);
        for (const Cluster& c : triplets)
            ++th[static_cast<std::size_t>(c.orbit) * tripKeys
                 + sortedIndex({occ[c.sites[0]], occ[c.sites[1]], occ[c.sites[2]]})];
        fp.insert(fp.end(), th.begin(), th.end());
        // Quadruplets.
        std::vector<int> qh(static_cast<std::size_t>(nQuadOrbits) * quadKeys, 0);
        for (const Cluster& c : quads)
            ++qh[static_cast<std::size_t>(c.orbit) * quadKeys
                 + sortedIndex({occ[c.sites[0]], occ[c.sites[1]], occ[c.sites[2]],
                                occ[c.sites[3]]})];
        fp.insert(fp.end(), qh.begin(), qh.end());
        return fp;
    };

    // --- Build a decorated structure from an occupation --------------------
    auto decorate = [&](const std::vector<int>& occ) {
        Structure s = super;
        for (int a = 0; a < M; ++a)
            s.atoms()[static_cast<std::size_t>(active[a])].atomicNumber =
                options.speciesZ[static_cast<std::size_t>(occ[a])];
        return s;
    };
    auto counts = [&](const std::vector<int>& occ) {
        std::vector<int> c(K, 0);
        for (int o : occ)
            ++c[o];
        return c;
    };

    // --- Enumerate / sample occupations, dedup by fingerprint --------------
    // Maps a canonical fingerprint to its config's index in result.configs,
    // rather than a plain set, so a repeat hit can increment that config's
    // degeneracy — the g_j count EGQCA (core::EgqcaCluster::degeneracy)
    // needs and this enumerator did not track before.
    std::map<std::vector<int>, std::size_t> seen;
    std::mt19937 rng(options.seed);

    auto consider = [&](const std::vector<int>& occ) {
        ++result.enumerated;
        auto fp = fingerprint(occ);
        const auto it = seen.find(fp);
        if (it != seen.end()) {
            ++result.configs[it->second].degeneracy;
            return;
        }
        ClusterExpansionConfig cfg;
        cfg.structure = decorate(occ);
        cfg.speciesCounts = counts(occ);
        cfg.correlation.assign(fp.begin(), fp.end());
        seen.emplace(std::move(fp), result.configs.size());
        result.configs.push_back(std::move(cfg));
    };

    // Total size of the occupation space (K^M), guarding overflow.
    auto spaceExceedsCap = [&]() {
        double total = 1.0;
        for (int i = 0; i < M; ++i) {
            total *= K;
            if (total > static_cast<double>(options.maxEnumeration))
                return true;
        }
        return false;
    };

    if (options.fixedComposition) {
        // Multiset permutations of the fixed composition, via next_permutation.
        std::vector<int> occ;
        for (int s = 0; s < K; ++s)
            occ.insert(occ.end(),
                       static_cast<std::size_t>(
                           s < static_cast<int>(options.composition.size())
                               ? options.composition[static_cast<std::size_t>(s)]
                               : 0),
                       s);
        if (static_cast<int>(occ.size()) != M) {
            result.note = "composition counts must sum to the number of active sites";
            return result;
        }
        std::sort(occ.begin(), occ.end());
        do {
            consider(occ);
            if (result.enumerated >= options.maxEnumeration
                || static_cast<int>(result.configs.size()) >= options.maxConfigs) {
                result.sampled = std::next_permutation(occ.begin(), occ.end());
                break;
            }
        } while (std::next_permutation(occ.begin(), occ.end()));
    } else if (!spaceExceedsCap()) {
        // Exhaustive base-K enumeration.
        std::vector<int> occ(M, 0);
        while (true) {
            consider(occ);
            if (static_cast<int>(result.configs.size()) >= options.maxConfigs)
                break;
            int pos = 0;
            for (; pos < M; ++pos) {
                if (++occ[pos] < K)
                    break;
                occ[pos] = 0;
            }
            if (pos == M)
                break; // wrapped around — done
        }
    } else {
        // Random sampling of the occupation space.
        result.sampled = true;
        std::uniform_int_distribution<int> pick(0, K - 1);
        for (long long n = 0; n < options.maxEnumeration
             && static_cast<int>(result.configs.size()) < options.maxConfigs;
             ++n) {
            std::vector<int> occ(M);
            for (int a = 0; a < M; ++a)
                occ[a] = pick(rng);
            consider(occ);
        }
    }

    return result;
}

} // namespace calango::core
