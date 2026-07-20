#pragma once

#include "core/UnitCell.hpp"
#include "core/Vec3.hpp"

#include <array>
#include <vector>

namespace calango::core {

/// First Brillouin zone: the Wigner-Seitz cell of the reciprocal lattice
/// (2π convention). `faces` hold vertex indices ordered counter-clockwise
/// when viewed from outside (outward normal = the generating G vector).
struct BrillouinZoneData {
    std::array<Vec3, 3> reciprocal; ///< b1, b2, b3 (include the 2π factor)
    std::vector<Vec3> vertices;     ///< Å⁻¹, zone centered at Γ = origin
    std::vector<std::vector<int>> faces;
};

/// Computes the Wigner-Seitz cell of the reciprocal lattice by half-space
/// intersection: a point x is inside iff  x·G <= |G|²/2  for every
/// reciprocal lattice vector G (searched over the ±2 neighbor shells,
/// which is sufficient for any Niggli-reasonable cell). Vertices are
/// enumerated as triple-plane intersections and filtered against all
/// half-spaces. Throws std::invalid_argument for a degenerate cell.
BrillouinZoneData computeBrillouinZone(const UnitCell& cell);

} // namespace calango::core
