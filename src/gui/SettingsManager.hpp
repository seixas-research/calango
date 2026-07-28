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
    /// Per-calculator shell command templates (Preferences → "Run"): a
    /// JSON-object string mapping a calculator name to its launch template,
    /// e.g. {"GPAW": "gpaw -P {cores} python {script}"}. Engines absent from
    /// the map use RunCommands::defaultTemplate().
    static constexpr auto kRunCommands = "jobs/runCommands";
    /// MPI rank count substituted for {cores} in those templates.
    static constexpr auto kRunCores = "jobs/runCores";
    static constexpr auto kShowWelcome = "welcome/showAtStartup";
    /// The camera state the "Reset camera" toolbar button restores, encoded by
    /// PointOfViewDialog::encode(). Empty (the default) means "no default has
    /// been set", and Reset camera falls back to auto-framing the structure.
    ///
    /// Mirrored to settings.json rather than left in QSettings alone because it
    /// is a figure-making preference: the same framing is wanted on the next
    /// machine, and — being one plain comma-separated line — it can be pasted
    /// between config files or hand-edited.
    static constexpr auto kDefaultPointOfView = "camera/defaultPointOfView";
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
