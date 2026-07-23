#include "core/Distributions.hpp"
#include "core/PeriodicImages.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace calango::core {

namespace {


std::vector<Vec3> translationsFor(const Structure& structure,
                                  const DistributionOptions& options)
{
    std::vector<Vec3> translations{{0.0, 0.0, 0.0}};
    const auto pbc = structure.cell().pbc();
    if (options.usePbc && structure.cell().isDefined()
        && (pbc[0] || pbc[1] || pbc[2])) {
        translations.clear();
        const auto range = imageRange(structure.cell(), options.cutoff);
        const auto& v = structure.cell().vectors();
        for (int i = -range[0]; i <= range[0]; ++i)
            for (int j = -range[1]; j <= range[1]; ++j)
                for (int k = -range[2]; k <= range[2]; ++k)
                    translations.push_back(v[0] * i + v[1] * j + v[2] * k);
    }
    return translations;
}

bool matches(const Atom& atom, int filter)
{
    return filter == 0 || atom.atomicNumber == filter;
}

HistogramResult binned(std::vector<double> values, double lo, double hi, int bins)
{
    HistogramResult result;
    if (bins < 1 || hi <= lo)
        return result;
    const double width = (hi - lo) / bins;
    result.x.resize(static_cast<std::size_t>(bins));
    result.y.assign(static_cast<std::size_t>(bins), 0.0);
    for (int k = 0; k < bins; ++k)
        result.x[static_cast<std::size_t>(k)] = lo + (k + 0.5) * width;
    for (const double value : values) {
        auto bin = static_cast<std::ptrdiff_t>((value - lo) / width);
        if (bin == bins && value <= hi)
            --bin; // inclusive upper edge (e.g. exactly 180° angles)
        if (bin >= 0 && bin < bins)
            result.y[static_cast<std::size_t>(bin)] += 1.0;
    }
    return result;
}

} // namespace

HistogramResult computeBondLengthDistribution(const Structure& structure,
                                              const DistributionOptions& options)
{
    const auto& atoms = structure.atoms();
    const auto n = static_cast<int>(atoms.size());
    const auto translations = translationsFor(structure, options);
    const double cutoffSq = options.cutoff * options.cutoff;

    // Directed pairs (i -> j image) with the species filter applied both
    // ways; every unordered pair is therefore seen twice, so lengths are
    // collected once for i < j plus the self-image half for i == j.
    std::vector<double> lengths;
    for (int i = 0; i < n; ++i) {
        for (int j = i; j < n; ++j) {
            const bool pairMatches =
                (matches(atoms[static_cast<std::size_t>(i)], options.elementA)
                 && matches(atoms[static_cast<std::size_t>(j)], options.elementB))
                || (matches(atoms[static_cast<std::size_t>(i)], options.elementB)
                    && matches(atoms[static_cast<std::size_t>(j)], options.elementA));
            if (!pairMatches)
                continue;
            const Vec3 base = atoms[static_cast<std::size_t>(j)].position
                - atoms[static_cast<std::size_t>(i)].position;
            for (const Vec3& t : translations) {
                const Vec3 d = base + t;
                const double distSq = d.dot(d);
                if (distSq >= cutoffSq || distSq < 1e-8)
                    continue;
                if (i == j && (t.x < 0 || (t.x == 0 && (t.y < 0 || (t.y == 0 && t.z < 0)))))
                    continue; // self-images: count each ± translation once
                lengths.push_back(std::sqrt(distSq));
            }
        }
    }
    return binned(std::move(lengths), 0.0, options.cutoff, options.bins);
}

HistogramResult computeBondAngleDistribution(const Structure& structure,
                                             const DistributionOptions& options)
{
    const auto& atoms = structure.atoms();
    const auto n = static_cast<int>(atoms.size());
    const auto translations = translationsFor(structure, options);
    const double cutoffSq = options.cutoff * options.cutoff;

    std::vector<double> angles;
    std::vector<Vec3> neighbors; // displacement vectors from the center
    for (int i = 0; i < n; ++i) {
        if (!matches(atoms[static_cast<std::size_t>(i)], options.elementA))
            continue;
        neighbors.clear();
        for (int j = 0; j < n; ++j) {
            if (!matches(atoms[static_cast<std::size_t>(j)], options.elementB))
                continue;
            const Vec3 base = atoms[static_cast<std::size_t>(j)].position
                - atoms[static_cast<std::size_t>(i)].position;
            for (const Vec3& t : translations) {
                const Vec3 d = base + t;
                const double distSq = d.dot(d);
                if (distSq < cutoffSq && distSq > 1e-8)
                    neighbors.push_back(d);
            }
        }
        for (std::size_t m = 0; m + 1 < neighbors.size(); ++m) {
            for (std::size_t k = m + 1; k < neighbors.size(); ++k) {
                const double cosine = neighbors[m].dot(neighbors[k])
                    / (neighbors[m].norm() * neighbors[k].norm());
                angles.push_back(std::acos(std::clamp(cosine, -1.0, 1.0))
                                 * 180.0 / M_PI);
            }
        }
    }
    return binned(std::move(angles), 0.0, 180.0, options.bins);
}

} // namespace calango::core
