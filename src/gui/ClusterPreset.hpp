#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace calango::gui {

/// One saved HPC cluster configuration.
///
/// Everything a user would otherwise retype for each cluster they use: where
/// it is, who they are on it, which scheduler it runs, and the resource
/// request that machine's queues expect.
///
/// THE PASSWORD IS NOT A FIELD, and its absence is the point rather than an
/// omission. `remote::SshConfig` keeps the password in memory only and hands
/// it to the helper over stdin precisely so it never reaches disk or a command
/// line; a preset that stored it would undo that in the one file most likely
/// to be copied between machines or committed to a dotfiles repo. Key PATHS
/// are stored — a path is not a secret, and the key it names is protected by
/// its own permissions and passphrase.
struct ClusterPreset {
    QString name; ///< what the user calls this cluster; the combo box entry

    // -- Connection --------------------------------------------------------
    QString host;
    int port = 22;
    QString username;
    /// Matches remote::SshConfig::Auth (0 = Key, 1 = Password).
    int auth = 0;
    QString keyPath;
    QString remoteDir = QStringLiteral("calango_jobs");

    // -- Scheduler ---------------------------------------------------------
    /// Matches core::Scheduler (0 = Slurm, 1 = Pbs, 2 = Sge).
    int scheduler = 0;
    QString queue;
    int nodes = 1;
    int tasksPerNode = 1;
    int memoryMbPerNode = 0; ///< 0 = cluster default
    QString walltime = QStringLiteral("01:00:00");
    QString parallelEnvironment = QStringLiteral("smp"); ///< SGE only
    QString setupLines; ///< module load / conda activate prologue
    /// This cluster's VASP POTCAR library — a per-installation path, so it
    /// belongs to the profile rather than to global Preferences. Exported as
    /// CALANGO_VASP_PP_PATH ahead of `setupLines` in the generated job
    /// wrapper (SchedulerScript.cpp) when non-empty; the generated VASP
    /// script prefers it over whatever was baked in at generation time on
    /// the LOCAL machine (AseScriptGenerator.cpp, emitVasp()) — the one
    /// value guaranteed to describe the machine the job actually runs on.
    /// Empty leaves the run.py-baked path (or the environment's own
    /// VASP_PP_PATH) in charge, exactly as before this field existed.
    QString vaspPotcarPath;

    // -- SLURM-only extensions (Task 4) -------------------------------
    // Mirror core::RemoteJobSpec's own fields of the same name exactly —
    // see SchedulerScript.hpp for what each means and why it is SLURM-only.
    QString account;
    QString qos;
    int cpusPerTask = 1;
    int gpusPerNode = 0;
    QString nodeList;
    QString extraDirectives; ///< raw #SBATCH lines, verbatim
    /// The payload command — may span multiple lines (a launcher line, then
    /// post-run cleanup like "conda deactivate"). Falls back to
    /// RemoteJobSpec's own "python3 run.py" default when empty, exactly
    /// like every other per-cluster field defaults to "use the built-in
    /// default" rather than persisting the default itself.
    QString command;

    QJsonObject toJson() const;
    /// Tolerant: a preset written by an older build simply keeps the defaults
    /// for fields it does not carry, rather than failing the whole list and
    /// costing the user every other cluster they had saved.
    static ClusterPreset fromJson(const QJsonObject& json);

    bool operator==(const ClusterPreset& other) const;
};

/// The saved presets, in user order.
///
/// Persisted through QSettings — which SettingsManager mirrors into
/// ~/.calango/settings.json — as a single JSON array under one key, rather
/// than as a QSettings group per cluster. One key means a preset list can be
/// copied between machines by moving one value, and it keeps ordering, which
/// a group hierarchy does not.
namespace ClusterPresets {

/// Settings key the list lives under.
inline constexpr const char* kSettingsKey = "hpc/clusterPresets";

QVector<ClusterPreset> load();
void save(const QVector<ClusterPreset>& presets);

/// Insert or replace by name, preserving position when replacing. Returns the
/// index the preset ended up at.
///
/// Replace-by-name rather than append: "Save" on a cluster you already have
/// is an edit, and a list that grew a second "Cluster" entry every time would
/// be unusable within a week.
int upsert(QVector<ClusterPreset>& presets, const ClusterPreset& preset);
/// Remove by name; true when something was removed.
bool remove(QVector<ClusterPreset>& presets, const QString& name);
/// Index of `name`, or -1.
int indexOf(const QVector<ClusterPreset>& presets, const QString& name);

/// Serialize / parse the whole list, exposed so the round trip is testable
/// without touching a real QSettings store.
QString toJsonText(const QVector<ClusterPreset>& presets);
QVector<ClusterPreset> fromJsonText(const QString& text);

} // namespace ClusterPresets

} // namespace calango::gui
