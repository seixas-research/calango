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

/// One convex polyhedron: the vertices and the faces indexing them,
/// counter-clockwise seen from outside.
struct PolyhedronMesh {
    std::vector<Vec3> vertices;
    std::vector<std::vector<int>> faces;
};

/// The Wigner-Seitz cell of the lattice spanned by `basis`: the set of points
/// closer to the origin than to any other lattice point.
///
/// Computed by half-space intersection — x is inside iff x·L <= |L|²/2 for
/// every lattice vector L (searched over the ±2 neighbour shells, sufficient
/// for any Niggli-reasonable cell). Vertices are enumerated as triple-plane
/// intersections and filtered against all half-spaces.
///
/// One function for both lattices because it IS one construction. The first
/// Brillouin zone is this cell of the reciprocal lattice; the Voronoi cell
/// drawn in the viewport is this cell of the direct lattice. Only the basis
/// differs, and duplicating ~80 lines of plane geometry to say that would mean
/// two chances to get the vertex de-duplication and the face winding right.
///
/// Throws std::invalid_argument when `basis` is degenerate.
PolyhedronMesh wignerSeitzCell(const std::array<Vec3, 3>& basis);

/// First Brillouin zone: wignerSeitzCell() of the reciprocal lattice, with the
/// reciprocal basis returned alongside. Throws for a degenerate cell.
BrillouinZoneData computeBrillouinZone(const UnitCell& cell);

/// Voronoi (Wigner-Seitz) cell of the DIRECT lattice, in Å, centred on the
/// origin — the primitive cell that carries the full point symmetry of the
/// lattice, as opposed to the parallelepiped of the chosen basis vectors.
/// Returns an empty mesh for a cell that is not defined.
PolyhedronMesh computeWignerSeitzCell(const UnitCell& cell);

} // namespace calango::core
