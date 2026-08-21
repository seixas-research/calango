#include "core/SchedulerScript.hpp"

#include <algorithm>
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

    const int nodes = spec.nodes > 0 ? spec.nodes : 1;
    const int perNode = spec.tasksPerNode > 0 ? spec.tasksPerNode : 1;

    switch (spec.scheduler) {
    case Scheduler::Slurm:
        out << "#SBATCH --job-name=" << name << "\n"
            << "#SBATCH --output=" << kStdoutFile << "\n"
            << "#SBATCH --error=" << kStderrFile << "\n"
            << "#SBATCH --nodes=" << nodes << "\n"
            << "#SBATCH --ntasks-per-node=" << perNode << "\n"
            << "#SBATCH --time=" << spec.walltime << "\n";
        // --mem is per node in SLURM, which is what the field means.
        if (spec.memoryMbPerNode > 0)
            out << "#SBATCH --mem=" << spec.memoryMbPerNode << "M\n";
        if (!spec.queue.empty())
            out << "#SBATCH --partition=" << spec.queue << "\n";
        // SLURM-only extensions (Task 4): each "" / 0 = omit, so a spec
        // that never touches these produces exactly the output it always
        // did.
        if (!spec.account.empty())
            out << "#SBATCH --account=" << spec.account << "\n";
        if (!spec.qos.empty())
            out << "#SBATCH --qos=" << spec.qos << "\n";
        if (spec.cpusPerTask > 1)
            out << "#SBATCH --cpus-per-task=" << spec.cpusPerTask << "\n";
        if (spec.gpusPerNode > 0)
            out << "#SBATCH --gres=gpu:" << spec.gpusPerNode << "\n";
        if (!spec.nodeList.empty())
            out << "#SBATCH --nodelist=" << spec.nodeList << "\n";
        if (!spec.extraDirectives.empty()) {
            out << spec.extraDirectives;
            if (spec.extraDirectives.back() != '\n')
                out << "\n";
        }
        break;

    case Scheduler::Pbs:
        out << "#PBS -N " << name << "\n"
            << "#PBS -o " << kStdoutFile << "\n"
            << "#PBS -e " << kStderrFile << "\n";
        // One select chunk per node. Memory belongs INSIDE the chunk — a
        // separate "-l mem=" is the whole job on some PBS builds and per
        // chunk on others, and the ambiguity is not worth inheriting.
        out << "#PBS -l select=" << nodes << ":ncpus=" << perNode
            << ":mpiprocs=" << perNode;
        if (spec.memoryMbPerNode > 0)
            out << ":mem=" << spec.memoryMbPerNode << "mb";
        out << "\n"
            << "#PBS -l walltime=" << spec.walltime << "\n";
        if (!spec.queue.empty())
            out << "#PBS -q " << spec.queue << "\n";
        // PBS starts jobs in $HOME — hop back to the submission directory.
        out << "\ncd \"$PBS_O_WORKDIR\"\n";
        break;

    case Scheduler::Sge: {
        const std::string pe =
            spec.parallelEnvironment.empty() ? "smp" : spec.parallelEnvironment;
        out << "#$ -N " << name << "\n"
            << "#$ -o " << kStdoutFile << "\n"
            << "#$ -e " << kStderrFile << "\n"
            << "#$ -cwd\n"
            // SGE requests SLOTS, not nodes: the parallel environment decides
            // how they are laid out across machines. So the total goes here
            // and the layout is the site's PE definition, which is why the PE
            // name is a setting rather than a constant.
            << "#$ -pe " << pe << " " << spec.totalTasks() << "\n"
            << "#$ -l h_rt=" << spec.walltime << "\n";
        if (spec.memoryMbPerNode > 0) {
            // h_vmem is PER SLOT. Dividing is not a nicety: asking for a
            // node's worth of memory on every slot multiplies the request by
            // the core count and the job simply never starts.
            const int perSlot = std::max(1, spec.memoryMbPerNode / perNode);
            out << "#$ -l h_vmem=" << perSlot << "M\n";
        }
        if (!spec.queue.empty())
            out << "#$ -q " << spec.queue << "\n";
        break;
    }
    }

    out << "\n";
    if (!spec.setupLines.empty()) {
        out << "# --- environment setup (from the HPC panel) ---\n"
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
