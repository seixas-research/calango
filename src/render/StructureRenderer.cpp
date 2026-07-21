#include "render/StructureRenderer.hpp"

#include "core/Structure.hpp"

#include <QOpenGLFunctions_3_3_Core>
#include <QQuaternion>
#include <QtMath>

#include <algorithm>
#include <utility>
#include <vector>

namespace calango::render {

namespace {

constexpr int kFloatsPerInstance = 24; // mat4 (16) + rgba (4) + rgba2 (4)

/// One instance record. `color` is sampled at the mesh's z = 0 end and
/// `color2` at z = 1 (mesh.vert interpolates axially — the bond gradient);
/// pass the same color twice for uniform meshes (spheres, cell tubes).
void appendInstance(std::vector<float>& data, const QMatrix4x4& model,
                    const QColor& color, const QColor& color2)
{
    const float* m = model.constData(); // column-major, matching the shader
    data.insert(data.end(), m, m + 16);
    data.insert(data.end(),
                {static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
                 static_cast<float>(color.blueF()), 1.0f});
    data.insert(data.end(),
                {static_cast<float>(color2.redF()), static_cast<float>(color2.greenF()),
                 static_cast<float>(color2.blueF()), 1.0f});
}

void appendInstance(std::vector<float>& data, const QMatrix4x4& model, const QColor& color)
{
    appendInstance(data, model, color, color);
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

QVector3D toQt(const calango::core::Vec3& v)
{
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
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
        indices.insert(indices.end(), {a, a + 1, a + 2, a + 2, a + 1, a + 3});
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
    // Base cap (facing -z).
    const auto capCenter = static_cast<unsigned int>(vertices.size() / 6);
    vertices.insert(vertices.end(), {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f});
    for (int j = 0; j <= segments; ++j) {
        const float theta = 2.0f * float(M_PI) * float(j) / float(segments);
        vertices.insert(vertices.end(),
                        {std::cos(theta), std::sin(theta), 0.0f, 0.0f, 0.0f, -1.0f});
    }
    for (unsigned int j = 0; j < static_cast<unsigned int>(segments); ++j)
        indices.insert(indices.end(),
                       {capCenter, capCenter + 1 + j, capCenter + 2 + j});
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

/// Any unit vector perpendicular to `axis` — the offset direction for
/// parallel cylinders of double/triple bonds. Camera-independent so the
/// instance buffer stays static while orbiting.
QVector3D perpendicularTo(const QVector3D& axis)
{
    const QVector3D reference = std::abs(axis.z()) < 0.9f
        ? QVector3D(0.0f, 0.0f, 1.0f)
        : QVector3D(1.0f, 0.0f, 0.0f);
    return QVector3D::crossProduct(axis, reference).normalized();
}

/// Lateral center offsets (in units of the single-bond radius) and the
/// per-cylinder radius shrink for a bond of the given order.
void multiBondLayout(int order, std::vector<float>& offsets, float& radiusScale)
{
    switch (std::clamp(order, 1, 3)) {
    case 2:
        offsets = {-0.8f, 0.8f};
        radiusScale = 0.55f;
        break;
    case 3:
        offsets = {-1.5f, 0.0f, 1.5f};
        radiusScale = 0.45f;
        break;
    default:
        offsets = {0.0f};
        radiusScale = 1.0f;
        break;
    }
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

    return {key, fill};
}

float StructureRenderer::displayRadius(int atomicNumber, const Style& style)
{
    const float covalent = core::Elements::data(atomicNumber).covalentRadius;
    float radius = 0.25f;
    switch (style.mode) {
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
    }
    float perElement = 1.0f;
    if (const auto it = style.radiusScaleOverrides.find(atomicNumber);
        it != style.radiusScaleOverrides.end())
        perElement = it->second;
    return radius * style.atomScaleFactor * perElement;
}

QColor StructureRenderer::atomColor(int atomicNumber, const Style& style)
{
    if (const auto it = style.colorOverrides.find(atomicNumber);
        it != style.colorOverrides.end())
        return it->second;
    const auto& element = core::Elements::data(atomicNumber);
    return QColor(element.rgb[0], element.rgb[1], element.rgb[2]);
}

void StructureRenderer::setAtomScalars(std::vector<float> scalars)
{
    atomScalars_ = std::move(scalars);
    scalarMin_ = 0.0f;
    scalarMax_ = 1.0f;
    if (!atomScalars_.empty()) {
        const auto [lo, hi] = std::minmax_element(atomScalars_.begin(), atomScalars_.end());
        scalarMin_ = *lo;
        scalarMax_ = *hi;
    }
}

QColor StructureRenderer::resolvedAtomColor(std::size_t index, int atomicNumber) const
{
    if (style_.colorMode == ColorMode::Element || index >= atomScalars_.size())
        return atomColor(atomicNumber, style_);
    // A flat field (all atoms identical) maps to the middle of the gradient.
    const float range = scalarMax_ - scalarMin_;
    const float t = range > 1e-12f ? (atomScalars_[index] - scalarMin_) / range : 0.5f;
    return ColorMap::sample(style_.gradient, t);
}

void StructureRenderer::initialize(QOpenGLFunctions_3_3_Core* gl)
{
    gl_ = gl;

    meshProgram_.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/assets/shaders/mesh.vert");
    meshProgram_.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/assets/shaders/mesh.frag");
    meshProgram_.link();

    lineProgram_.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/assets/shaders/line.vert");
    lineProgram_.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/assets/shaders/line.frag");
    lineProgram_.link();

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

    createColoredBuffer(wireBonds_);
    createColoredBuffer(wireAtoms_);

    cellVao_.create();
    cellVao_.bind();
    cellVbo_.create();
    cellVbo_.bind();
    gl_->glEnableVertexAttribArray(0);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    cellVao_.release();

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

    mesh.vao.release();
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

void StructureRenderer::setStructure(const core::Structure* structure,
                                     const std::set<int>* selection)
{
    if (!initialized_)
        return;

    std::vector<float> atomInstances;
    std::vector<float> bondInstances;
    std::vector<float> coneInstances;
    std::vector<float> wireBondVertices;
    std::vector<float> wireAtomVertices;
    std::vector<float> cellVertices;
    std::vector<float> cellTubeInstances;

    const bool wantBonds = style_.mode != RepresentationMode::SpaceFilling;
    const bool wireframe = style_.mode == RepresentationMode::Wireframe;

    if (structure && !structure->empty()) {
        const auto& atoms = structure->atoms();
        atomInstances.reserve(atoms.size() * kFloatsPerInstance);

        for (std::size_t index = 0; index < atoms.size(); ++index) {
            const core::Atom& atom = atoms[index];
            const bool selected =
                selection && selection->count(static_cast<int>(index)) > 0;

            QColor color = resolvedAtomColor(index, atom.atomicNumber);
            if (selected)
                color = selectionTint(color);

            if (wireframe) {
                appendColoredVertex(wireAtomVertices, toQt(atom.position), color);
            } else {
                QMatrix4x4 model;
                model.translate(toQt(atom.position));
                model.scale(displayRadius(atom.atomicNumber, style_)
                            * (selected ? 1.2f : 1.0f));
                appendInstance(atomInstances, model, color);
            }
        }

        if (wantBonds) {
            const float baseRadius = style_.bondRadius * style_.bondWidthFactor;
            for (const core::Bond& bond :
                 structure->detectBonds(style_.bondTolerance, style_.autoBonds)) {
                const auto& a = atoms[static_cast<std::size_t>(bond.i)];
                const auto& b = atoms[static_cast<std::size_t>(bond.j)];
                const QVector3D pa = toQt(a.position);
                const QVector3D pbReal = toQt(b.position);
                const QVector3D pbImage = toQt(b.position + bond.imageOffset);
                const QVector3D dir = (pbImage - pa).normalized();
                const float half = pa.distanceToPoint(pbImage) * 0.5f;

                QColor colorA = resolvedAtomColor(
                    static_cast<std::size_t>(bond.i), a.atomicNumber);
                QColor colorB = resolvedAtomColor(
                    static_cast<std::size_t>(bond.j), b.atomicNumber);
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
                    if (!bond.crossesBoundary()) {
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
                                   style_.gradientBonds ? mid : colorA);
                    if (!bond.crossesBoundary()) {
                        appendInstance(
                            bondInstances,
                            bondTransform(pa + shift + dir * half, dir, half, radius),
                            style_.gradientBonds ? mid : colorB, colorB);
                    } else {
                        // Wrapped bond: atom j's half is a stub pointing back
                        // toward its own periodic image of atom i (z = 0 sits
                        // at the atom, so the gradient runs atom -> midpoint).
                        appendInstance(
                            bondInstances,
                            bondTransform(pbReal + shift, -dir, half, radius),
                            colorB, style_.gradientBonds ? mid : colorB);
                    }
                }
            }
        }

        // Force / velocity arrows: shaft (cylinder) + head (cone) per atom,
        // sharing the lit instanced pipeline. Skipped in wireframe mode.
        if (!wireframe && (style_.showForces || style_.showVelocities)) {
            const auto addArrows = [&](const std::string& fieldName,
                                       const QColor& color) {
                const auto& fields = structure->vectorFields();
                const auto it = fields.find(fieldName);
                if (it == fields.end() || it->second.size() != atoms.size())
                    return;
                const float shaftRadius = 0.045f;
                for (std::size_t index = 0; index < atoms.size(); ++index) {
                    const core::Vec3& v = it->second[index];
                    const double length = v.norm() * style_.vectorScale;
                    if (length < 0.05)
                        continue; // invisible / zero vector
                    const QVector3D origin = toQt(atoms[index].position);
                    const QVector3D dir = toQt(v.normalized());
                    const auto headLength =
                        static_cast<float>(std::min(0.30 * length, 0.45));
                    const auto shaftLength =
                        static_cast<float>(length) - headLength;
                    appendInstance(bondInstances,
                                   bondTransform(origin, dir, shaftLength,
                                                 shaftRadius),
                                   color);
                    // Arrowhead cone: unit radius scales laterally via
                    // bondTransform's radius parameter.
                    appendInstance(coneInstances,
                                   bondTransform(origin + dir * shaftLength, dir,
                                                 headLength, shaftRadius * 2.6f),
                                   color);
                }
            };
            if (style_.showForces)
                addArrows("forces", style_.forceColor);
            if (style_.showVelocities)
                addArrows("velocities", style_.velocityColor);
        }

        if (structure->cell().isDefined()) {
            const auto corners = structure->cell().corners();
            const bool tubes = style_.cellLineWidth > 1.01f;
            const float tubeRadius = 0.015f * style_.cellLineWidth;
            for (const auto& [i, j] : core::UnitCell::edges()) {
                const QVector3D from = toQt(corners[static_cast<std::size_t>(i)]);
                const QVector3D to = toQt(corners[static_cast<std::size_t>(j)]);
                if (tubes) {
                    const QVector3D dir = (to - from).normalized();
                    appendInstance(cellTubeInstances,
                                   bondTransform(from, dir, from.distanceToPoint(to),
                                                 tubeRadius),
                                   style_.cellColor);
                } else {
                    for (const QVector3D& p : {from, to})
                        cellVertices.insert(cellVertices.end(), {p.x(), p.y(), p.z()});
                }
            }
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

    cellTube_.instanceCount =
        static_cast<int>(cellTubeInstances.size()) / kFloatsPerInstance;
    cellTube_.instanceBuffer.bind();
    cellTube_.instanceBuffer.allocate(
        cellTubeInstances.data(),
        static_cast<int>(cellTubeInstances.size() * sizeof(float)));

    cellVertexCount_ = static_cast<int>(cellVertices.size()) / 3;
    cellVbo_.bind();
    cellVbo_.allocate(cellVertices.data(),
                      static_cast<int>(cellVertices.size() * sizeof(float)));
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
}

void StructureRenderer::render(const QMatrix4x4& view, const QMatrix4x4& projection)
{
    if (!initialized_)
        return;

    if (style_.mode == RepresentationMode::Wireframe) {
        wireProgram_.bind();
        wireProgram_.setUniformValue("uMvp", projection * view);
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
    } else {
        meshProgram_.bind();
        meshProgram_.setUniformValue("uView", view);
        meshProgram_.setUniformValue("uProj", projection);
        uploadLights();

        for (InstancedMesh* mesh : {&sphere_, &cylinder_, &cone_}) {
            if (mesh->instanceCount == 0)
                continue;
            mesh->vao.bind();
            gl_->glDrawElementsInstanced(GL_TRIANGLES, mesh->indexCount, GL_UNSIGNED_INT,
                                         nullptr, mesh->instanceCount);
            mesh->vao.release();
        }
        meshProgram_.release();
    }

    if (style_.showCell && cellTube_.instanceCount > 0) {
        // Thick wireframe: lit tubes (independent of representation mode).
        meshProgram_.bind();
        meshProgram_.setUniformValue("uView", view);
        meshProgram_.setUniformValue("uProj", projection);
        uploadLights();
        cellTube_.vao.bind();
        gl_->glDrawElementsInstanced(GL_TRIANGLES, cellTube_.indexCount, GL_UNSIGNED_INT,
                                     nullptr, cellTube_.instanceCount);
        cellTube_.vao.release();
        meshProgram_.release();
    } else if (style_.showCell && cellVertexCount_ > 0) {
        lineProgram_.bind();
        lineProgram_.setUniformValue("uMvp", projection * view);
        lineProgram_.setUniformValue(
            "uColor", QVector4D(static_cast<float>(style_.cellColor.redF()),
                                static_cast<float>(style_.cellColor.greenF()),
                                static_cast<float>(style_.cellColor.blueF()), 1.0f));
        cellVao_.bind();
        gl_->glDrawArrays(GL_LINES, 0, cellVertexCount_);
        cellVao_.release();
        lineProgram_.release();
    }
}

} // namespace calango::render
