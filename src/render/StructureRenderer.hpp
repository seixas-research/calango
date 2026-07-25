#pragma once

#include "core/UnitCell.hpp"

#include "render/ColorMap.hpp"

#include <QColor>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QVector3D>

#include <map>
#include <set>
#include <vector>

namespace calango::core {
class Structure;
}

class QOpenGLFunctions_3_3_Core;

namespace calango::render {

/// Per-atom vector property drawn as 3D arrows over the atomic sites.
/// Enum order is the "Vector overlay" combo order in the Representation
/// panel; each entry names one of the structure's vector fields, populated
/// from the extended-XYZ per-atom columns (or from ASE momenta, for
/// velocities).
enum class VectorOverlay {
    None,
    Velocity,       ///< "velocities" (Å/fs·√amu)
    Force,          ///< "forces" (eV/Å)
    MagneticMoment, ///< "magmoms" (μB) — non-collinear (N,3), see below
};

/// Vector field backing an overlay, and the label/unit used in the UI.
/// Empty field name for None.
const char* vectorFieldName(VectorOverlay overlay);

/// Surface material for the instanced meshes (atoms, bonds, cell tubes).
/// Enum order is the "Surface finish" combo order in the Representation
/// panel and matches the FINISH_* constants in mesh.frag.
enum class SurfaceFinish {
    Standard, ///< Blinn-Phong with specular highlights, opaque
    Shiny,    ///< high specular, tight highlight (low roughness)
    Matte,    ///< diffuse only, no specular ("fosco")
    Glassy,   ///< alpha-blended, tight highlight, Fresnel rim
};

enum class RepresentationMode {
    BallAndStick,
    SpaceFilling, ///< CPK: van-der-Waals-sized spheres, no bonds
    Wireframe,    ///< bonds as colored lines, isolated atoms as points
    Polyhedral,   ///< coordination polyhedra (translucent faces + edges) on
                  ///< atoms with >= 4 bonded neighbors, plus atom spheres
};

/// One directional light, defined in VIEW space (camera-relative), so the
/// lighting stays fixed with respect to the viewer while orbiting.
/// Colors encode both hue and intensity of each Blinn-Phong component.
struct Light {
    QVector3D direction{-0.4f, -0.5f, -1.0f}; ///< direction the light travels
    QColor ambient = QColor::fromRgbF(0.28f, 0.28f, 0.28f);
    QColor diffuse = QColor::fromRgbF(0.72f, 0.72f, 0.72f);
    QColor specular = QColor::fromRgbF(0.30f, 0.30f, 0.30f);
};

/// Hard cap mirrored by MAX_LIGHTS in mesh.frag.
inline constexpr int kMaxLights = 4;

/// Draws a core::Structure. Depending on the representation mode:
///   - atoms  -> instanced unit spheres (covalent- or vdW-scaled)
///   - bonds  -> instanced half-cylinders, colored per atom; bond order
///               n renders as n parallel cylinders offset perpendicular
///               to the bond axis; or colored GL_LINES in wireframe
///   - cell   -> 12 wireframe edges
///
/// Strictly a View in MVC terms: it holds no reference to the Structure,
/// only GPU buffers derived from it. Call setStructure() again after the
/// model or style changes (a current GL context is required).
class StructureRenderer {
public:
    struct Style {
        RepresentationMode mode = RepresentationMode::BallAndStick;
        float atomScaleFactor = 1.0f; ///< global sphere-radius multiplier (UI)
        float bondWidthFactor = 1.0f; ///< global cylinder-width multiplier (UI)
        float bondRadius = 0.078f;    ///< Å, base radius of a single bond
        float bondTolerance = 1.15f;  ///< bond-detection cutoff factor
        bool autoBonds = true;        ///< distance-based bond perception on/off
                                      ///< (manual bond overrides always render)
        /// Smooth axial color gradient across each bond (atom A color at one
        /// end blending to atom B color at the other); off = classic
        /// half-and-half coloring.
        bool gradientBonds = true;
        // -- Coordination polyhedra (Polyhedral mode) ----------------------
        /// Face opacity. Translucent by default so the coordinated atoms stay
        /// visible through the hull — an opaque polyhedron hides exactly the
        /// geometry it is drawn to explain.
        float polyhedronOpacity = 0.38f;
        /// Draw the hull's edge wireframe. On by default: the edges are what
        /// make a translucent polyhedron read as a solid rather than a smear.
        bool polyhedronEdges = true;
        /// Edge width. Core-profile GL clamps glLineWidth on most drivers, so
        /// values much above ~2 may not visibly thicken.
        float polyhedronEdgeWidth = 1.5f;
        /// Per-element coordination cutoff override (Z -> Å). A central cation
        /// absent from the map uses the global bondTolerance rule; an entry
        /// here fixes ITS coordination shell at an absolute radius, which is
        /// what a cation whose covalent radii give the wrong shell needs.
        std::map<int, float> polyhedronCutoffOverrides;
        /// Per-atom vector overlays drawn as 3D arrows from each atom
        /// center (mesh representations only). Data comes from the
        /// structure's vector fields "forces" / "velocities".
        VectorOverlay vectorOverlay = VectorOverlay::None;
        SurfaceFinish surfaceFinish = SurfaceFinish::Standard;
        // -- Directional shadow mapping (Visual Effects -> Shadow) ---------
        /// Off by default: the depth pass roughly doubles draw calls, and
        /// shadows help far more in a figure than while orbiting a structure.
        bool shadowsEnabled = false;
        /// How dark an occluded fragment becomes (0 = no visible shadow,
        /// 1 = full loss of direct light; ambient is never attenuated).
        float shadowStrength = 0.55f;
        /// PCF kernel half-width in shadow-map texels. 0 = hard edges,
        /// larger = softer and more expensive ((2r+1)^2 taps per fragment).
        int shadowSoftness = 2;
        /// Base opacity of the Glassy finish at face-on incidence (the
        /// Fresnel term drives edges toward opaque). Ignored otherwise.
        float glassOpacity = 0.45f;
        /// Normalized vector-overlay length: 1.0 is the calibrated baseline
        /// (kVectorBaseScale Å of arrow per field unit), not a raw Å factor.
        float vectorScale = 1.0f;
        QColor forceColor{242, 92, 54};
        QColor velocityColor{54, 166, 242};
        QColor magmomColor{168, 120, 240};
        bool showCell = true;
        /// Draw duplicate "ghost" atoms and their bonds at the far faces,
        /// edges and vertices of the cell: an atom sitting at fractional 0
        /// along an axis is repeated at 1, so the cell reads as a closed,
        /// continuous motif instead of one with atoms sliced off two of its
        /// faces.
        ///
        /// Purely a rendering duplication — the ghosts are extra GPU instances
        /// and never enter the Structure, so the atom count, the chemical
        /// formula and every exported POSCAR/CIF are unchanged.
        bool showBoundaryGhosts = false;
        /// How close to a cell face (in fractional coordinates) counts as
        /// "on" it. 1e-3 of a ~5 Å cell is ~5 mÅ: tight enough that an atom
        /// merely near the face is not duplicated, loose enough to catch
        /// coordinates that have been through a round trip through a file.
        float boundaryGhostTolerance = 1e-3f;
        QColor cellColor{166, 166, 178};
        /// 1 = plain GL lines; > 1 renders the edges as thin lit tubes
        /// (core-profile GL clamps glLineWidth, so tubes are the portable
        /// way to get thick cell wireframes).
        float cellLineWidth = 2.0f;
        std::map<int, QColor> colorOverrides;      ///< Z -> user color
        std::map<int, float> radiusScaleOverrides; ///< Z -> per-element radius factor
        /// Scalar color mapping: Element uses the CPK palette; the other
        /// modes color atoms (and their bond halves) by the per-atom
        /// scalars passed to setAtomScalars(), sampled along `gradient`.
        ColorMode colorMode = ColorMode::Element;
        ColorGradient gradient = ColorGradient::Viridis;
        /// Reverse the scalar -> color mapping of `gradient` (minima get
        /// the high end of the palette), like matplotlib's "_r" maps.
        bool invertGradient = false;
        /// Fixed color-scale bounds. Off by default, so the ramp auto-scales
        /// to the data's own min/max; on, the ramp is pinned to
        /// [customScalarMin, customScalarMax], which is what comparing frames
        /// or structures on one scale requires (auto-scaling silently
        /// renormalizes every frame and makes them incomparable). Values
        /// outside the window clamp to the ramp ends.
        bool useCustomScalarRange = false;
        float customScalarMin = 0.0f;
        float customScalarMax = 1.0f;
        /// Distance fog (View -> Visual Effects): 0 = off, 1 = linear
        /// between fogStart/fogEnd, 2 = exponential with fogDensity.
        /// fogColor tracks the viewport background. Off by default; the
        /// exponential density/start/end below are the values used once the
        /// user enables it in the Visual Effects panel.
        int fogMode = 0;
        float fogStart = 15.0f; ///< Å (view-space distance)
        float fogEnd = 80.0f;   ///< Å
        float fogDensity = 0.300f;
        QColor fogColor{26, 28, 33};
    };

    /// Display radius of an atom (Å) — the single source of truth shared
    /// by instance building and by ray-cast picking in the viewport.
    static float displayRadius(int atomicNumber, const Style& style);

    /// Element color after applying user overrides (default: Jmol CPK).
    static QColor atomColor(int atomicNumber, const Style& style);

    /// Must be called once with a current GL context (from initializeGL).
    void initialize(QOpenGLFunctions_3_3_Core* gl);

    /// Rebuild instance/vertex buffers from the model. nullptr clears the
    /// scene. Atoms whose index is in `selection` are drawn highlighted.
    void setStructure(const core::Structure* structure,
                      const std::set<int>* selection = nullptr);

    /// Cartesian translations that duplicate `position` onto the far cell
    /// faces/edges/vertices it lies on: empty for an interior atom, one entry
    /// for a face, three for an edge, seven for the origin vertex. Public so
    /// the same rule can be reused (e.g. by an exporter that wants to show
    /// what the viewport shows).
    static std::vector<core::Vec3> boundaryGhostShifts(const core::Vec3& position,
                                                       const core::UnitCell& cell,
                                                       float tolerance);

    /// Per-atom scalars driving the non-Element color modes (CN, GCN,
    /// custom fields). Values are normalized to the [min, max] range
    /// internally; an empty vector (or a size mismatch with the current
    /// structure) falls back to element colors. Call setStructure()
    /// afterwards to rebuild the instance colors.
    void setAtomScalars(std::vector<float> scalars);

    void render(const QMatrix4x4& view, const QMatrix4x4& projection);

    /// Interactive "Lattice Plane" overlay: a translucent, per-vertex-colored
    /// quad (a Miller-index plane, optionally color-mapped from a volumetric
    /// scalar field) plus its edge outline. `faceTris` / `edgeLines` are
    /// interleaved pos(3)+color(3) streams (GL_TRIANGLES / GL_LINES). Pass empty
    /// streams (or visible=false) to hide it. Requires a current GL context.
    void setLatticePlane(const std::vector<float>& faceTris,
                         const std::vector<float>& edgeLines, float alpha,
                         bool visible, bool showEdges);

    /// A contiguous run of triangles in the custom-overlay face buffer that
    /// share one opacity — one per user primitive, so each can blend
    /// independently.
    struct OverlayRange {
        int first = 0;    ///< first vertex (not byte / not float) in the buffer
        int count = 0;    ///< number of vertices
        float alpha = 1.0f;
    };

    /// "Custom Overlay" geometric primitives (spheres, boxes, cylinders,
    /// planes…) drawn over the structure. `faces`/`edges` are interleaved
    /// pos(3)+color(3) streams; `faceRanges` slices `faces` into per-primitive
    /// runs so each blends at its own opacity. Edges render opaque.
    void setCustomOverlay(const std::vector<float>& faces,
                          const std::vector<float>& edges,
                          const std::vector<OverlayRange>& faceRanges,
                          bool visible);

    /// Hydrogen-bond overlay: an interleaved pos(3)+color(3) GL_LINES stream
    /// of PRE-DASHED segments (see buildHydrogenBondDashes). Empty clears it.
    void setHydrogenBonds(const std::vector<float>& segments);

    /// Split each D-H···A contact into dashes of `dashLength` Å separated by
    /// equal gaps, appending an interleaved pos+color stream. Static so the
    /// geometry can be built without a current GL context.
    static void buildHydrogenBondDashes(
        const std::vector<std::pair<QVector3D, QVector3D>>& contacts,
        const QColor& color, float dashLength, std::vector<float>& out);

    Style& style() { return style_; }
    const Style& style() const { return style_; }

    /// 1..kMaxLights directional lights (extra entries are ignored).
    std::vector<Light>& lights() { return lights_; }
    const std::vector<Light>& lights() const { return lights_; }

private:
    struct InstancedMesh {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vertexBuffer{QOpenGLBuffer::VertexBuffer};
        QOpenGLBuffer indexBuffer{QOpenGLBuffer::IndexBuffer};
        QOpenGLBuffer instanceBuffer{QOpenGLBuffer::VertexBuffer};
        int indexCount = 0;
        int instanceCount = 0;
    };

    struct ColoredVertexBuffer { // pos(3) + color(3) per vertex
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        int vertexCount = 0;
    };

    /// Color of atom `index`: the scalar mapping when active, otherwise
    /// the (possibly overridden) element color.
    QColor resolvedAtomColor(std::size_t index, int atomicNumber) const;

    void createMesh(InstancedMesh& mesh,
                    const std::vector<float>& vertices,
                    const std::vector<unsigned int>& indices);
    /// Coordination-polyhedra geometry (Polyhedral mode): translucent hull
    /// faces (pos+color triangles) and solid hull edges (pos+color lines) for
    /// every atom with >= 4 bonded neighbors. Emits into the two vertex vectors.
    void buildPolyhedra(const core::Structure* structure,
                        const std::set<int>* selection,
                        std::vector<float>& faceVertices,
                        std::vector<float>& edgeVertices) const;
    void createColoredBuffer(ColoredVertexBuffer& buffer);
    void uploadColoredBuffer(ColoredVertexBuffer& buffer, const std::vector<float>& data);
    void uploadLights();

    /// Two-light studio default: warm key light + soft cool fill light.
    static std::vector<Light> defaultLights();

    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    bool initialized_ = false;
    Style style_;
    std::vector<Light> lights_ = defaultLights();
    std::vector<float> atomScalars_; ///< per-atom values for scalar coloring
    float scalarMin_ = 0.0f;
    float scalarMax_ = 1.0f;

    /// Fit an orthographic light frustum around the current scene and return
    /// the world -> light-clip matrix used by both the depth pass and the
    /// lookup in mesh.frag.
    QMatrix4x4 lightSpaceMatrix() const;
    /// Lazily create the depth FBO + texture; returns false if unavailable.
    bool ensureShadowTarget();
    /// Depth-only pass over every instanced mesh from the light's viewpoint.
    void renderShadowMap(const QMatrix4x4& lightSpace);

    QOpenGLShaderProgram meshProgram_;
    QOpenGLShaderProgram shadowProgram_; ///< depth-only, light's-eye pass
    QOpenGLShaderProgram lineProgram_; ///< uniform-color lines (unit cell)
    QOpenGLShaderProgram wireProgram_; ///< per-vertex-color lines/points

    InstancedMesh sphere_;
    InstancedMesh cylinder_;
    InstancedMesh cone_;     ///< arrowheads of force/velocity vectors
    InstancedMesh cellTube_; ///< thick cell wireframe (cellLineWidth > 1)
    ColoredVertexBuffer wireBonds_;  ///< GL_LINES
    ColoredVertexBuffer wireAtoms_;  ///< GL_POINTS (isolated atoms visible)
    ColoredVertexBuffer polyhedronFaces_; ///< GL_TRIANGLES (translucent)
    ColoredVertexBuffer polyhedronEdges_; ///< GL_LINES (opaque outline)
    ColoredVertexBuffer latticePlaneFaces_; ///< GL_TRIANGLES (translucent slice)
    ColoredVertexBuffer latticePlaneEdges_; ///< GL_LINES (plane border)
    float latticePlaneAlpha_ = 0.4f;
    bool latticePlaneVisible_ = false;
    bool latticePlaneEdgesOn_ = true;
    ColoredVertexBuffer customOverlayFaces_; ///< GL_TRIANGLES (custom primitives)
    ColoredVertexBuffer customOverlayEdges_; ///< GL_LINES (primitive wireframes)
    std::vector<OverlayRange> customOverlayRanges_;
    bool customOverlayVisible_ = false;
    /// Hydrogen bonds, GL_LINES. The dash pattern is BAKED INTO THE GEOMETRY
    /// (many short segments) rather than drawn with line stipple: core-profile
    /// GL removed glLineStipple, so this is the portable way to get a dashed
    /// line — and it keeps the dashes a fixed length in Å, so they do not
    /// stretch or crowd as the camera zooms.
    ColoredVertexBuffer hydrogenBonds_;

    // -- Shadow map --------------------------------------------------------
    /// 2048² is the sweet spot here: structures are compact, so the fitted
    /// light frustum is small and this resolves individual atoms cleanly
    /// without the memory of a 4k map.
    static constexpr int kShadowMapSize = 2048;
    unsigned shadowFbo_ = 0;
    /// Per-frame shadow state, produced by render() and consumed by
    /// uploadLights() (which runs once per mesh-program pass).
    bool shadowsActive_ = false;
    QMatrix4x4 lightSpace_;
    unsigned shadowTexture_ = 0;
    /// Scene bounds in world space, refreshed on every setStructure(); the
    /// light frustum is fitted to this sphere so the map's depth precision
    /// tracks the actual model rather than a fixed guess.
    QVector3D sceneCenter_;
    float sceneRadius_ = 1.0f;

    QOpenGLVertexArrayObject cellVao_;
    QOpenGLBuffer cellVbo_{QOpenGLBuffer::VertexBuffer};
    int cellVertexCount_ = 0;
};

} // namespace calango::render
