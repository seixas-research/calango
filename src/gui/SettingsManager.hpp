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
    /// Where a job's working directory is created for an UNSAVED session.
    ///
    /// The default used to be the platform's application-data location, which
    /// on macOS is ~/Library/Application Support/... and on Linux
    /// ~/.local/share/... — correct places for an application's own state, and
    /// the wrong place for a user's simulation output: buried, hidden by the
    /// file manager, and not where anyone looks for a trajectory they want to
    /// keep. It now defaults to ~/My Simulations and is editable.
    ///
    /// A SAVED project is unaffected: its jobs stay in .calango_tmp/ beside
    /// the .calproj, so a project remains self-contained and movable.
    static constexpr auto kSimulationsDir = "jobs/simulationsDir";
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
    /// Where each plane-wave engine's pseudopotential library lives.
    ///
    /// These are properties of the MACHINE, not of a run: a VASP POTCAR
    /// library is licensed and installed once, and re-typing its path in every
    /// wizard is how a job ends up pointing at the wrong set. Each maps onto
    /// the environment variable the engine already reads, so the value is
    /// exported for a run rather than substituted into the input file:
    ///   VASP            -> VASP_PP_PATH
    ///   Quantum Espresso-> ESPRESSO_PSEUDO
    ///   SIESTA          -> SIESTA_PP_PATH
    /// Empty means "leave the environment alone", which is the right default:
    /// a machine that already exports one must not have it silently replaced.
    static constexpr auto kPseudopotentialsVasp = "pseudopotentials/vasp";
    static constexpr auto kPseudopotentialsEspresso =
        "pseudopotentials/quantumEspresso";
    static constexpr auto kPseudopotentialsSiesta = "pseudopotentials/siesta";
    /// Where trained machine-learning potentials are read from and written to
    /// (MACE, NequIP, a fine-tuned checkpoint). One directory rather than one
    /// per architecture: a model file is identified by its own name, and the
    /// thing the user actually wants is "my models live here".
    static constexpr auto kMlPotentialsDir = "mlPotentials/directory";
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

    /// The directory new job folders are created under for an unsaved session
    /// — the configured one when it is usable, otherwise the application-data
    /// fallback.
    ///
    /// Resolved rather than read raw because the configured path can fail on a
    /// machine the settings file was copied to: a home directory that moved, a
    /// read-only volume, a path pointing at a file. Silently writing nowhere
    /// would lose a run's output, so an unusable setting degrades to the
    /// location that always works.
    static QString simulationsDirectory();
    /// The shipped default, exposed so Preferences can offer "Reset".
    static QString defaultSimulationsDirectory();

    /// Where a model-file picker should open, given whatever path the field
    /// already holds. The field wins when it is non-empty (the user is editing
    /// an existing choice); otherwise the configured ML potentials directory;
    /// otherwise Qt's default. Exists so the preference is honoured
    /// identically by all four model pickers instead of three of them
    /// forgetting.
    static QString mlPotentialsStartPath(const QString& currentValue = {});
};

} // namespace calango::gui
