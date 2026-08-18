#pragma once

#include "core/Vec3.hpp"
#include "core/VolumetricData.hpp"

#include <vector>

namespace calango::core {

/// Triangle soup of an extracted isosurface (positions.size() is a
/// multiple of 3; consecutive triples form triangles). `colorValues`
/// carries the secondary field sampled at each vertex when a color field
/// was supplied (EPM mode) — empty otherwise.
struct IsoMesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals; ///< from the field gradient (smooth shading)
    std::vector<double> colorValues;
    /// |∇field| at each vertex, from the SAME central-difference stencil
    /// `normals` is already built from — always populated (it costs nothing
    /// beyond what normalizing the gradient already computes), regardless of
    /// whether `colorValues` is. For a Fermi surface this is the group
    /// velocity magnitude up to a constant (E_n(k) is the field), which is
    /// why this exists rather than overloading `colorValues`: that field's
    /// existing contract (empty unless an explicit colorField was supplied)
    /// is relied on elsewhere to mean "no secondary field," and quietly
    /// filling it by default would change what every other caller sees.
    std::vector<double> gradientMagnitude;
};

/// What the extractor believes lies beyond the last grid node.
///
/// This is a statement about the DATA, not a preference: a grid holding one
/// period of a crystal field really does continue into the next period, and a
/// grid that already holds several periods (see periodicWindow()) really does
/// not continue past its own edge — wrapping it would stitch two unrelated
/// images together and hang a spurious surface off the outer shell.
enum class FieldWrap {
    /// Node n is node 0. Surfaces close across the box faces, at the price of
    /// a lobe that leaves through one face reappearing at the opposite one.
    Periodic,
    /// The box is isolated; the outermost shell of nodes bounds the surface.
    Clamped,
};

/// Marching-cubes-family isosurface extraction using the tetrahedral
/// decomposition of each grid cell (6 tetrahedra sharing the main
/// diagonal). The variant is table-free and topologically unambiguous —
/// no 256-case lookup — at the cost of a few more triangles. Under
/// FieldWrap::Periodic values are sampled periodically so surfaces close
/// across cell boundaries of crystal grids; `colorField` (e.g. an
/// electrostatic potential on the same box) is interpolated at every
/// generated vertex.
IsoMesh extractIsosurface(const VolumetricData& field, double isovalue,
                          const VolumetricData* colorField = nullptr,
                          FieldWrap wrap = FieldWrap::Periodic);

/// A triangle soup with its coincident vertices identified.
///
/// Marching cubes emits three vertices per triangle with no index buffer, so
/// two triangles meeting along an edge carry two bit-identical copies of each
/// of its endpoints. Anything that needs the surface as a GRAPH rather than as
/// a pile of triangles — smoothing, connected components, an edge-manifold
/// check — has to identify those copies first.
struct WeldedMesh {
    std::vector<Vec3> points;  ///< unique positions, in first-seen order
    std::vector<int> index;    ///< one per mesh vertex → its index in `points`
};

/// Weld `mesh`'s coincident vertices on a quantized position.
///
/// The default tolerance is tight on purpose: vertices that should be one
/// point come out of the extractor bit-identical or within rounding of each
/// other, never merely close, so a fine grid identifies them all without ever
/// merging two genuinely distinct sheets that happen to pass nearby.
WeldedMesh weldVertices(const IsoMesh& mesh, double tolerance = 1e-5);

/// Replace each triangle's three (smooth, gradient-derived) vertex normals
/// with a single flat one: their average, or the geometric
/// (edge1 × edge2) normal when that average degenerates. Positions,
/// colorValues and gradientMagnitude pass through unchanged.
///
/// A sheet built of small, curved facets reads as faceted under smooth
/// shading no differently than under flat — the facets are geometry, not a
/// shading choice — but a facet that has been CUT (see
/// BrillouinZone::clipToWignerSeitzCell) no longer has three well-defined
/// gradient normals of its own, since new vertices were interpolated along a
/// cut edge rather than sampled from the field. Flattening first, before any
/// cutting, sidesteps the question: every fragment of a triangle inherits
/// that triangle's one normal, cut or not.
IsoMesh flattenTriangleNormals(const IsoMesh& mesh);

/// Laplacian-smooth `mesh` in place, `passes` iterations, under-relaxed
/// (λ = 0.5, so the surface creeps toward its neighbours instead of
/// collapsing — a few passes at λ = 1 visibly shrinks a lobe).
///
/// The mesh has no shared vertices — every triangle carries its own three —
/// so neighbours are found by welding on quantized position first (the
/// SAME weldVertices() the periodic-continuation component filter uses, so
/// the two can never disagree about which vertices are the same point).
/// Positions AND normals are smoothed — re-derived area-weighted over the
/// triangles meeting each welded point — since leaving the normals alone
/// would keep the faceted shading the smoothing was meant to remove.
/// `colorValues`/`gradientMagnitude` are left untouched: they are physical
/// samples of the field, and smoothing the shape they are attached to must
/// not also blur what they say. A `passes <= 0` or a mesh under one
/// triangle is a no-op.
void smoothMesh(IsoMesh& mesh, int passes);

} // namespace calango::core
