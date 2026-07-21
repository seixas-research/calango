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

/// Marching-cubes-family isosurface extraction using the tetrahedral
/// decomposition of each grid cell (6 tetrahedra sharing the main
/// diagonal). The variant is table-free and topologically unambiguous —
/// no 256-case lookup — at the cost of a few more triangles. Values are
/// sampled periodically so surfaces close across cell boundaries of
/// crystal grids; `colorField` (e.g. an electrostatic potential on the
/// same box) is interpolated at every generated vertex.
IsoMesh extractIsosurface(const VolumetricData& field, double isovalue,
                          const VolumetricData* colorField = nullptr);

} // namespace calango::core
