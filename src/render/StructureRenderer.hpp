#pragma once

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

enum class RepresentationMode {
    BallAndStick,
    SpaceFilling, ///< CPK: van-der-Waals-sized spheres, no bonds
    Wireframe,    ///< bonds as colored lines, isolated atoms as points
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
        float bondRadius = 0.06f;     ///< Å, base radius of a single bond
        float bondTolerance = 1.15f;  ///< bond-detection cutoff factor
        bool autoBonds = true;        ///< distance-based bond perception on/off
                                      ///< (manual bond overrides always render)
        /// Smooth axial color gradient across each bond (atom A color at one
        /// end blending to atom B color at the other); off = classic
        /// half-and-half coloring.
        bool gradientBonds = true;
        /// Per-atom vector overlays drawn as 3D arrows from each atom
        /// center (mesh representations only). Data comes from the
        /// structure's vector fields "forces" / "velocities".
        bool showForces = false;
        bool showVelocities = false;
        float vectorScale = 1.0f; ///< Å of arrow length per field unit
        QColor forceColor{242, 92, 54};
        QColor velocityColor{54, 166, 242};
        bool showCell = true;
        QColor cellColor{166, 166, 178};
        /// 1 = plain GL lines; > 1 renders the edges as thin lit tubes
        /// (core-profile GL clamps glLineWidth, so tubes are the portable
        /// way to get thick cell wireframes).
        float cellLineWidth = 1.0f;
        std::map<int, QColor> colorOverrides;      ///< Z -> user color
        std::map<int, float> radiusScaleOverrides; ///< Z -> per-element radius factor
        /// Scalar color mapping: Element uses the CPK palette; the other
        /// modes color atoms (and their bond halves) by the per-atom
        /// scalars passed to setAtomScalars(), sampled along `gradient`.
        ColorMode colorMode = ColorMode::Element;
        ColorGradient gradient = ColorGradient::Viridis;
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

    /// Per-atom scalars driving the non-Element color modes (CN, GCN,
    /// custom fields). Values are normalized to the [min, max] range
    /// internally; an empty vector (or a size mismatch with the current
    /// structure) falls back to element colors. Call setStructure()
    /// afterwards to rebuild the instance colors.
    void setAtomScalars(std::vector<float> scalars);

    void render(const QMatrix4x4& view, const QMatrix4x4& projection);

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

    QOpenGLShaderProgram meshProgram_;
    QOpenGLShaderProgram lineProgram_; ///< uniform-color lines (unit cell)
    QOpenGLShaderProgram wireProgram_; ///< per-vertex-color lines/points

    InstancedMesh sphere_;
    InstancedMesh cylinder_;
    InstancedMesh cone_;     ///< arrowheads of force/velocity vectors
    InstancedMesh cellTube_; ///< thick cell wireframe (cellLineWidth > 1)
    ColoredVertexBuffer wireBonds_;  ///< GL_LINES
    ColoredVertexBuffer wireAtoms_;  ///< GL_POINTS (isolated atoms visible)

    QOpenGLVertexArrayObject cellVao_;
    QOpenGLBuffer cellVbo_{QOpenGLBuffer::VertexBuffer};
    int cellVertexCount_ = 0;
};

} // namespace calango::render
