// Shader compilation + linkage test.
//
// A GLSL error does not crash Calango — Qt logs it and the program silently
// renders nothing, so a broken shader shows up as a black viewport that looks
// like a driver problem. This compiles and links every program the renderer
// builds, against a real GL 3.3 core context, so the failure is caught here
// with the compiler's message attached.
//
// Self-skips (exit 0) when no OpenGL context can be created — headless CI
// boxes and the offscreen Qt platform plugin have no GL, and that is not a
// shader defect.

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

namespace {

int failures = 0;

/// Compile + link one program from the resource paths the renderer uses.
void checkProgram(const char* label, const char* vertex, const char* fragment)
{
    QOpenGLShaderProgram program;
    if (!program.addShaderFromSourceFile(QOpenGLShader::Vertex,
                                         QLatin1String(vertex))) {
        std::printf("  FAIL %s — vertex:\n%s\n", label,
                    program.log().toUtf8().constData());
        ++failures;
        return;
    }
    if (!program.addShaderFromSourceFile(QOpenGLShader::Fragment,
                                         QLatin1String(fragment))) {
        std::printf("  FAIL %s — fragment:\n%s\n", label,
                    program.log().toUtf8().constData());
        ++failures;
        return;
    }
    if (!program.link()) {
        std::printf("  FAIL %s — link:\n%s\n", label,
                    program.log().toUtf8().constData());
        ++failures;
        return;
    }
    std::printf("  ok   %s\n", label);
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    QOffscreenSurface surface;
    surface.setFormat(format);
    surface.create();
    QOpenGLContext context;
    context.setFormat(format);
    if (!surface.isValid() || !context.create() || !context.makeCurrent(&surface)) {
        std::printf("No OpenGL 3.3 core context available — skipping "
                    "(this is an environment limitation, not a shader error).\n");
        return EXIT_SUCCESS;
    }

    std::printf("GL renderer: %s\n",
                reinterpret_cast<const char*>(
                    context.functions()->glGetString(GL_RENDERER)));
    std::printf("Shader programs:\n");

    // Scene programs. mesh/line/wire additionally write the SSAO G-buffer
    // normal attachment, which is where a layout-qualifier mistake would land.
    checkProgram("mesh", ":/assets/shaders/mesh.vert",
                 ":/assets/shaders/mesh.frag");
    checkProgram("line", ":/assets/shaders/line.vert",
                 ":/assets/shaders/line.frag");
    checkProgram("wire", ":/assets/shaders/wire.vert",
                 ":/assets/shaders/wire.frag");
    checkProgram("shadow", ":/assets/shaders/shadow.vert",
                 ":/assets/shaders/shadow.frag");
    checkProgram("volume", ":/assets/shaders/volume.vert",
                 ":/assets/shaders/volume.frag");

    // Post-processing: all share the fullscreen-triangle vertex shader.
    checkProgram("depth of field", ":/assets/shaders/dof.vert",
                 ":/assets/shaders/dof.frag");
    checkProgram("ssao", ":/assets/shaders/dof.vert",
                 ":/assets/shaders/ssao.frag");
    checkProgram("ssao blur", ":/assets/shaders/dof.vert",
                 ":/assets/shaders/ssao_blur.frag");
    checkProgram("ssao composite", ":/assets/shaders/dof.vert",
                 ":/assets/shaders/ssao_composite.frag");

    context.doneCurrent();
    std::printf(failures == 0 ? "\nAll shaders compiled and linked.\n"
                              : "\n%d shader program(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
