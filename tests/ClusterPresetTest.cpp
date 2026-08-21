// HPC cluster presets: serialization, list management, and the one security
// property the format has to hold.
//
// Presets exist so a user does not retype a cluster's address, account,
// scheduler and queue shape on every run. That makes the file they live in a
// file people copy between machines — which is exactly why the password must
// not be in it, and why that is asserted here rather than assumed from the
// struct definition.

#include "gui/ClusterPreset.hpp"

#include <QCoreApplication>

#include <string>

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

calango::gui::ClusterPreset sample()
{
    calango::gui::ClusterPreset preset;
    preset.name = QStringLiteral("Marenostrum");
    preset.host = QStringLiteral("mn1.bsc.es");
    preset.port = 2222;
    preset.username = QStringLiteral("leseixas");
    preset.auth = 1;
    preset.keyPath = QStringLiteral("/Users/leseixas/.ssh/id_ed25519");
    preset.remoteDir = QStringLiteral("scratch/calango");
    preset.scheduler = 0;
    preset.queue = QStringLiteral("debug");
    preset.nodes = 4;
    preset.tasksPerNode = 48;
    preset.memoryMbPerNode = 96000;
    preset.walltime = QStringLiteral("06:00:00");
    preset.parallelEnvironment = QStringLiteral("mpi");
    preset.setupLines = QStringLiteral("module load gpaw\nconda activate dft");
    preset.vaspPotcarPath = QStringLiteral("/gpfs/projects/bsc/pseudo/potcars");
    preset.account = QStringLiteral("phys-2026");
    preset.qos = QStringLiteral("priority");
    preset.cpusPerTask = 8;
    preset.gpusPerNode = 2;
    preset.nodeList = QStringLiteral("work1");
    preset.extraDirectives = QStringLiteral("#SBATCH --mail-type=END");
    preset.command = QStringLiteral("mpirun -n 4 gpaw python run_gpaw.py\n\nconda deactivate");
    return preset;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using namespace calango::gui;

    std::printf("Round trip:\n");
    {
        const ClusterPreset original = sample();
        const ClusterPreset restored =
            ClusterPreset::fromJson(original.toJson());
        check(restored == original, "every field survives JSON round trip");
        // Spot-check the ones a lazy serializer drops.
        check(restored.port == 2222 && restored.nodes == 4
                  && restored.tasksPerNode == 48
                  && restored.memoryMbPerNode == 96000,
              "including the numeric resource request");
        check(restored.setupLines.contains(QLatin1Char('\n')),
              "and a multi-line setup prologue keeps its newlines");
        check(restored.vaspPotcarPath == original.vaspPotcarPath
                  && !restored.vaspPotcarPath.isEmpty(),
              "and the per-cluster VASP POTCAR override (Task 1)");
        check(restored.account == original.account
                  && restored.qos == original.qos
                  && restored.cpusPerTask == 8 && restored.gpusPerNode == 2
                  && restored.nodeList == original.nodeList
                  && restored.extraDirectives == original.extraDirectives
                  && restored.command == original.command,
              "and every Task 4 SLURM extension (account/QOS/cpus-per-task/"
              "GPUs/node list/extra directives/command)");
    }

    std::printf("The password is not persisted:\n");
    {
        // The property, stated as a property: no key of the serialized object
        // may look like a secret, whatever the struct happens to contain.
        const QJsonObject json = sample().toJson();
        bool clean = true;
        for (const QString& key : json.keys()) {
            const QString lower = key.toLower();
            if (lower.contains(QStringLiteral("pass"))
                || lower.contains(QStringLiteral("secret"))
                || lower.contains(QStringLiteral("token")))
                clean = false;
        }
        check(clean,
              "no field of a preset is a password, passphrase or token — the "
              "preset file is meant to be copyable between machines");
        // The key PATH is fine and is kept: a path is not a secret, and
        // without it the preset cannot reconnect unattended.
        check(json.contains(QStringLiteral("keyPath")),
              "but the key path is kept, since it is not itself a secret");
    }

    std::printf("List serialization:\n");
    {
        QVector<ClusterPreset> presets;
        ClusterPreset a = sample();
        ClusterPreset b = sample();
        b.name = QStringLiteral("Local cluster");
        b.host = QStringLiteral("head.lab.local");
        presets << a << b;

        const QString text = ClusterPresets::toJsonText(presets);
        const QVector<ClusterPreset> back = ClusterPresets::fromJsonText(text);
        check(back.size() == 2, "two presets survive the list round trip");
        check(back[0].name == a.name && back[1].name == b.name,
              "in the order the user arranged them");
        check(back[0] == a && back[1] == b, "and unchanged");
    }

    std::printf("Editing the list:\n");
    {
        QVector<ClusterPreset> presets;
        ClusterPreset a = sample();
        ClusterPresets::upsert(presets, a);
        check(presets.size() == 1, "upsert adds a new preset");

        // Saving over an existing name is an EDIT. Appending instead would
        // grow a duplicate every time the user pressed Save.
        ClusterPreset edited = a;
        edited.queue = QStringLiteral("bsc_case");
        const int at = ClusterPresets::upsert(presets, edited);
        check(presets.size() == 1 && at == 0,
              "and saving the same name replaces rather than duplicating");
        check(presets[0].queue == QStringLiteral("bsc_case"),
              "with the new values");

        ClusterPreset second = sample();
        second.name = QStringLiteral("Other");
        ClusterPresets::upsert(presets, second);
        check(presets.size() == 2, "a different name appends");
        // Position is preserved on replace, so the combo does not reshuffle
        // under the user while they are working.
        ClusterPreset again = presets[0];
        again.walltime = QStringLiteral("00:30:00");
        check(ClusterPresets::upsert(presets, again) == 0,
              "replacing keeps the preset where it was in the list");

        check(ClusterPresets::indexOf(presets, QStringLiteral("Other")) == 1,
              "lookup by name finds it");
        check(ClusterPresets::indexOf(presets, QStringLiteral("MARENOSTRUM"))
                  == 0,
              "case-insensitively, so Save does not create a near-duplicate "
              "differing only in capitals");
        check(ClusterPresets::indexOf(presets, QStringLiteral("absent")) == -1,
              "and reports a miss");

        check(ClusterPresets::remove(presets, QStringLiteral("Other")),
              "remove deletes by name");
        check(presets.size() == 1, "leaving the rest");
        check(!ClusterPresets::remove(presets, QStringLiteral("Other")),
              "and removing a missing preset is a no-op, not a crash");
    }

    std::printf("Damaged and older input:\n");
    {
        check(ClusterPresets::fromJsonText(QString()).isEmpty(),
              "empty settings give no presets");
        check(ClusterPresets::fromJsonText(QStringLiteral("not json")).isEmpty(),
              "and neither does corrupt text — the panel opens empty rather "
              "than not at all");
        check(ClusterPresets::fromJsonText(QStringLiteral("{\"a\":1}")).isEmpty(),
              "nor a JSON object where an array belongs");

        // A preset written before nodes/memory existed must still load, with
        // the defaults, rather than taking every other cluster down with it.
        const QVector<ClusterPreset> old = ClusterPresets::fromJsonText(
            QStringLiteral("[{\"name\":\"Legacy\",\"host\":\"old.host\"}]"));
        check(old.size() == 1, "a preset from an older build still loads");
        check(old[0].nodes == 1 && old[0].tasksPerNode == 1
                  && old[0].walltime == QStringLiteral("01:00:00")
                  && old[0].port == 22,
              "with usable defaults for the fields it predates — an empty "
              "walltime would be submitted verbatim and rejected");
        check(old[0].vaspPotcarPath.isEmpty(),
              "including an empty VASP POTCAR override, which just leaves "
              "the local default in charge — not a crash on the field it "
              "predates");
        check(old[0].account.isEmpty() && old[0].qos.isEmpty()
                  && old[0].cpusPerTask == 1 && old[0].gpusPerNode == 0
                  && old[0].nodeList.isEmpty()
                  && old[0].extraDirectives.isEmpty() && old[0].command.isEmpty(),
              "and every Task 4 SLURM extension it predates too — cpusPerTask "
              "specifically defaults to 1, not 0, matching SLURM's own "
              "default so an old preset does not suddenly request 0 cores");

        // A nameless entry can never be selected, saved over or deleted.
        check(ClusterPresets::fromJsonText(
                  QStringLiteral("[{\"host\":\"h\"},{\"name\":\"ok\"}]"))
                  .size() == 1,
              "and an entry with no name is dropped rather than becoming an "
              "unreachable row");
    }

    if (failures == 0) {
        std::printf("\nAll cluster-preset checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d cluster-preset check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
