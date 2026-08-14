#include "render/ShaderProfile.hpp"

#include <QCoreApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSettings>

#include <algorithm>

namespace calango::render {

namespace {

/// The PBR entry is identical for atoms and bonds apart from the slot — one
/// factory, so the description (which is also a translated string) exists
/// once.
ShaderProfile pbrEntry(ShaderSlot slot)
{
    return {QStringLiteral("pbr"),
            QCoreApplication::translate("calango::render", "Modern / PBR"),
            QCoreApplication::translate(
                "calango::render",
                "Cook-Torrance physically based shading (GGX + Smith + "
                "Schlick) "
                "on impostor geometry, driven by metallic and roughness rather "
                "than by an ad-hoc specular multiplier. Metals tint their "
                "reflection and lose their diffuse lobe, which is what makes a "
                "metal look like metal instead of grey plastic.\n\n"
                "The environment is an analytic studio gradient rather than a "
                "captured cubemap — no image asset is shipped — so reflections "
                "read as light and shade rather than as a recognisable room."),
            slot, {}, true, true, 1, false};
}

/// Same arrangement for the Toon entry.
ShaderProfile toonEntry(ShaderSlot slot)
{
    return {QStringLiteral("toon"),
            QCoreApplication::translate("calango::render", "Stylized / Toon"),
            QCoreApplication::translate(
                "calango::render",
                "Quantized diffuse bands with a darkened silhouette — the "
                "flat-shaded look a figure or a teaching slide usually wants. "
                "On "
                "spheres and cylinders the rim IS the outline, so no "
                "screen-space "
                "edge pass is needed."),
            slot, {}, true, true, 2, false};
}

/// The registered profiles, per slot.
///
/// Phase 0 ships one profile per slot for atoms and bonds — the existing
/// Blinn-Phong path, unchanged — so the infrastructure lands with provably
/// zero visual difference. Isosurfaces get the second entry immediately,
/// because that slot had a genuine defect to fix rather than a technique to
/// upgrade.
const std::vector<ShaderProfile>& atomProfiles()
{
    static const std::vector<ShaderProfile> profiles = {
        {QStringLiteral("legacy"),
         QCoreApplication::translate("calango::render", "Legacy / Fast"),
         QCoreApplication::translate(
             "calango::render",
             "Blinn-Phong with up to four directional lights, shadow mapping "
             "and the four surface finishes. Tessellated spheres: 1200 "
             "triangles per atom, paid whether the sphere covers 400 pixels "
             "or 4."),
         ShaderSlot::Atoms, {}, false, false, 0, true},
        {QStringLiteral("impostor"),
         QCoreApplication::translate("calango::render", "Impostor spheres"),
         QCoreApplication::translate(
             "calango::render",
             "One camera-facing quad per atom, on which the fragment stage "
             "intersects a real analytic sphere. The silhouette is exact at "
             "every zoom — no facets appear when an atom fills the screen — "
             "and the vertex cost drops from 651 vertices per atom to 4. "
             "Shading, finishes, shadows and fog are identical to Legacy."),
         ShaderSlot::Atoms, {}, true, true, 0, false},
        pbrEntry(ShaderSlot::Atoms),
        toonEntry(ShaderSlot::Atoms),
    };
    return profiles;
}

const std::vector<ShaderProfile>& bondProfiles()
{
    static const std::vector<ShaderProfile> profiles = {
        {QStringLiteral("legacy"),
         QCoreApplication::translate("calango::render", "Legacy / Fast"),
         QCoreApplication::translate(
             "calango::render",
             "The same Blinn-Phong program the atoms use, so a bond and the "
             "atoms it joins are shaded identically. Tessellated cylinders "
             "with an axial colour gradient."),
         ShaderSlot::Bonds, {}, false, false, 0, true},
        {QStringLiteral("impostor"),
         QCoreApplication::translate("calango::render", "Impostor cylinders"),
         QCoreApplication::translate(
             "calango::render",
             "One camera-facing quad per bond, ray-traced as a capped "
             "cylinder. Exact silhouettes and caps at any zoom, 4 vertices "
             "per bond instead of 100, and the axial colour gradient is "
             "reproduced exactly."),
         ShaderSlot::Bonds, {}, true, true, 0, false},
        pbrEntry(ShaderSlot::Bonds),
        toonEntry(ShaderSlot::Bonds),
    };
    return profiles;
}

const std::vector<ShaderProfile>& isosurfaceProfiles()
{
    static const std::vector<ShaderProfile> profiles = {
        {QStringLiteral("legacy"),
         QCoreApplication::translate("calango::render", "Legacy / Baked"),
         QCoreApplication::translate(
             "calango::render",
             "The historical path: an unlit fill whose shading was computed on "
             "the CPU and baked into the vertex colours. The highlight is "
             "frozen to a fixed direction — it does not follow the camera or "
             "the scene lights — and the surface is invisible to ambient "
             "occlusion. Kept for reproducing older figures."),
         ShaderSlot::Isosurfaces, {}, false, false, 0, true},
        {QStringLiteral("lit"),
         QCoreApplication::translate("calango::render", "Lit surface"),
         QCoreApplication::translate(
             "calango::render",
             "Shades the surface on the GPU from the normals marching cubes "
             "already derives, using the SAME lights as the atoms — so the "
             "highlight tracks the camera and the surface sits in the scene "
             "rather than on top of it. It also writes a real normal into the "
             "G-buffer, so isosurfaces finally take part in ambient "
             "occlusion."),
         ShaderSlot::Isosurfaces, {}, false, false, 0, false},
    };
    return profiles;
}

ShaderCapabilities& capabilityCache()
{
    static ShaderCapabilities caps;
    return caps;
}

} // namespace

const std::vector<ShaderProfile>& ShaderRegistry::profiles(ShaderSlot slot)
{
    switch (slot) {
    case ShaderSlot::Atoms: return atomProfiles();
    case ShaderSlot::Bonds: return bondProfiles();
    case ShaderSlot::Isosurfaces: break;
    }
    return isosurfaceProfiles();
}

const ShaderProfile& ShaderRegistry::legacyProfile(ShaderSlot slot)
{
    const auto& list = profiles(slot);
    const auto it = std::find_if(list.begin(), list.end(),
                                 [](const ShaderProfile& p) { return p.isLegacy; });
    // Every slot declares exactly one legacy profile; the fallback to the
    // first entry keeps this total rather than throwing in a render path.
    return it != list.end() ? *it : list.front();
}

const ShaderProfile& ShaderRegistry::profile(ShaderSlot slot, const QString& id)
{
    const auto& list = profiles(slot);
    const auto it = std::find_if(list.begin(), list.end(),
                                 [&id](const ShaderProfile& p) { return p.id == id; });
    return it != list.end() ? *it : legacyProfile(slot);
}

const char* ShaderRegistry::settingsKey(ShaderSlot slot)
{
    switch (slot) {
    case ShaderSlot::Atoms: return "render/atomShaderProfile";
    case ShaderSlot::Bonds: return "render/bondShaderProfile";
    case ShaderSlot::Isosurfaces: break;
    }
    return "render/isosurfaceShaderProfile";
}

const char* ShaderRegistry::slotName(ShaderSlot slot)
{
    switch (slot) {
    case ShaderSlot::Atoms: return "atoms";
    case ShaderSlot::Bonds: return "bonds";
    case ShaderSlot::Isosurfaces: break;
    }
    return "isosurfaces";
}

QString ShaderRegistry::activeProfileId(ShaderSlot slot)
{
    // The SHIPPED default lives in SettingsManager's managed-key table, which
    // seeds settings.json on first run — one place, so "what does a fresh
    // install draw with" has a single answer. The fallback here is only for a
    // key that is genuinely absent (a settings file hand-edited, or one
    // predating the key), and legacy is the right answer there: it is the
    // profile that always works.
    const QString stored = QSettings()
                               .value(QLatin1String(settingsKey(slot)),
                                      legacyProfile(slot).id)
                               .toString();
    const ShaderProfile& resolved = profile(slot, stored);
    QString reason;
    // Validated on every read, not only when it is set: a settings file is
    // portable between machines, and the driver that has to run it is not the
    // one that wrote it.
    if (!isSupported(resolved, &reason))
        return legacyProfile(slot).id;
    return resolved.id;
}

const ShaderProfile& ShaderRegistry::activeProfile(ShaderSlot slot)
{
    return profile(slot, activeProfileId(slot));
}

void ShaderRegistry::setActiveProfileId(ShaderSlot slot, const QString& id)
{
    QSettings().setValue(QLatin1String(settingsKey(slot)),
                         profile(slot, id).id);
}

const ShaderCapabilities& ShaderRegistry::capabilities()
{
    ShaderCapabilities& caps = capabilityCache();
    if (caps.valid)
        return caps;

    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context)
        return caps; // valid stays false; the caller reports "unknown"

    // Everything here is core GL or Qt's own portable wrapper — no platform
    // conditionals. The same code answers for Mesa, NVIDIA, AMD and Apple.
    const QSurfaceFormat format = context->format();
    caps.glMajor = format.majorVersion();
    caps.glMinor = format.minorVersion();

    QOpenGLFunctions* fn = context->functions();
    const auto readString = [fn](GLenum name) {
        const GLubyte* value = fn->glGetString(name);
        return value ? QString::fromLatin1(reinterpret_cast<const char*>(value))
                     : QString();
    };
    caps.renderer = readString(GL_RENDERER);
    caps.vendor = readString(GL_VENDOR);
    caps.glslVersion = readString(GL_SHADING_LANGUAGE_VERSION);

    const QSet<QByteArray> extensions = context->extensions();
    caps.extensions.reserve(extensions.size());
    for (const QByteArray& name : extensions)
        caps.extensions << QString::fromLatin1(name);
    caps.extensions.sort();

    GLint value = 0;
    fn->glGetIntegerv(GL_MAX_SAMPLES, &value);
    caps.maxSamples = value;
    value = 0;
    fn->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
    caps.maxTextureSize = value;

    caps.valid = true;
    return caps;
}

bool ShaderRegistry::isSupported(const ShaderProfile& profile, QString* reason)
{
    // The legacy profile is supported by definition: it is the fallback, and a
    // fallback that can itself be unavailable is not one.
    if (profile.isLegacy)
        return true;

    const ShaderCapabilities& caps = capabilities();
    if (!caps.valid) {
        // No context yet — during construction, or in a headless test. Assume
        // supported rather than silently pinning everything to legacy before
        // the viewport has even been created.
        return true;
    }

    const ShaderRequirements& need = profile.requirements;
    if (caps.glMajor < need.glMajor
        || (caps.glMajor == need.glMajor && caps.glMinor < need.glMinor)) {
        if (reason)
            *reason = QCoreApplication::translate(
                          "calango::render",
                          "needs OpenGL %1.%2; this context is %3.%4")
                          .arg(need.glMajor)
                          .arg(need.glMinor)
                          .arg(caps.glMajor)
                          .arg(caps.glMinor);
        return false;
    }
    for (const QString& extension : need.extensions) {
        if (!caps.extensions.contains(extension)) {
            if (reason)
                *reason = QCoreApplication::translate(
                              "calango::render",
                              "the driver does not report %1")
                              .arg(extension);
            return false;
        }
    }
    return true;
}

} // namespace calango::render
