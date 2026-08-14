#include "core/Coordination.hpp"
#include "core/PeriodicImages.hpp"

#include <algorithm>
#include <cmath>

namespace calango::core {


CoordinationResult computeCoordination(const Structure& structure,
                                       const CoordinationOptions& options)
{
    CoordinationResult result;
    const auto& atoms = structure.atoms();
    const auto n = static_cast<int>(atoms.size());
    if (n == 0)
        return result;

    // Pair cutoff and the largest cutoff any pair can have (bounds the
    // periodic-image search radius).
    const auto pairCutoff = [&](int i, int j) {
        if (options.cutoffMode == CoordinationOptions::CutoffMode::Fixed)
            return options.fixedCutoff;
        return options.tolerance
            * (atoms[static_cast<std::size_t>(i)].covalentRadius()
               + atoms[static_cast<std::size_t>(j)].covalentRadius());
    };
    double rMax = options.fixedCutoff;
    if (options.cutoffMode == CoordinationOptions::CutoffMode::CovalentScaled) {
        float largestRadius = 0.0f;
        for (const Atom& atom : atoms)
            largestRadius = std::max(largestRadius, atom.covalentRadius());
        rMax = options.tolerance * 2.0 * largestRadius;
    }

    const auto pbc = structure.cell().pbc();
    const bool usePbc = structure.cell().isDefined() && (pbc[0] || pbc[1] || pbc[2]);
    const std::vector<Vec3> translations =
        imageTranslations(structure.cell(), rMax, usePbc);

    // Neighbor sites of atom i, recorded as the index of the neighbor's
    // representative in the cell (images count once per translation).
    std::vector<std::vector<int>> neighborIndices(static_cast<std::size_t>(n));
    result.cn.assign(static_cast<std::size_t>(n), 0);

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const Vec3 base = atoms[static_cast<std::size_t>(j)].position
                - atoms[static_cast<std::size_t>(i)].position;
            const double cutoff = pairCutoff(i, j);
            const double cutoffSq = cutoff * cutoff;
            for (const Vec3& t : translations) {
                if (i == j && t.dot(t) < 1e-12)
                    continue; // the atom itself
                const Vec3 d = base + t;
                const double distSq = d.dot(d);
                if (distSq >= cutoffSq || distSq < 0.16) // 0.4 Å overlap floor
                    continue;
                neighborIndices[static_cast<std::size_t>(i)].push_back(j);
            }
        }
        result.cn[static_cast<std::size_t>(i)] =
            static_cast<int>(neighborIndices[static_cast<std::size_t>(i)].size());
    }

    double cnMax = options.bulkCoordination;
    if (cnMax <= 0.0)
        cnMax = static_cast<double>(*std::max_element(result.cn.begin(), result.cn.end()));
    result.bulkCoordinationUsed = cnMax;

    result.gcn.assign(static_cast<std::size_t>(n), 0.0);
    if (cnMax > 0.0) {
        for (int i = 0; i < n; ++i) {
            double sum = 0.0;
            for (const int j : neighborIndices[static_cast<std::size_t>(i)])
                sum += result.cn[static_cast<std::size_t>(j)];
            result.gcn[static_cast<std::size_t>(i)] = sum / cnMax;
        }
    }
    return result;
}

} // namespace calango::core
