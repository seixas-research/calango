#pragma once

#include "core/UnitCell.hpp"

#include <array>
#include <cmath>
#include <vector>

namespace calango::core {

/// How many periodic images to visit along each lattice vector so that every
/// neighbor within `rMax` of any atom in the cell is reached.
///
/// The bound is the cell's *perpendicular width* along each axis — the
/// distance between the two lattice planes normal to that axis, i.e.
/// volume / |a_j x a_k|. Using the vector length instead would under-count
/// images in strongly sheared cells, missing real neighbors.
///
/// Non-periodic directions get 0 (no images). A degenerate cell (zero or
/// near-zero volume) also yields 0 rather than dividing by a zero width:
/// `rMax / 0` is +inf, and narrowing +inf to int is undefined behavior —
/// in practice a garbage image count that can hang or crash the neighbor
/// loops. This guard is why every consumer must use this one function
/// rather than keeping a private copy.
///
/// Header-only: it is a handful of arithmetic ops called once per analysis
/// run, and every consumer lives in core/ where inlining it costs nothing.
inline std::array<int, 3> imageRange(const UnitCell& cell, double rMax)
{
    const auto& a = cell.vectors();
    const double volume = std::abs(a[0].dot(a[1].cross(a[2])));
    const auto pbc = cell.pbc();

    std::array<int, 3> range{0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        const auto axis = static_cast<std::size_t>(i);
        if (!pbc[axis] || volume < 1e-9)
            continue;
        const Vec3 crossArea = a[(i + 1) % 3].cross(a[(i + 2) % 3]);
        const double area = crossArea.norm();
        if (area < 1e-12)
            continue; // parallel lattice vectors: no well-defined width
        const double width = volume / area;
        range[axis] = static_cast<int>(std::ceil(rMax / width));
    }
    return range;
}

/// The lattice translation of every periodic image to visit for a neighbor
/// search of radius `rMax`, via imageRange(). With `usePbc` false — the open
/// case, or however the caller spells "this structure is not periodic" — the
/// list is just the zero translation, so callers can loop over the result
/// unconditionally.
inline std::vector<Vec3> imageTranslations(const UnitCell& cell, double rMax,
                                           bool usePbc)
{
    std::vector<Vec3> translations{{0.0, 0.0, 0.0}};
    if (usePbc) {
        translations.clear();
        const auto range = imageRange(cell, rMax);
        const auto& v = cell.vectors();
        for (int i = -range[0]; i <= range[0]; ++i)
            for (int j = -range[1]; j <= range[1]; ++j)
                for (int k = -range[2]; k <= range[2]; ++k)
                    translations.push_back(v[0] * i + v[1] * j + v[2] * k);
    }
    return translations;
}

} // namespace calango::core
