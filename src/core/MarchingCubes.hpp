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

} // namespace calango::core
