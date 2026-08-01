#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace calango::render {

/// Which class of geometry a shader profile applies to.
///
/// Three slots rather than one global setting because the three have genuinely
/// different constraints: atoms and bonds are instanced closed meshes that can
/// become impostors, while an isosurface is an arbitrary triangle soup that
/// cannot. A single "quality" dial would have to move all three together and
/// would be wrong for at least one of them.
enum class ShaderSlot {
    Atoms,
    Bonds,
    Isosurfaces,
};

/// The number of slots — kept next to the enum so a new slot cannot be added
/// without the loops that walk them being updated.
inline constexpr int kShaderSlotCount = 3;

/// What a profile needs from the driver.
///
/// Deliberately coarse. A finer capability model would be guesswork: what
/// actually varies between the Mesa, NVIDIA, AMD and Apple stacks this has to
/// run on is which OPTIONAL features are exposed, and every one of those is a
/// yes/no the context can be asked directly.
struct ShaderRequirements {
    /// Minimum GL major/minor. Everything currently shipped is 3.3 core, the
    /// baseline the application requests in main().
    int glMajor = 3;
    int glMinor = 3;
    /// Extensions that must be present, by their GL name. Queried through
    /// QOpenGLContext::hasExtension, which is portable across every platform
    /// the application targets — no driver-specific or OS-specific probing.
    QStringList extensions;
};

/// One selectable shader implementation for one slot.
struct ShaderProfile {
    QString id;          ///< stable, stored in settings — never localize this
    QString displayName; ///< shown in Preferences
    QString description; ///< one or two sentences for the tool tip
    ShaderSlot slot = ShaderSlot::Atoms;
    ShaderRequirements requirements;

    /// True when switching TO or AWAY FROM this profile changes the vertex
    /// layout, so the geometry buffers have to be rebuilt rather than merely
    /// redrawn.
    bool requiresGeometryRebuild = false;

    /// Draw with the impostor quad instead of the tessellated mesh.
    bool impostorGeometry = false;

    /// Which BRDF the fragment stage evaluates. A uniform rather than a
    /// separate shader pair per model: the ray/primitive intersection is the
    /// expensive, delicate part and is identical for all of them, so
    /// duplicating it three times to vary the shading would triple the surface
    /// area of the one thing most likely to be got wrong. The branch is on a
    /// uniform, so it is coherent across the whole draw call and effectively
    /// free.
    ///   0 = Blinn-Phong  (matches the legacy mesh path exactly)
    ///   1 = Cook-Torrance PBR (GGX + Smith + Schlick)
    ///   2 = Toon (quantized bands + rim)
    int shadingModel = 0;

    /// Marks the profile that reproduces the historical output exactly. There
    /// is always exactly one per slot, it is what an unsupported or failed
    /// profile falls back to, and it is what the regression tests pin.
    bool isLegacy = false;
};

/// What the live GL context actually offers. Filled once, from the current
/// context, by capabilities().
struct ShaderCapabilities {
    bool valid = false;      ///< false when queried with no current context
    int glMajor = 0;
    int glMinor = 0;
    QString renderer;        ///< GL_RENDERER, e.g. "Mesa Intel(R) UHD Graphics"
    QString vendor;          ///< GL_VENDOR
    QString glslVersion;     ///< GL_SHADING_LANGUAGE_VERSION
    QStringList extensions;  ///< as reported by QOpenGLContext
    int maxSamples = 0;
    int maxTextureSize = 0;
};

/// The registry of selectable shader profiles.
///
/// Exists so that adding a rendering technique is a DATA change — one entry
/// here plus its GLSL — instead of surgery on StructureRenderer. The renderer
/// asks which profile is active for a slot and binds the matching program; the
/// Preferences tab lists what is available and why anything is not.
///
/// Selection is global (it describes the installation's rendering, like the
/// theme), and persists through QSettings, which SettingsManager mirrors to
/// ~/.calango/settings.json. Per-object appearance stays where it already is:
/// on render::Style, edited in the Representation panel. Global default in
/// Preferences, per-object override in the panel — the split the application
/// already uses everywhere else.
class ShaderRegistry {
public:
    /// Every profile registered for `slot`, in presentation order (legacy
    /// first, then increasing sophistication).
    static const std::vector<ShaderProfile>& profiles(ShaderSlot slot);

    /// The profile with `id` in `slot`, or the slot's legacy profile when the
    /// id is unknown — which is what makes a settings file written by a newer
    /// build load safely in an older one.
    static const ShaderProfile& profile(ShaderSlot slot, const QString& id);

    /// The slot's legacy (always-available) profile.
    static const ShaderProfile& legacyProfile(ShaderSlot slot);

    /// The selection from settings, validated against what this driver
    /// supports: an unsupported or unknown selection resolves to the legacy
    /// profile rather than failing to draw.
    static const ShaderProfile& activeProfile(ShaderSlot slot);
    /// Persist a selection. Does not itself trigger a redraw — the caller owns
    /// the viewport and knows whether a geometry rebuild is also needed.
    static void setActiveProfileId(ShaderSlot slot, const QString& id);

    /// Whether `profile` can run here. `reason` receives a human-readable
    /// explanation naming the MISSING REQUIREMENT when it cannot: "not
    /// supported" is useless to a user who then has to guess whether it is
    /// their driver, their hardware or a bug.
    static bool isSupported(const ShaderProfile& profile, QString* reason);

    /// Slot ↔ settings-key mapping, exposed so the Preferences tab and the
    /// renderer cannot disagree about where a selection lives.
    static const char* settingsKey(ShaderSlot slot);
    /// Untranslated slot name for logs; the UI supplies its own translated
    /// labels.
    static const char* slotName(ShaderSlot slot);

private:
    /// The raw id from settings, validated; activeProfile() is the public
    /// face of this.
    static QString activeProfileId(ShaderSlot slot);

    /// Capabilities of the CURRENT GL context. Must be called with one bound;
    /// returns `valid == false` otherwise. Cached after the first successful
    /// query, since the context outlives every caller.
    static const ShaderCapabilities& capabilities();
};

} // namespace calango::render
