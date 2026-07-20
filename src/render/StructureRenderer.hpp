#pragma once

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>

namespace calango::core {
class Structure;
}

class QOpenGLFunctions_3_3_Core;

namespace calango::render {

/// Draws a core::Structure with instanced meshes:
///   - atoms  -> instanced unit spheres  (ball-and-stick scaling)
///   - bonds  -> two instanced half-cylinders, colored per atom
///   - cell   -> 12 wireframe edges
///
/// Strictly a View in MVC terms: it holds no reference to the Structure,
/// only GPU buffers derived from it. Call setStructure() again after the
/// model changes (a current GL context is required).
class StructureRenderer {
public:
    struct Style {
        float atomScale = 0.4f;      ///< fraction of covalent radius
        float bondRadius = 0.12f;    ///< Å
        float bondTolerance = 1.15f; ///< bond-detection cutoff factor
    };

    /// Must be called once with a current GL context (from initializeGL).
    void initialize(QOpenGLFunctions_3_3_Core* gl);

    /// Rebuild instance buffers from the model. nullptr clears the scene.
    void setStructure(const core::Structure* structure);

    void render(const QMatrix4x4& view, const QMatrix4x4& projection);

    Style& style() { return style_; }

private:
    struct InstancedMesh {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vertexBuffer{QOpenGLBuffer::VertexBuffer};
        QOpenGLBuffer indexBuffer{QOpenGLBuffer::IndexBuffer};
        QOpenGLBuffer instanceBuffer{QOpenGLBuffer::VertexBuffer};
        int indexCount = 0;
        int instanceCount = 0;
    };

    void createMesh(InstancedMesh& mesh,
                    const std::vector<float>& vertices,
                    const std::vector<unsigned int>& indices);

    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    bool initialized_ = false;
    Style style_;

    QOpenGLShaderProgram meshProgram_;
    QOpenGLShaderProgram lineProgram_;

    InstancedMesh sphere_;
    InstancedMesh cylinder_;

    QOpenGLVertexArrayObject cellVao_;
    QOpenGLBuffer cellVbo_{QOpenGLBuffer::VertexBuffer};
    int cellVertexCount_ = 0;
};

} // namespace calango::render
