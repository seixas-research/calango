// GPAW MPI pre-flight (Task 2): the LOCAL, before-anything-is-staged check
// for "requested cores>1 for GPAW, but either the build has no MPI support
// or no mpirun/mpiexec launcher can be found" -- the exact failure mode
// behind "GPAW runs on 1 core despite cores=4" that a silent launch would
// hide.
//
// Layered like MaceTrainerPreflightTest.cpp:
//   1. cores<=1 and the "interpreter itself doesn't work" paths run
//      unconditionally -- no GPAW install needed.
//   2. The real build-detection path needs an actual GPAW install and looks
//      for a "gpaw_fast"-named conda environment (this repo's own documented
//      name, CLAUDE.md), skipping cleanly, loudly, if none is found.

#include "gui/CondaEnvs.hpp"
#include "gui/GpawMpiPreflight.hpp"

#include <QDir>
#include <QFileInfo>

#include <cstdio>
#include <cstdlib>

using namespace calango::gui;

namespace {

int failures = 0;

void check(bool ok, const char* label)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", label);
    if (!ok)
        ++failures;
}

QString findGpawPython()
{
    // Exact "gpaw_fast" match preferred over a loose substring search --
    // CLAUDE.md documents "gpaw_env" on this machine as a STALE, unrelated
    // build (25.7.0) that must never be picked by accident.
    const QList<CondaEnv> envs = CondaEnvs::discover();
    for (const CondaEnv& env : envs)
        if (env.name.compare(QStringLiteral("gpaw_fast"), Qt::CaseInsensitive) == 0)
            return CondaEnvs::resolvePython(env.path);
    return QString();
}

} // namespace

int main()
{
    std::printf("GPAW MPI pre-flight: cores<=1 is always trivially ok\n");
    {
        const auto zero = checkGpawMpi(QStringLiteral("/definitely/not/real"), 0);
        check(zero.ok, "0 cores needs no interpreter and no MPI at all");
        const auto one = checkGpawMpi(QString(), 1);
        check(one.ok, "1 core needs no interpreter either -- nothing to parallelize");
    }

    std::printf("GPAW MPI pre-flight: a broken interpreter, cores>1\n");
    {
        const auto empty = checkGpawMpi(QString(), 4);
        check(!empty.ok && !empty.errorMessage.isEmpty(),
              "an empty interpreter string is refused with a message");

        const auto bogus =
            checkGpawMpi(QStringLiteral("/definitely/not/a/real/interpreter"), 4);
        check(!bogus.ok && !bogus.errorMessage.isEmpty(),
              "a nonexistent interpreter path fails cleanly, not a crash or a hang");
    }

    const QString gpawPython = findGpawPython();
    if (gpawPython.isEmpty()) {
        std::printf(
            "SKIP: no conda environment named \"gpaw_fast\" was found "
            "(checked ~/miniconda3/envs and the other common locations) -- "
            "the real build-detection path below needs an actual GPAW "
            "install and cannot run without one.\n");
        std::printf("\n%d check(s) FAILED.\n", failures);
        return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    std::printf("Found a GPAW-capable interpreter: %s\n", qPrintable(gpawPython));

    std::printf("GPAW MPI pre-flight: the real gpaw_fast build, cores>1\n");
    {
        const auto result = checkGpawMpi(gpawPython, 4);
        check(result.mpiEnabled,
              "gpaw_fast reports MPI support compiled in (gpaw.cgpaw.have_mpi)"
              " -- CLAUDE.md's own verified fact about this environment");
        // launcherFound is genuinely machine-dependent (an mpirun that
        // happens not to be reachable next to this interpreter or on PATH
        // would be a real, reportable problem) -- reported, not asserted
        // true unconditionally, so this test does not assume a specific
        // machine's PATH.
        std::printf("       launcher found: %s (%s)\n",
                   result.launcherFound ? "yes" : "no",
                   qPrintable(result.launcherPath));
        if (result.launcherFound) {
            check(result.ok,
                  "with both an MPI-enabled build and a launcher found, the "
                  "pre-flight passes -- this is the case that actually "
                  "parallelizes");
        } else {
            check(!result.ok && result.errorMessage.contains(QStringLiteral("mpirun")),
                  "with no launcher found, the pre-flight fails and names "
                  "the missing launcher specifically");
        }

        const auto trivial = checkGpawMpi(gpawPython, 1);
        check(trivial.ok, "the SAME real interpreter is fine at cores=1 "
                          "regardless of MPI/launcher state");
    }

    std::printf("GPAW MPI pre-flight: mpirun found next to the interpreter itself\n");
    {
        // findMpiLauncher() (GpawMpiPreflight.cpp) checks beside the
        // interpreter FIRST -- this is what makes a conda-installed
        // OpenMPI/MPICH resolve without relying on the system PATH at all,
        // matching exactly where JobRunner::start() itself looks (it
        // prepends the interpreter's own bin/ to PATH before running the
        // job). Verified indirectly: gpaw_fast's own bin/ is checked here
        // directly, independent of whatever this machine's PATH happens to
        // contain.
        const QString interpreterDir = QFileInfo(gpawPython).absolutePath();
        const bool mpirunBesideInterpreter =
            QFileInfo(QDir(interpreterDir).filePath(QStringLiteral("mpirun")))
                .isExecutable();
        std::printf("       mpirun beside the gpaw_fast interpreter: %s\n",
                   mpirunBesideInterpreter ? "yes" : "no");
        if (mpirunBesideInterpreter) {
            const auto result = checkGpawMpi(gpawPython, 4);
            check(result.launcherFound
                      && result.launcherPath.startsWith(interpreterDir),
                  "the launcher found is the one INSIDE the interpreter's "
                  "own env, not a same-named binary elsewhere on PATH");
        }
    }

    if (failures == 0) {
        std::printf("\nAll GPAW MPI pre-flight checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d GPAW MPI pre-flight check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
