// Batch-scheduler submission scripts.
//
// The three schedulers do not describe the same machine the same way, and the
// differences are silent: a request that is merely mistranslated does not
// error, it queues forever or runs on the wrong shape of allocation. So this
// checks the translation of ONE resource request into all three dialects.
//
// The memory field is the sharpest case. It means memory PER NODE, because
// that is how a user thinks about it, and:
//   SLURM  --mem is per node          -> maps across unchanged
//   PBS    memory sits in the chunk   -> maps across unchanged
//   SGE    h_vmem is per SLOT         -> must be DIVIDED by the slot count
// Getting the last one wrong multiplies the request by the cores per node,
// which on a 16-core node turns 4 GB into 64 GB and the job never starts.

#include "core/SchedulerScript.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

void checkContains(const std::string& text, const std::string& needle,
                   const std::string& what)
{
    const bool ok = text.find(needle) != std::string::npos;
    std::printf("  %s %s  [%s]\n", ok ? "ok  " : "FAIL", what.c_str(),
                needle.c_str());
    if (!ok) {
        ++failures;
        std::printf("      --- script ---\n%s\n", text.c_str());
    }
}

void checkAbsent(const std::string& text, const std::string& needle,
                 const std::string& what)
{
    const bool ok = text.find(needle) == std::string::npos;
    std::printf("  %s %s  [no %s]\n", ok ? "ok  " : "FAIL", what.c_str(),
                needle.c_str());
    if (!ok)
        ++failures;
}

} // namespace

int main()
{
    using calango::core::RemoteJobSpec;
    using calango::core::Scheduler;
    namespace SS = calango::core::SchedulerScript;

    // One request, expressed three ways: 4 nodes x 16 ranks, 64 GB per node,
    // 12 hours, on a named partition.
    RemoteJobSpec spec;
    spec.jobName = "calango run 01";  // spaces, to exercise sanitizing
    spec.queue = "compute";
    spec.nodes = 4;
    spec.tasksPerNode = 16;
    spec.memoryMbPerNode = 64000;
    spec.walltime = "12:00:00";
    spec.command = "mpirun -np 64 gpaw python run.py";

    check(spec.totalTasks() == 64, "4 nodes x 16 ranks is 64 tasks");

    std::printf("SLURM:\n");
    {
        spec.scheduler = Scheduler::Slurm;
        const std::string script = SS::generate(spec);
        checkContains(script, "#SBATCH --nodes=4", "nodes");
        checkContains(script, "#SBATCH --ntasks-per-node=16", "tasks per node");
        checkContains(script, "#SBATCH --time=12:00:00", "walltime");
        checkContains(script, "#SBATCH --partition=compute", "partition");
        // --mem is per node, so the number goes across unchanged.
        checkContains(script, "#SBATCH --mem=64000M", "memory, per node");
        checkContains(script, "#SBATCH --job-name=calango_run_01",
                      "job name with spaces sanitized");
        checkContains(script, "#SBATCH --output=calango_job.out",
                      "stdout capture the monitor tails");
        // The old generator hardcoded a single node; a multi-node request that
        // silently ran on one would look like a slow job, not a wrong one.
        checkAbsent(script, "--nodes=1", "no hardcoded single node");
        check(script.rfind("#!/bin/bash", 0) == 0, "starts with a shebang");
        checkContains(script, "mpirun -np 64 gpaw python run.py",
                      "and ends with the payload");
    }

    std::printf("PBS:\n");
    {
        spec.scheduler = Scheduler::Pbs;
        const std::string script = SS::generate(spec);
        // Chunks are nodes; memory belongs inside the chunk.
        checkContains(script, "#PBS -l select=4:ncpus=16:mpiprocs=16:mem=64000mb",
                      "one select chunk per node, with its own memory");
        checkContains(script, "#PBS -l walltime=12:00:00", "walltime");
        checkContains(script, "#PBS -q compute", "queue");
        // PBS starts in $HOME, so the script has to return to the job dir or
        // every relative path in the payload resolves somewhere else.
        checkContains(script, "cd \"$PBS_O_WORKDIR\"",
                      "returns to the submission directory");
    }

    std::printf("SGE:\n");
    {
        spec.scheduler = Scheduler::Sge;
        const std::string script = SS::generate(spec);
        // SGE asks for SLOTS; the parallel environment decides the layout.
        checkContains(script, "#$ -pe smp 64", "total slots, not nodes");
        checkContains(script, "#$ -l h_rt=12:00:00", "walltime");
        checkContains(script, "#$ -cwd", "runs in the submission directory");
        // THE TRAP: 64000 MB per node over 16 slots is 4000 MB per slot.
        checkContains(script, "#$ -l h_vmem=4000M",
                      "memory divided down to per-slot, which is what h_vmem "
                      "means");
        checkAbsent(script, "h_vmem=64000M",
                    "and not the per-node figure, which would be a 16x "
                    "over-request");

        // The PE name is site-specific; "smp" cannot span nodes anywhere.
        RemoteJobSpec mpi = spec;
        mpi.parallelEnvironment = "mpi";
        checkContains(SS::generate(mpi), "#$ -pe mpi 64",
                      "the parallel environment is configurable");
    }

    std::printf("Defaults and edge cases:\n");
    {
        RemoteJobSpec minimal;
        minimal.scheduler = Scheduler::Slurm;
        const std::string script = SS::generate(minimal);
        // No memory request means "cluster default" — an explicit 0 must not
        // become "--mem=0M", which SLURM reads as "all the memory on the node".
        checkAbsent(script, "--mem=", "zero memory asks for the default");
        checkAbsent(script, "--partition=", "an empty queue is omitted");
        checkContains(script, "#SBATCH --nodes=1", "one node by default");

        // Zero or negative counts are clamped rather than emitted: a
        // "--nodes=0" is rejected at submission, which is a confusing way to
        // discover an empty spin box.
        RemoteJobSpec zero;
        zero.nodes = 0;
        zero.tasksPerNode = 0;
        const std::string clamped = SS::generate(zero);
        checkContains(clamped, "#SBATCH --nodes=1", "zero nodes clamps to one");
        checkContains(clamped, "#SBATCH --ntasks-per-node=1",
                      "and zero tasks likewise");
        check(zero.totalTasks() == 1, "totalTasks() clamps too");

        // Tiny memory on many slots must not round to zero on SGE.
        RemoteJobSpec tiny;
        tiny.scheduler = Scheduler::Sge;
        tiny.tasksPerNode = 64;
        tiny.memoryMbPerNode = 8;
        checkContains(SS::generate(tiny), "h_vmem=1M",
                      "a per-slot share below 1 MB floors at 1 rather than "
                      "asking for none");
    }

    std::printf("Commands:\n");
    {
        check(SS::submitCommand(Scheduler::Slurm) == "sbatch job.sh",
              "SLURM submits with sbatch");
        check(SS::submitCommand(Scheduler::Pbs) == "qsub job.sh",
              "PBS and SGE with qsub");
        check(SS::cancelCommand(Scheduler::Slurm, "123") == "scancel 123",
              "and cancel with scancel / qdel");
        check(SS::cancelCommand(Scheduler::Sge, "123") == "qdel 123",
              "respectively");
        check(SS::schedulerKey(Scheduler::Pbs) == "pbs",
              "the wire name the remote helper switches on");
    }

    if (failures == 0) {
        std::printf("\nAll scheduler-script checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d scheduler-script check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
