#pragma once

#include "core/PeriodicImages.hpp"
#include "core/Structure.hpp"

#include <algorithm>
#include <cmath>

namespace calango::core {

/// The minimum-image vector from `structure`'s atom `from` to its atom `to`,
/// over every periodic image within `cutoff` of each other — the same
/// "closest approach across the periodic boundary" search HydrogenBonds.cpp
/// and the graphene oxide builder's own bond perception already do, each
/// independently. Falls back to the raw, unwrapped difference for a
/// structure with no defined cell.
///
/// Header-only: a handful of arithmetic ops, called from bond/angle
/// distributions that may run it many thousands of times per frame.
inline Vec3 minimumImageVector(const Structure& structure, int from, int to,
                               double cutoff)
{
    const auto& atoms = structure.atoms();
    const Vec3 fromPos = atoms[static_cast<std::size_t>(from)].position;
    const Vec3 toPos = atoms[static_cast<std::size_t>(to)].position;
    Vec3 best = toPos - fromPos;
    if (!structure.cell().isDefined())
        return best;
    double bestNorm = best.norm();
    const auto range = imageRange(structure.cell(), cutoff);
    const auto& v = structure.cell().vectors();
    for (int ia = -range[0]; ia <= range[0]; ++ia) {
        for (int ib = -range[1]; ib <= range[1]; ++ib) {
            for (int ic = -range[2]; ic <= range[2]; ++ic) {
                if (ia == 0 && ib == 0 && ic == 0)
                    continue;
                const Vec3 shift = v[0] * static_cast<double>(ia)
                    + v[1] * static_cast<double>(ib)
                    + v[2] * static_cast<double>(ic);
                const Vec3 candidate = toPos + shift - fromPos;
                const double norm = candidate.norm();
                if (norm < bestNorm) {
                    bestNorm = norm;
                    best = candidate;
                }
            }
        }
    }
    return best;
}

/// Angle j-i-k at vertex `i` (degrees), PBC-aware: `j` and `k` are each given
/// their own minimum-image position relative to `i` independently — see
/// minimumImageVector() — so a group bridging the periodic boundary (an
/// epoxide's two host carbons, on a small periodic sheet) is measured
/// correctly rather than torn across the cell.
///
/// No generic, Structure::detectBonds()-based angle utility existed before
/// this one; HydrogenBonds.cpp and Distributions.cpp each carry their own
/// private, non-reusable copy of the same acos(clamp(cosine)) idiom. This is
/// the shared version, first used by the GO Functional Group Analysis
/// module's C-C-C / C-O-C / C-O-H distributions.
///
/// `armCutoff` bounds the minimum-image search and should exceed the longest
/// bond either arm can be — the default covers any covalent bond this
/// application draws.
///
/// Returns a negative value (never a valid angle) if either arm has near-
/// zero length: `i`, `j` or `k` coincide, which is a malformed call, not a
/// degenerate structure to report an angle for.
inline double angleBetween(const Structure& structure, int vertex, int j,
                           int k, double armCutoff = 2.5)
{
    const Vec3 vj = minimumImageVector(structure, vertex, j, armCutoff);
    const Vec3 vk = minimumImageVector(structure, vertex, k, armCutoff);
    const double nj = vj.norm();
    const double nk = vk.norm();
    if (nj < 1e-6 || nk < 1e-6)
        return -1.0;
    const double cosine = std::clamp(vj.dot(vk) / (nj * nk), -1.0, 1.0);
    return std::acos(cosine) * 180.0 / M_PI;
}

} // namespace calango::core
