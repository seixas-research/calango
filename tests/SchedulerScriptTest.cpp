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
        // --mem is per node, so the number goes across unchanged, but the
        // UNIT does not (Task 3): SLURM's own directive is GB now, rounded
        // UP to the next whole GB — 64000 MB / 1024 = 62.5, so 63, never 62
        // (which would under-request memory).
        checkContains(script, "#SBATCH --mem=63G", "memory, per node, in GB");
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

    std::printf("Task 4 extensions (cpus-per-task/GPUs/node list/extra "
                "directives), SLURM only:\n");
    {
        RemoteJobSpec ext;
        ext.scheduler = Scheduler::Slurm;
        ext.cpusPerTask = 8;
        ext.gpusPerNode = 2;
        ext.nodeList = "gpu-node-03";
        // account/qos removed from RemoteJobSpec (Task 3) — extraDirectives
        // is now how a cluster that requires either reaches the job, so
        // this fixture puts them here rather than on their own removed
        // fields, right alongside the other verbatim/disabled-directive
        // cases it already covered.
        ext.extraDirectives = "#SBATCH --account=phys-2026\n"
                              "#SBATCH --qos=priority\n"
                              "#SBATCH --mail-type=END\n"
                              "##SBATCH --exclusive";
        const std::string script = SS::generate(ext);
        checkContains(script, "#SBATCH --account=phys-2026",
                      "account, via extraDirectives (Task 3's escape hatch, "
                      "not a dedicated field any more)");
        checkContains(script, "#SBATCH --qos=priority", "QOS, likewise");
        checkContains(script, "#SBATCH --cpus-per-task=8", "cpus per task");
        checkContains(script, "#SBATCH --gres=gpu:2", "GPUs per node");
        checkContains(script, "#SBATCH --nodelist=gpu-node-03", "node list");
        checkContains(script, "#SBATCH --mail-type=END",
                      "a real extra directive, verbatim");
        checkContains(script, "##SBATCH --exclusive",
                      "and a deliberately-disabled one, verbatim -- "
                      "SLURM itself ignores anything not spelled exactly "
                      "\"#SBATCH\", so this is intentionally inert");

        // Defaults omit every one of them.
        RemoteJobSpec plain;
        plain.scheduler = Scheduler::Slurm;
        const std::string plainScript = SS::generate(plain);
        checkAbsent(plainScript, "--account=",
                    "no account by default (nothing populates extraDirectives)");
        checkAbsent(plainScript, "--qos=", "no QOS by default, likewise");
        checkAbsent(plainScript, "--cpus-per-task=",
                    "no cpus-per-task at the default of 1 (SLURM's own "
                    "default already)");
        checkAbsent(plainScript, "--gres=", "no GPU request by default");
        checkAbsent(plainScript, "--nodelist=", "no node list by default");

        // PBS/SGE generation is untouched: the SAME populated spec, submitted
        // to a different scheduler, must not leak any SLURM-only directive.
        RemoteJobSpec extPbs = ext;
        extPbs.scheduler = Scheduler::Pbs;
        const std::string pbsScript = SS::generate(extPbs);
        checkAbsent(pbsScript, "account", "PBS carries none of the SLURM-only fields");
        checkAbsent(pbsScript, "gres", "not even by accidental substring overlap");
        RemoteJobSpec extSge = ext;
        extSge.scheduler = Scheduler::Sge;
        checkAbsent(SS::generate(extSge), "nodelist", "nor does SGE");
    }

    std::printf("A multi-line command (launcher line + post-run cleanup):\n");
    {
        RemoteJobSpec multi;
        multi.scheduler = Scheduler::Slurm;
        multi.command = "mpirun -n 4 gpaw python run_gpaw.py\n\nconda deactivate";
        const std::string script = SS::generate(multi);
        checkContains(script, "mpirun -n 4 gpaw python run_gpaw.py",
                      "the launcher line");
        checkContains(script, "conda deactivate",
                      "and cleanup running AFTER it, both from one Command "
                      "field");
        check(script.find("mpirun") < script.find("conda deactivate"),
              "in that order");
    }

    // --- job_heisenberg.sh: the real script this task was scoped from -----
    //
    // #!/bin/bash
    // #SBATCH -J sc4x4x4
    // ##SBATCH --ntasks=16
    // #SBATCH --nodes=1
    // #SBATCH --ntasks-per-node=4
    // #SBATCH -t 24:00:00
    // #SBATCH -p cpu
    // #SBATCH --mem 16G
    // #SBATCH -w work1
    //
    // module purge
    // source ~/.bashrc
    //
    // conda activate gpaw_env
    //
    // mpirun -n 4 gpaw python run_gpaw.py
    //
    // conda deactivate
    //
    // Reproduced here as the RemoteJobSpec the HPC panel's widgets would
    // build from those exact values (specFromUi() in HpcPanel.cpp) --
    // checked functionally (every directive present with the right value,
    // in the same relative order, prologue before the launcher line) rather
    // than as a literal byte diff: `-J`/`-t`/`-p`/`-w` vs. Calango's
    // `--job-name=`/`--time=`/`--partition=`/`--nodelist=`, and `--mem 16G`
    // vs. `--mem=16G` (space vs. `=` — SLURM accepts either), are exactly
    // the "trivial formatting differences" this task says are fine. The
    // memory VALUE itself is now an exact match, not just an equivalent one
    // (Task 3 switched Calango's own field to GB) — `job.memoryMbPerNode`
    // below stays MB internally (RemoteJobSpec's own representation is
    // unchanged), converted to GB only where the SLURM directive is
    // written. The commented-out `##SBATCH --ntasks=16` is not a
    // functional directive at all (SLURM only ever reads a line spelled
    // EXACTLY "#SBATCH"), so it needs no dedicated field -- a user who wants
    // that exact line reproduced types it into "Extra #SBATCH lines".
    std::printf("job_heisenberg.sh fixture:\n");
    {
        RemoteJobSpec job;
        job.scheduler = Scheduler::Slurm;
        job.jobName = "sc4x4x4";
        job.nodes = 1;
        job.tasksPerNode = 4;
        job.walltime = "24:00:00";
        job.queue = "cpu";
        job.memoryMbPerNode = 16 * 1024; // 16G
        job.nodeList = "work1";
        job.setupLines = "module purge\nsource ~/.bashrc\n\nconda activate gpaw_env";
        job.command = "mpirun -n 4 gpaw python run_gpaw.py\n\nconda deactivate";
        const std::string script = SS::generate(job);

        checkContains(script, "#SBATCH --job-name=sc4x4x4", "-J sc4x4x4");
        checkContains(script, "#SBATCH --nodes=1", "--nodes=1");
        checkContains(script, "#SBATCH --ntasks-per-node=4", "--ntasks-per-node=4");
        checkContains(script, "#SBATCH --time=24:00:00", "-t 24:00:00");
        checkContains(script, "#SBATCH --partition=cpu", "-p cpu");
        checkContains(script, "#SBATCH --mem=16G",
                      "--mem 16G -- an EXACT match now (Task 3), not just "
                      "an equivalent one in a different unit");
        checkContains(script, "#SBATCH --nodelist=work1", "-w work1");
        checkContains(script, "module purge", "module purge, verbatim");
        checkContains(script, "source ~/.bashrc", "source ~/.bashrc, verbatim");
        checkContains(script, "conda activate gpaw_env", "conda activate gpaw_env");
        checkContains(script, "mpirun -n 4 gpaw python run_gpaw.py",
                      "the exact launcher line");
        checkContains(script, "conda deactivate", "and the trailing cleanup");

        // Order: every #SBATCH directive before the blank line that starts
        // the prologue, the prologue before the launcher, the launcher
        // before the cleanup that has to run after it -- the same shape
        // job_heisenberg.sh itself has, not just the same ingredients.
        const auto pos = [&script](const char* needle) {
            return script.find(needle);
        };
        check(pos("--job-name=sc4x4x4") < pos("--nodelist=work1"),
              "job name precedes the node list, matching the script's own "
              "directive order");
        check(pos("--nodelist=work1") < pos("module purge"),
              "every #SBATCH directive precedes the prologue");
        check(pos("module purge") < pos("source ~/.bashrc"),
              "module purge precedes sourcing .bashrc");
        check(pos("source ~/.bashrc") < pos("conda activate gpaw_env"),
              "which precedes activating the environment");
        check(pos("conda activate gpaw_env") < pos("mpirun -n 4 gpaw python"),
              "which precedes the launcher line");
        check(pos("mpirun -n 4 gpaw python") < pos("conda deactivate"),
              "which precedes deactivating it again");
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
