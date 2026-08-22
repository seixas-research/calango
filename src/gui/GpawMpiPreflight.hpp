#pragma once

#include <QString>

namespace calango::gui {

/// Whether a parallel GPAW launch (`RunCommands::defaultTemplate(Gpaw)`'s
/// `mpirun -n {cores} gpaw python {script}`) will actually run in parallel
/// under a GIVEN interpreter, checked LOCALLY before a job is staged or
/// submitted — the same "catch it before a queue wait" reasoning as
/// VaspPotcarPreflight.hpp, applied to the bug this was written for
/// ("GPAW runs on 1 core despite cores=4").
///
/// Two independent things can go wrong, and they fail differently, which is
/// why this checks both explicitly rather than just trying to run the job
/// and seeing what happens:
///   * The GPAW build has no MPI support compiled in
///     (`gpaw.cgpaw.have_mpi` is False — a build-time property, unrelated to
///     how THIS process happens to be launched). `mpirun -n 4` in front of
///     it does not fail outright; it spawns 4 completely independent serial
///     GPAW processes that each redundantly perform the WHOLE calculation
///     rather than 4 ranks of one domain-decomposed run — worse than
///     serial, not merely equivalent to it, and nothing about the run's own
///     output distinguishes this from a real parallel run.
///   * No `mpirun`/`mpiexec` launcher can be found at all, in which case the
///     generated shell command simply fails outright with a "command not
///     found" the user sees only after staging/submitting.
struct GpawMpiPreflightResult {
    bool ok = false;
    /// gpaw.cgpaw.have_mpi under `pythonExecutable` — a build-time property.
    /// False both when the build is genuinely serial-only and when the
    /// probe itself could not run (see errorMessage for which).
    bool mpiEnabled = false;
    /// Whether an mpirun/mpiexec launcher was found — see launcherPath.
    bool launcherFound = false;
    /// The launcher actually found (absolute path), empty if none. Searched
    /// next to `pythonExecutable` first — a conda-installed OpenMPI/MPICH
    /// keeps mpirun in the same environment's bin/, exactly where
    /// JobRunner::start() itself prepends to PATH before running the job —
    /// then the system PATH, matching what the launched job will actually
    /// see either way.
    QString launcherPath;
    /// Human-readable reason, populated whenever `!ok`. Names the specific
    /// problem(s) found (build vs. launcher vs. both), never a raw
    /// traceback.
    QString errorMessage;
};

/// `cores <= 1` always returns `ok = true` trivially: nothing is being
/// parallelized, so neither an MPI-less build nor a missing mpirun is a
/// problem — this mirrors a bare `python script.py` invocation, which is
/// what a single-core GPAW job already resolves to.
GpawMpiPreflightResult checkGpawMpi(const QString& pythonExecutable,
                                    int cores, int timeoutMs = 15000);

} // namespace calango::gui
