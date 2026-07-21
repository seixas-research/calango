#include "core/Rdf.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace calango::core {

namespace {

/// Number of periodic repeats needed along each axis so that every image
/// within rMax is covered: rMax / (perpendicular width of the cell slab).
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

RdfResult computeRdf(const Structure& structure, const RdfOptions& options)
{
    RdfResult result;
    const auto& atoms = structure.atoms();
    const auto n = static_cast<int>(atoms.size());
    if (n == 0 || options.bins < 1 || options.rMax <= 0.0)
        return result;

    const auto matches = [](const Atom& atom, int filter) {
        return filter == 0 || atom.atomicNumber == filter;
    };

    int countA = 0, countB = 0;
    for (const Atom& atom : atoms) {
        if (matches(atom, options.elementA))
            ++countA;
        if (matches(atom, options.elementB))
            ++countB;
    }
    if (countA == 0 || countB == 0)
        return result;

    const double dr = options.rMax / options.bins;
    std::vector<double> histogram(static_cast<std::size_t>(options.bins), 0.0);

    const bool pbc = options.usePbc && structure.cell().isDefined();

    // Translation images to consider ((0,0,0) only in the open case).
    std::vector<Vec3> translations{{0.0, 0.0, 0.0}};
    if (pbc) {
        translations.clear();
        const auto range = imageRange(structure.cell(), options.rMax);
        const auto& v = structure.cell().vectors();
        for (int i = -range[0]; i <= range[0]; ++i)
            for (int j = -range[1]; j <= range[1]; ++j)
                for (int k = -range[2]; k <= range[2]; ++k)
                    translations.push_back(v[0] * i + v[1] * j + v[2] * k);
    }

    const double rMaxSq = options.rMax * options.rMax;
    for (int i = 0; i < n; ++i) {
        if (!matches(atoms[static_cast<std::size_t>(i)], options.elementA))
            continue;
        for (int j = 0; j < n; ++j) {
            if (!matches(atoms[static_cast<std::size_t>(j)], options.elementB))
                continue;
            const Vec3 base = atoms[static_cast<std::size_t>(j)].position
                - atoms[static_cast<std::size_t>(i)].position;
            for (const Vec3& t : translations) {
                if (i == j && t.dot(t) < 1e-12)
                    continue; // self
                const Vec3 d = base + t;
                const double distSq = d.dot(d);
                if (distSq >= rMaxSq || distSq < 1e-12)
                    continue;
                const auto bin =
                    static_cast<std::size_t>(std::sqrt(distSq) / dr);
                if (bin < histogram.size())
                    histogram[bin] += 1.0;
            }
        }
    }

    // Reference density of B partners.
    double volume = 0.0;
    if (pbc) {
        volume = structure.cell().volume();
    } else {
        Vec3 lo{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max()};
        Vec3 hi{-lo.x, -lo.y, -lo.z};
        for (const Atom& atom : atoms) {
            lo = {std::min(lo.x, atom.position.x), std::min(lo.y, atom.position.y),
                  std::min(lo.z, atom.position.z)};
            hi = {std::max(hi.x, atom.position.x), std::max(hi.y, atom.position.y),
                  std::max(hi.z, atom.position.z)};
        }
        // Pad so planar/linear molecules don't yield a zero volume.
        volume = (hi.x - lo.x + 2.0) * (hi.y - lo.y + 2.0) * (hi.z - lo.z + 2.0);
    }
    const double densityB = countB / volume;

    result.r.resize(histogram.size());
    result.g.resize(histogram.size());
    for (std::size_t k = 0; k < histogram.size(); ++k) {
        const double rLo = k * dr;
        const double rHi = rLo + dr;
        const double shell = 4.0 / 3.0 * M_PI * (rHi * rHi * rHi - rLo * rLo * rLo);
        result.r[k] = rLo + 0.5 * dr;
        result.g[k] = histogram[k] / (static_cast<double>(countA) * densityB * shell);
    }
    return result;
}

RdfResult computeRdfAveraged(const std::vector<Structure>& frames,
                             const RdfOptions& options)
{
    RdfResult accumulated;
    std::size_t contributing = 0;
    for (const Structure& frame : frames) {
        const RdfResult single = computeRdf(frame, options);
        if (single.g.empty())
            continue;
        if (accumulated.g.empty()) {
            accumulated = single;
        } else {
            for (std::size_t k = 0; k < accumulated.g.size(); ++k)
                accumulated.g[k] += single.g[k];
        }
        ++contributing;
    }
    if (contributing > 1)
        for (double& g : accumulated.g)
            g /= static_cast<double>(contributing);
    return accumulated;
}

} // namespace calango::core
