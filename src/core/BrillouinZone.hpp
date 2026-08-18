#pragma once

#include "core/MarchingCubes.hpp"
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

/// Tile `mesh` across the periodic images of `basis` needed to fully cover
/// the Wigner-Seitz cell of `basis`, then clip every copy to that cell.
///
/// A field is commonly sampled — and marching cubes always extracts — on the
/// PARALLELEPIPED spanned by `basis`, because that is the one shape a regular
/// grid can be laid on. The physically meaningful cell of the same lattice is
/// its Wigner-Seitz cell instead: same volume, different shape. For a skewed
/// lattice (the textbook case: a cubic direct lattice's reciprocal is itself
/// cubic and the two cells coincide, but an FCC direct lattice's reciprocal
/// is BCC, whose Wigner-Seitz cell is a truncated octahedron) the
/// Wigner-Seitz cell's corners reach past the parallelepiped's own faces —
/// clipping a single un-replicated copy to those corners then just deletes
/// that region instead of filling it in, leaving a hole exactly where the
/// corner should be. Replicating the mesh across its neighbouring images
/// before clipping recovers it: the field is periodic by construction, so a
/// translated copy of the SAME mesh is an equally valid sample of it, and
/// because the parallelepiped and the Wigner-Seitz cell tile the same
/// lattice, every point of the latter is covered by exactly one translated
/// copy of the former (shared faces aside).
///
/// Which images are needed is read off the Wigner-Seitz cell's own vertices
/// — not assumed from a fixed neighbour-shell count — so this holds for any
/// non-degenerate basis, not only cubic/BCC/FCC ones. `mesh.colorValues` is
/// dropped rather than carried through: nothing that calls this passes a
/// colour field alongside the isosurface today. One normal per SOURCE
/// triangle (its vertex normals, averaged) is kept on every fragment that
/// triangle clips into, matching how marching cubes' flat facets are already
/// shaded elsewhere. Throws std::invalid_argument for a degenerate basis, the
/// same as wignerSeitzCell().
IsoMesh clipToWignerSeitzCell(const IsoMesh& mesh, const std::array<Vec3, 3>& basis);

} // namespace calango::core
