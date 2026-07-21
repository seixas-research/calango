#include "core/SchedulerScript.hpp"

#include <sstream>

namespace calango::core {

namespace {

/// Scheduler job names dislike whitespace and shell metacharacters —
/// keep [A-Za-z0-9._-], replace everything else with '_'.
std::string sanitizeJobName(const std::string& name)
{
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        out.push_back(ok ? c : '_');
    }
    if (out.empty())
        out = "calango";
    return out;
}

} // namespace

namespace SchedulerScript {

std::string generate(const RemoteJobSpec& spec)
{
    const std::string name = sanitizeJobName(spec.jobName);
    std::ostringstream out;
    out << "#!/bin/bash\n";

    switch (spec.scheduler) {
    case Scheduler::Slurm:
        out << "#SBATCH --job-name=" << name << "\n"
            << "#SBATCH --output=" << kStdoutFile << "\n"
            << "#SBATCH --error=" << kStderrFile << "\n"
            << "#SBATCH --nodes=1\n"
            << "#SBATCH --ntasks=" << spec.tasks << "\n"
            << "#SBATCH --time=" << spec.walltime << "\n";
        if (!spec.queue.empty())
            out << "#SBATCH --partition=" << spec.queue << "\n";
        break;

    case Scheduler::Pbs:
        out << "#PBS -N " << name << "\n"
            << "#PBS -o " << kStdoutFile << "\n"
            << "#PBS -e " << kStderrFile << "\n"
            << "#PBS -l select=1:ncpus=" << spec.tasks << "\n"
            << "#PBS -l walltime=" << spec.walltime << "\n";
        if (!spec.queue.empty())
            out << "#PBS -q " << spec.queue << "\n";
        // PBS starts jobs in $HOME — hop back to the submission directory.
        out << "\ncd \"$PBS_O_WORKDIR\"\n";
        break;

    case Scheduler::Sge:
        out << "#$ -N " << name << "\n"
            << "#$ -o " << kStdoutFile << "\n"
            << "#$ -e " << kStderrFile << "\n"
            << "#$ -cwd\n"
            << "#$ -pe smp " << spec.tasks << "\n"
            << "#$ -l h_rt=" << spec.walltime << "\n";
        if (!spec.queue.empty())
            out << "#$ -q " << spec.queue << "\n";
        break;
    }

    out << "\n";
    if (!spec.setupLines.empty()) {
        out << "# --- environment setup (from the Remote Access panel) ---\n"
            << spec.setupLines;
        if (spec.setupLines.back() != '\n')
            out << "\n";
        out << "\n";
    }
    out << spec.command << "\n";
    return out.str();
}

std::string schedulerKey(Scheduler scheduler)
{
    switch (scheduler) {
    case Scheduler::Slurm:
        return "slurm";
    case Scheduler::Pbs:
        return "pbs";
    case Scheduler::Sge:
        return "sge";
    }
    return "slurm";
}

std::string submitCommand(Scheduler scheduler)
{
    switch (scheduler) {
    case Scheduler::Slurm:
        return "sbatch job.sh";
    case Scheduler::Pbs:
    case Scheduler::Sge:
        return "qsub job.sh";
    }
    return "sbatch job.sh";
}

std::string cancelCommand(Scheduler scheduler, const std::string& jobId)
{
    switch (scheduler) {
    case Scheduler::Slurm:
        return "scancel " + jobId;
    case Scheduler::Pbs:
    case Scheduler::Sge:
        return "qdel " + jobId;
    }
    return "scancel " + jobId;
}

} // namespace SchedulerScript
} // namespace calango::core
