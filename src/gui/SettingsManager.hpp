#pragma once

#include <QString>

namespace calango::gui {

/// Centralized user-settings bridge backed by `~/.calango/settings.json`.
///
/// The application code keeps reading/writing QSettings (the native backend)
/// everywhere it already does; this manager mirrors a curated set of
/// user-facing preference keys to a human-readable JSON file so they survive,
/// are portable, and can be hand-edited. On startup `loadOrInitialize()`
/// creates `~/.calango/` and a default `settings.json` if missing, otherwise
/// parses the file and applies its values into QSettings (JSON is
/// authoritative). `save()` writes the current values back out.
class SettingsManager {
public:
    // "group/key" style keys mirrored between QSettings and settings.json.
    static constexpr auto kTheme = "appearance/theme";       ///< system|dark|light
    static constexpr auto kOmpThreads = "jobs/ompThreads";   ///< int (OMP_NUM_THREADS)
    static constexpr auto kCondaDir = "jobs/condaDir";       ///< conda envs directory
    static constexpr auto kEnvironmentPath = "jobs/environmentPath";
    /// Per-calculator environment presets: a JSON-object string mapping a
    /// calculator name ("GPAW", "MACE", …) to its last-used Python/Conda env.
    static constexpr auto kEnvironmentPresets = "jobs/environmentPresets";
    static constexpr auto kShowWelcome = "welcome/showAtStartup";
    static constexpr auto kEnvFilePath = "config/envFilePath";
    static constexpr auto kMaterialsProjectApiKey = "materialsProject/apiKey";

    /// `~/.calango`
    static QString directory();
    /// `~/.calango/settings.json`
    static QString filePath();

    /// Create the config dir + a default settings.json on first run; otherwise
    /// parse settings.json and push every managed key into QSettings.
    static void loadOrInitialize();

    /// Serialize the managed keys (read from QSettings) to settings.json.
    static void save();
};

} // namespace calango::gui
