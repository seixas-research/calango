#pragma once

#include "core/Vec3.hpp"

#include <array>
#include <utility>

namespace calango::core {

/// Periodic cell described by three row vectors (ASE convention:
/// cell[i] is the i-th lattice vector in Cartesian Å).
class UnitCell {
public:
    UnitCell() = default;
    UnitCell(const Vec3& a, const Vec3& b, const Vec3& c,
             std::array<bool, 3> pbc = {true, true, true});

    const std::array<Vec3, 3>& vectors() const { return vectors_; }
    void setVectors(const std::array<Vec3, 3>& v) { vectors_ = v; }

    std::array<bool, 3> pbc() const { return pbc_; }
    void setPbc(std::array<bool, 3> pbc) { pbc_ = pbc; }

    /// True if the cell spans a non-degenerate volume.
    bool isDefined() const;
    double volume() const;

    Vec3 fractionalToCartesian(const Vec3& frac) const;
    Vec3 cartesianToFractional(const Vec3& cart) const;

    /// The 8 Cartesian corners of the parallelepiped (origin at 0,0,0).
    std::array<Vec3, 8> corners() const;

    /// Corner-index pairs forming the 12 cell edges (for wireframe drawing).
    static const std::array<std::pair<int, int>, 12>& edges();

private:
    std::array<Vec3, 3> vectors_{};
    std::array<bool, 3> pbc_{false, false, false};
};

} // namespace calango::core
