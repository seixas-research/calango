#pragma once

#include "core/Structure.hpp"
#include "render/Camera.hpp"
#include "render/StructureRenderer.hpp"

#include <QColor>
#include <QString>

#include <vector>

namespace calango::render {

/// Generates ray-tracer scene files from the *active viewport scene*:
/// same atom radii, colors, multi-bond layout, cell wireframe, camera pose
/// and lights as the OpenGL view — so the ray-traced image matches what is
/// on screen, at publication quality.
///
/// Supported backends: POV-Ray (.pov) and Tachyon (.dat, VMD-style
/// syntax). Rendering itself is launched by the GUI via QProcess on the
/// user-configured binary.
class RayTraceExporter {
public:
    struct SceneInputs {
        const core::Structure* structure = nullptr;
        StructureRenderer::Style style;
        std::vector<Light> lights;
        OrbitCamera camera;
        float aspect = 4.0f / 3.0f;
        QColor background = Qt::white;
        int width = 1600;  ///< embedded in Tachyon scenes
        int height = 1200;
    };

    static QString povray(const SceneInputs& inputs);
    static QString tachyon(const SceneInputs& inputs);
};

} // namespace calango::render
