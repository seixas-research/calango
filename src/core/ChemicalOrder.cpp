#include "core/ChemicalOrder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

namespace calango::core {

namespace {

/// Number of periodic repeats along each axis covering every image within
/// rMax (same slab-width construction as the RDF).
std::array<int, 3> imageRange(const UnitCell& cell, double rMax)
{
    const auto& a = cell.vectors();
    const double volume = std::abs(a[0].dot(a[1].cross(a[2])));
    std::array<int, 3> range{0, 0, 0};
    const auto pbc = cell.pbc();
    for (int i = 0; i < 3; ++i) {
        if (!pbc[static_cast<std::size_t>(i)])
            continue;
        const Vec3 crossArea = a[(i + 1) % 3].cross(a[(i + 2) % 3]);
        const double width = volume / crossArea.norm();
        range[static_cast<std::size_t>(i)] = static_cast<int>(std::ceil(rMax / width));
    }
    return range;
}

} // namespace

WarrenCowleyResult computeWarrenCowley(const Structure& structure,
                                       const WarrenCowleyOptions& options)
{
    WarrenCowleyResult result;
    const auto& atoms = structure.atoms();
    const auto n = static_cast<int>(atoms.size());
    if (n == 0 || options.shellCutoffs.empty())
        return result;

    std::vector<double> cutoffs = options.shellCutoffs;
    std::sort(cutoffs.begin(), cutoffs.end());

    // Species table and concentrations.
    std::map<int, int> speciesIndex; // Z -> row/column
    for (const Atom& atom : atoms)
        speciesIndex.emplace(atom.atomicNumber, 0);
    int index = 0;
    for (auto& [z, idx] : speciesIndex) {
        idx = index++;
        result.species.push_back(z);
    }
    const auto speciesCount = result.species.size();
    result.concentrations.assign(speciesCount, 0.0);
    for (const Atom& atom : atoms)
        result.concentrations[static_cast<std::size_t>(
            speciesIndex[atom.atomicNumber])] += 1.0 / n;

    // Neighbor-pair counts per shell: counts[shell][i][j], plus the
    // number of neighbors of each central species per shell.
    const auto shellCount = cutoffs.size();
    std::vector<std::vector<std::vector<double>>> counts(
        shellCount, std::vector<std::vector<double>>(
                        speciesCount, std::vector<double>(speciesCount, 0.0)));
    std::vector<double> totalNeighbors(shellCount, 0.0);

    const bool pbc = structure.cell().isDefined()
        && (structure.cell().pbc()[0] || structure.cell().pbc()[1]
            || structure.cell().pbc()[2]);
    const double rMax = cutoffs.back();
    std::vector<Vec3> translations{{0.0, 0.0, 0.0}};
    if (pbc) {
        translations.clear();
        const auto range = imageRange(structure.cell(), rMax);
        const auto& v = structure.cell().vectors();
        for (int i = -range[0]; i <= range[0]; ++i)
            for (int j = -range[1]; j <= range[1]; ++j)
                for (int k = -range[2]; k <= range[2]; ++k)
                    translations.push_back(v[0] * i + v[1] * j + v[2] * k);
    }

    const double rMaxSq = rMax * rMax;
    for (int i = 0; i < n; ++i) {
        const auto si = static_cast<std::size_t>(
            speciesIndex[atoms[static_cast<std::size_t>(i)].atomicNumber]);
        for (int j = 0; j < n; ++j) {
            const auto sj = static_cast<std::size_t>(
                speciesIndex[atoms[static_cast<std::size_t>(j)].atomicNumber]);
            const Vec3 base = atoms[static_cast<std::size_t>(j)].position
                - atoms[static_cast<std::size_t>(i)].position;
            for (const Vec3& t : translations) {
                if (i == j && t.dot(t) < 1e-12)
                    continue; // self
                const Vec3 d = base + t;
                const double distSq = d.dot(d);
                if (distSq >= rMaxSq || distSq < 1e-12)
                    continue;
                const double dist = std::sqrt(distSq);
                const auto shell = static_cast<std::size_t>(
                    std::lower_bound(cutoffs.begin(), cutoffs.end(), dist)
                    - cutoffs.begin());
                if (shell < shellCount) {
                    counts[shell][si][sj] += 1.0;
                    totalNeighbors[shell] += 1.0;
                }
            }
        }
    }

    // α_ij = 1 − p_ij / c_j, per shell.
    for (std::size_t shell = 0; shell < shellCount; ++shell) {
        WarrenCowleyShell out;
        out.rMin = shell == 0 ? 0.0 : cutoffs[shell - 1];
        out.rMax = cutoffs[shell];
        out.meanNeighbors = totalNeighbors[shell] / n;
        out.alpha.assign(speciesCount, std::vector<double>(
                                           speciesCount,
                                           std::numeric_limits<double>::quiet_NaN()));
        for (std::size_t si = 0; si < speciesCount; ++si) {
            double neighborsOfI = 0.0;
            for (std::size_t sj = 0; sj < speciesCount; ++sj)
                neighborsOfI += counts[shell][si][sj];
            if (neighborsOfI <= 0.0)
                continue; // no neighbors in this shell — α undefined (NaN)
            for (std::size_t sj = 0; sj < speciesCount; ++sj) {
                const double cj = result.concentrations[sj];
                if (cj > 0.0)
                    out.alpha[si][sj] =
                        1.0 - (counts[shell][si][sj] / neighborsOfI) / cj;
            }
        }
        result.shells.push_back(std::move(out));
    }
    return result;
}

} // namespace calango::core
