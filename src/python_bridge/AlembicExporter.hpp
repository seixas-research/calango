#pragma once

#include "core/Structure.hpp"
#include "render/StructureRenderer.hpp"

#include <QString>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace calango::pybridge {

/// Writes the viewport scene to an Alembic (`.abc`) geometry cache.
///
/// Alembic is the interchange format the DCC tools speak — Blender, Houdini,
/// Maya, Cinema 4D and Unreal all import it — and it is a *baked geometry*
/// format: no rig, no procedural graph, just polygons and their positions
/// sampled over time. That is exactly the right shape for a molecular scene
/// bound for a renderer or a video, and it is what the ray-trace exporters
/// (POV-Ray, Tachyon) cannot do, since those describe a single still image to
/// one specific renderer.
///
/// The scene is emitted as one PolyMesh per chemical element ("atoms_Fe",
/// "atoms_O", …) plus one for the bonds and one for the cell wireframe, rather
/// than as a single merged blob: the split is what lets a material be assigned
/// per element downstream, which is the first thing anyone does after
/// importing.
///
/// A trajectory writes one time sample per frame. Topology is written on every
/// sample (Alembic permits heterogeneous topology), so a frame whose bonds
/// break or reform is carried honestly instead of being forced onto the first
/// frame's connectivity.
///
/// Requires PyAlembic in the embedded interpreter; throws std::runtime_error
/// with the install line when it is absent. GUI-thread only (like all of
/// pybridge).
class AlembicExporter {
public:
    struct Options {
        /// Radii, per-element colours and the bond-perception rules the
        /// viewport is currently using, so the exported cache matches what is
        /// on screen rather than a second set of defaults.
        render::StructureRenderer::Style style;
        /// Sphere tessellation: `sphereSegments` around the equator and half
        /// as many pole to pole. 24 is a good default — smooth at figure scale
        /// and still only ~1 100 triangles per atom.
        int sphereSegments = 24;
        /// Sides of each bond cylinder.
        int cylinderSides = 16;
        bool includeBonds = true;
        bool includeCell = true;
        /// Radius of the cell wireframe tubes (Å). The viewport draws the cell
        /// as GL lines below a width of 1, which has no thickness to export, so
        /// a definite radius is used here.
        double cellTubeRadius = 0.03;
        /// Playback rate the samples are written at (a trajectory only).
        double fps = 24.0;
    };

    /// Write `frames` (one entry = a still, several = an animated cache).
    /// Throws std::runtime_error on any failure, including a missing
    /// PyAlembic.
    static void exportScene(
        const std::vector<std::shared_ptr<const core::Structure>>& frames,
        const QString& path, const Options& options);

    // -- Testable seam ------------------------------------------------------
    //
    // Everything above the Python boundary is plain geometry, and it is where
    // the defects that matter live: a face index out of range, a mesh grouped
    // under the wrong element, a bond emitted for a hidden hydrogen. Those
    // produce a FILE — one that imports as garbage in a DCC tool rather than
    // failing — so they need checking directly, and PyAlembic is not available
    // on every platform (notably not on conda-forge for arm64 macOS) to check
    // them through a round trip.

    /// One exported PolyMesh: a flat position stream plus the face table
    /// Alembic wants (per-face vertex counts and concatenated vertex indices).
    struct Mesh {
        std::vector<float> positions; ///< x, y, z per vertex
        std::vector<int> indices;     ///< concatenated face vertex indices
        std::vector<int> counts;      ///< vertices per face
        float color[3] = {0.8f, 0.8f, 0.8f};

        std::size_t vertexCount() const { return positions.size() / 3; }
    };

    /// Every mesh one frame contributes, keyed by the object name it takes in
    /// the archive ("atoms_Fe", "bonds", "unit_cell").
    static std::map<std::string, Mesh> buildMeshes(const core::Structure& structure,
                                                   const Options& options);

    /// Source of the Python-side writer class. Exposed so a test can check it
    /// is valid Python without a PyAlembic install — it is a large embedded
    /// literal, and a syntax error in it would otherwise surface only on a
    /// machine that has the module.
    static const char* writerSource();
};

} // namespace calango::pybridge
