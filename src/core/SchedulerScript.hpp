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
    int tasks = 1;          ///< ntasks / ncpus / smp slots
    std::string walltime = "01:00:00"; ///< HH:MM:SS
    std::string setupLines; ///< verbatim shell prologue (module load, conda activate, ...)
    std::string command = "python3 run.py"; ///< the actual payload
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
