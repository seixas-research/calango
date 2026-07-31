#include "render/StructureRenderer.hpp"

#include "render/ShaderProfile.hpp"
#include "render/RenderGeometry.hpp"

#include "core/BrillouinZone.hpp"
#include "core/MarchingCubes.hpp"
#include "core/PeriodicImages.hpp"
#include "core/Structure.hpp"

#include <QOpenGLFunctions_3_3_Core>
#include <QQuaternion>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace calango::render {

const char* vectorFieldName(VectorOverlay overlay)
{
    switch (overlay) {
    case VectorOverlay::Velocity: return "velocities";
    case VectorOverlay::Force: return "forces";
    case VectorOverlay::MagneticMoment: return "magmoms";
    case VectorOverlay::InitialMagneticMoment: return "initial_magmoms";
    case VectorOverlay::None: break;
    }
    return "";
}

namespace {

// mat4 (16) + rgba (4) + rgba2 (4) + surface finish (1).
//
// The finish is per-INSTANCE rather than a uniform because it is a per-cast
// setting: one scene can hold a matte substrate and a glassy adsorbate, and a
// single uniform could only describe one of them. Per-cast OPACITY rides in
// the existing alpha channel of the two colours rather than costing a 26th
// float — the shader already reads vColor.a.
constexpr int kFloatsPerInstance = 25;

/// Licorice tube radius as a multiple of Style::bondRadius. ~2.5x a default
/// single bond: thick enough to read as tubing at a glance, thin enough that a
/// benzene ring's inner opening stays open.
constexpr float kLicoriceRadius = 2.5f;

/// Repeat `data` once per entry of `shifts`, translating each copy.
///
/// Works on any of the renderer's float streams: `stride` is the record length
/// and `offset` the index of the record's x component — 12 for an instance
/// (the translation column of the column-major model matrix), 0 for a vertex.
/// The whole stream is copied once and re-emitted per shift, which is what
/// makes replication independent of HOW a record was built: an atom, a bond,
/// an arrowhead and a ribbon segment all travel by the same lattice vector,
/// and none of the builders above needs to know this exists.
void replicateStream(std::vector<float>& data, int stride, int offset,
                     const std::vector<QVector3D>& shifts)
{
    if (data.empty() || shifts.empty())
        return;
    // Nothing to do for the identity-only case, which is the default and must
    // not pay for a copy of every buffer on every rebuild.
    if (shifts.size() == 1 && shifts.front().isNull())
        return;
    const std::vector<float> base = data;
    data.clear();
    data.reserve(base.size() * shifts.size());
    const auto span = static_cast<std::size_t>(stride);
    for (const QVector3D& shift : shifts) {
        const std::size_t start = data.size();
        data.insert(data.end(), base.begin(), base.end());
        if (shift.isNull())
            continue;
        for (std::size_t r = start; r + span <= data.size(); r += span) {
            data[r + static_cast<std::size_t>(offset) + 0] += shift.x();
            data[r + static_cast<std::size_t>(offset) + 1] += shift.y();
            data[r + static_cast<std::size_t>(offset) + 2] += shift.z();
        }
    }
}

/// One instance record. `color` is sampled at the mesh's z = 0 end and
/// `color2` at z = 1 (mesh.vert interpolates axially — the bond gradient);
/// pass the same color twice for uniform meshes (spheres, cell tubes).
void appendInstance(std::vector<float>& data, const QMatrix4x4& model,
                    const QColor& color, const QColor& color2,
                    SurfaceFinish finish, float opacity = 1.0f)
{
    const float alpha = std::clamp(opacity, 0.0f, 1.0f);
    const float* m = model.constData(); // column-major, matching the shader
    data.insert(data.end(), m, m + 16);
    data.insert(data.end(),
                {static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
                 static_cast<float>(color.blueF()), alpha});
    data.insert(data.end(),
                {static_cast<float>(color2.redF()), static_cast<float>(color2.greenF()),
                 static_cast<float>(color2.blueF()), alpha});
    data.push_back(static_cast<float>(finish));
}

void appendInstance(std::vector<float>& data, const QMatrix4x4& model,
                    const QColor& color, SurfaceFinish finish,
                    float opacity = 1.0f)
{
    appendInstance(data, model, color, color, finish, opacity);
}

/// Distinct colour per chain identifier, for the ribbon representation.
///
/// A qualitative palette, not a gradient: chains are nominal labels with no
/// order, so neighbouring letters must not get neighbouring hues — the whole
/// point is telling four chains of one complex apart at a glance.
QColor chainColor(const std::string& chain)
{
    static const QColor kPalette[] = {
        QColor(102, 170, 255), QColor(255, 153, 102), QColor(120, 210, 140),
        QColor(220, 130, 220), QColor(240, 210, 100), QColor(120, 220, 220),
        QColor(230, 120, 150), QColor(170, 170, 190),
    };
    constexpr int kCount = static_cast<int>(std::size(kPalette));
    if (chain.empty())
        return kPalette[0];
    // Sum the characters so multi-letter chain ids ("AA", "BB") also spread.
    int hash = 0;
    for (const char c : chain)
        hash += static_cast<unsigned char>(c);
    return kPalette[hash % kCount];
}

QColor midpointColor(const QColor& a, const QColor& b)
{
    return QColor::fromRgbF(0.5f * static_cast<float>(a.redF() + b.redF()),
                            0.5f * static_cast<float>(a.greenF() + b.greenF()),
                            0.5f * static_cast<float>(a.blueF() + b.blueF()));
}

/// Selection highlight (tint toward orange). Applied to the sphere AND to
/// every bond endpoint touching the selected atom, so bond colors always
/// match the adjacent sphere exactly — selected or not.
QColor selectionTint(const QColor& color)
{
    return QColor::fromRgbF(
        0.45f * static_cast<float>(color.redF()) + 0.55f,
        0.45f * static_cast<float>(color.greenF()) + 0.55f * 0.62f,
        0.45f * static_cast<float>(color.blueF()) + 0.55f * 0.10f);
}

void appendColoredVertex(std::vector<float>& data, const QVector3D& pos, const QColor& color)
{
    data.insert(data.end(),
                {pos.x(), pos.y(), pos.z(), static_cast<float>(color.redF()),
                 static_cast<float>(color.greenF()), static_cast<float>(color.blueF())});
}

/// Convex hull of a small point set (coordination neighbors). Returns the hull
/// faces as fan-triangulated convex polygons (`tris`, index triples into
/// `pts`) and the unique hull boundary edges (`edges`). Robust and simple for
/// the small N (≤ ~14) of a coordination shell: enumerate candidate face
/// planes from point triples, keep those with every other point on one side,
/// then group co-planar points into one polygon each. Empty output if the
/// points are (near-)coplanar or degenerate (no enclosing volume).
void convexHull(const std::vector<QVector3D>& pts,
                std::vector<std::array<int, 3>>& tris,
                std::vector<std::pair<int, int>>& edges)
{
    const int n = static_cast<int>(pts.size());
    if (n < 4)
        return;
    QVector3D centroid;
    for (const QVector3D& p : pts)
        centroid += p;
    centroid /= static_cast<float>(n);

    struct Plane {
        QVector3D normal; // outward
        float d;          // plane: dot(normal, x) = d
    };
    std::vector<Plane> planes;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            for (int k = j + 1; k < n; ++k) {
                QVector3D nrm =
                    QVector3D::crossProduct(pts[j] - pts[i], pts[k] - pts[i]);
                const float len = nrm.length();
                if (len < 1e-5f)
                    continue; // collinear triple
                nrm /= len;
                float d = QVector3D::dotProduct(nrm, pts[i]);
                bool pos = false, neg = false;
                for (int m = 0; m < n; ++m) {
                    if (m == i || m == j || m == k)
                        continue;
                    const float s = QVector3D::dotProduct(nrm, pts[m]) - d;
                    if (s > 1e-3f)
                        pos = true;
                    else if (s < -1e-3f)
                        neg = true;
                }
                if (pos && neg)
                    continue; // interior plane, not on the hull
                if (pos) {    // flip so the interior sits on the negative side
                    nrm = -nrm;
                    d = -d;
                }
                bool dup = false;
                for (const Plane& p : planes) {
                    if (QVector3D::dotProduct(p.normal, nrm) > 0.999f
                        && std::abs(p.d - d) < 1e-2f) {
                        dup = true;
                        break;
                    }
                }
                if (!dup)
                    planes.push_back({nrm, d});
            }
        }
    }

    for (const Plane& plane : planes) {
        // Gather every point lying on this face plane.
        std::vector<int> face;
        for (int m = 0; m < n; ++m) {
            if (std::abs(QVector3D::dotProduct(plane.normal, pts[m]) - plane.d)
                < 1e-2f)
                face.push_back(m);
        }
        if (face.size() < 3)
            continue;
        // Order the face vertices CCW around their centroid in the plane.
        QVector3D fc;
        for (int idx : face)
            fc += pts[idx];
        fc /= static_cast<float>(face.size());
        const QVector3D u = (pts[face[0]] - fc).normalized();
        const QVector3D v = QVector3D::crossProduct(plane.normal, u);
        std::sort(face.begin(), face.end(), [&](int a, int b) {
            const QVector3D da = pts[a] - fc;
            const QVector3D db = pts[b] - fc;
            return std::atan2(QVector3D::dotProduct(da, v),
                              QVector3D::dotProduct(da, u))
                 < std::atan2(QVector3D::dotProduct(db, v),
                              QVector3D::dotProduct(db, u));
        });
        // Fan-triangulate the convex polygon and record its boundary edges.
        for (std::size_t t = 1; t + 1 < face.size(); ++t)
            tris.push_back({face[0], face[t], face[t + 1]});
        for (std::size_t e = 0; e < face.size(); ++e) {
            int a = face[e];
            int b = face[(e + 1) % face.size()];
            if (a > b)
                std::swap(a, b);
            if (std::find(edges.begin(), edges.end(), std::pair{a, b})
                == edges.end())
                edges.emplace_back(a, b);
        }
    }
}

/// Interleaved position+normal unit sphere (radius 1, centered at origin).
void buildSphere(int stacks, int slices,
                 std::vector<float>& vertices, std::vector<unsigned int>& indices)
{
    for (int i = 0; i <= stacks; ++i) {
        const float phi = float(M_PI) * float(i) / float(stacks);
        for (int j = 0; j <= slices; ++j) {
            const float theta = 2.0f * float(M_PI) * float(j) / float(slices);
            const float x = std::sin(phi) * std::cos(theta);
            const float y = std::sin(phi) * std::sin(theta);
            const float z = std::cos(phi);
            vertices.insert(vertices.end(), {x, y, z, x, y, z});
        }
    }
    const auto cols = static_cast<unsigned int>(slices + 1);
    for (unsigned int i = 0; i < static_cast<unsigned int>(stacks); ++i) {
        for (unsigned int j = 0; j < static_cast<unsigned int>(slices); ++j) {
            const unsigned int a = i * cols + j;
            const unsigned int b = a + cols;
            indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }
}

/// Open-ended unit cylinder: radius 1, axis +z, spanning z in [0, 1].
/// End caps are unnecessary — sphere instances cover the joints. Each
/// vertex carries its radial surface normal, so bonds receive the same
/// multi-light Blinn-Phong shading as the atom spheres (shared program).
///
/// Winding matters even without face culling: triangles must be CCW as
/// seen from OUTSIDE, or the fragment shader's two-sided rule
/// (gl_FrontFacing) flips the outward normals inward and bonds collapse
/// to ambient-only shading — the long-standing "bonds darker than atoms"
/// bug captured in assets/bond_test.png.
void buildCylinder(int segments,
                   std::vector<float>& vertices, std::vector<unsigned int>& indices)
{
    for (int j = 0; j <= segments; ++j) {
        const float theta = 2.0f * float(M_PI) * float(j) / float(segments);
        const float x = std::cos(theta);
        const float y = std::sin(theta);
        vertices.insert(vertices.end(), {x, y, 0.0f, x, y, 0.0f});
        vertices.insert(vertices.end(), {x, y, 1.0f, x, y, 0.0f});
    }
    for (unsigned int j = 0; j < static_cast<unsigned int>(segments); ++j) {
        const unsigned int a = j * 2;
        // CCW from outside: (ring j, z0) -> (ring j+1, z0) -> (ring j, z1).
        indices.insert(indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
    }
}

/// Unit cone (arrowhead): base circle radius 1 at z = 0, apex at z = 1,
/// closed base cap. Side normals are the exact cone-surface normals
/// (radial + axial)/√2, lit by the same shared program.
void buildCone(int segments,
               std::vector<float>& vertices, std::vector<unsigned int>& indices)
{
    const float inv = 1.0f / std::sqrt(2.0f);
    // Side: base ring + one apex vertex per segment (for correct normals).
    for (int j = 0; j <= segments; ++j) {
        const float theta = 2.0f * float(M_PI) * float(j) / float(segments);
        const float x = std::cos(theta);
        const float y = std::sin(theta);
        vertices.insert(vertices.end(), {x, y, 0.0f, x * inv, y * inv, inv});
        vertices.insert(vertices.end(), {0.0f, 0.0f, 1.0f, x * inv, y * inv, inv});
    }
    for (unsigned int j = 0; j < static_cast<unsigned int>(segments); ++j) {
        const unsigned int a = j * 2;
        indices.insert(indices.end(), {a, a + 2, a + 1});
    }
    // Base cap (facing -z); wound CCW as seen from below (-z), matching
    // the -z normals so gl_FrontFacing agrees with the attribute normal.
    const auto capCenter = static_cast<unsigned int>(vertices.size() / 6);
    vertices.insert(vertices.end(), {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f});
    for (int j = 0; j <= segments; ++j) {
        const float theta = 2.0f * float(M_PI) * float(j) / float(segments);
        vertices.insert(vertices.end(),
                        {std::cos(theta), std::sin(theta), 0.0f, 0.0f, 0.0f, -1.0f});
    }
    for (unsigned int j = 0; j < static_cast<unsigned int>(segments); ++j)
        indices.insert(indices.end(),
                       {capCenter, capCenter + 2 + j, capCenter + 1 + j});
}

QMatrix4x4 bondTransform(const QVector3D& from, const QVector3D& direction,
                         float length, float radius)
{
    QMatrix4x4 m;
    m.translate(from);
    m.rotate(QQuaternion::rotationTo(QVector3D(0.0f, 0.0f, 1.0f), direction));
    m.scale(radius, radius, length);
    return m;
}



} // namespace

std::vector<Light> StructureRenderer::defaultLights()
{
    Light key; // slightly warm, from upper left, carries the speculars
    key.direction = QVector3D(-0.4f, -0.5f, -1.0f);
    key.ambient = QColor::fromRgbF(0.24f, 0.24f, 0.23f);
    key.diffuse = QColor::fromRgbF(0.66f, 0.64f, 0.60f);
    key.specular = QColor::fromRgbF(0.30f, 0.30f, 0.30f);

    Light fill; // softer, cooler, from the opposite side, no ambient
    fill.direction = QVector3D(0.7f, 0.25f, -0.55f);
    fill.ambient = QColor::fromRgbF(0.0f, 0.0f, 0.0f);
    fill.diffuse = QColor::fromRgbF(0.24f, 0.27f, 0.33f);
    fill.specular = QColor::fromRgbF(0.05f, 0.05f, 0.07f);

    Light back; // rim light from behind/above: separates spheres from the
                // background and adds depth to sphere silhouettes
    back.direction = QVector3D(0.15f, 0.45f, 0.90f);
    back.ambient = QColor::fromRgbF(0.0f, 0.0f, 0.0f);
    back.diffuse = QColor::fromRgbF(0.14f, 0.14f, 0.16f);
    back.specular = QColor::fromRgbF(0.10f, 0.10f, 0.12f);

    return {key, fill, back};
}

float StructureRenderer::displayRadius(int atomicNumber, const Style& style)
{
    const CastStyle cast = style.castStyle(0);
    const float base = displayRadius(atomicNumber, cast);
    // The per-element override lives on the scene-wide style, not on the cast:
    // "make every sulfur 20% bigger" is a statement about sulfur, not about a
    // group of atoms.
    float perElement = 1.0f;
    if (const auto it = style.radiusScaleOverrides.find(atomicNumber);
        it != style.radiusScaleOverrides.end())
        perElement = it->second;
    return base * perElement;
}

float StructureRenderer::displayRadius(int atomicNumber, const CastStyle& cast)
{
    const float covalent = core::Elements::data(atomicNumber).covalentRadius;
    float radius = 0.25f;
    switch (cast.mode) {
    case RepresentationMode::BallAndStick:
        radius = std::max(0.2f, covalent * 0.4f);
        break;
    case RepresentationMode::SpaceFilling:
        // vdW radius approximated as r_cov + 0.8 Å (good to ~0.1 Å for
        // main-group elements; replace with a Bondi/Alvarez table later).
        radius = covalent + 0.8f;
        break;
    case RepresentationMode::Wireframe:
        radius = 0.25f; // used by picking only
        break;
    case RepresentationMode::Polyhedral:
        // Compact nodes so the coordination polyhedra read as the primary
        // shape, with atoms still visible at the vertices/centers.
        radius = std::max(0.18f, covalent * 0.3f);
        break;
    case RepresentationMode::Licorice:
        // Matches the drawn tube (Style::bondRadius default x kLicoriceRadius)
        // so a click lands where the tube looks, not beside it.
        radius = 0.078f * kLicoriceRadius;
        break;
    case RepresentationMode::Ribbon:
    case RepresentationMode::MolecularSurface:
        // No per-atom sphere is drawn in either; the value is what picking
        // uses, and it stays generous enough that a click still selects the
        // atom under the ribbon or the surface.
        radius = std::max(0.3f, covalent * 0.5f);
        break;
    }
    return radius * cast.atomScaleFactor;
}

std::vector<StructureRenderer::CastStyle> StructureRenderer::atomCastStyles(
    const core::Structure* structure, const Style& style)
{
    const std::size_t count = structure ? structure->size() : 0;
    const CastStyle base = style.castStyle(0);
    // A cast assignment that does not match the atom count belongs to a
    // structure that has since been replaced. Falling back to the uniform
    // cast-0 style is the only safe reading — the alternative is drawing atoms
    // in the casts of whatever geometry was loaded before.
    if (style.atomCasts.size() != count)
        return std::vector<CastStyle>(count, base);
    // Resolve once per CAST, not once per atom: a 15 000-atom protein in two
    // casts would otherwise copy the same struct 15 000 times.
    std::vector<CastStyle> byCast;
    byCast.reserve(static_cast<std::size_t>(style.castCount()));
    for (int cast = 0; cast < style.castCount(); ++cast)
        byCast.push_back(style.castStyle(cast));

    std::vector<CastStyle> perAtom(count, base);
    for (std::size_t i = 0; i < count; ++i) {
        const int cast = style.atomCasts[i];
        perAtom[i] = (cast >= 0 && cast < static_cast<int>(byCast.size()))
            ? byCast[static_cast<std::size_t>(cast)]
            : base;
    }
    return perAtom;
}

QColor StructureRenderer::atomColor(int atomicNumber, const Style& style)
{
    if (const auto it = style.colorOverrides.find(atomicNumber);
        it != style.colorOverrides.end())
        return it->second;
    const auto& element = core::Elements::data(atomicNumber);
    return QColor(element.rgb[0], element.rgb[1], element.rgb[2]);
}

void StructureRenderer::setAtomScalars(ColorMode mode, std::vector<float> values)
{
    if (mode == ColorMode::Element)
        return; // element colours need no field
    ScalarField field;
    field.values = std::move(values);
    if (!field.values.empty()) {
        const auto [lo, hi] =
            std::minmax_element(field.values.begin(), field.values.end());
        field.min = *lo;
        field.max = *hi;
    }
    scalars_[mode] = std::move(field);
}

void StructureRenderer::clearAtomScalars()
{
    scalars_.clear();
}

const std::vector<float>* StructureRenderer::atomScalars(ColorMode mode) const
{
    const auto it = scalars_.find(mode);
    if (mode == ColorMode::Element || it == scalars_.end()
        || it->second.values.empty())
        return nullptr;
    return &it->second.values;
}

StructureRenderer::ScalarRange StructureRenderer::scalarRangeFor(
    ColorMode mode) const
{
    const auto it = scalars_.find(mode);
    if (mode == ColorMode::Element || it == scalars_.end()
        || it->second.values.empty())
        return {};
    return {true, it->second.min, it->second.max};
}

QColor StructureRenderer::resolvedAtomColor(std::size_t index, int atomicNumber,
                                            ColorMode colorMode) const
{
    if (colorMode == ColorMode::Element)
        return atomColor(atomicNumber, style_);
    const auto it = scalars_.find(colorMode);
    if (it == scalars_.end() || index >= it->second.values.size())
        return atomColor(atomicNumber, style_); // no data for this mode
    const ScalarField& field = it->second;
    // User-pinned bounds win over the data's own range; values outside the
    // window clamp to the ramp ends rather than wrapping.
    const bool custom = style_.useCustomScalarRange;
    const float lo = custom ? style_.customScalarMin : field.min;
    const float hi = custom ? style_.customScalarMax : field.max;
    // A flat field (all atoms identical) maps to the middle of the gradient.
    const float range = hi - lo;
    const float t = range > 1e-12f
        ? std::clamp((field.values[index] - lo) / range, 0.0f, 1.0f)
        : 0.5f;
    return ColorMap::sample(style_.gradient, t, style_.invertGradient);
}

void StructureRenderer::initialize(QOpenGLFunctions_3_3_Core* gl)
{
    gl_ = gl;

    meshProgram_.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/assets/shaders/mesh.vert");
    meshProgram_.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/assets/shaders/mesh.frag");
    meshProgram_.link();


    shadowProgram_.addShaderFromSourceFile(QOpenGLShader::Vertex,
                                           ":/assets/shaders/shadow.vert");
    shadowProgram_.addShaderFromSourceFile(QOpenGLShader::Fragment,
                                           ":/assets/shaders/shadow.frag");
    shadowProgram_.link();

    wireProgram_.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/assets/shaders/wire.vert");
    wireProgram_.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/assets/shaders/wire.frag");
    wireProgram_.link();

    gl_->glEnable(GL_PROGRAM_POINT_SIZE); // wire.vert sets gl_PointSize

    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    buildSphere(20, 30, vertices, indices);
    createMesh(sphere_, vertices, indices);

    vertices.clear();
    indices.clear();
    buildCylinder(24, vertices, indices);
    createMesh(cylinder_, vertices, indices);
    createMesh(cellTube_, vertices, indices); // same geometry, own instances

    vertices.clear();
    indices.clear();
    buildCone(20, vertices, indices);
    createMesh(cone_, vertices, indices);

    // Impostor quads over the same instance buffers. Built unconditionally:
    // they are 4 vertices each, and having them ready means switching profiles
    // in Preferences never has to touch GL resources mid-frame.
    createImpostorQuad(sphere_);
    createImpostorQuad(cylinder_);

    createColoredBuffer(wireBonds_);
    createColoredBuffer(wireAtoms_);
    createColoredBuffer(polyhedronFaces_);
    createColoredBuffer(polyhedronEdges_);
    createColoredBuffer(molecularSurface_);
    createColoredBuffer(latticePlaneFaces_);
    createColoredBuffer(latticePlaneEdges_);
    createLitBuffer(customOverlayFaces_);
    createColoredBuffer(customOverlayEdges_);
    createColoredBuffer(managedOverlayFaces_);
    createColoredBuffer(managedOverlayEdges_);
    createColoredBuffer(hydrogenBonds_);
    createLitBuffer(cellFaces_);

    // Unit 0 must never be left empty while the mesh program is drawing —
    // see dummyTexture_. Created once, for the life of the context.
    {
        const unsigned char white[4] = {255, 255, 255, 255};
        gl_->glGenTextures(1, &dummyTexture_);
        gl_->glBindTexture(GL_TEXTURE_2D, dummyTexture_);
        gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
                          GL_UNSIGNED_BYTE, white);
        // A complete texture needs a non-mipmapped min filter; the default
        // GL_NEAREST_MIPMAP_LINEAR would leave it incomplete and the warning
        // would stand.
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_->glBindTexture(GL_TEXTURE_2D, 0);
    }

    initialized_ = true;
}

void StructureRenderer::createMesh(InstancedMesh& mesh,
                                   const std::vector<float>& vertices,
                                   const std::vector<unsigned int>& indices)
{
    mesh.indexCount = static_cast<int>(indices.size());

    mesh.vao.create();
    mesh.vao.bind();

    mesh.vertexBuffer.create();
    mesh.vertexBuffer.bind();
    mesh.vertexBuffer.allocate(vertices.data(),
                               static_cast<int>(vertices.size() * sizeof(float)));
    gl_->glEnableVertexAttribArray(0); // position
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    gl_->glEnableVertexAttribArray(1); // normal
    gl_->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                               reinterpret_cast<void*>(3 * sizeof(float)));

    mesh.indexBuffer.create();
    mesh.indexBuffer.bind();
    mesh.indexBuffer.allocate(indices.data(),
                              static_cast<int>(indices.size() * sizeof(unsigned int)));

    // Per-instance data: locations 2..5 = mat4 columns, 6 = rgba color at
    // z = 0, 7 = rgba color at z = 1 (axial bond gradient).
    mesh.instanceBuffer.create();
    mesh.instanceBuffer.bind();
    const auto stride = kFloatsPerInstance * static_cast<int>(sizeof(float));
    for (int column = 0; column < 4; ++column) {
        const auto location = static_cast<GLuint>(2 + column);
        gl_->glEnableVertexAttribArray(location);
        gl_->glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, stride,
                                   reinterpret_cast<void*>(sizeof(float) * 4 * static_cast<std::size_t>(column)));
        gl_->glVertexAttribDivisor(location, 1);
    }
    for (int slot = 0; slot < 2; ++slot) {
        const auto location = static_cast<GLuint>(6 + slot);
        gl_->glEnableVertexAttribArray(location);
        gl_->glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, stride,
                                   reinterpret_cast<void*>(sizeof(float) * (16 + 4 * static_cast<std::size_t>(slot))));
        gl_->glVertexAttribDivisor(location, 1);
    }
    // Location 8: the instance's surface finish, as a float the fragment
    // shader rounds back to an int (there is no integer attribute path worth
    // adding for a single value).
    gl_->glEnableVertexAttribArray(8);
    gl_->glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, stride,
                               reinterpret_cast<void*>(sizeof(float) * 24));
    gl_->glVertexAttribDivisor(8, 1);

    mesh.vao.release();
}

void StructureRenderer::createImpostorQuad(InstancedMesh& mesh)
{
    // A unit quad in the xy-plane. The impostor vertex shaders read aPos.xy as
    // corner offsets in [-1, 1] and expand it in view space, so the quad's own
    // orientation never matters — only its corners.
    static const float kQuad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
    static const unsigned int kIndices[] = {0, 1, 2, 0, 2, 3};
    mesh.quadIndexCount = 6;

    mesh.impostorVao.create();
    mesh.impostorVao.bind();

    mesh.quadVertexBuffer.create();
    mesh.quadVertexBuffer.bind();
    mesh.quadVertexBuffer.allocate(kQuad, sizeof(kQuad));
    gl_->glEnableVertexAttribArray(0);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                               nullptr);
    gl_->glEnableVertexAttribArray(1);
    gl_->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                               reinterpret_cast<void*>(3 * sizeof(float)));

    mesh.quadIndexBuffer.create();
    mesh.quadIndexBuffer.bind();
    mesh.quadIndexBuffer.allocate(kIndices, sizeof(kIndices));

    // The SAME instance buffer the tessellated VAO uses — bound again here
    // with the identical attribute layout, so nothing has to be uploaded twice
    // and the two paths can never disagree about what they are drawing.
    mesh.instanceBuffer.bind();
    const auto stride = kFloatsPerInstance * static_cast<int>(sizeof(float));
    for (int column = 0; column < 4; ++column) {
        const auto location = static_cast<GLuint>(2 + column);
        gl_->glEnableVertexAttribArray(location);
        gl_->glVertexAttribPointer(
            location, 4, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<void*>(sizeof(float) * 4
                                    * static_cast<std::size_t>(column)));
        gl_->glVertexAttribDivisor(location, 1);
    }
    for (int slot = 0; slot < 2; ++slot) {
        const auto location = static_cast<GLuint>(6 + slot);
        gl_->glEnableVertexAttribArray(location);
        gl_->glVertexAttribPointer(
            location, 4, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<void*>(sizeof(float)
                                    * (16 + 4 * static_cast<std::size_t>(slot))));
        gl_->glVertexAttribDivisor(location, 1);
    }
    gl_->glEnableVertexAttribArray(8);
    gl_->glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, stride,
                               reinterpret_cast<void*>(sizeof(float) * 24));
    gl_->glVertexAttribDivisor(8, 1);

    mesh.impostorVao.release();
}

bool StructureRenderer::useImpostors(int slot)
{
    const auto shaderSlot = slot == 0 ? ShaderSlot::Atoms : ShaderSlot::Bonds;
    // The PROFILE says whether it wants impostor geometry — not "is it
    // legacy". PBR and Toon are separate profiles that happen to share the
    // impostor path, and a future mesh-based profile must not be forced onto
    // it by an is-legacy test.
    if (!ShaderRegistry::activeProfile(shaderSlot).impostorGeometry)
        return false;
    if (impostorTried_[slot])
        return impostorReady_[slot];
    impostorTried_[slot] = true;
    const char* vert = slot == 0 ? ":/assets/shaders/impostor_sphere.vert"
                                 : ":/assets/shaders/impostor_cylinder.vert";
    const char* frag = slot == 0 ? ":/assets/shaders/impostor_sphere.frag"
                                 : ":/assets/shaders/impostor_cylinder.frag";
    impostorReady_[slot] =
        impostorProgram_[slot].addShaderFromSourceFile(QOpenGLShader::Vertex, vert)
        && impostorProgram_[slot].addShaderFromSourceFile(QOpenGLShader::Fragment,
                                                          frag)
        && impostorProgram_[slot].link();
    if (!impostorReady_[slot]) {
        qWarning("Calango: the %s impostor shader did not build on this "
                 "driver; falling back to tessellated geometry. Log:\n%s",
                 slot == 0 ? "sphere" : "cylinder",
                 qPrintable(impostorProgram_[slot].log()));
    }
    return impostorReady_[slot];
}

void StructureRenderer::uploadImpostorUniforms(QOpenGLShaderProgram& program,
                                               const QMatrix4x4& view,
                                               const QMatrix4x4& projection,
                                               const QMatrix4x4& lightSpace)
{
    program.setUniformValue("uView", view);
    program.setUniformValue("uProj", projection);
    program.setUniformValue("uLightSpace", lightSpace);
    // The impostor's surface does not exist until the ray is solved, so the
    // shadow lookup needs to get from the hit point back to world space —
    // which the tessellated path does in its vertex stage and cannot here.
    program.setUniformValue("uInvView", view.inverted());
    // 48.0 is the same literal the mesh path uses; keeping them in step is
    // what makes a Shiny atom look identical under either profile.
    program.setUniformValue("uShininess", 48.0f);
    program.setUniformValue("uSurfaceOpacity", style_.glassOpacity);

    const int count = std::min(static_cast<int>(lights_.size()), kMaxLights);
    program.setUniformValue("uLightCount", count);
    const auto rgb = [](const QColor& c) {
        return QVector3D(static_cast<float>(c.redF()),
                         static_cast<float>(c.greenF()),
                         static_cast<float>(c.blueF()));
    };
    for (int i = 0; i < count; ++i) {
        const Light& light = lights_[static_cast<std::size_t>(i)];
        const QString index = QStringLiteral("[%1]").arg(i);
        program.setUniformValue(qPrintable(QStringLiteral("uLightDir") + index),
                                light.direction.normalized());
        program.setUniformValue(
            qPrintable(QStringLiteral("uLightAmbient") + index),
            rgb(light.ambient));
        program.setUniformValue(
            qPrintable(QStringLiteral("uLightDiffuse") + index),
            rgb(light.diffuse));
        program.setUniformValue(
            qPrintable(QStringLiteral("uLightSpecular") + index),
            rgb(light.specular));
    }

    program.setUniformValue("uShadowEnabled", style_.shadowsEnabled ? 1 : 0);
    program.setUniformValue("uShadowStrength", style_.shadowStrength);
    program.setUniformValue("uShadowRadius", style_.shadowSoftness);
    program.setUniformValue("uShadowTexelSize",
                            1.0f / static_cast<float>(kShadowMapSize));
    program.setUniformValue("uShadowMap", 0);

    program.setUniformValue("uFogMode", static_cast<int>(style_.fogMode));
    program.setUniformValue("uFogColor",
                            QVector3D(static_cast<float>(style_.fogColor.redF()),
                                      static_cast<float>(style_.fogColor.greenF()),
                                      static_cast<float>(style_.fogColor.blueF())));
    program.setUniformValue("uFogStart", style_.fogStart);
    program.setUniformValue("uFogEnd", style_.fogEnd);
    program.setUniformValue("uFogDensity", style_.fogDensity);

    // Shading model + its material parameters. The model comes from the
    // PROFILE (which BRDF), the parameters from the style (how that BRDF is
    // configured) — so switching profiles never silently rewrites the user's
    // material settings.
    program.setUniformValue("uMetallic", style_.metallic);
    program.setUniformValue("uRoughness", style_.roughness);
    program.setUniformValue("uToonBands", style_.toonBands);
    program.setUniformValue("uToonRim", style_.toonRim);
}

void StructureRenderer::setVolumeField(int nx, int ny, int nz,
                                       const std::vector<float>& values,
                                       const std::vector<float>& transfer,
                                       const QMatrix4x4& boxTransform)
{
    if (!gl_ || nx < 2 || ny < 2 || nz < 2
        || values.size() != static_cast<std::size_t>(nx) * ny * nz) {
        clearVolumeField();
        return;
    }
    volumeTransform_ = boxTransform;

    if (volumeTexture_ == 0)
        gl_->glGenTextures(1, &volumeTexture_);
    gl_->glBindTexture(GL_TEXTURE_3D, volumeTexture_);
    // GL_R32F: a scalar field, at the precision it was computed in. A
    // normalized 8-bit format would band visibly wherever the transfer
    // function is steep, which is exactly where the interesting structure is.
    gl_->glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, nx, ny, nz, 0, GL_RED,
                      GL_FLOAT, values.data());
    gl_->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Clamp, not repeat: a ray leaving the box must not wrap around and
    // sample the far side, which would smear the cell's own edge across it.
    for (const GLenum axis : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T,
                              GL_TEXTURE_WRAP_R})
        gl_->glTexParameteri(GL_TEXTURE_3D, axis, GL_CLAMP_TO_EDGE);

    if (transferTexture_ == 0)
        gl_->glGenTextures(1, &transferTexture_);
    gl_->glBindTexture(GL_TEXTURE_1D, transferTexture_);
    gl_->glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8,
                      static_cast<GLsizei>(transfer.size() / 4), 0, GL_RGBA,
                      GL_FLOAT, transfer.data());
    gl_->glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->glBindTexture(GL_TEXTURE_1D, 0);
    gl_->glBindTexture(GL_TEXTURE_3D, 0);

    // The unit cube the ray is clipped against, built once.
    if (!volumeVao_.isCreated()) {
        static const float kCube[] = {
            0, 0, 0, 0, 0, 1, 0, 0, 0,   1, 0, 0, 0, 0, 1, 0, 0, 0,
            1, 1, 0, 0, 0, 1, 0, 0, 0,   0, 1, 0, 0, 0, 1, 0, 0, 0,
            0, 0, 1, 0, 0, 1, 0, 0, 0,   1, 0, 1, 0, 0, 1, 0, 0, 0,
            1, 1, 1, 0, 0, 1, 0, 0, 0,   0, 1, 1, 0, 0, 1, 0, 0, 0};
        static const unsigned int kIndices[] = {
            0, 1, 2, 0, 2, 3,  4, 6, 5, 4, 7, 6,
            0, 4, 5, 0, 5, 1,  3, 2, 6, 3, 6, 7,
            0, 3, 7, 0, 7, 4,  1, 5, 6, 1, 6, 2};
        volumeVao_.create();
        volumeVao_.bind();
        volumeVbo_.create();
        volumeVbo_.bind();
        volumeVbo_.allocate(kCube, sizeof(kCube));
        gl_->glEnableVertexAttribArray(0);
        gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                                   nullptr);
        volumeIbo_.create();
        volumeIbo_.bind();
        volumeIbo_.allocate(kIndices, sizeof(kIndices));
        volumeVao_.release();
    }
    volumeVisible_ = true;
}

void StructureRenderer::clearVolumeField()
{
    volumeVisible_ = false;
}

void StructureRenderer::setVolumeParams(int steps, float density,
                                        float isoLevel, bool lit)
{
    volumeSteps_ = steps;
    volumeDensity_ = density;
    volumeIsoLevel_ = isoLevel;
    volumeLit_ = lit;
}

void StructureRenderer::drawVolume(const QMatrix4x4& view,
                                   const QMatrix4x4& projection)
{
    if (!volumeVisible_ || volumeTexture_ == 0)
        return;
    if (!raymarchTried_) {
        raymarchTried_ = true;
        raymarchReady_ =
            raymarchProgram_.addShaderFromSourceFile(
                QOpenGLShader::Vertex, ":/assets/shaders/raymarch.vert")
            && raymarchProgram_.addShaderFromSourceFile(
                QOpenGLShader::Fragment, ":/assets/shaders/raymarch.frag")
            && raymarchProgram_.link();
        if (!raymarchReady_)
            qWarning("Calango: the volume ray-marching shader did not build on "
                     "this driver. Log:\n%s",
                     qPrintable(raymarchProgram_.log()));
    }
    if (!raymarchReady_)
        return;

    raymarchProgram_.bind();
    raymarchProgram_.setUniformValue("uView", view);
    raymarchProgram_.setUniformValue("uProj", projection);
    raymarchProgram_.setUniformValue("uModel", volumeTransform_);
    raymarchProgram_.setUniformValue("uInvModel", volumeTransform_.inverted());
    raymarchProgram_.setUniformValue("uInvView", view.inverted());
    // The eye in WORLD space: the ray is built in the cube's own space, and
    // getting there from the camera needs the world position, not the view one
    // (which is the origin by definition).
    const QVector3D eyeWorld = view.inverted().map(QVector3D(0.0f, 0.0f, 0.0f));
    raymarchProgram_.setUniformValue("uEyeWorld", eyeWorld);
    raymarchProgram_.setUniformValue("uSteps", volumeSteps_);
    raymarchProgram_.setUniformValue("uDensity", volumeDensity_);
    raymarchProgram_.setUniformValue("uIsoLevel", volumeIsoLevel_);
    raymarchProgram_.setUniformValue("uLightingOn", volumeLit_ ? 1 : 0);
    raymarchProgram_.setUniformValue("uVolume", 1);
    raymarchProgram_.setUniformValue("uTransfer", 2);

    gl_->glActiveTexture(GL_TEXTURE1);
    gl_->glBindTexture(GL_TEXTURE_3D, volumeTexture_);
    gl_->glActiveTexture(GL_TEXTURE2);
    gl_->glBindTexture(GL_TEXTURE_1D, transferTexture_);

    // Back faces, so the box still covers the screen when the camera is inside
    // the volume — the common case once a density is zoomed into. Depth writes
    // off: the volume is a participating medium, not a surface, and writing
    // depth would let it occlude atoms it should be blended over.
    gl_->glEnable(GL_CULL_FACE);
    gl_->glCullFace(GL_FRONT);
    gl_->glEnable(GL_BLEND);
    gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl_->glDepthMask(GL_FALSE);
    volumeVao_.bind();
    gl_->glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
    volumeVao_.release();
    gl_->glDepthMask(GL_TRUE);
    gl_->glDisable(GL_BLEND);
    gl_->glCullFace(GL_BACK);
    gl_->glDisable(GL_CULL_FACE);

    gl_->glActiveTexture(GL_TEXTURE0);
    raymarchProgram_.release();
}

bool StructureRenderer::useLitIsosurface()
{
    // Two separate questions, and they used to be one. "Should an ISOSURFACE
    // be lit" is a Preferences choice; "can this driver run the lit surface
    // program at all" is a hardware fact. The unit cell wants the second
    // without the first — it is lit unconditionally — so the link-and-guard
    // half lives in ensureLitSurfaceProgram() below.
    if (ShaderRegistry::activeProfile(ShaderSlot::Isosurfaces).isLegacy)
        return false;
    return ensureLitSurfaceProgram();
}

bool StructureRenderer::ensureLitSurfaceProgram()
{
    if (isosurfaceProgramTried_)
        return isosurfaceProgramReady_;
    isosurfaceProgramTried_ = true;
    // Linked lazily and guarded: a driver that rejects the program must fall
    // back to the path that always works rather than take the viewport with
    // it. Qt logs the compiler output, which is what makes a report from a
    // machine we do not have actionable.
    isosurfaceProgramReady_ =
        isosurfaceProgram_.addShaderFromSourceFile(
            QOpenGLShader::Vertex, ":/assets/shaders/isosurface.vert")
        && isosurfaceProgram_.addShaderFromSourceFile(
            QOpenGLShader::Fragment, ":/assets/shaders/isosurface.frag")
        && isosurfaceProgram_.link();
    if (!isosurfaceProgramReady_) {
        qWarning("Calango: the lit isosurface shader did not build on this "
                 "driver; falling back to the legacy unlit path. Log:\n%s",
                 qPrintable(isosurfaceProgram_.log()));
    }
    return isosurfaceProgramReady_;
}

void StructureRenderer::uploadIsosurfaceLights()
{
    // Deliberately the same uniform names and the same view-space direction
    // convention as mesh.frag: the two programs describe one scene, and a
    // second lighting convention here is exactly how the surface would drift
    // out of agreement with the atoms the first time the lights are edited.
    const int count = std::min(static_cast<int>(lights_.size()), kMaxLights);
    isosurfaceProgram_.setUniformValue("uLightCount", count);
    for (int i = 0; i < count; ++i) {
        const Light& light = lights_[static_cast<std::size_t>(i)];
        const QString index = QStringLiteral("[%1]").arg(i);
        isosurfaceProgram_.setUniformValue(
            qPrintable(QStringLiteral("uLightDir") + index),
            light.direction.normalized());
        const auto rgb = [](const QColor& c) {
            return QVector3D(static_cast<float>(c.redF()),
                             static_cast<float>(c.greenF()),
                             static_cast<float>(c.blueF()));
        };
        isosurfaceProgram_.setUniformValue(
            qPrintable(QStringLiteral("uLightAmbient") + index),
            rgb(light.ambient));
        isosurfaceProgram_.setUniformValue(
            qPrintable(QStringLiteral("uLightDiffuse") + index),
            rgb(light.diffuse));
        isosurfaceProgram_.setUniformValue(
            qPrintable(QStringLiteral("uLightSpecular") + index),
            rgb(light.specular));
    }
}

void StructureRenderer::createLitBuffer(LitVertexBuffer& buffer)
{
    buffer.vao.create();
    buffer.vao.bind();
    buffer.vbo.create();
    buffer.vbo.bind();
    constexpr int stride = kOverlayFaceFloats * sizeof(float);
    // ATTRIBUTE ORDER IS NOT THE MEMORY ORDER, and deliberately so.
    //
    // The data is laid out pos(3), normal(3), colour(3) — but location 1 is
    // bound to the COLOUR, not the normal, so that locations 0 and 1 mean
    // exactly what they mean in the old 6-float layout. wire.vert (the legacy
    // isosurface profile) declares 0 = position, 1 = colour and knows nothing
    // about normals; binding the normal to location 1 made it paint the
    // surface orientation as its colour, which showed as a hue sweep across
    // every lobe.
    //
    // Keeping the 9-float layout a strict SUPERSET of the 6-float one is what
    // lets one buffer serve both programs with no second VAO and no branch.
    gl_->glEnableVertexAttribArray(0); // position   (offset 0)
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    gl_->glEnableVertexAttribArray(1); // colour     (offset 6) -- see above
    gl_->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                               reinterpret_cast<void*>(6 * sizeof(float)));
    gl_->glEnableVertexAttribArray(2); // normal     (offset 3)
    gl_->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride,
                               reinterpret_cast<void*>(3 * sizeof(float)));
    buffer.vao.release();
}

void StructureRenderer::uploadLitBuffer(LitVertexBuffer& buffer,
                                        const std::vector<float>& data)
{
    buffer.vertexCount = static_cast<int>(data.size()) / 9;
    buffer.vbo.bind();
    buffer.vbo.allocate(data.data(),
                        static_cast<int>(data.size() * sizeof(float)));
}

void StructureRenderer::createColoredBuffer(ColoredVertexBuffer& buffer)
{
    buffer.vao.create();
    buffer.vao.bind();
    buffer.vbo.create();
    buffer.vbo.bind();
    gl_->glEnableVertexAttribArray(0); // position
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    gl_->glEnableVertexAttribArray(1); // color
    gl_->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                               reinterpret_cast<void*>(3 * sizeof(float)));
    buffer.vao.release();
}

void StructureRenderer::uploadColoredBuffer(ColoredVertexBuffer& buffer,
                                            const std::vector<float>& data)
{
    buffer.vertexCount = static_cast<int>(data.size()) / 6;
    buffer.vbo.bind();
    buffer.vbo.allocate(data.data(), static_cast<int>(data.size() * sizeof(float)));
}

void StructureRenderer::setLatticePlane(const std::vector<float>& faceTris,
                                        const std::vector<float>& edgeLines,
                                        float alpha, bool visible, bool showEdges)
{
    if (!initialized_)
        return;
    uploadColoredBuffer(latticePlaneFaces_, faceTris);
    uploadColoredBuffer(latticePlaneEdges_, edgeLines);
    latticePlaneAlpha_ = alpha;
    latticePlaneVisible_ = visible;
    latticePlaneEdgesOn_ = showEdges;
}

void StructureRenderer::setCustomOverlay(const std::vector<float>& faces,
                                         const std::vector<float>& edges,
                                         const std::vector<OverlayRange>& faceRanges,
                                         bool visible, float edgeAlpha)
{
    if (!initialized_)
        return;
    // Reject a stream that is not a whole number of 9-float vertices, and one
    // whose triangle count is not a multiple of 3. Either means a producer is
    // writing a different vertex layout than the VAO reads — which does not
    // fail loudly on its own: the draw simply samples positions from the wrong
    // offsets and the surface explodes into a fan of triangles that looks like
    // a physics bug rather than a buffer one.
    const int faceFloats = static_cast<int>(faces.size());
    if (faceFloats % (kOverlayFaceFloats * 3) != 0) {
        qWarning("Calango: custom-overlay face stream is %d floats, which is "
                 "not a whole number of %d-float triangles. Dropping it rather "
                 "than drawing garbage.",
                 faceFloats, kOverlayFaceFloats * 3);
        uploadLitBuffer(customOverlayFaces_, {});
    } else {
        uploadLitBuffer(customOverlayFaces_, faces);
    }
    uploadColoredBuffer(customOverlayEdges_, edges);
    customOverlayRanges_ = faceRanges;
    customOverlayVisible_ = visible;
    customOverlayEdgeAlpha_ = std::clamp(edgeAlpha, 0.0f, 1.0f);
}

void StructureRenderer::setManagedOverlay(
    const std::vector<float>& faces, const std::vector<float>& edges,
    const std::vector<OverlayRange>& faceRanges, bool visible)
{
    if (!initialized_)
        return;
    uploadColoredBuffer(managedOverlayFaces_, faces);
    uploadColoredBuffer(managedOverlayEdges_, edges);
    managedOverlayRanges_ = faceRanges;
    managedOverlayVisible_ = visible;
}

void StructureRenderer::setHydrogenBonds(const std::vector<float>& segments)
{
    if (!initialized_)
        return;
    uploadColoredBuffer(hydrogenBonds_, segments);
}

void StructureRenderer::buildHydrogenBondDashes(
    const std::vector<std::pair<QVector3D, QVector3D>>& contacts,
    const QColor& color, float dashLength, std::vector<float>& out)
{
    const float r = static_cast<float>(color.redF());
    const float g = static_cast<float>(color.greenF());
    const float b = static_cast<float>(color.blueF());
    const float period = std::max(dashLength, 1e-3f) * 2.0f; // dash + equal gap

    for (const auto& [from, to] : contacts) {
        const QVector3D delta = to - from;
        const float length = delta.length();
        if (length < 1e-4f)
            continue;
        const QVector3D direction = delta / length;
        // Walk the contact in dash+gap periods. The dash length is fixed in
        // Angstroms, so a long contact simply gets more dashes rather than
        // longer ones — the pattern reads the same at every bond length, and
        // (unlike line stipple) it does not change with zoom.
        for (float t = 0.0f; t < length; t += period) {
            const float end = std::min(t + dashLength, length);
            const QVector3D a = from + direction * t;
            const QVector3D c = from + direction * end;
            out.insert(out.end(), {a.x(), a.y(), a.z(), r, g, b,
                                   c.x(), c.y(), c.z(), r, g, b});
        }
    }
}

void StructureRenderer::buildRibbon(const core::Structure* structure,
                                    const std::vector<CastStyle>& casts,
                                    const std::set<int>* selection,
                                    std::vector<float>& bondInstances,
                                    std::vector<float>& atomInstances) const
{
    if (!structure || structure->empty() || !structure->hasResidues())
        return; // no backbone to trace

    // Collect the α-carbon trace of each chain, in residue order. The CA trace
    // IS the cartoon: it is the one atom per residue that every ribbon
    // representation in every viewer follows, because it sits on the backbone
    // and is present in every amino acid.
    struct Chain {
        std::vector<QVector3D> points;
        std::vector<QColor> colors;
    };
    std::map<std::string, Chain> chains;
    const auto& atoms = structure->atoms();
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        if (casts[i].mode != RepresentationMode::Ribbon)
            continue;
        const core::ResidueInfo& info = structure->residue(i);
        if (!info.isAlphaCarbon())
            continue;
        Chain& chain = chains[info.chain];
        chain.points.push_back(toQt(atoms[i].position));
        QColor color = resolvedAtomColor(i, atoms[i].atomicNumber,
                                         casts[i].colorMode);
        // In Element mode every α-carbon is the same grey, which would make a
        // four-chain complex one indistinguishable tangle. Colouring by chain
        // instead is what a ribbon diagram is FOR, so that is the default the
        // Element mode maps to here.
        if (casts[i].colorMode == ColorMode::Element)
            color = chainColor(info.chain);
        if (selection && selection->count(static_cast<int>(i)) > 0)
            color = selectionTint(color);
        chain.colors.push_back(color);
    }

    // One representative cast for the tube geometry: every ribbon atom shares
    // the mode, so its scale and material are the ribbon's.
    CastStyle ribbonCast{};
    for (std::size_t i = 0; i < casts.size(); ++i)
        if (casts[i].mode == RepresentationMode::Ribbon) {
            ribbonCast = casts[i];
            break;
        }
    // A protein backbone rises ~1.5 Å per residue, so a ~0.3 Å tube reads as a
    // ribbon rather than as a string of beads or a fat sausage.
    const float tubeRadius = 0.30f * ribbonCast.bondWidthFactor;

    for (const auto& [name, chain] : chains) {
        (void)name;
        if (chain.points.size() < 2)
            continue;
        // Catmull-Rom through the CA points: consecutive α-carbons are 3.8 Å
        // apart, and joining them with straight segments would draw a
        // zig-zagging polyline rather than the smooth fold the eye is meant to
        // read. Four subdivisions per segment is enough to hide the corners.
        constexpr int kSubdivisions = 4;
        const auto pointAt = [&chain](int index) {
            const int last = static_cast<int>(chain.points.size()) - 1;
            return chain.points[static_cast<std::size_t>(std::clamp(index, 0, last))];
        };
        const auto colorAt = [&chain](int index) {
            const int last = static_cast<int>(chain.colors.size()) - 1;
            return chain.colors[static_cast<std::size_t>(std::clamp(index, 0, last))];
        };

        QVector3D previous = chain.points.front();
        QColor previousColor = chain.colors.front();
        for (std::size_t segment = 0; segment + 1 < chain.points.size(); ++segment) {
            const auto i = static_cast<int>(segment);
            // A real chain break (a disordered loop the model omits) leaves a
            // gap far longer than the 3.8 Å CA-CA spacing. Bridging it would
            // draw a rod straight through the protein.
            if (pointAt(i).distanceToPoint(pointAt(i + 1)) > 5.0f) {
                previous = pointAt(i + 1);
                previousColor = colorAt(i + 1);
                continue;
            }
            const QVector3D p0 = pointAt(i - 1);
            const QVector3D p1 = pointAt(i);
            const QVector3D p2 = pointAt(i + 1);
            const QVector3D p3 = pointAt(i + 2);
            for (int step = 1; step <= kSubdivisions; ++step) {
                const float t = static_cast<float>(step) / kSubdivisions;
                const float t2 = t * t;
                const float t3 = t2 * t;
                const QVector3D point = 0.5f
                    * ((2.0f * p1) + (-p0 + p2) * t
                       + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                       + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
                const QColor color = t < 0.5f ? colorAt(i) : colorAt(i + 1);
                const QVector3D delta = point - previous;
                const float length = delta.length();
                if (length > 1e-4f) {
                    appendInstance(bondInstances,
                                   bondTransform(previous, delta / length, length,
                                                 tubeRadius),
                                   previousColor, color,
                                   ribbonCast.surfaceFinish,
                                   ribbonCast.opacity);
                    // A sphere at every joint: without it the tube shows a
                    // visible notch wherever two cylinders meet at an angle.
                    QMatrix4x4 joint;
                    joint.translate(point);
                    joint.scale(tubeRadius);
                    appendInstance(atomInstances, joint, color,
                                   ribbonCast.surfaceFinish,
                                   ribbonCast.opacity);
                }
                previous = point;
                previousColor = color;
            }
        }
    }
}

void StructureRenderer::buildMolecularSurface(
    const core::Structure* structure, const std::vector<CastStyle>& casts,
    std::vector<float>& faceVertices) const
{
    if (!structure || structure->empty())
        return;
    const auto& atoms = structure->atoms();

    std::vector<std::size_t> members;
    for (std::size_t i = 0; i < atoms.size(); ++i)
        if (casts[i].mode == RepresentationMode::MolecularSurface)
            members.push_back(i);
    if (members.empty())
        return;

    // Bounding box padded by the probe reach, so the envelope closes instead of
    // being clipped flat against the box faces.
    constexpr double kPadding = 3.0;   // Å
    core::Vec3 low = atoms[members.front()].position;
    core::Vec3 high = low;
    for (const std::size_t i : members) {
        const core::Vec3& p = atoms[i].position;
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y),
                std::max(high.z, p.z)};
    }
    low = {low.x - kPadding, low.y - kPadding, low.z - kPadding};
    high = {high.x + kPadding, high.y + kPadding, high.z + kPadding};

    // Grid spacing is a direct time/quality trade, and a protein-sized box at
    // 0.4 Å would be ~10^8 points. 0.6 Å resolves side-chain grooves while
    // keeping a 15 000-atom envelope buildable in about a second; the cap
    // protects against a pathological box rather than a large molecule.
    double spacing = 0.6;
    const core::Vec3 extent{high.x - low.x, high.y - low.y, high.z - low.z};
    constexpr long long kMaxPoints = 24'000'000;
    for (;;) {
        const long long nx = static_cast<long long>(extent.x / spacing) + 2;
        const long long ny = static_cast<long long>(extent.y / spacing) + 2;
        const long long nz = static_cast<long long>(extent.z / spacing) + 2;
        if (nx * ny * nz <= kMaxPoints)
            break;
        spacing *= 1.25;
    }

    core::VolumetricData field;
    field.nx = static_cast<int>(extent.x / spacing) + 2;
    field.ny = static_cast<int>(extent.y / spacing) + 2;
    field.nz = static_cast<int>(extent.z / spacing) + 2;
    field.origin = low;
    field.spanA = {spacing * (field.nx - 1), 0, 0};
    field.spanB = {0, spacing * (field.ny - 1), 0};
    field.spanC = {0, 0, spacing * (field.nz - 1)};
    field.values.assign(static_cast<std::size_t>(field.nx) * field.ny * field.nz,
                        0.0);
    // Nearest-atom index per grid node, so the extracted surface can be
    // coloured by the atom it actually belongs to rather than uniformly.
    std::vector<int> nearest(field.values.size(), -1);
    std::vector<double> nearestWeight(field.values.size(), 0.0);

    // Gaussian "blobby" density, splatted per atom into its own local box.
    // Summing every atom at every grid point would be O(N · grid) — 10^11 for
    // a protein — whereas each atom only reaches a few Å, so the splat is
    // O(N · small) and finishes in a fraction of a second.
    for (const std::size_t index : members) {
        const core::Atom& atom = atoms[index];
        const double radius =
            core::Elements::data(atom.atomicNumber).covalentRadius + 0.8;
        const double reach = radius + 1.2;
        const auto lo = [&](double value, double origin) {
            return std::max(0, static_cast<int>((value - reach - origin) / spacing));
        };
        const int ix0 = lo(atom.position.x, low.x);
        const int iy0 = lo(atom.position.y, low.y);
        const int iz0 = lo(atom.position.z, low.z);
        const int ix1 = std::min(field.nx - 1,
                                 static_cast<int>((atom.position.x + reach - low.x) / spacing));
        const int iy1 = std::min(field.ny - 1,
                                 static_cast<int>((atom.position.y + reach - low.y) / spacing));
        const int iz1 = std::min(field.nz - 1,
                                 static_cast<int>((atom.position.z + reach - low.z) / spacing));
        for (int ix = ix0; ix <= ix1; ++ix) {
            const double x = low.x + ix * spacing;
            for (int iy = iy0; iy <= iy1; ++iy) {
                const double y = low.y + iy * spacing;
                for (int iz = iz0; iz <= iz1; ++iz) {
                    const double z = low.z + iz * spacing;
                    const double dx = x - atom.position.x;
                    const double dy = y - atom.position.y;
                    const double dz = z - atom.position.z;
                    const double d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 > reach * reach)
                        continue;
                    // exp(-(d/r)^2 · 2) falls to ~0.13 at d = r, so an
                    // isovalue of 0.13 traces the van der Waals envelope.
                    const double weight = std::exp(-2.0 * d2 / (radius * radius));
                    const auto slot =
                        (static_cast<std::size_t>(ix) * field.ny + iy) * field.nz + iz;
                    field.values[slot] += weight;
                    if (weight > nearestWeight[slot]) {
                        nearestWeight[slot] = weight;
                        nearest[slot] = static_cast<int>(index);
                    }
                }
            }
        }
    }

    const core::IsoMesh mesh = core::extractIsosurface(field, 0.13);
    faceVertices.reserve(mesh.positions.size() * 6);
    for (const core::Vec3& position : mesh.positions) {
        // Colour from the nearest atom recorded at the enclosing grid node.
        const int ix = std::clamp(static_cast<int>((position.x - low.x) / spacing),
                                  0, field.nx - 1);
        const int iy = std::clamp(static_cast<int>((position.y - low.y) / spacing),
                                  0, field.ny - 1);
        const int iz = std::clamp(static_cast<int>((position.z - low.z) / spacing),
                                  0, field.nz - 1);
        const auto slot =
            (static_cast<std::size_t>(ix) * field.ny + iy) * field.nz + iz;
        const int owner = nearest[slot];
        QColor color(200, 200, 210);
        if (owner >= 0) {
            const auto index = static_cast<std::size_t>(owner);
            color = resolvedAtomColor(index, atoms[index].atomicNumber,
                                      casts[index].colorMode);
        }
        appendColoredVertex(faceVertices,
                            QVector3D(static_cast<float>(position.x),
                                      static_cast<float>(position.y),
                                      static_cast<float>(position.z)),
                            color);
    }
}

void StructureRenderer::buildPolyhedra(const core::Structure* structure,
                                       const std::set<int>* selection,
                                       std::vector<float>& faceVertices,
                                       std::vector<float>& edgeVertices) const
{
    if (!structure || structure->empty())
        return;
    const auto& atoms = structure->atoms();
    const std::size_t count = atoms.size();

    // Only the atoms whose own cast asks for Polyhedral get a hull. The
    // neighbour shells are still built from ALL bonds — a polyhedron's vertices
    // are its ligands, which have no reason to be in the same cast as the
    // cation at its centre.
    const std::vector<CastStyle> castStyles = atomCastStyles(structure, style_);

    // Bonded-neighbor positions per atom in world coordinates, honoring
    // periodic image offsets so a polyhedron straddling a cell boundary stays
    // whole rather than collapsing to the in-cell images.
    std::vector<std::vector<QVector3D>> neighbors(count);
    for (const core::Bond& bond :
         structure->detectBonds(style_.bondTolerance, style_.autoBonds)) {
        const auto i = static_cast<std::size_t>(bond.i);
        const auto j = static_cast<std::size_t>(bond.j);
        neighbors[i].push_back(toQt(atoms[j].position + bond.imageOffset));
        neighbors[j].push_back(toQt(atoms[i].position - bond.imageOffset));
    }

    // Per-cation cutoff overrides replace that atom's bond-derived shell with
    // every neighbour inside an absolute radius. Covalent-radius bonding gets
    // the coordination number wrong for some cations (an octahedral Ti in an
    // oxide picks up 4 or 8 rather than 6), and no global tolerance fixes one
    // element without breaking the others.
    if (!style_.polyhedronCutoffOverrides.empty()) {
        const auto range = core::imageRange(
            structure->cell(),
            std::max_element(style_.polyhedronCutoffOverrides.begin(),
                             style_.polyhedronCutoffOverrides.end(),
                             [](const auto& a, const auto& b) {
                                 return a.second < b.second;
                             })
                ->second);
        const auto& cellVectors = structure->cell().vectors();
        for (std::size_t c = 0; c < count; ++c) {
            const auto override =
                style_.polyhedronCutoffOverrides.find(atoms[c].atomicNumber);
            if (override == style_.polyhedronCutoffOverrides.end())
                continue;
            const double cutoff = override->second;
            neighbors[c].clear();
            for (std::size_t n = 0; n < count; ++n) {
                if (n == c)
                    continue;
                for (int ia = -range[0]; ia <= range[0]; ++ia)
                    for (int ib = -range[1]; ib <= range[1]; ++ib)
                        for (int ic = -range[2]; ic <= range[2]; ++ic) {
                            const core::Vec3 shift =
                                cellVectors[0] * static_cast<double>(ia)
                                + cellVectors[1] * static_cast<double>(ib)
                                + cellVectors[2] * static_cast<double>(ic);
                            const core::Vec3 delta =
                                atoms[n].position + shift - atoms[c].position;
                            if (delta.norm() <= cutoff)
                                neighbors[c].push_back(
                                    toQt(atoms[n].position + shift));
                        }
            }
        }
    }

    for (std::size_t c = 0; c < count; ++c) {
        if (castStyles[c].mode != RepresentationMode::Polyhedral)
            continue;
        // A coordination polyhedron needs at least four vertices to enclose a
        // volume; fewer neighbors (edges/triangles) are left to the spheres.
        if (neighbors[c].size() < 4)
            continue;
        std::vector<std::array<int, 3>> tris;
        std::vector<std::pair<int, int>> edges;
        convexHull(neighbors[c], tris, edges);
        if (tris.empty())
            continue; // coplanar coordination shell (no volume)

        QColor color = resolvedAtomColor(c, atoms[c].atomicNumber,
                                         castStyles[c].colorMode);
        if (selection && selection->count(static_cast<int>(c)) > 0)
            color = selectionTint(color);
        const QColor edgeColor = color.darker(170);

        for (const auto& tri : tris)
            for (int vi : tri)
                appendColoredVertex(faceVertices, neighbors[c][vi], color);
        for (const auto& [a, b] : edges) {
            appendColoredVertex(edgeVertices, neighbors[c][a], edgeColor);
            appendColoredVertex(edgeVertices, neighbors[c][b], edgeColor);
        }
    }
}


void StructureRenderer::setStructure(const core::Structure* structure,
                                     const std::set<int>* selection)
{
    if (!initialized_)
        return;

    // Scene bounds drive the shadow frustum. Recomputed here rather than per
    // frame: the geometry only changes when the structure does.
    if (structure && !structure->empty()) {
        const core::Vec3 centroid = structure->centroid();
        sceneCenter_ = QVector3D(static_cast<float>(centroid.x),
                                 static_cast<float>(centroid.y),
                                 static_cast<float>(centroid.z));
        // Pad by a couple of Angstrom so atom radii and bond caps at the rim
        // are inside the frustum, not clipped out of the depth map.
        sceneRadius_ = static_cast<float>(structure->boundingRadius(centroid)) + 2.0f;
        if (structure->cell().isDefined()) {
            // A cell wireframe can extend past the atoms; include its corners.
            for (const core::Vec3& corner : structure->cell().corners()) {
                const QVector3D p(static_cast<float>(corner.x),
                                  static_cast<float>(corner.y),
                                  static_cast<float>(corner.z));
                sceneRadius_ = std::max(sceneRadius_,
                                        (p - sceneCenter_).length() + 1.0f);
            }
        }
        sceneRadius_ = std::max(sceneRadius_, 1.0f);
    } else {
        sceneCenter_ = QVector3D();
        sceneRadius_ = 1.0f;
    }

    std::vector<float> atomInstances;
    std::vector<float> bondInstances;
    std::vector<float> coneInstances;
    std::vector<float> wireBondVertices;
    std::vector<float> wireAtomVertices;
    std::vector<float> cellTubeInstances;
    std::vector<float> cellFaceVertices;

    // Per-atom settings: every atom's own cast decides how it is drawn, from
    // its mode down to its radius and material. With no casts defined this is
    // a vector of one repeated value and every branch below behaves exactly as
    // the single-cast renderer did.
    const std::vector<CastStyle> casts = atomCastStyles(structure, style_);
    const auto modeAt = [&casts](std::size_t index) {
        return casts[index].mode;
    };
    const auto anyCastUses = [&casts](RepresentationMode mode) {
        return std::any_of(casts.begin(), casts.end(),
                           [mode](const CastStyle& cast) {
                               return cast.mode == mode;
                           });
    };
    // Polyhedra / ribbons / surfaces are built when ANY cast asks for them;
    // each builder then skips the atoms whose own cast does not.
    const bool polyhedral = anyCastUses(RepresentationMode::Polyhedral);
    const bool ribbon = anyCastUses(RepresentationMode::Ribbon);
    const bool surface = anyCastUses(RepresentationMode::MolecularSurface);
    // "Show hydrogens" off drops H spheres, any bond with an H end, and the
    // H-bond dashes. A bond kept while its hydrogen went would be a stick
    // ending in mid-air, so the filter has to reach the bond pass too.
    const auto hiddenHydrogen = [this](int atomicNumber) {
        return !style_.showHydrogens && atomicNumber == 1;
    };

    if (structure && !structure->empty()) {
        const auto& atoms = structure->atoms();
        atomInstances.reserve(atoms.size() * kFloatsPerInstance);

        // Bonds are needed by the neighbouring-cell image rule below, so they
        // are perceived once here rather than inside the bond pass.
        const std::vector<core::Bond> bonds =
            structure->detectBonds(style_.bondTolerance, style_.autoBonds);

        // Periodic images of each atom, as Cartesian translations, computed
        // once and reused by the bond pass so an atom and its bonds are never
        // duplicated inconsistently. ghostShifts[i] never contains the identity
        // — that is the atom itself.
        std::vector<std::vector<core::Vec3>> ghostShifts;
        const bool ghosts =
            style_.showNeighborCellAtoms && structure->cell().isDefined();
        const auto addGhost = [&ghostShifts](std::size_t index,
                                             const core::Vec3& shift) {
            if (shift.dot(shift) < 1e-12)
                return; // the atom's own position
            auto& shifts = ghostShifts[index];
            for (const core::Vec3& existing : shifts)
                if ((existing - shift).norm() < 1e-6)
                    return;
            shifts.push_back(shift);
        };
        if (ghosts) {
            ghostShifts.resize(atoms.size());
            // Exactly the images that terminate a bond leaving the cell, and
            // nothing else.
            //
            // The setting used to also duplicate every atom sitting on a face,
            // edge or vertex, and then complete those copies' own bonds. That
            // filled the view with atoms the user had not asked for — a
            // face-centred cell grew by more than half again, most of it
            // periodic repetition rather than information. What people
            // actually want from "show the neighboring cell" is that a bond
            // crossing the boundary ends ON AN ATOM instead of in mid-air, so
            // that is all this does now: for every wrapped bond, the image at
            // each end.
            for (const core::Bond& bond : bonds) {
                if (!bond.crossesBoundary())
                    continue;
                addGhost(static_cast<std::size_t>(bond.j), bond.imageOffset);
                addGhost(static_cast<std::size_t>(bond.i),
                         core::Vec3{} - bond.imageOffset);
            }
        }
        /// True when atom `index` is drawn at translation `shift` — either as
        /// itself (the zero shift) or as one of its images.
        const auto drawnAt = [&ghostShifts, ghosts](std::size_t index,
                                                    const core::Vec3& shift) {
            if (shift.dot(shift) < 1e-12)
                return true;
            if (!ghosts)
                return false;
            for (const core::Vec3& existing : ghostShifts[index])
                if ((existing - shift).norm() < 1e-6)
                    return true;
            return false;
        };

        for (std::size_t index = 0; index < atoms.size(); ++index) {
            const core::Atom& atom = atoms[index];
            const CastStyle& cast = casts[index];
            // Ribbon and molecular surface REPLACE the per-atom geometry —
            // drawing 15 000 spheres underneath a surface would cost the frame
            // rate for something nothing can see.
            if (isMacromolecularMode(cast.mode))
                continue;
            if (hiddenHydrogen(atom.atomicNumber))
                continue;

            const bool selected =
                selection && selection->count(static_cast<int>(index)) > 0;

            QColor color =
                resolvedAtomColor(index, atom.atomicNumber, cast.colorMode);
            if (selected)
                color = selectionTint(color);

            const bool atomWireframe = cast.mode == RepresentationMode::Wireframe;
            float radius = displayRadius(atom.atomicNumber, cast)
                * (selected ? 1.2f : 1.0f);
            if (const auto it = style_.radiusScaleOverrides.find(atom.atomicNumber);
                it != style_.radiusScaleOverrides.end())
                radius *= it->second;
            if (cast.mode == RepresentationMode::Licorice) {
                // A sphere of exactly the tube radius at each site: it caps the
                // bond ends and rounds the joint where two tubes meet at an
                // angle, which is what makes licorice read as continuous
                // tubing rather than as a pile of cut cylinders. Element radii
                // deliberately do NOT apply — uniform thickness is the point.
                radius = style_.bondRadius * kLicoriceRadius
                    * cast.bondWidthFactor * (selected ? 1.2f : 1.0f);
            }

            if (atomWireframe) {
                appendColoredVertex(wireAtomVertices, toQt(atom.position), color);
            } else {
                QMatrix4x4 model;
                model.translate(toQt(atom.position));
                model.scale(radius);
                appendInstance(atomInstances, model, color, cast.surfaceFinish,
                               cast.opacity);
            }

            // Ghosts use the same radius and colour as their source, so the
            // duplicated face is visually indistinguishable from the real one.
            if (ghosts) {
                for (const core::Vec3& shift : ghostShifts[index]) {
                    const QVector3D ghostPosition = toQt(atom.position + shift);
                    if (atomWireframe) {
                        appendColoredVertex(wireAtomVertices, ghostPosition, color);
                        continue;
                    }
                    QMatrix4x4 model;
                    model.translate(ghostPosition);
                    model.scale(radius);
                    appendInstance(atomInstances, model, color,
                                   cast.surfaceFinish, cast.opacity);
                }
            }
        }

        {
            for (const core::Bond& bond : bonds) {
                // Which cast a bond belongs to is only well defined when both
                // of its atoms agree. Space-filling has no bonds at all, so a
                // bond touching a CPK atom is dropped — that is what lets a
                // CPK metal surface sit under a ball-and-stick molecule
                // without a thicket of substrate sticks growing through the
                // vdW spheres. Ribbon and surface casts replace their geometry
                // wholesale, so their bonds go too. A bond whose two ends
                // disagree on anything else (one wireframe, one solid) is drawn
                // solid: a half-line, half-cylinder bond reads as a fault.
                const CastStyle& castI = casts[static_cast<std::size_t>(bond.i)];
                const CastStyle& castJ = casts[static_cast<std::size_t>(bond.j)];
                const RepresentationMode modeI = castI.mode;
                const RepresentationMode modeJ = castJ.mode;
                if (modeI == RepresentationMode::SpaceFilling
                    || modeJ == RepresentationMode::SpaceFilling
                    || isMacromolecularMode(modeI) || isMacromolecularMode(modeJ))
                    continue;
                if (hiddenHydrogen(
                        atoms[static_cast<std::size_t>(bond.i)].atomicNumber)
                    || hiddenHydrogen(
                        atoms[static_cast<std::size_t>(bond.j)].atomicNumber))
                    continue;
                const bool wireframe = modeI == RepresentationMode::Wireframe
                    && modeJ == RepresentationMode::Wireframe;
                // Both ends' width factors average into one radius rather than
                // each half taking its own: a bond that changed thickness at
                // its midpoint would read as a modelling error, not a setting.
                // Licorice draws every bond at one fixed, generous radius —
                // the uniform thickness IS the representation. A licorice cast
                // meeting a ball-and-stick one averages, like the widths do.
                const auto tubeScale = [](const CastStyle& cast) {
                    return (cast.mode == RepresentationMode::Licorice
                                ? kLicoriceRadius
                                : 1.0f)
                        * cast.bondWidthFactor;
                };
                const float baseRadius = style_.bondRadius * 0.5f
                    * (tubeScale(castI) + tubeScale(castJ));
                // The material likewise comes from one end — a half-matte,
                // half-glassy cylinder is not a thing anyone wants.
                const SurfaceFinish bondFinish = castI.surfaceFinish;
                const float bondOpacity =
                    0.5f * (castI.opacity + castJ.opacity);

                // Translations at which this bond is drawn. A copy is only
                // emitted when BOTH of its ends are actually drawn there —
                // atom i at `offset` and atom j at `offset + imageOffset`.
                //
                // That condition is the whole fix: the old rule took the union
                // of the two endpoints' shifts and drew the bond at each, so a
                // face atom's bond was duplicated onto the far face while its
                // partner was not, leaving a stick growing out of the copied
                // atom toward nothing. The candidates below are exactly the
                // translations that can satisfy it, and the filter drops the
                // rest.
                std::vector<core::Vec3> offsets{core::Vec3{}};
                if (ghosts) {
                    const auto consider = [&offsets](const core::Vec3& shift) {
                        const bool duplicate = std::any_of(
                            offsets.begin(), offsets.end(),
                            [&shift](const core::Vec3& existing) {
                                return (existing - shift).norm() < 1e-9;
                            });
                        if (!duplicate)
                            offsets.push_back(shift);
                    };
                    for (const core::Vec3& shift :
                         ghostShifts[static_cast<std::size_t>(bond.i)])
                        consider(shift);
                    for (const core::Vec3& shift :
                         ghostShifts[static_cast<std::size_t>(bond.j)])
                        consider(shift - bond.imageOffset);
                    offsets.erase(
                        std::remove_if(
                            offsets.begin(), offsets.end(),
                            [&](const core::Vec3& t) {
                                return !drawnAt(
                                           static_cast<std::size_t>(bond.i), t)
                                    || !drawnAt(
                                           static_cast<std::size_t>(bond.j),
                                           t + bond.imageOffset);
                            }),
                        offsets.end());
                }
                // With the neighbouring cell shown, a wrapped bond runs the
                // whole way to the image atom that is now on screen; without
                // it, the conventional pair of half-length stubs stands in for
                // the periodicity.
                const bool stubbed = bond.crossesBoundary() && !ghosts;
                for (const core::Vec3& offset : offsets) {
                const auto& a = atoms[static_cast<std::size_t>(bond.i)];
                const auto& b = atoms[static_cast<std::size_t>(bond.j)];
                const QVector3D pa = toQt(a.position + offset);
                const QVector3D pbReal = toQt(b.position + offset);
                const QVector3D pbImage =
                    toQt(b.position + bond.imageOffset + offset);
                const QVector3D dir = (pbImage - pa).normalized();
                const float half = pa.distanceToPoint(pbImage) * 0.5f;

                QColor colorA = resolvedAtomColor(
                    static_cast<std::size_t>(bond.i), a.atomicNumber,
                    castI.colorMode);
                QColor colorB = resolvedAtomColor(
                    static_cast<std::size_t>(bond.j), b.atomicNumber,
                    castJ.colorMode);
                // Keep bond endpoints identical to their spheres, including
                // the selection highlight (previously spheres tinted but
                // bonds did not — a visible color mismatch at the joint).
                if (selection && selection->count(bond.i) > 0)
                    colorA = selectionTint(colorA);
                if (selection && selection->count(bond.j) > 0)
                    colorB = selectionTint(colorB);

                const QColor mid = style_.gradientBonds
                    ? midpointColor(colorA, colorB)
                    : QColor();

                if (wireframe) {
                    // Single line regardless of order. GL interpolates the
                    // per-vertex colors, so handing the joint the midpoint
                    // color yields a smooth end-to-end gradient.
                    const QColor jointA = style_.gradientBonds ? mid : colorA;
                    const QColor jointB = style_.gradientBonds ? mid : colorB;
                    appendColoredVertex(wireBondVertices, pa, colorA);
                    appendColoredVertex(wireBondVertices, pa + dir * half, jointA);
                    if (!stubbed) {
                        appendColoredVertex(wireBondVertices, pa + dir * half, jointB);
                        appendColoredVertex(wireBondVertices, pbImage, colorB);
                    } else {
                        appendColoredVertex(wireBondVertices, pbReal, colorB);
                        appendColoredVertex(wireBondVertices, pbReal - dir * half, jointB);
                    }
                    continue;
                }

                // Bond order n -> n parallel cylinders offset sideways.
                std::vector<float> lateral;
                float radiusScale = 1.0f;
                multiBondLayout(bond.order, lateral, radiusScale);
                const float radius = baseRadius * radiusScale;
                const QVector3D perp = perpendicularTo(dir);

                for (const float offsetUnits : lateral) {
                    const QVector3D shift = perp * (offsetUnits * baseRadius);
                    // Each half-cylinder carries a start/end color pair; with
                    // gradient bonds the halves meet at the midpoint color,
                    // producing one continuous atomA -> atomB gradient.
                    appendInstance(bondInstances,
                                   bondTransform(pa + shift, dir, half, radius),
                                   colorA,
                                   style_.gradientBonds ? mid : colorA,
                                   bondFinish, bondOpacity);
                    if (!stubbed) {
                        appendInstance(
                            bondInstances,
                            bondTransform(pa + shift + dir * half, dir, half, radius),
                            style_.gradientBonds ? mid : colorB, colorB,
                            bondFinish, bondOpacity);
                    } else {
                        // Wrapped bond: atom j's half is a stub pointing back
                        // toward its own periodic image of atom i (z = 0 sits
                        // at the atom, so the gradient runs atom -> midpoint).
                        appendInstance(
                            bondInstances,
                            bondTransform(pbReal + shift, -dir, half, radius),
                            colorB, style_.gradientBonds ? mid : colorB,
                            bondFinish, bondOpacity);
                    }
                }
                } // ghost offsets
            }
        }

        // Vector overlay arrows: shaft (cylinder) + head (cone) per atom,
        // sharing the lit instanced pipeline. Atoms drawn as wireframe are
        // skipped — there is no solid geometry there for a lit arrow to sit on.
        if (style_.vectorOverlay != VectorOverlay::None) {
            // vectorScale is normalized, not raw Å: 1.0 means the calibrated
            // baseline below. The former 1:1 Å-per-unit baseline drew forces
            // long enough to overlap neighboring atoms and obscure the
            // geometry, so the baseline is half that — and the slider's 1.0×
            // is this recalibrated length.
            constexpr double kVectorBaseScale = 0.5;
            // Velocities are tiny (Å/fs·√amu) next to forces/moments, so the
            // Velocity overlay keeps a 20× visual multiplier on top.
            const double effectiveScale = style_.vectorScale * kVectorBaseScale
                * (style_.vectorOverlay == VectorOverlay::Velocity ? 20.0 : 1.0);
            const auto addArrows = [&](const std::string& fieldName,
                                       const QColor& color) {
                // resolvedVectorField, not vectorFields(): a COLLINEAR
                // magnetic moment is stored as one scalar per atom — the
                // calculation quantized the spin along z and reported only
                // that component — so there is no vector field to find, and
                // the overlay drew nothing at all. The scalar is promoted to
                // (0, 0, m) here, which is the direction it always meant.
                const std::vector<core::Vec3> values =
                    structure->resolvedVectorField(fieldName);
                if (values.size() != atoms.size())
                    return;
                // Baseline 0.045 Å, scaled by the user's width setting.
                const float shaftRadius =
                    0.045f * std::max(0.05f, style_.vectorWidth);
                for (std::size_t index = 0; index < atoms.size(); ++index) {
                    if (modeAt(index) == RepresentationMode::Wireframe
                        || isMacromolecularMode(modeAt(index)))
                        continue;
                    const core::Vec3& v = values[index];
                    // Filter on the FIELD magnitude, before the display scale:
                    // otherwise the set of hidden atoms would change every time
                    // the arrow length slider moved.
                    if (v.norm() < style_.vectorMinMagnitude)
                        continue;
                    const double length = v.norm() * effectiveScale;
                    if (length < 0.05)
                        continue; // invisible / zero vector
                    const QVector3D origin = toQt(atoms[index].position);
                    const QVector3D dir = toQt(v.normalized());
                    // Always headed: a plain shaft does not say which way the
                    // vector points, which is the one thing the overlay is for.
                    const auto headLength =
                        static_cast<float>(std::min(0.30 * length, 0.45));
                    const auto shaftLength =
                        static_cast<float>(length) - headLength;
                    appendInstance(bondInstances,
                                   bondTransform(origin, dir, shaftLength,
                                                 shaftRadius),
                                   color, style_.surfaceFinish);
                    // Arrowhead cone: unit radius scales laterally via
                    // bondTransform's radius parameter.
                    appendInstance(coneInstances,
                                   bondTransform(origin + dir * shaftLength,
                                                 dir, headLength,
                                                 shaftRadius * 2.6f),
                                   color, style_.surfaceFinish);
                }
            };
            switch (style_.vectorOverlay) {
            case VectorOverlay::Velocity:
                addArrows("velocities", style_.velocityColor);
                break;
            case VectorOverlay::Force:
                addArrows("forces", style_.forceColor);
                break;
            case VectorOverlay::MagneticMoment:
                // Collinear runs write a scalar magmom per atom; AseBridge
                // promotes those to (0, 0, m) so the same arrow path shows
                // spin up/down along z. Non-collinear (N,3) magmoms import
                // directly.
                addArrows("magmoms", style_.magmomColor);
                break;
            case VectorOverlay::InitialMagneticMoment:
                // Set in Edit Structure, not computed. Collinear moments are
                // stored alongside a promoted (0, 0, m) vector copy so this
                // same arrow path serves both spin modes.
                addArrows("initial_magmoms", style_.initialMagmomColor);
                break;
            case VectorOverlay::None:
                break;
            }
        }

        if (structure->cell().isDefined()) {
            // The cell outline, as a vertex list + edges + polygon faces.
            //
            // Two shapes go through this: the parallelepiped of the basis
            // vectors, and the Wigner-Seitz cell of the same lattice. They are
            // built into ONE representation so that every style below — the
            // dash pattern, the lit-tube threshold, the translucent fill — is
            // written once and applies to both. A second copy of that code for
            // the Voronoi case would be four settings with two implementations
            // apiece and no reason for them ever to agree.
            std::vector<QVector3D> outlinePoints;
            std::vector<std::pair<int, int>> outlineEdges;
            std::vector<std::vector<int>> outlineFaces;

            if (style_.showVoronoiCell) {
                // Drawn about the CENTRE of the conventional cell, not about
                // the origin.
                //
                // The construction itself is centred on a lattice point, and
                // the origin is one — but a structure's atoms are given in
                // fractional [0, 1), so an origin-centred cell encloses one
                // corner of the model and leaves the rest of it outside. It is
                // the correct polyhedron in a place that makes the picture
                // read as a rendering fault. Since this REPLACES the box, it is
                // drawn where the box was: same volume, same contents, so
                // toggling between the two compares like with like.
                //
                // Shape and volume are exact either way; only which point it is
                // centred on is a viewing choice, and the tool tip says so.
                const core::PolyhedronMesh ws =
                    core::computeWignerSeitzCell(structure->cell());
                const auto& vectors = structure->cell().vectors();
                const QVector3D centre =
                    toQt((vectors[0] + vectors[1] + vectors[2]) * 0.5);
                outlinePoints.reserve(ws.vertices.size());
                for (const core::Vec3& v : ws.vertices)
                    outlinePoints.push_back(toQt(v) + centre);
                outlineFaces = ws.faces;
                // Edges from the faces, each shared pair emitted once — a
                // convex polyhedron has every edge in exactly two faces, so
                // without the de-duplication every line would be drawn twice
                // (visible as double-density dashes, and twice the tubes).
                std::set<std::pair<int, int>> unique;
                for (const std::vector<int>& face : outlineFaces) {
                    for (std::size_t k = 0; k < face.size(); ++k) {
                        const int a = face[k];
                        const int b = face[(k + 1) % face.size()];
                        unique.insert({std::min(a, b), std::max(a, b)});
                    }
                }
                outlineEdges.assign(unique.begin(), unique.end());
            } else {
                const auto corners = structure->cell().corners();
                for (const core::Vec3& corner : corners)
                    outlinePoints.push_back(toQt(corner));
                for (const auto& [i, j] : core::UnitCell::edges())
                    outlineEdges.emplace_back(i, j);
                // Corner index i encodes the fractional coordinate in its bits
                // (bit 0 = a, bit 1 = b, bit 2 = c), so each face is the four
                // corners that agree on one bit.
                outlineFaces = {{0, 2, 6, 4}, {1, 5, 7, 3},  // a = 0, a = 1
                                {0, 4, 5, 1}, {2, 3, 7, 6},  // b = 0, b = 1
                                {0, 1, 3, 2}, {4, 6, 7, 5}}; // c = 0, c = 1
            }

            // Edges are ALWAYS lit tubes now, at every width.
            //
            // They used to be plain GL_LINES below width 1.01 and tubes above
            // it, which meant a thin cell was drawn by an unlit program and sat
            // in the scene as a flat stroke that ignored every light — while
            // the same cell one notch thicker was shaded. A line has no surface
            // to shade, so the way to light an edge is to give it one: the tube
            // radius simply keeps scaling with the width, down to a hairline.
            const float tubeRadius = 0.015f * style_.cellLineWidth;
            // A broken stroke is cut into the geometry: each edge becomes N
            // marks with gaps between them, and a solid edge is the N = 1
            // case, so both styles run the same emit below.
            //
            // The mark count is derived PER EDGE rather than from a fixed
            // period in angstrom. A whole number of marks is what makes the
            // pattern start and finish exactly on the two corners — a fixed
            // period leaves the last mark truncated at a random fraction,
            // which reads as a rendering fault rather than as a dash. It also
            // keeps a 3 A cell and a 30 A slab looking like the same style.
            const float duty = style_.cellLineStyle == CellLineStyle::Dotted
                ? 0.30f
                : 0.62f;
            const int marks = style_.cellLineStyle == CellLineStyle::Solid
                ? 1
                : (style_.cellLineStyle == CellLineStyle::Dotted ? 14 : 8);
            for (const auto& [i, j] : outlineEdges) {
                const QVector3D from = outlinePoints[static_cast<std::size_t>(i)];
                const QVector3D to = outlinePoints[static_cast<std::size_t>(j)];
                const float length = from.distanceToPoint(to);
                if (length <= 0.0f)
                    continue;
                const QVector3D dir = (to - from) / length;
                for (int m = 0; m < marks; ++m) {
                    // Marks are centred in their slot, so the gaps at the two
                    // ends are half-width and the corners stay closed.
                    const float slot = length / static_cast<float>(marks);
                    const float markLength =
                        marks == 1 ? length : slot * duty;
                    const QVector3D a =
                        from + dir * (static_cast<float>(m) * slot
                                      + 0.5f * (slot - markLength));
                    appendInstance(cellTubeInstances,
                                   bondTransform(a, dir, markLength,
                                                 tubeRadius),
                                   style_.cellColor, style_.surfaceFinish);
                }
            }

            // Filled cell: the faces as triangles ("hatched cell" in the Unit
            // Cell tab). Built alongside the wireframe rather than instead of
            // it — the two are independent depictions of the same shape, and
            // Style::fillCell decides at DRAW time whether this stream is
            // used, exactly as showCell does for the edges above.
            //
            // Fanned from each polygon's first vertex, which is valid because
            // both shapes are convex and their faces arrive wound in order.
            // Winding itself is irrelevant here: face culling is off in the
            // main pass, which is what a translucent cell needs — you see its
            // far side through its near one.
            const float fillR = static_cast<float>(style_.cellFillColor.redF());
            const float fillG = static_cast<float>(style_.cellFillColor.greenF());
            const float fillB = static_cast<float>(style_.cellFillColor.blueF());
            for (const std::vector<int>& face : outlineFaces) {
                // One normal per face, from its first triangle. Both shapes are
                // convex polyhedra with planar faces, so a face normal IS the
                // exact surface normal everywhere on it — no smoothing to do,
                // and flat shading is what makes a faceted cell read as
                // faceted.
                QVector3D normal;
                if (face.size() >= 3) {
                    const QVector3D& p0 =
                        outlinePoints[static_cast<std::size_t>(face[0])];
                    const QVector3D& p1 =
                        outlinePoints[static_cast<std::size_t>(face[1])];
                    const QVector3D& p2 =
                        outlinePoints[static_cast<std::size_t>(face[2])];
                    normal = QVector3D::crossProduct(p1 - p0, p2 - p0);
                    normal = normal.lengthSquared() > 1e-12f
                        ? normal.normalized()
                        : QVector3D(0.0f, 0.0f, 1.0f);
                }
                for (std::size_t k = 1; k + 1 < face.size(); ++k) {
                    for (const int index : {face[0], face[k], face[k + 1]}) {
                        const QVector3D p =
                            outlinePoints[static_cast<std::size_t>(index)];
                        cellFaceVertices.insert(
                            cellFaceVertices.end(),
                            {p.x(), p.y(), p.z(), normal.x(), normal.y(),
                             normal.z(), fillR, fillG, fillB});
                    }
                }
            }
        }
    }

    // Macromolecular geometry, built after the per-atom pass so it can append
    // into the same instance streams (the ribbon is cylinders and spheres like
    // everything else — it just follows a backbone instead of bonds).
    if (ribbon)
        buildRibbon(structure, casts, selection, bondInstances, atomInstances);

    // --- Neighbouring cells -------------------------------------------------
    // Done here, on the finished streams, rather than inside each builder: the
    // scene of one cell is already assembled, and every periodic image of it
    // is that same scene translated by a lattice vector. One pass over the
    // buffers therefore replicates atoms, bonds, vector arrows, wireframe
    // geometry and ribbons alike, with no builder above aware of it.
    //
    // `cellShifts` is empty when there is nothing to do, which is the default
    // (0 -> 1 on every axis) and costs nothing.
    std::vector<QVector3D> cellShifts;
    if (structure && structure->cell().isDefined()
        && !style_.neighborCells.homeCellOnly()) {
        const auto& vectors = structure->cell().vectors();
        QVector3D mean;
        for (const auto& offset : style_.neighborCells.cellOffsets()) {
            const core::Vec3 shift = vectors[0] * static_cast<double>(offset[0])
                + vectors[1] * static_cast<double>(offset[1])
                + vectors[2] * static_cast<double>(offset[2]);
            cellShifts.push_back(toQt(shift));
            mean += cellShifts.back();
        }
        // Re-centre the shadow/depth volume on the replicated set. Leaving it
        // around the home cell alone would clip the images out of the shadow
        // map, which shows as neighbours that cast and receive no shadow.
        mean /= static_cast<float>(cellShifts.size());
        float reach = 0.0f;
        for (const QVector3D& shift : cellShifts)
            reach = std::max(reach, (shift - mean).length());
        sceneCenter_ += mean;
        sceneRadius_ += reach;
    }
    if (!cellShifts.empty()) {
        for (std::vector<float>* stream :
             {&atomInstances, &bondInstances, &coneInstances})
            replicateStream(*stream, kFloatsPerInstance, /*offset=*/12,
                            cellShifts);
        for (std::vector<float>* stream : {&wireBondVertices, &wireAtomVertices})
            replicateStream(*stream, /*stride=*/6, /*offset=*/0, cellShifts);
        // The cell wireframe follows only when the user asked for the boxes.
        // Unchecked, the home cell keeps its own outline (that is `showCell`'s
        // business) and the neighbours are drawn as bare atoms and bonds.
        if (style_.neighborCells.showEdges) {
            replicateStream(cellTubeInstances, kFloatsPerInstance,
                            /*offset=*/12, cellShifts);
            // The fill is the same box as the edges, so it follows the same
            // rule: boxes around the images means filled boxes around them.
            // Stride 9 now that the fill carries normals; offset 0 is still
            // the position, which is the only part a translation moves.
            replicateStream(cellFaceVertices, /*stride=*/9, /*offset=*/0,
                            cellShifts);
        }
    }

    sphere_.instanceCount = static_cast<int>(atomInstances.size()) / kFloatsPerInstance;
    sphere_.instanceBuffer.bind();
    sphere_.instanceBuffer.allocate(atomInstances.data(),
                                    static_cast<int>(atomInstances.size() * sizeof(float)));

    cylinder_.instanceCount = static_cast<int>(bondInstances.size()) / kFloatsPerInstance;
    cylinder_.instanceBuffer.bind();
    cylinder_.instanceBuffer.allocate(bondInstances.data(),
                                      static_cast<int>(bondInstances.size() * sizeof(float)));

    cone_.instanceCount = static_cast<int>(coneInstances.size()) / kFloatsPerInstance;
    cone_.instanceBuffer.bind();
    cone_.instanceBuffer.allocate(coneInstances.data(),
                                  static_cast<int>(coneInstances.size() * sizeof(float)));

    uploadColoredBuffer(wireBonds_, wireBondVertices);
    uploadColoredBuffer(wireAtoms_, wireAtomVertices);

    // Coordination polyhedra (Polyhedral mode only).
    std::vector<float> polyFaceVertices;
    std::vector<float> polyEdgeVertices;
    if (polyhedral)
        buildPolyhedra(structure, selection, polyFaceVertices, polyEdgeVertices);
    // Built after the replication block above, so they are repeated here — a
    // coordination polyhedron or a molecular surface is part of the cell's
    // contents like everything else.
    replicateStream(polyFaceVertices, /*stride=*/6, /*offset=*/0, cellShifts);
    replicateStream(polyEdgeVertices, /*stride=*/6, /*offset=*/0, cellShifts);
    uploadColoredBuffer(polyhedronFaces_, polyFaceVertices);
    uploadColoredBuffer(polyhedronEdges_, polyEdgeVertices);

    // Molecular surface (MolecularSurface mode only).
    std::vector<float> surfaceVertices;
    if (surface)
        buildMolecularSurface(structure, casts, surfaceVertices);
    replicateStream(surfaceVertices, /*stride=*/6, /*offset=*/0, cellShifts);
    uploadColoredBuffer(molecularSurface_, surfaceVertices);

    cellTube_.instanceCount =
        static_cast<int>(cellTubeInstances.size()) / kFloatsPerInstance;
    cellTube_.instanceBuffer.bind();
    cellTube_.instanceBuffer.allocate(
        cellTubeInstances.data(),
        static_cast<int>(cellTubeInstances.size() * sizeof(float)));

    uploadLitBuffer(cellFaces_, cellFaceVertices);
}

void StructureRenderer::uploadLights()
{
    QVector3D directions[kMaxLights];
    QVector3D ambient[kMaxLights];
    QVector3D diffuse[kMaxLights];
    QVector3D specular[kMaxLights];

    const int count = std::min<int>(kMaxLights, static_cast<int>(lights_.size()));
    for (int i = 0; i < count; ++i) {
        const Light& light = lights_[static_cast<std::size_t>(i)];
        directions[i] = light.direction.normalized();
        ambient[i] = {static_cast<float>(light.ambient.redF()),
                      static_cast<float>(light.ambient.greenF()),
                      static_cast<float>(light.ambient.blueF())};
        diffuse[i] = {static_cast<float>(light.diffuse.redF()),
                      static_cast<float>(light.diffuse.greenF()),
                      static_cast<float>(light.diffuse.blueF())};
        specular[i] = {static_cast<float>(light.specular.redF()),
                       static_cast<float>(light.specular.greenF()),
                       static_cast<float>(light.specular.blueF())};
    }

    meshProgram_.setUniformValue("uLightCount", count);
    meshProgram_.setUniformValueArray("uLightDir", directions, kMaxLights);
    meshProgram_.setUniformValueArray("uLightAmbient", ambient, kMaxLights);
    meshProgram_.setUniformValueArray("uLightDiffuse", diffuse, kMaxLights);
    meshProgram_.setUniformValueArray("uLightSpecular", specular, kMaxLights);
    meshProgram_.setUniformValue("uShininess", 48.0f);
    // The surface finish is no longer a uniform — it rides in the instance
    // buffer, one value per cast (see appendInstance / mesh.vert's iFinish).
    meshProgram_.setUniformValue("uSurfaceOpacity", style_.glassOpacity);

    meshProgram_.setUniformValue("uFogMode", style_.fogMode);
    meshProgram_.setUniformValue(
        "uFogColor",
        QVector3D(static_cast<float>(style_.fogColor.redF()),
                  static_cast<float>(style_.fogColor.greenF()),
                  static_cast<float>(style_.fogColor.blueF())));
    meshProgram_.setUniformValue("uFogStart", style_.fogStart);
    meshProgram_.setUniformValue("uFogEnd", style_.fogEnd);
    meshProgram_.setUniformValue("uFogDensity", style_.fogDensity);

    // Shadow lookup. Bound here so every mesh-program pass (structure meshes
    // and the cell tubes) receives a consistent set.
    meshProgram_.setUniformValue("uLightSpace", lightSpace_);
    meshProgram_.setUniformValue("uShadowEnabled", shadowsActive_ ? 1 : 0);
    meshProgram_.setUniformValue("uShadowStrength", style_.shadowStrength);
    meshProgram_.setUniformValue("uShadowRadius", style_.shadowSoftness);
    meshProgram_.setUniformValue("uShadowTexelSize",
                                 1.0f / static_cast<float>(kShadowMapSize));
    meshProgram_.setUniformValue("uShadowMap", 0); // texture unit 0
    // ALWAYS bind something complete here, not just when shadows are on: the
    // sampler points at unit 0 on every draw, and an empty unit is what the
    // driver complains about. The post-process passes also leave their own
    // textures on unit 0, so this re-establishes a known one each pass.
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D,
                       shadowsActive_ ? shadowTexture_ : dummyTexture_);
}

QMatrix4x4 StructureRenderer::lightSpaceMatrix() const
{
    // Lights are stored in VIEW space (they follow the camera), but the shadow
    // map must be built in WORLD space or the shadows would swim as the user
    // orbits. The caller passes the view matrix in via render(); here the
    // primary light direction is taken as-is and the frustum is fitted around
    // the scene sphere, so the projection stays tight whatever the model size.
    const QVector3D direction =
        lights_.empty() ? QVector3D(-0.4f, -0.5f, -1.0f)
                        : lights_.front().direction.normalized();
    const QVector3D safeDirection =
        direction.isNull() ? QVector3D(0.0f, 0.0f, -1.0f) : direction.normalized();

    // Place the light outside the scene sphere, looking at its center.
    const QVector3D eye = sceneCenter_ - safeDirection * (sceneRadius_ * 2.0f);
    // Any up vector not parallel to the light direction works; swap axes when
    // the light is near-vertical so the cross product never degenerates.
    const QVector3D up = std::abs(safeDirection.y()) > 0.95f
        ? QVector3D(0.0f, 0.0f, 1.0f)
        : QVector3D(0.0f, 1.0f, 0.0f);

    QMatrix4x4 view;
    view.lookAt(eye, sceneCenter_, up);

    QMatrix4x4 projection;
    // Orthographic: a directional light has parallel rays. The near/far span
    // brackets the whole sphere from the eye placed above.
    projection.ortho(-sceneRadius_, sceneRadius_, -sceneRadius_, sceneRadius_,
                     0.05f, sceneRadius_ * 4.0f);
    return projection * view;
}

bool StructureRenderer::ensureShadowTarget()
{
    if (shadowFbo_ != 0)
        return true;
    if (!gl_)
        return false;

    gl_->glGenTextures(1, &shadowTexture_);
    gl_->glBindTexture(GL_TEXTURE_2D, shadowTexture_);
    gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowMapSize,
                      kShadowMapSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Clamp to a white (fully lit) border so geometry sampling outside the
    // map is never spuriously shadowed.
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    gl_->glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    gl_->glGenFramebuffers(1, &shadowFbo_);
    gl_->glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    gl_->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_TEXTURE_2D, shadowTexture_, 0);
    // Depth-only: no color attachment exists, so both buffers must be off or
    // the FBO is incomplete.
    gl_->glDrawBuffer(GL_NONE);
    gl_->glReadBuffer(GL_NONE);
    const bool complete =
        gl_->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl_->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        // Leave shadows disabled rather than rendering through a broken FBO.
        gl_->glDeleteFramebuffers(1, &shadowFbo_);
        gl_->glDeleteTextures(1, &shadowTexture_);
        shadowFbo_ = 0;
        shadowTexture_ = 0;
        return false;
    }
    return true;
}

void StructureRenderer::renderShadowMap(const QMatrix4x4& lightSpace)
{
    // The caller restores the previous framebuffer binding: inside a
    // QOpenGLWidget the "default" target is the widget's own FBO, not 0.
    GLint previousFbo = 0;
    gl_->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    GLint viewport[4] = {0, 0, 0, 0};
    gl_->glGetIntegerv(GL_VIEWPORT, viewport);

    // Break last frame's sampler binding FIRST. uploadLights() leaves the
    // shadow depth texture bound to unit 0 for the shading pass, and it is
    // still there when this runs again next frame — at which point the same
    // texture would be an attachment of the framebuffer about to be bound AND
    // a live sampler source. That is a framebuffer feedback loop: undefined by
    // the spec, and what makes a driver report the texture as unloadable.
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, dummyTexture_);

    gl_->glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    gl_->glViewport(0, 0, kShadowMapSize, kShadowMapSize);
    gl_->glClear(GL_DEPTH_BUFFER_BIT);
    // Front-face culling during the depth pass pushes the stored depth to the
    // back faces of solid geometry, which removes most self-shadowing acne on
    // the closed sphere/cylinder meshes used here.
    gl_->glEnable(GL_CULL_FACE);
    gl_->glCullFace(GL_FRONT);

    shadowProgram_.bind();
    shadowProgram_.setUniformValue("uLightSpace", lightSpace);
    for (InstancedMesh* mesh : {&sphere_, &cylinder_, &cone_, &cellTube_}) {
        if (mesh->instanceCount == 0)
            continue;
        mesh->vao.bind();
        gl_->glDrawElementsInstanced(GL_TRIANGLES, mesh->indexCount,
                                     GL_UNSIGNED_INT, nullptr,
                                     mesh->instanceCount);
        mesh->vao.release();
    }
    shadowProgram_.release();

    gl_->glCullFace(GL_BACK);
    gl_->glDisable(GL_CULL_FACE);
    gl_->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
    gl_->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void StructureRenderer::render(const QMatrix4x4& view, const QMatrix4x4& projection)
{
    if (!initialized_)
        return;

    // What is drawn is decided by what the geometry build produced, not by the
    // style's mode: with casts in play a single scene can hold solid spheres
    // AND wireframe lines AND polyhedra at once, so each pass runs whenever its
    // own buffers are non-empty.
    const bool hasMeshes = sphere_.instanceCount > 0 || cylinder_.instanceCount > 0
        || cone_.instanceCount > 0;
    const bool hasWires =
        wireBonds_.vertexCount > 0 || wireAtoms_.vertexCount > 0;

    // Depth pass first: the shadow map must exist before the shading pass
    // samples it. A purely wireframe scene has no solid geometry to occlude
    // anything, so it skips the whole thing.
    QMatrix4x4 lightSpace;
    bool shadowsActive = false;
    if (style_.shadowsEnabled && hasMeshes && ensureShadowTarget()) {
        lightSpace = lightSpaceMatrix();
        renderShadowMap(lightSpace);
        shadowsActive = true;
    }
    shadowsActive_ = shadowsActive;
    lightSpace_ = lightSpace;

    if (hasWires) {
        wireProgram_.bind();
        wireProgram_.setUniformValue("uMvp", projection * view);
        wireProgram_.setUniformValue("uAlpha", 1.0f);
        if (wireBonds_.vertexCount > 0) {
            wireBonds_.vao.bind();
            gl_->glDrawArrays(GL_LINES, 0, wireBonds_.vertexCount);
            wireBonds_.vao.release();
        }
        if (wireAtoms_.vertexCount > 0) {
            wireAtoms_.vao.bind();
            gl_->glDrawArrays(GL_POINTS, 0, wireAtoms_.vertexCount);
            wireAtoms_.vao.release();
        }
        wireProgram_.release();
    }
    if (hasMeshes) {
        meshProgram_.bind();
        meshProgram_.setUniformValue("uView", view);
        meshProgram_.setUniformValue("uProj", projection);
        uploadLights();

        // Glassy is the only translucent finish, and with per-cast finishes a
        // scene can hold both kinds at once. So the instances are drawn TWICE
        // over the same buffers, with the fragment shader discarding whichever
        // kind the pass does not own:
        //
        //   pass 0 — opaque instances, depth writes ON (an opaque cast must
        //            still occlude what is behind it);
        //   pass 1 — glassy instances, blended with depth writes OFF, so
        //            spheres behind other spheres stay visible.
        //
        // A single pass with blending forced on for everything (what a global
        // finish could get away with) would silently strip the opaque casts of
        // their self-occlusion. Order-independent transparency is out of scope;
        // the Fresnel rim in mesh.frag is what keeps the unsorted glassy draw
        // readable.
        // Each primitive is drawn by whichever profile its slot selects.
        // Spheres and cylinders have an impostor form; the vector-arrow cones
        // do not — an arrowhead is a one-off shape with no analytic
        // intersection worth writing — so they always take the tessellated
        // path. The caller has meshProgram_ bound and its uFinishPass set;
        // this restores that state on the way out so the two passes below can
        // treat it as unchanged.
        const auto drawMeshes = [this, &view, &projection, &lightSpace](
                                    int finishPass) {
            struct Entry {
                InstancedMesh* mesh;
                int slot; // render::ShaderSlot index, or -1 for "no impostor"
            };
            const Entry entries[] = {
                {&sphere_, 0}, {&cylinder_, 1}, {&cone_, -1}};
            for (const Entry& entry : entries) {
                InstancedMesh* mesh = entry.mesh;
                if (mesh->instanceCount == 0)
                    continue;
                const bool impostor =
                    entry.slot >= 0 && useImpostors(entry.slot);
                if (!impostor) {
                    mesh->vao.bind();
                    gl_->glDrawElementsInstanced(GL_TRIANGLES, mesh->indexCount,
                                                 GL_UNSIGNED_INT, nullptr,
                                                 mesh->instanceCount);
                    mesh->vao.release();
                    continue;
                }
                QOpenGLShaderProgram& program = impostorProgram_[entry.slot];
                meshProgram_.release();
                program.bind();
                uploadImpostorUniforms(program, view, projection, lightSpace);
                program.setUniformValue(
                    "uShadingModel",
                    ShaderRegistry::activeProfile(
                        entry.slot == 0 ? ShaderSlot::Atoms : ShaderSlot::Bonds)
                        .shadingModel);
                program.setUniformValue("uFinishPass", finishPass);
                mesh->impostorVao.bind();
                // Two triangles per instance instead of 1200 (spheres) or 48
                // (cylinders) — the whole point of the profile.
                gl_->glDrawElementsInstanced(GL_TRIANGLES, mesh->quadIndexCount,
                                             GL_UNSIGNED_INT, nullptr,
                                             mesh->instanceCount);
                mesh->impostorVao.release();
                program.release();
                meshProgram_.bind();
            }
        };

        meshProgram_.setUniformValue("uFinishPass", 0);
        drawMeshes(0);

        if (style_.anyTranslucentCast()) {
            meshProgram_.setUniformValue("uFinishPass", 1);
            // DEPTH PRE-PASS over the translucent instances, colour writes off.
            //
            // Without it the blended pass is drawn in buffer order — every
            // sphere, then every cylinder — with depth writes off, so the bonds
            // paint straight over the atoms they end inside and the model turns
            // into a flat tangle the moment the opacity leaves 1.0 (a fully
            // opaque scene never showed it, because the opaque pass depth-tests
            // normally). Establishing the nearest translucent surface here and
            // testing against it below makes the blend order-independent: each
            // pixel blends its front-most translucent fragment over whatever the
            // opaque pass already put in the colour buffer, whichever order the
            // instances happen to arrive in.
            gl_->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            drawMeshes(1);
            gl_->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

            gl_->glEnable(GL_BLEND);
            gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            gl_->glDepthMask(GL_FALSE);
            // LEQUAL, not LESS: the fragments that survive are exactly the ones
            // the pre-pass just stored, so they must compare equal and pass.
            gl_->glDepthFunc(GL_LEQUAL);
            drawMeshes(1);
            gl_->glDepthFunc(GL_LESS);
            gl_->glDepthMask(GL_TRUE);
            gl_->glDisable(GL_BLEND);
        }
        meshProgram_.release();
    }

    // Molecular surface: a solid envelope over whatever else is drawn. Opaque
    // and depth-written — it is the outer shape of the molecule, and making it
    // see-through would defeat the representation.
    if (molecularSurface_.vertexCount > 0) {
        wireProgram_.bind();
        wireProgram_.setUniformValue("uMvp", projection * view);
        wireProgram_.setUniformValue("uAlpha", 1.0f);
        molecularSurface_.vao.bind();
        gl_->glDrawArrays(GL_TRIANGLES, 0, molecularSurface_.vertexCount);
        molecularSurface_.vao.release();
        wireProgram_.release();
    }

    // Coordination polyhedra: translucent hull faces over the opaque spheres,
    // then their solid edge outline. Faces blend without writing depth (no
    // order-independent sort here) so overlapping polyhedra stay readable;
    // edges write depth normally so the wireframe reads crisply.
    if (polyhedronFaces_.vertexCount > 0 || polyhedronEdges_.vertexCount > 0) {
        wireProgram_.bind();
        wireProgram_.setUniformValue("uMvp", projection * view);
        if (polyhedronFaces_.vertexCount > 0) {
            wireProgram_.setUniformValue("uAlpha", style_.polyhedronOpacity);
            gl_->glEnable(GL_BLEND);
            gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            gl_->glDepthMask(GL_FALSE);
            polyhedronFaces_.vao.bind();
            gl_->glDrawArrays(GL_TRIANGLES, 0, polyhedronFaces_.vertexCount);
            polyhedronFaces_.vao.release();
            gl_->glDepthMask(GL_TRUE);
            gl_->glDisable(GL_BLEND);
        }
        if (style_.polyhedronEdges && polyhedronEdges_.vertexCount > 0) {
            wireProgram_.setUniformValue("uAlpha", 1.0f);
            gl_->glLineWidth(style_.polyhedronEdgeWidth);
            polyhedronEdges_.vao.bind();
            gl_->glDrawArrays(GL_LINES, 0, polyhedronEdges_.vertexCount);
            polyhedronEdges_.vao.release();
            gl_->glLineWidth(1.0f);
        }
        wireProgram_.release();
    }

    // Filled unit cell, drawn BEFORE the wireframe so the edges land on top of
    // their own fill instead of being half-swallowed by it. Same unlit,
    // depth-write-off blend as the polyhedra faces: the box encloses the
    // structure, so it must never occlude the atoms inside it.
    //
    // Gated on fillCell alone, not on showCell: the fill and the edges are two
    // independent depictions of one box, and a solid block with no wireframe is
    // as legitimate a figure as the wireframe with no fill.
    if (style_.fillCell && cellFaces_.vertexCount > 0) {
        // Lit, like every other surface in the scene. The buffer's 9-float
        // layout is a strict superset of the 6-float one, so the unlit program
        // remains a valid reader of it — which is the fallback when a driver
        // rejects the lit shader.
        const bool lit = ensureLitSurfaceProgram();
        QOpenGLShaderProgram& program =
            lit ? isosurfaceProgram_ : wireProgram_;
        program.bind();
        if (lit) {
            program.setUniformValue("uView", view);
            program.setUniformValue("uProj", projection);
            uploadIsosurfaceLights();
            // Diffuse, explicitly — NOT style_.isoShadingMode.
            //
            // That field is the Edit Volumetric Render dialog's setting and
            // defaults to Flat, which makes this shader emit the raw colour and
            // no shading at all. Inheriting it would mean the cell is lit only
            // when someone happens to have changed an isosurface preference,
            // which is not a relationship either setting should have with the
            // other. Diffuse rather than glossy: a specular highlight sliding
            // across a large flat face reads as a reflective slab, and the cell
            // is meant to sit behind the structure.
            constexpr int kShadingDiffuse = 1;
            program.setUniformValue("uShadingMode", kShadingDiffuse);
            program.setUniformValue("uAmbient", style_.isoAmbient);
            program.setUniformValue("uSpecular", 0.0f);
            program.setUniformValue("uShininess", style_.isoShininess);
        } else {
            program.setUniformValue("uMvp", projection * view);
        }
        program.setUniformValue("uAlpha",
                                std::clamp(style_.cellFillAlpha, 0.0f, 1.0f));
        gl_->glEnable(GL_BLEND);
        gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl_->glDepthMask(GL_FALSE);
        cellFaces_.vao.bind();
        gl_->glDrawArrays(GL_TRIANGLES, 0, cellFaces_.vertexCount);
        cellFaces_.vao.release();
        gl_->glDepthMask(GL_TRUE);
        gl_->glDisable(GL_BLEND);
        program.release();
    }

    if (style_.showCell && cellTube_.instanceCount > 0) {
        // Thick wireframe: lit tubes (independent of representation mode).
        meshProgram_.bind();
        meshProgram_.setUniformValue("uView", view);
        meshProgram_.setUniformValue("uProj", projection);
        uploadLights();
        const auto drawCellTubes = [this] {
            cellTube_.vao.bind();
            gl_->glDrawElementsInstanced(GL_TRIANGLES, cellTube_.indexCount,
                                         GL_UNSIGNED_INT, nullptr,
                                         cellTube_.instanceCount);
            cellTube_.vao.release();
        };
        // uFinishPass MUST be set here, not inherited. The atom/bond passes
        // above leave it at 1 whenever anything in the scene is translucent,
        // and mesh.frag discards every opaque instance in that pass — which is
        // why dialling a cast's opacity down to 0.95 used to make the whole
        // unit cell disappear. The cell is scene furniture and carries no cast
        // opacity, so it belongs in the opaque pass.
        meshProgram_.setUniformValue("uFinishPass", 0);
        drawCellTubes();
        // ...unless the shared surface finish is Glassy, which IS translucent
        // and would be discarded by the opaque pass instead.
        if (style_.surfaceFinish == SurfaceFinish::Glassy) {
            meshProgram_.setUniformValue("uFinishPass", 1);
            gl_->glEnable(GL_BLEND);
            gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            gl_->glDepthMask(GL_FALSE);
            drawCellTubes();
            gl_->glDepthMask(GL_TRUE);
            gl_->glDisable(GL_BLEND);
        }
        meshProgram_.release();
    }

    // Interactive Lattice Plane overlay: a translucent, per-vertex-colored quad
    // (Miller-index plane / volumetric color-slice) drawn last so it blends
    // over the opaque scene. Same unlit per-vertex-color path as the polyhedra
    // faces; blend without writing depth so it never occludes the atoms behind.
    if (latticePlaneVisible_ && latticePlaneFaces_.vertexCount > 0) {
        wireProgram_.bind();
        wireProgram_.setUniformValue("uMvp", projection * view);
        wireProgram_.setUniformValue("uAlpha", latticePlaneAlpha_);
        gl_->glEnable(GL_BLEND);
        gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl_->glDepthMask(GL_FALSE);
        latticePlaneFaces_.vao.bind();
        gl_->glDrawArrays(GL_TRIANGLES, 0, latticePlaneFaces_.vertexCount);
        latticePlaneFaces_.vao.release();
        gl_->glDepthMask(GL_TRUE);
        gl_->glDisable(GL_BLEND);
        if (latticePlaneEdgesOn_ && latticePlaneEdges_.vertexCount > 0) {
            wireProgram_.setUniformValue("uAlpha", 1.0f);
            latticePlaneEdges_.vao.bind();
            gl_->glDrawArrays(GL_LINES, 0, latticePlaneEdges_.vertexCount);
            latticePlaneEdges_.vao.release();
        }
        wireProgram_.release();
    }

    // Direct volume rendering, before the translucent overlays so an
    // isosurface drawn at the same time blends over the medium rather than
    // being swallowed by it.
    drawVolume(view, projection);

    // Custom Overlay primitives: each face run blends at its own opacity, so
    // draw them range by range (opaque ones first would be ideal, but convex
    // primitives read fine without a global sort). Wireframes render opaque.
    if (customOverlayVisible_
        && (customOverlayFaces_.vertexCount > 0
            || customOverlayEdges_.vertexCount > 0)) {
        if (customOverlayFaces_.vertexCount > 0 && !customOverlayRanges_.empty()) {
            // Which program shades the surface is the Preferences "Rendering"
            // selection. The lit profile needs the scene lights and the view
            // matrix; the legacy one only needs the MVP, because its shading
            // was already baked into the vertex colours.
            const bool lit = useLitIsosurface();
            QOpenGLShaderProgram& program = lit ? isosurfaceProgram_ : wireProgram_;
            program.bind();
            if (lit) {
                program.setUniformValue("uView", view);
                program.setUniformValue("uProj", projection);
                uploadIsosurfaceLights();
                program.setUniformValue("uShadingMode", style_.isoShadingMode);
                program.setUniformValue("uAmbient", style_.isoAmbient);
                program.setUniformValue("uSpecular", style_.isoSpecular);
                program.setUniformValue("uShininess", style_.isoShininess);
            } else {
                program.setUniformValue("uMvp", projection * view);
            }
            gl_->glEnable(GL_BLEND);
            gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            gl_->glDepthMask(GL_FALSE);
            customOverlayFaces_.vao.bind();
            for (const OverlayRange& r : customOverlayRanges_) {
                if (r.count <= 0 || r.first < 0
                    || r.first + r.count > customOverlayFaces_.vertexCount)
                    continue;
                program.setUniformValue("uAlpha", r.alpha);
                gl_->glDrawArrays(GL_TRIANGLES, r.first, r.count);
            }
            customOverlayFaces_.vao.release();
            gl_->glDepthMask(GL_TRUE);
            gl_->glDisable(GL_BLEND);
            program.release();
        }
        wireProgram_.bind();
        wireProgram_.setUniformValue("uMvp", projection * view);
        if (customOverlayEdges_.vertexCount > 0) {
            // Translucent wires (the volumetric mesh/dot styles) blend without
            // writing depth, exactly as the faces above do; an opaque outline
            // takes the plain path and keeps occluding what is behind it.
            const bool blendEdges = customOverlayEdgeAlpha_ < 1.0f;
            if (blendEdges) {
                gl_->glEnable(GL_BLEND);
                gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                gl_->glDepthMask(GL_FALSE);
            }
            wireProgram_.setUniformValue("uAlpha", customOverlayEdgeAlpha_);
            customOverlayEdges_.vao.bind();
            gl_->glDrawArrays(GL_LINES, 0, customOverlayEdges_.vertexCount);
            customOverlayEdges_.vao.release();
            if (blendEdges) {
                gl_->glDepthMask(GL_TRUE);
                gl_->glDisable(GL_BLEND);
            }
        }
        wireProgram_.release();
    }

    // "Additional overlays" dock geometry. Identical treatment to the custom
    // overlay above — per-range alpha, depth-write off, opaque edges — but out
    // of its own buffers, so the two cannot overwrite each other.
    if (managedOverlayVisible_
        && (managedOverlayFaces_.vertexCount > 0
            || managedOverlayEdges_.vertexCount > 0)) {
        wireProgram_.bind();
        wireProgram_.setUniformValue("uMvp", projection * view);
        if (managedOverlayFaces_.vertexCount > 0
            && !managedOverlayRanges_.empty()) {
            gl_->glEnable(GL_BLEND);
            gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            gl_->glDepthMask(GL_FALSE);
            managedOverlayFaces_.vao.bind();
            for (const OverlayRange& r : managedOverlayRanges_) {
                if (r.count <= 0 || r.first < 0
                    || r.first + r.count > managedOverlayFaces_.vertexCount)
                    continue;
                wireProgram_.setUniformValue("uAlpha", r.alpha);
                gl_->glDrawArrays(GL_TRIANGLES, r.first, r.count);
            }
            managedOverlayFaces_.vao.release();
            gl_->glDepthMask(GL_TRUE);
            gl_->glDisable(GL_BLEND);
        }
        if (managedOverlayEdges_.vertexCount > 0) {
            wireProgram_.setUniformValue("uAlpha", 1.0f);
            managedOverlayEdges_.vao.bind();
            gl_->glDrawArrays(GL_LINES, 0, managedOverlayEdges_.vertexCount);
            managedOverlayEdges_.vao.release();
        }
        wireProgram_.release();
    }

    // Hydrogen bonds: dashed lines, drawn last so they read on top of the
    // covalent geometry they connect. The dashes are baked into the vertex
    // stream (core-profile GL has no line stipple). Every dash starts on a
    // hydrogen, so hiding the hydrogens hides these with them — the alternative
    // is a dash floating away from an acceptor toward nothing.
    if (hydrogenBonds_.vertexCount > 0 && style_.showHydrogens) {
        wireProgram_.bind();
        wireProgram_.setUniformValue("uMvp", projection * view);
        wireProgram_.setUniformValue("uAlpha", 1.0f);
        hydrogenBonds_.vao.bind();
        gl_->glDrawArrays(GL_LINES, 0, hydrogenBonds_.vertexCount);
        hydrogenBonds_.vao.release();
        wireProgram_.release();
    }
}

} // namespace calango::render
