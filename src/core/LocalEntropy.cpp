#include "core/LocalEntropy.hpp"
#include "core/PeriodicImages.hpp"

#include <array>
#include <cmath>
#include <numbers>

namespace calango::core {


std::vector<double> computeLocalEntropy(const Structure& structure,
                                        const LocalEntropyOptions& options)
{
    const auto& atoms = structure.atoms();
    const auto n = static_cast<int>(atoms.size());
    std::vector<double> entropy(static_cast<std::size_t>(n), 0.0);
    if (n == 0 || options.cutoff <= 0.0 || options.sigma <= 0.0
        || options.gridPoints < 8)
        return entropy;

    const double rc = options.cutoff;
    const double sigma = options.sigma;

    const bool pbc = structure.cell().isDefined()
        && (structure.cell().pbc()[0] || structure.cell().pbc()[1]
            || structure.cell().pbc()[2]);
    // Broadened tails reach past rc — pad the image search a little.
    const std::vector<Vec3> translations =
        imageTranslations(structure.cell(), rc + 3.0 * sigma, pbc);

    // Neighbor distances per atom (r <= rc + 3σ so gaussians at the edge
    // still contribute to the integral inside rc).
    const double collectMax = rc + 3.0 * sigma;
    const double collectMaxSq = collectMax * collectMax;
    std::vector<std::vector<double>> distances(static_cast<std::size_t>(n));
    std::vector<std::vector<int>> neighbors(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const Vec3 base = atoms[static_cast<std::size_t>(j)].position
                - atoms[static_cast<std::size_t>(i)].position;
            for (const Vec3& t : translations) {
                if (i == j && t.dot(t) < 1e-12)
                    continue;
                const Vec3 d = base + t;
                const double distSq = d.dot(d);
                if (distSq >= collectMaxSq || distSq < 1e-12)
                    continue;
                const double dist = std::sqrt(distSq);
                distances[static_cast<std::size_t>(i)].push_back(dist);
                if (dist <= rc)
                    neighbors[static_cast<std::size_t>(i)].push_back(j);
            }
        }
    }

    // Reference density: exact from the cell when periodic, otherwise a
    // per-atom estimate from the neighbor count inside the cutoff sphere.
    const double sphereVolume = 4.0 / 3.0 * std::numbers::pi * rc * rc * rc;
    const double globalDensity =
        pbc ? n / structure.cell().volume() : 0.0;

    const double dr = rc / options.gridPoints;
    const double gaussNorm = 1.0 / (std::sqrt(2.0 * std::numbers::pi) * sigma);

    for (int i = 0; i < n; ++i) {
        const auto& dists = distances[static_cast<std::size_t>(i)];
        const double density = pbc
            ? globalDensity
            : (dists.empty() ? 0.0
                             : static_cast<double>(dists.size()) / sphereVolume);
        if (density <= 0.0)
            continue; // isolated atom — entropy stays 0

        // s_i = −2πρ ∫ [g ln g − g + 1] r² dr  (trapezoid on midpoints)
        double integral = 0.0;
        for (int k = 0; k < options.gridPoints; ++k) {
            const double r = (k + 0.5) * dr;
            double g = 0.0;
            for (const double rij : dists)
                g += gaussNorm * std::exp(-(r - rij) * (r - rij)
                                          / (2.0 * sigma * sigma));
            g /= 4.0 * std::numbers::pi * density * r * r;
            const double integrand =
                (g > 1e-12 ? g * std::log(g) : 0.0) - g + 1.0;
            integral += integrand * r * r * dr;
        }
        entropy[static_cast<std::size_t>(i)] =
            -2.0 * std::numbers::pi * density * integral;
    }

    if (options.averageOverNeighbors) {
        std::vector<double> averaged(entropy.size());
        for (int i = 0; i < n; ++i) {
            double sum = entropy[static_cast<std::size_t>(i)];
            double count = 1.0;
            for (const int j : neighbors[static_cast<std::size_t>(i)]) {
                sum += entropy[static_cast<std::size_t>(j)];
                count += 1.0;
            }
            averaged[static_cast<std::size_t>(i)] = sum / count;
        }
        entropy = std::move(averaged);
    }
    return entropy;
}

} // namespace calango::core
