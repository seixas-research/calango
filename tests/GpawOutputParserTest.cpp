// GPAW output header "cores:  N" parsing (Task 1, 2026-08-22) — the
// post-launch half of "make silent-serial impossible": MainWindow compares
// this against the cores actually requested and flags a mismatch, rather
// than trusting that a correctly-built command line necessarily ran as one.

#include "core/GpawOutputParser.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using calango::core::parseGpawWorldSize;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

const char* kSerialHeader =
    "  __  _  _\n"
    " | _ |_)|_||  |\n"
    " |__||  | ||/\\| - 26.7.1b1\n"
    "\n"
    "User:   leseixas@safira\n"
    "Date:   Fri Aug 21 23:54:34 2026\n"
    "Arch:   arm64\n"
    "Pid:    4049\n"
    "CWD:    /Users/leseixas/calango_simulations/proc_0\n"
    "units:  Angstrom and eV\n"
    "cores:  1\n"
    "OpenMP: True\n";

const char* kParallelHeader =
    "  __  _  _\n"
    " | _ |_)|_||  |\n"
    " |__||  | ||/\\| - 26.7.1b1\n"
    "\n"
    "User:   leseixas@safira\n"
    "Date:   Sat Aug 22 00:24:09 2026\n"
    "Arch:   arm64\n"
    "Pid:    5209\n"
    "CWD:    /tmp/scratch\n"
    "units:  Angstrom and eV\n"
    "cores:  4\n"
    "OpenMP: True\n";

} // namespace

int main()
{
    std::printf("Real fixtures (verbatim proc_0 header, and a genuine "
               "4-rank mpirun run captured live):\n");
    {
        const auto serial = parseGpawWorldSize(kSerialHeader);
        check(serial.has_value() && *serial == 1,
              "proc_0's actual serial header reads back as 1");
        const auto parallel = parseGpawWorldSize(kParallelHeader);
        check(parallel.has_value() && *parallel == 4,
              "a real 4-rank mpirun run's header reads back as 4");
    }

    std::printf("Robustness:\n");
    {
        check(!parseGpawWorldSize("").has_value(),
              "an empty string is unknown, not misread as 1");
        check(!parseGpawWorldSize("nothing relevant here\nat all\n")
                   .has_value(),
              "text with no cores: line is unknown");
        check(parseGpawWorldSize("cores:  8\r\nOpenMP: True\r\n")
                      .value_or(-1)
                  == 8,
              "a CRLF-terminated line (Windows-written log) still parses");
        check(parseGpawWorldSize("cores:8").value_or(-1) == 8,
              "no space after the colon still parses");
        check(parseGpawWorldSize("  cores:  16  \n").value_or(-1) == 16,
              "leading indentation and trailing spaces are tolerated");
        check(!parseGpawWorldSize("cores:  many\n").has_value(),
              "a non-numeric value is unknown, not a crash or a garbage "
              "int");
        check(!parseGpawWorldSize("scores:  4\n").has_value(),
              "a line that merely CONTAINS \"cores:\" as a substring, "
              "rather than starting with it, is not matched");

        // A 60-line cap keeps this from scanning a routinely tens-of-MB
        // relaxation log line by line -- must not find a "cores:" that
        // only appears deep in the body (which never happens for a real
        // GPAW header, but the cap's own boundary is worth pinning).
        std::string farAway;
        for (int i = 0; i < 100; ++i)
            farAway += "filler line\n";
        farAway += "cores:  4\n";
        check(!parseGpawWorldSize(farAway).has_value(),
              "a \"cores:\" line past the header window is not found -- "
              "the cap is real, not just documentation");
    }

    std::printf("The requested-vs-actual comparison this feeds (Task 1's "
               "own \"flag the mismatch\" requirement):\n");
    {
        const auto actual = parseGpawWorldSize(kSerialHeader);
        const int requested = 4;
        check(actual.has_value() && *actual != requested,
              "proc_0's exact scenario is detected: requested=4, "
              "actual=1, a genuine mismatch worth flagging");
    }

    if (failures == 0) {
        std::printf("\nAll GPAW output parser checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d GPAW output parser check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
