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
#include "render/ShaderProfile.hpp"

#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>

#include <cstdio>
#include <cstring>
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
    checkProgram("wire", ":/assets/shaders/wire.vert",
                 ":/assets/shaders/wire.frag");
    checkProgram("shadow", ":/assets/shaders/shadow.vert",
                 ":/assets/shaders/shadow.frag");
    checkProgram("volume", ":/assets/shaders/volume.vert",
                 ":/assets/shaders/volume.frag");
    // The lit isosurface profile. Compiled here for the same reason as the
    // rest: a shader that fails to build is only discovered when someone opens
    // the viewport, and by then it is a bug report rather than a test failure.
    checkProgram("isosurface", ":/assets/shaders/isosurface.vert",
                 ":/assets/shaders/isosurface.frag");
    // Impostors. These carry real work in the fragment stage (ray/primitive
    // intersection and gl_FragDepth), so a driver that rejects them is exactly
    // the case the lazy-link fallback exists for — and the case worth catching
    // in CI on a machine we do not own.
    checkProgram("impostor sphere", ":/assets/shaders/impostor_sphere.vert",
                 ":/assets/shaders/impostor_sphere.frag");
    checkProgram("impostor cylinder", ":/assets/shaders/impostor_cylinder.vert",
                 ":/assets/shaders/impostor_cylinder.frag");
    checkProgram("volume ray march", ":/assets/shaders/raymarch.vert",
                 ":/assets/shaders/raymarch.frag");

    // --- Shader registry -----------------------------------------------------
    // SettingsManager spells the three profile keys literally rather than
    // pulling them from the registry (mirroring settings to JSON should not
    // drag QOpenGLContext into every target that reads a preference). That
    // duplication is deliberate and therefore has to be pinned: if the two
    // drift, a user's selection silently stops persisting.
    std::printf("Shader registry:\n");
    {
        using calango::render::ShaderRegistry;
        using calango::render::ShaderSlot;
        const struct { ShaderSlot slot; const char* key; } kKeys[] = {
            {ShaderSlot::Atoms, "render/atomShaderProfile"},
            {ShaderSlot::Bonds, "render/bondShaderProfile"},
            {ShaderSlot::Isosurfaces, "render/isosurfaceShaderProfile"},
        };
        for (const auto& entry : kKeys) {
            const bool match =
                std::strcmp(ShaderRegistry::settingsKey(entry.slot), entry.key)
                == 0;
            std::printf("  %s settings key matches SettingsManager (%s)\n",
                        match ? "ok  " : "FAIL", entry.key);
            if (!match)
                ++failures;
        }
        // Every slot must offer exactly one legacy profile: it is the fallback
        // for an unsupported selection, and a fallback that is missing or
        // ambiguous is not one.
        for (int i = 0; i < calango::render::kShaderSlotCount; ++i) {
            const auto slot = static_cast<ShaderSlot>(i);
            int legacy = 0;
            for (const auto& profile : ShaderRegistry::profiles(slot))
                legacy += profile.isLegacy ? 1 : 0;
            const bool ok = legacy == 1;
            std::printf("  %s %s has exactly one legacy profile\n",
                        ok ? "ok  " : "FAIL", ShaderRegistry::slotName(slot));
            if (!ok)
                ++failures;
            // The legacy profile is supported by definition — it is what
            // everything else falls back TO.
            QString reason;
            const bool supported = ShaderRegistry::isSupported(
                ShaderRegistry::legacyProfile(slot), &reason);
            std::printf("  %s %s legacy profile is always available\n",
                        supported ? "ok  " : "FAIL",
                        ShaderRegistry::slotName(slot));
            if (!supported)
                ++failures;
        }
        // An id written by a newer build must resolve, not crash or blank the
        // viewport.
        const bool fallback =
            ShaderRegistry::profile(ShaderSlot::Atoms,
                                    QStringLiteral("profile-from-the-future"))
                .isLegacy;
        std::printf("  %s an unknown profile id falls back to legacy\n",
                    fallback ? "ok  " : "FAIL");
        if (!fallback)
            ++failures;
    }

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
