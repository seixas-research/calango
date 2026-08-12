#include "core/SqsGenerator.hpp"

#include "core/Element.hpp"
#include "core/PeriodicImages.hpp"

#include <algorithm>
#include <array>
#include <cmath>
// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>
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

/// One periodic neighbour of a sublattice site: which site, and through which
/// lattice translation. The shift has to be carried, not just the site index —
/// a triangle is only a triangle if the THREE distances close, and the third
/// one is between two images, neither of which is in the home cell.
struct Neighbor {
    int site = 0;
    Vec3 shift{0.0, 0.0, 0.0};
    /// Distance to the site the list belongs to. Cached because ONE neighbour
    /// list serves both multi-body orders, built at the larger of the two
    /// cutoffs — so the smaller order has to re-check the anchor distance it
    /// would otherwise have taken on trust.
    double distance = 0.0;
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

/// Every lattice translation reaching `cutoff`, precomputed once. The pair
/// enumeration used to rebuild these inside its innermost loop; the multi-body
/// enumeration needs them per site, so they are hoisted.
std::vector<Vec3> translationsWithin(const UnitCell& cell, double cutoff)
{
    const auto range = imageRange(cell, cutoff);
    const auto& v = cell.vectors();
    std::vector<Vec3> shifts;
    shifts.reserve(static_cast<std::size_t>((2 * range[0] + 1))
                   * static_cast<std::size_t>((2 * range[1] + 1))
                   * static_cast<std::size_t>((2 * range[2] + 1)));
    for (int ia = -range[0]; ia <= range[0]; ++ia)
        for (int ib = -range[1]; ib <= range[1]; ++ib)
            for (int ic = -range[2]; ic <= range[2]; ++ic)
                shifts.push_back(v[0] * static_cast<double>(ia)
                                 + v[1] * static_cast<double>(ib)
                                 + v[2] * static_cast<double>(ic));
    return shifts;
}

/// One multi-body cluster order (triplets or quadruplets) as a single orbit:
/// the cluster list, the per-site index into it, and the species-tuple
/// histogram it maintains.
///
/// The histogram is over the SORTED species tuple — {A,B,A} and {B,A,A} are
/// the same cluster decoration, and there is no ordering on the vertices of a
/// triangle that would make them different. The random-alloy target of a
/// sorted tuple is therefore (number of orderings) × Π c, which `buildTarget`
/// gets right by construction rather than by a multinomial coefficient that
/// would have to be maintained alongside the bin layout.
///
/// Bins are indexed in mixed radix over the sorted tuple, so the array has
/// nSpecies^order entries of which only the sorted ones are ever touched. That
/// wastes at most a few hundred doubles (6 species, quadruplets = 1296 bins)
/// and buys an index that is three multiply-adds rather than a lookup table.
struct BodyOrbit {
    int order = 0;
    double weight = 0.0;
    /// First `order` entries used; the rest are -1.
    std::vector<std::array<int, 4>> clusters;
    /// site → the clusters it belongs to. This is what makes a swap
    /// incremental: only these bins can change.
    std::vector<std::vector<int>> atSite;
    std::vector<double> bin;
    std::vector<double> target;

    /// The bin `clusters[id]` currently falls in.
    int binOf(int id, const std::vector<int>& labels, int nSpecies) const
    {
        std::array<int, 4> l{{0, 0, 0, 0}};
        const std::array<int, 4>& c = clusters[static_cast<std::size_t>(id)];
        for (int i = 0; i < order; ++i)
            l[static_cast<std::size_t>(i)] =
                labels[static_cast<std::size_t>(c[static_cast<std::size_t>(i)])];
        std::sort(l.begin(), l.begin() + order);
        int index = 0;
        for (int i = order - 1; i >= 0; --i)
            index = index * nSpecies + l[static_cast<std::size_t>(i)];
        return index;
    }

    bool contains(int id, int site) const
    {
        const std::array<int, 4>& c = clusters[static_cast<std::size_t>(id)];
        for (int i = 0; i < order; ++i)
            if (c[static_cast<std::size_t>(i)] == site)
                return true;
        return false;
    }

    /// Random-alloy probabilities of every sorted tuple.
    ///
    /// Built by walking all nSpecies^order ORDERED tuples and adding Π c into
    /// the bin its sorted form indexes: that is exactly "multiplicity × Π c"
    /// without ever computing a multiplicity, and it sums to 1 by
    /// construction — which is the invariant that makes ΔΠ a comparison of two
    /// probability distributions rather than of two arbitrary histograms.
    void buildTarget(int nSpecies, const std::vector<double>& concentration)
    {
        std::size_t size = 1;
        for (int i = 0; i < order; ++i)
            size *= static_cast<std::size_t>(nSpecies);
        bin.assign(size, 0.0);
        target.assign(size, 0.0);
        for (std::size_t code = 0; code < size; ++code) {
            std::array<int, 4> l{{0, 0, 0, 0}};
            std::size_t rest = code;
            double probability = 1.0;
            for (int i = 0; i < order; ++i) {
                l[static_cast<std::size_t>(i)] =
                    static_cast<int>(rest % static_cast<std::size_t>(nSpecies));
                rest /= static_cast<std::size_t>(nSpecies);
                probability *= concentration[static_cast<std::size_t>(
                    l[static_cast<std::size_t>(i)])];
            }
            std::sort(l.begin(), l.begin() + order);
            std::size_t index = 0;
            for (int i = order - 1; i >= 0; --i)
                index = index * static_cast<std::size_t>(nSpecies)
                    + static_cast<std::size_t>(l[static_cast<std::size_t>(i)]);
            target[index] += probability;
        }
    }
};

/// Everything the objective needs about one sublattice: the cluster lists, the
/// random-alloy targets and the live histograms.
///
/// One structure rather than a pile of locals because `generate` and
/// `evaluate` must agree exactly — an SQS whose quality is measured by a
/// second, independently written enumeration is an SQS nobody can check.
struct SqsModel {
    int nSpecies = 0;
    int nSites = 0;
    std::vector<double> concentration;

    // -- Pairs, per shell ---------------------------------------------------
    // Kept in the ordered-tuple form the pair-only objective has always used
    // (each unordered pair increments both (p,q) and (q,p), normalized by
    // 2 × count) rather than folded into the sorted-tuple form the multi-body
    // orbits use. The two are algebraically identical, but not bit-identical
    // in floating point, and every existing SQS in the wild was annealed
    // against this arithmetic.
    std::vector<ShellPair> pairs;
    std::vector<std::vector<int>> pairsAt;
    std::vector<int> shellPairCount;
    std::vector<double> shellWeight;
    std::vector<double> pairBin;
    int pairStride = 0;

    std::vector<BodyOrbit> orbits; ///< 0, 1 or 2 entries (order 3 and/or 4)

    void applyPair(int id, const std::vector<int>& labels, double delta)
    {
        const ShellPair& p = pairs[static_cast<std::size_t>(id)];
        const int la = labels[static_cast<std::size_t>(p.a)];
        const int lb = labels[static_cast<std::size_t>(p.b)];
        const std::size_t base =
            static_cast<std::size_t>(p.shell) * static_cast<std::size_t>(pairStride);
        // Both orderings: an unordered pair contributes to (p,q) and (q,p).
        pairBin[base + static_cast<std::size_t>(la * nSpecies + lb)] += delta;
        pairBin[base + static_cast<std::size_t>(lb * nSpecies + la)] += delta;
    }

    /// Fill every histogram from scratch.
    void tally(const std::vector<int>& labels)
    {
        std::fill(pairBin.begin(), pairBin.end(), 0.0);
        for (std::size_t i = 0; i < pairs.size(); ++i)
            applyPair(static_cast<int>(i), labels, 1.0);
        for (BodyOrbit& orbit : orbits) {
            std::fill(orbit.bin.begin(), orbit.bin.end(), 0.0);
            for (std::size_t i = 0; i < orbit.clusters.size(); ++i)
                orbit.bin[static_cast<std::size_t>(
                    orbit.binOf(static_cast<int>(i), labels, nSpecies))] += 1.0;
        }
    }

    /// Move `a`'s and `b`'s species onto each other's site, updating only the
    /// clusters that touch them.
    ///
    /// Calling it twice with the same arguments restores the previous state
    /// exactly — every bin holds a whole number of clusters, so the ±1
    /// bookkeeping is integer arithmetic in a double and cannot drift. That is
    /// what lets a rejected Metropolis move be undone by simply repeating the
    /// swap instead of by re-tallying, and what keeps the incremental
    /// objective bit-identical to the full recompute it replaced.
    void swapSites(std::vector<int>& labels, int a, int b)
    {
        touch(labels, a, b, -1.0);
        std::swap(labels[static_cast<std::size_t>(a)],
                  labels[static_cast<std::size_t>(b)]);
        touch(labels, a, b, 1.0);
    }

    SqsGenerator::Deviation objective() const
    {
        SqsGenerator::Deviation deviation;
        for (std::size_t s = 0; s < shellPairCount.size(); ++s) {
            if (shellPairCount[s] == 0)
                continue;
            const double norm = 2.0 * shellPairCount[s];
            const std::size_t base = s * static_cast<std::size_t>(pairStride);
            for (int p = 0; p < nSpecies; ++p) {
                for (int q = 0; q < nSpecies; ++q) {
                    const double observed =
                        pairBin[base + static_cast<std::size_t>(p * nSpecies + q)]
                        / norm;
                    const double target =
                        concentration[static_cast<std::size_t>(p)]
                        * concentration[static_cast<std::size_t>(q)];
                    deviation.pair +=
                        shellWeight[s] * std::abs(observed - target);
                }
            }
        }
        for (const BodyOrbit& orbit : orbits) {
            if (orbit.clusters.empty())
                continue;
            const double norm = static_cast<double>(orbit.clusters.size());
            double sum = 0.0;
            for (std::size_t i = 0; i < orbit.bin.size(); ++i)
                sum += orbit.weight
                    * std::abs(orbit.bin[i] / norm - orbit.target[i]);
            (orbit.order == 3 ? deviation.triplet : deviation.quadruplet) += sum;
        }
        deviation.total =
            deviation.pair + deviation.triplet + deviation.quadruplet;
        return deviation;
    }

private:
    void touch(const std::vector<int>& labels, int a, int b, double delta)
    {
        for (const int id : pairsAt[static_cast<std::size_t>(a)])
            applyPair(id, labels, delta);
        for (const int id : pairsAt[static_cast<std::size_t>(b)]) {
            const ShellPair& p = pairs[static_cast<std::size_t>(id)];
            if (p.a == a || p.b == a)
                continue; // already done through a's list — never twice
            applyPair(id, labels, delta);
        }
        for (BodyOrbit& orbit : orbits) {
            for (const int id : orbit.atSite[static_cast<std::size_t>(a)])
                orbit.bin[static_cast<std::size_t>(
                    orbit.binOf(id, labels, nSpecies))] += delta;
            for (const int id : orbit.atSite[static_cast<std::size_t>(b)]) {
                if (orbit.contains(id, a))
                    continue; // a cluster spanning both sites moves once
                orbit.bin[static_cast<std::size_t>(
                    orbit.binOf(id, labels, nSpecies))] += delta;
            }
        }
    }
};

/// Pair list over the sublattice, binned by shell.
///
/// Minimum-image is not enough here: a supercell can be thinner than the outer
/// cutoff along one axis, so the same pair is a neighbor through several
/// images and every one of them contributes.
void buildPairs(SqsModel& model, const std::vector<Vec3>& position,
                const std::vector<Vec3>& shifts,
                const std::vector<double>& cutoffs)
{
    const double rmax = cutoffs.back();
    const int n = static_cast<int>(position.size());
    model.shellPairCount.assign(cutoffs.size(), 0);
    model.pairsAt.assign(static_cast<std::size_t>(n), {});
    for (int a = 0; a < n; ++a) {
        const Vec3 pa = position[static_cast<std::size_t>(a)];
        for (int b = a + 1; b < n; ++b) {
            const Vec3 pb = position[static_cast<std::size_t>(b)];
            for (const Vec3& shift : shifts) {
                const double d = (pb + shift - pa).norm();
                if (d < 1e-6 || d > rmax)
                    continue;
                // First cutoff the distance falls under wins.
                for (std::size_t s = 0; s < cutoffs.size(); ++s) {
                    if (d <= cutoffs[s]) {
                        const int id = static_cast<int>(model.pairs.size());
                        model.pairs.push_back({a, b, static_cast<int>(s)});
                        model.pairsAt[static_cast<std::size_t>(a)].push_back(id);
                        model.pairsAt[static_cast<std::size_t>(b)].push_back(id);
                        ++model.shellPairCount[s];
                        break;
                    }
                }
            }
        }
    }
}

/// Neighbour list of every sublattice site within `cutoff`, images included.
std::vector<std::vector<Neighbor>>
buildNeighbors(const std::vector<Vec3>& position,
               const std::vector<Vec3>& shifts, double cutoff)
{
    const int n = static_cast<int>(position.size());
    std::vector<std::vector<Neighbor>> neighbors(static_cast<std::size_t>(n));
    for (int a = 0; a < n; ++a) {
        const Vec3 pa = position[static_cast<std::size_t>(a)];
        for (int b = 0; b < n; ++b) {
            const Vec3 pb = position[static_cast<std::size_t>(b)];
            for (const Vec3& shift : shifts) {
                const double d = (pb + shift - pa).norm();
                if (d < 1e-6 || d > cutoff)
                    continue;
                neighbors[static_cast<std::size_t>(a)].push_back({b, shift, d});
            }
        }
    }
    return neighbors;
}

/// Enumerate the compact clusters of one multi-body order.
///
/// Each geometric cluster is emitted EXACTLY ONCE, anchored at its
/// lowest-numbered vertex sitting in the home cell: the anchor's neighbour
/// list supplies the other vertices (with their images), every candidate whose
/// site index is below the anchor is dropped, and the remaining vertices are
/// taken as an unordered set of neighbour-list positions — so the same
/// triangle cannot be found again by choosing its vertices in another order.
///
/// Clusters that would use two images of the SAME site are skipped, for
/// consistency with the pair list, which has never paired a site with its own
/// image either. In a cell that small the correlations are dominated by
/// self-interaction and the SQS is meaningless anyway; the honest fix is a
/// bigger supercell, not a cluster list that half-counts one.
void enumerateBody(BodyOrbit& orbit, const std::vector<Vec3>& position,
                   const std::vector<std::vector<Neighbor>>& neighbors,
                   double cutoff, int maxClusters, const char* what)
{
    const int n = static_cast<int>(position.size());
    orbit.atSite.assign(static_cast<std::size_t>(n), {});
    const auto separation = [&](const Neighbor& x, const Neighbor& y) {
        return (position[static_cast<std::size_t>(y.site)] + y.shift
                - position[static_cast<std::size_t>(x.site)] - x.shift)
            .norm();
    };
    const auto keep = [&](std::array<int, 4> sites) {
        if (static_cast<int>(orbit.clusters.size()) >= maxClusters)
            throw std::invalid_argument(
                std::string("too many ") + what
                + " clusters within the cutoff — lower it, or raise "
                  "Params::maxClusters if the enumeration really is meant to "
                  "be this large");
        const int id = static_cast<int>(orbit.clusters.size());
        orbit.clusters.push_back(sites);
        for (int i = 0; i < orbit.order; ++i)
            orbit.atSite[static_cast<std::size_t>(sites[static_cast<std::size_t>(i)])]
                .push_back(id);
    };

    for (int a = 0; a < n; ++a) {
        const std::vector<Neighbor>& nb = neighbors[static_cast<std::size_t>(a)];
        const auto count = static_cast<int>(nb.size());
        for (int i = 0; i < count; ++i) {
            const Neighbor& ni = nb[static_cast<std::size_t>(i)];
            if (ni.site <= a || ni.distance > cutoff)
                continue;
            for (int j = i + 1; j < count; ++j) {
                const Neighbor& nj = nb[static_cast<std::size_t>(j)];
                if (nj.site <= a || nj.site == ni.site || nj.distance > cutoff)
                    continue;
                const double dij = separation(ni, nj);
                if (dij < 1e-6 || dij > cutoff)
                    continue;
                if (orbit.order == 3) {
                    keep({a, ni.site, nj.site, -1});
                    continue;
                }
                for (int k = j + 1; k < count; ++k) {
                    const Neighbor& nk = nb[static_cast<std::size_t>(k)];
                    if (nk.site <= a || nk.site == ni.site || nk.site == nj.site
                        || nk.distance > cutoff)
                        continue;
                    const double dik = separation(ni, nk);
                    const double djk = separation(nj, nk);
                    if (dik < 1e-6 || dik > cutoff || djk < 1e-6 || djk > cutoff)
                        continue;
                    keep({a, ni.site, nj.site, nk.site});
                }
            }
        }
    }
}

/// Shell cutoff list, in the order the params declare them. A cutoff of 0, or
/// one not greater than its predecessor, ends the list.
std::vector<double> shellCutoffs(const SqsGenerator::Params& params)
{
    std::vector<double> cutoffs;
    for (const double cutoff : {params.shell1, params.shell2})
        if (cutoff > 0.0 && (cutoffs.empty() || cutoff > cutoffs.back()))
            cutoffs.push_back(cutoff);
    return cutoffs;
}

/// Assemble the cluster tables and targets for one sublattice.
void buildModel(SqsModel& model, const SqsGenerator::Params& params,
                const UnitCell& cell, const std::vector<Vec3>& position,
                const std::vector<double>& cutoffs)
{
    model.nSites = static_cast<int>(position.size());
    model.pairStride = model.nSpecies * model.nSpecies;

    buildPairs(model, position, translationsWithin(cell, cutoffs.back()),
               cutoffs);
    model.pairBin.assign(cutoffs.size() * static_cast<std::size_t>(model.pairStride),
                         0.0);
    model.shellWeight.assign(cutoffs.size(), 0.0);
    for (std::size_t s = 0; s < cutoffs.size(); ++s)
        model.shellWeight[s] = s < params.shellWeights.size()
            ? params.shellWeights[s]
            : 1.0 / static_cast<double>(s + 1);

    // One neighbour list serves both multi-body orders; it is built at the
    // larger cutoff and the smaller order simply rejects what is too far.
    const double bodyCutoff =
        std::max(params.tripletCutoff, params.quadrupletCutoff);
    if (bodyCutoff <= 0.0)
        return;
    const std::vector<std::vector<Neighbor>> neighbors = buildNeighbors(
        position, translationsWithin(cell, bodyCutoff), bodyCutoff);

    if (params.tripletCutoff > 0.0) {
        BodyOrbit orbit;
        orbit.order = 3;
        orbit.weight = params.tripletWeight;
        enumerateBody(orbit, position, neighbors, params.tripletCutoff,
                      params.maxClusters, "triplet");
        orbit.buildTarget(model.nSpecies, model.concentration);
        model.orbits.push_back(std::move(orbit));
    }
    if (params.quadrupletCutoff > 0.0) {
        BodyOrbit orbit;
        orbit.order = 4;
        orbit.weight = params.quadrupletWeight;
        enumerateBody(orbit, position, neighbors, params.quadrupletCutoff,
                      params.maxClusters, "quadruplet");
        orbit.buildTarget(model.nSpecies, model.concentration);
        model.orbits.push_back(std::move(orbit));
    }
}

int orbitSize(const SqsModel& model, int order)
{
    for (const BodyOrbit& orbit : model.orbits)
        if (orbit.order == order)
            return static_cast<int>(orbit.clusters.size());
    return 0;
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

SqsGenerator::Deviation SqsGenerator::evaluate(const Structure& decorated,
                                               const Params& params)
{
    if (params.composition.size() < 2)
        throw std::invalid_argument(
            "SQS needs at least two species in the target composition");
    if (!decorated.cell().isDefined())
        throw std::invalid_argument("SQS needs a periodic cell");
    const std::vector<double> cutoffs = shellCutoffs(params);
    if (cutoffs.empty())
        throw std::invalid_argument("no positive coordination-shell cutoff");

    SqsModel model;
    model.nSpecies = static_cast<int>(params.composition.size());
    std::vector<int> speciesZ;
    for (const auto& [symbol, fraction] : params.composition) {
        const int z = Elements::atomicNumber(symbol);
        if (z == 0)
            throw std::invalid_argument("unknown chemical symbol: " + symbol);
        speciesZ.push_back(z);
    }

    // The sublattice is whatever carries one of the composition's species, and
    // the concentrations are what is actually there — not what was asked for.
    // Evaluating a decoration against the requested fractions would report a
    // deviation that is partly just the rounding onto whole sites.
    std::vector<Vec3> position;
    std::vector<int> labels;
    for (const Atom& atom : decorated.atoms()) {
        const auto it =
            std::find(speciesZ.begin(), speciesZ.end(), atom.atomicNumber);
        if (it == speciesZ.end())
            continue;
        position.push_back(atom.position);
        labels.push_back(static_cast<int>(it - speciesZ.begin()));
    }
    if (position.empty())
        throw std::invalid_argument(
            "none of the composition's species is present in the structure");
    model.concentration.assign(static_cast<std::size_t>(model.nSpecies), 0.0);
    for (const int label : labels)
        model.concentration[static_cast<std::size_t>(label)] +=
            1.0 / static_cast<double>(labels.size());

    buildModel(model, params, decorated.cell(), position, cutoffs);
    if (model.pairs.empty())
        throw std::invalid_argument(
            "no sublattice pairs within the shell cutoffs");
    model.tally(labels);
    return model.objective();
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
    const std::vector<double> cutoffs = shellCutoffs(params);
    if (cutoffs.empty())
        throw std::invalid_argument("no positive coordination-shell cutoff");

    // -- Target correlations for the ideal random alloy ---------------------
    // For a random solid solution the probability of finding species (p, q) on
    // the two ends of a bond is just c_p · c_q — no correlation between sites.
    // The counts are exact, so the concentrations come from them rather than
    // from the requested fractions (which the rounding may have nudged). The
    // same reasoning carries to the n-body clusters, whose targets are the
    // n-fold products.
    const std::vector<int> counts = siteCounts(fractions, nSites);

    SqsModel model;
    model.nSpecies = nSpecies;
    model.concentration.assign(static_cast<std::size_t>(nSpecies), 0.0);
    for (int s = 0; s < nSpecies; ++s)
        model.concentration[static_cast<std::size_t>(s)] =
            static_cast<double>(counts[static_cast<std::size_t>(s)]) / nSites;

    std::vector<Vec3> position;
    position.reserve(sites.size());
    for (const int site : sites)
        position.push_back(super.atoms()[static_cast<std::size_t>(site)].position);

    buildModel(model, params, super.cell(), position, cutoffs);
    if (model.pairs.empty())
        throw std::invalid_argument(
            "no sublattice pairs within the shell cutoffs — raise the first "
            "shell cutoff or enlarge the supercell");

    int populatedShells = 0;
    for (const int count : model.shellPairCount)
        if (count > 0)
            ++populatedShells;

    // -- Initial decoration: a plain random shuffle at the exact composition -
    std::vector<int> labels;
    labels.reserve(static_cast<std::size_t>(nSites));
    for (int s = 0; s < nSpecies; ++s)
        labels.insert(labels.end(), counts[static_cast<std::size_t>(s)], s);
    std::mt19937 rng(params.seed);
    std::shuffle(labels.begin(), labels.end(), rng);

    model.tally(labels);
    Deviation deviation = model.objective();
    double current = deviation.total;
    const double initial = current;
    std::vector<int> best = labels;
    Deviation bestDeviation = deviation;
    double bestObjective = current;

    // -- Metropolis annealing over species swaps ----------------------------
    // Swapping two sites conserves the composition exactly, so the search
    // never has to reject a move for being off-stoichiometry.
    //
    // The move is INCREMENTAL: only the clusters that touch the two sites can
    // change bin, so a proposal costs O(clusters per site) rather than
    // O(all clusters). With triplets enabled the full recompute is O(N·z²) per
    // step and the loop runs tens of thousands of steps, which is the
    // difference between an interactive dialog and a coffee break. The
    // histograms hold whole cluster counts, so ±1 bookkeeping is exact and a
    // rejected move is undone by repeating the swap.
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
        model.swapSites(labels, a, b);
        const Deviation proposed = model.objective();
        const double proposal = proposed.total;
        const double temperature =
            t0 * std::pow(t1 / t0, static_cast<double>(step) / (steps - 1));
        if (proposal <= current
            || unit(rng) < std::exp(-(proposal - current) / temperature)) {
            current = proposal;
            ++accepted;
            if (current < bestObjective) {
                bestObjective = current;
                bestDeviation = proposed;
                best = labels;
            }
        } else {
            model.swapSites(labels, a, b); // exact undo
        }
    }

    // -- Decorate ------------------------------------------------------------
    for (int s = 0; s < nSites; ++s)
        super.atoms()[static_cast<std::size_t>(sites[static_cast<std::size_t>(s)])]
            .atomicNumber =
            speciesZ[static_cast<std::size_t>(best[static_cast<std::size_t>(s)])];

    Result result;
    result.objective = bestObjective;
    result.initialObjective = initial;
    result.deviation = bestDeviation;
    result.shells = populatedShells;
    result.pairs = static_cast<int>(model.pairs.size());
    result.triplets = orbitSize(model, 3);
    result.quadruplets = orbitSize(model, 4);
    result.sublatticeSites = nSites;
    result.steps = steps;
    result.accepted = accepted;
    result.method = model.orbits.empty()
        ? "Monte Carlo simulated annealing (native)"
        : "Monte Carlo simulated annealing (native, multi-body clusters)";

    // -- Short-range order of what was produced ------------------------------
    // Free: the alloy people who use this read α, not ΔΠ, and reporting it
    // here is what makes "did the annealing work" answerable without running a
    // second tool over the output.
    //
    // On the sublattice ALONE — same cell, spectator atoms dropped — because
    // α is a conditional probability over neighbours and the neighbours the
    // SQS never had a say over would dilute every one of them. Dropping atoms
    // does not move anything, so the shells are the same shells.
    Structure sublattice;
    sublattice.setCell(super.cell());
    for (const int index : sites)
        sublattice.addAtom(super.atoms()[static_cast<std::size_t>(index)]);
    WarrenCowleyOptions order;
    order.shellCutoffs = cutoffs;
    result.shortRangeOrder = computeWarrenCowley(sublattice, order);

    result.structure = std::move(super);
    return result;
}

} // namespace calango::core
