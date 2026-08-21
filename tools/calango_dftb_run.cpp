// calango-dftb-run — the native SCC-DFTB engine's own entry point.
//
// Not invoked directly by a user: the thin Python wrapper
// AseScriptGenerator.cpp emits for CalculatorKind::CalangoDftb writes the
// structure (via ordinary ase.io.write) and a task manifest next to it, then
// execs this binary as a subprocess and relays its stdout line for line —
// see DftbTaskConfig.hpp for exactly why the manifest is plain text and not
// JSON, and JobRunner.hpp for the CALANGO_* marker convention both this
// binary and every generated Python script speak identically.
//
//     calango-dftb-run <manifest-path>
//
// Exit status is non-zero on any failure, matching every other CALANGO_*
// producer in this codebase.

#include "dftb/DftbEngine.hpp"
#include "dftb/DftbTaskConfig.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 2 || std::string(argv[1]) == "--help"
        || std::string(argv[1]) == "-h") {
        std::printf(
            "usage: calango-dftb-run <manifest-path>\n"
            "\n"
            "Reads the plain-text task manifest (see DftbTaskConfig.hpp),\n"
            "runs the native SCC-DFTB engine, and writes the result JSON\n"
            "the manifest's 'output' key names — bands.json, "
            "single_point.json,\n"
            "or the equivalent for whichever 'task' was requested.\n");
        return argc == 2 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    // Line-buffered even when stdout is a pipe (the normal case, launched
    // by the Python wrapper), so CALANGO_PROGRESS/CALANGO_RESULT markers
    // reach JobRunner promptly rather than sitting in a full-buffering
    // pipe buffer until exit.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    calango::dftb::DftbTaskConfig config;
    auto outcome = calango::dftb::loadDftbTaskConfig(argv[1], config);
    if (!outcome.ok()) {
        std::fprintf(stderr, "error: %s\n", outcome.message.c_str());
        return EXIT_FAILURE;
    }

    outcome = calango::dftb::runDftbTask(config);
    if (!outcome.ok()) {
        std::fprintf(stderr, "error: %s\n", outcome.message.c_str());
        return EXIT_FAILURE;
    }

    std::printf("CALANGO_DONE\n");
    return EXIT_SUCCESS;
}
