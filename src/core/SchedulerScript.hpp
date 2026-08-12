#pragma once

#include <string>

namespace calango::core {

/// HPC batch schedulers Calango can submit to.
enum class Scheduler {
    Slurm, ///< sbatch / squeue / scancel
    Pbs,   ///< qsub / qstat / qdel (PBS Pro & Torque)
    Sge,   ///< qsub / qstat / qdel (Sun/Univa Grid Engine)
};

/// Resource request + environment for one remote batch job. The wrapper
/// always redirects the scheduler's stdout/stderr to fixed file names
/// (calango_job.out / calango_job.err) so the monitor knows what to tail.
struct RemoteJobSpec {
    Scheduler scheduler = Scheduler::Slurm;
    std::string jobName = "calango";
    std::string queue;      ///< partition (SLURM) / queue (PBS, SGE); "" = cluster default
    int nodes = 1;          ///< whole machines requested
    int tasksPerNode = 1;   ///< ranks (or cores) on each of them
    /// Memory PER NODE in MB; 0 asks for the cluster default.
    ///
    /// Per node is the mental model the field is written in, and the three
    /// schedulers do not agree on it — which is exactly why this is
    /// documented here rather than left to the caller:
    ///
    ///   SLURM  `--mem` is per node, so it maps straight across.
    ///   PBS    memory sits inside the select chunk, and a chunk is a node,
    ///          so it also maps across.
    ///   SGE    `h_vmem` is per SLOT. A per-node request therefore has to be
    ///          DIVIDED by the slots on that node, or a 64 GB request on a
    ///          16-core node silently asks for 1 TB and the job never starts.
    int memoryMbPerNode = 0;
    std::string walltime = "01:00:00"; ///< HH:MM:SS
    /// SGE parallel environment (`-pe <name> <slots>`). Site-specific: "smp"
    /// is single-node shared memory almost everywhere, and a multi-node job
    /// needs whatever that site called its MPI environment. Ignored by SLURM
    /// and PBS, which describe the layout directly.
    std::string parallelEnvironment = "smp";
    std::string setupLines; ///< verbatim shell prologue (module load, conda activate, ...)
    std::string command = "python3 run.py"; ///< the actual payload

    /// Total ranks across every node.
    int totalTasks() const
    {
        const int n = nodes > 0 ? nodes : 1;
        const int perNode = tasksPerNode > 0 ? tasksPerNode : 1;
        return n * perNode;
    }
};

namespace SchedulerScript {

/// Scheduler's stdout / stderr capture files, tailed by the remote monitor.
inline constexpr const char* kStdoutFile = "calango_job.out";
inline constexpr const char* kStderrFile = "calango_job.err";

/// Contents of the job.sh wrapper (with #SBATCH / #PBS / #$ directives).
std::string generate(const RemoteJobSpec& spec);

/// "slurm" / "pbs" / "sge" — the wire name used by the paramiko helper.
std::string schedulerKey(Scheduler scheduler);

/// Submission command to run inside the remote job directory.
std::string submitCommand(Scheduler scheduler);

/// Command that cancels the given job id.
std::string cancelCommand(Scheduler scheduler, const std::string& jobId);

} // namespace SchedulerScript
} // namespace calango::core
