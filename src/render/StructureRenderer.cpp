#include "render/StructureRenderer.hpp"

#include "core/Structure.hpp"

#include <QOpenGLFunctions_3_3_Core>
#include <QQuaternion>
#include <QVector3D>
#include <QtMath>

#include <vector>

namespace calango::render {

namespace {

constexpr int kFloatsPerInstance = 20; // mat4 (16) + rgba (4)

void appendInstance(std::vector<float>& data, const QMatrix4x4& model,
                    float r, float g, float b, float a = 1.0f)
{
    const float* m = model.constData(); // column-major, matching the shader
    data.insert(data.end(), m, m + 16);
    data.insert(data.end(), {r, g, b, a});
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
/// End caps are unnecessary — sphere instances cover the joints.
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

float StructureRenderer::displayRadius(int atomicNumber, const Style& style)
{
    return std::max(0.2f, core::Elements::data(atomicNumber).covalentRadius * style.atomScale);
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

    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    buildSphere(20, 30, vertices, indices);
    createMesh(sphere_, vertices, indices);

    vertices.clear();
    indices.clear();
    buildCylinder(24, vertices, indices);
    createMesh(cylinder_, vertices, indices);

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

    // Per-instance data: locations 2..5 = mat4 columns, 6 = rgba color.
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
    gl_->glEnableVertexAttribArray(6);
    gl_->glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride,
                               reinterpret_cast<void*>(sizeof(float) * 16));
    gl_->glVertexAttribDivisor(6, 1);

    mesh.vao.release();
}

void StructureRenderer::setStructure(const core::Structure* structure,
                                     const std::set<int>* selection)
{
    if (!initialized_)
        return;

    std::vector<float> atomInstances;
    std::vector<float> bondInstances;
    std::vector<float> cellVertices;

    if (structure && !structure->empty()) {
        const auto& atoms = structure->atoms();
        atomInstances.reserve(atoms.size() * kFloatsPerInstance);

        for (std::size_t index = 0; index < atoms.size(); ++index) {
            const core::Atom& atom = atoms[index];
            const auto& element = core::Elements::data(atom.atomicNumber);
            const bool selected =
                selection && selection->count(static_cast<int>(index)) > 0;

            QMatrix4x4 model;
            model.translate(toQt(atom.position));
            model.scale(displayRadius(atom.atomicNumber, style_) * (selected ? 1.2f : 1.0f));

            float r = element.rgb[0] / 255.0f;
            float g = element.rgb[1] / 255.0f;
            float b = element.rgb[2] / 255.0f;
            if (selected) { // tint toward highlight orange
                r = 0.45f * r + 0.55f * 1.00f;
                g = 0.45f * g + 0.55f * 0.62f;
                b = 0.45f * b + 0.55f * 0.10f;
            }
            appendInstance(atomInstances, model, r, g, b);
        }

        for (const core::Bond& bond : structure->detectBonds(style_.bondTolerance)) {
            const auto& a = atoms[static_cast<std::size_t>(bond.i)];
            const auto& b = atoms[static_cast<std::size_t>(bond.j)];
            const QVector3D pa = toQt(a.position);
            const QVector3D pbImage = toQt(b.position + bond.imageOffset);
            const QVector3D dir = (pbImage - pa).normalized();
            const float half = pa.distanceToPoint(pbImage) * 0.5f;

            const auto& ea = core::Elements::data(a.atomicNumber);
            const auto& eb = core::Elements::data(b.atomicNumber);
            appendInstance(bondInstances,
                           bondTransform(pa, dir, half, style_.bondRadius),
                           ea.rgb[0] / 255.0f, ea.rgb[1] / 255.0f, ea.rgb[2] / 255.0f);
            if (!bond.crossesBoundary()) {
                appendInstance(bondInstances,
                               bondTransform(pa + dir * half, dir, half, style_.bondRadius),
                               eb.rgb[0] / 255.0f, eb.rgb[1] / 255.0f, eb.rgb[2] / 255.0f);
            } else {
                // Wrapped bond: draw atom j's half as a stub pointing back
                // toward its own periodic image of atom i.
                appendInstance(bondInstances,
                               bondTransform(toQt(b.position), -dir, half, style_.bondRadius),
                               eb.rgb[0] / 255.0f, eb.rgb[1] / 255.0f, eb.rgb[2] / 255.0f);
            }
        }

        if (structure->cell().isDefined()) {
            const auto corners = structure->cell().corners();
            for (const auto& [i, j] : core::UnitCell::edges()) {
                for (const int corner : {i, j}) {
                    const auto& p = corners[static_cast<std::size_t>(corner)];
                    cellVertices.insert(cellVertices.end(),
                                        {static_cast<float>(p.x), static_cast<float>(p.y),
                                         static_cast<float>(p.z)});
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

    cellVertexCount_ = static_cast<int>(cellVertices.size()) / 3;
    cellVbo_.bind();
    cellVbo_.allocate(cellVertices.data(),
                      static_cast<int>(cellVertices.size() * sizeof(float)));
}

void StructureRenderer::render(const QMatrix4x4& view, const QMatrix4x4& projection)
{
    if (!initialized_)
        return;

    meshProgram_.bind();
    meshProgram_.setUniformValue("uView", view);
    meshProgram_.setUniformValue("uProj", projection);
    // Headlight slightly above and to the right of the camera.
    meshProgram_.setUniformValue("uLightDirView",
                                 QVector3D(-0.4f, -0.5f, -1.0f).normalized());

    for (const InstancedMesh* mesh : {&sphere_, &cylinder_}) {
        if (mesh->instanceCount == 0)
            continue;
        const_cast<InstancedMesh*>(mesh)->vao.bind();
        gl_->glDrawElementsInstanced(GL_TRIANGLES, mesh->indexCount, GL_UNSIGNED_INT,
                                     nullptr, mesh->instanceCount);
        const_cast<InstancedMesh*>(mesh)->vao.release();
    }
    meshProgram_.release();

    if (style_.showCell && cellVertexCount_ > 0) {
        lineProgram_.bind();
        lineProgram_.setUniformValue("uMvp", projection * view);
        lineProgram_.setUniformValue("uColor", QVector4D(0.65f, 0.65f, 0.7f, 1.0f));
        cellVao_.bind();
        gl_->glDrawArrays(GL_LINES, 0, cellVertexCount_);
        cellVao_.release();
        lineProgram_.release();
    }
}

} // namespace calango::render
