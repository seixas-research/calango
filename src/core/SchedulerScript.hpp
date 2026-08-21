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

    // -- SLURM-only extensions (Task 4) --------------------------------
    //
    // Every field below is emitted ONLY inside Scheduler::Slurm's branch of
    // generate() — PBS and SGE generation is untouched, per design. Each is
    // "" / 0 = omit, the same convention `queue`/`memoryMbPerNode` already
    // use, so a spec built before these existed (or one for PBS/SGE) still
    // produces byte-identical output.
    std::string account;  ///< #SBATCH --account=
    std::string qos;      ///< #SBATCH --qos=
    /// OpenMP threads (or plain cores) per MPI rank — #SBATCH
    /// --cpus-per-task=. Distinct from tasksPerNode: that is how many RANKS
    /// share a node, this is how many CORES each rank itself gets.
    /// <= 1 omits the directive (SLURM's own default is 1 anyway).
    int cpusPerTask = 1;
    /// GPUs per node, emitted as --gres=gpu:N — the one gres spelling that
    /// works on essentially every SLURM cluster with GPU nodes, unlike the
    /// newer --gpus-per-node= (SLURM 20.02+ only). 0 omits the directive.
    int gpusPerNode = 0;
    /// Specific node(s) to run on — #SBATCH --nodelist=. Empty (the common
    /// case) lets the scheduler place the job anywhere that fits.
    std::string nodeList;
    /// Raw, verbatim lines inserted into the #SBATCH block after every
    /// structured directive above — the "never block the user" escape
    /// hatch for a directive this struct has no field for. Written EXACTLY
    /// as given, one line per line: a caller wanting a real directive
    /// includes its own "#SBATCH " prefix (or "##SBATCH " for one
    /// deliberately disabled, as a real cluster script often has).
    std::string extraDirectives;

    std::string setupLines; ///< verbatim shell prologue (module load, conda activate, ...)
    /// The actual payload — everything after the environment setup. Plain
    /// text, so it may itself launch under srun/mpirun with whatever flags
    /// the cluster needs ("mpirun -n 4 gpaw python run_gpaw.py"), and may
    /// span multiple lines for cleanup that has to run AFTER it (e.g. a
    /// trailing "conda deactivate") — written to the wrapper verbatim,
    /// exactly like setupLines.
    std::string command = "python3 run.py";

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
