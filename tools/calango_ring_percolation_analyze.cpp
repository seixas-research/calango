// calango-ring-percolation-analyze: the native counterpart to
// core::analyzeRingPercolationTrajectory() reachable from OUTSIDE the GUI —
// a test harness verifying a real MDMC trajectory, or a user who already
// has a trajectory file and just wants the ring/percolation numbers for it
// without opening the app.
//
// Reads a multi-frame trajectory via ase.io.read (any format ASE
// understands: extxyz, .traj, ...) and prints, per frame, the same
// analysis core::GrapheneOxidePercolation.hpp computes for the Analysis ->
// "Benzene-Ring / sp2 Percolation Analysis…" dialog, as a hand-formatted
// JSON array on stdout — one object per frame, in trajectory order.
//
// Usage: calango-ring-percolation-analyze <trajectory-file> [ase-format]

#include "core/GrapheneOxidePercolation.hpp"
#include "core/Structure.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

using namespace calango;

namespace {

void printFrame(const core::RingPercolationResult& r, bool first)
{
    int intactCount = 0;
    for (const core::CarbonRing& ring : r.rings)
        if (ring.intact)
            ++intactCount;
    const int largestRings = r.largestDomain >= 0
        ? static_cast<int>(
              r.domains[static_cast<std::size_t>(r.largestDomain)].rings.size())
        : 0;

    std::printf(
        "%s  {\"rings\": %zu, \"intact_rings\": %d, \"intact_fraction\": %.10g, "
        "\"sp2_fraction\": %.10g, \"domains\": %zu, \"largest_domain_rings\": %d, "
        "\"percolates_a\": %s, \"percolates_b\": %s, \"percolates_c\": %s}",
        first ? "" : ",\n", r.rings.size(), intactCount, r.intactRingFraction,
        r.sp2CarbonFraction, r.domains.size(), largestRings,
        r.percolatesAxis[0] ? "true" : "false", r.percolatesAxis[1] ? "true" : "false",
        r.percolatesAxis[2] ? "true" : "false");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: calango-ring-percolation-analyze <trajectory-file> "
                     "[ase-format]\n");
        return EXIT_FAILURE;
    }
    const std::string path = argv[1];
    const std::string format = argc >= 3 ? argv[2] : std::string();

    // RAII interpreter, declared first so every pybind11 object below is
    // destroyed before it finalizes — the same ordering every embedded-Python
    // test in this repo uses.
    pybridge::PythonEngine python;

    std::vector<core::Structure> frames;
    try {
        frames = pybridge::AseBridge::readTrajectory(path, format);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "could not read %s: %s\n", path.c_str(), e.what());
        return EXIT_FAILURE;
    }
    if (frames.empty()) {
        std::fprintf(stderr, "no frames in %s\n", path.c_str());
        return EXIT_FAILURE;
    }

    const std::vector<core::RingPercolationResult> results =
        core::analyzeRingPercolationTrajectory(frames);

    std::printf("[\n");
    for (std::size_t i = 0; i < results.size(); ++i)
        printFrame(results[i], i == 0);
    std::printf("\n]\n");
    return EXIT_SUCCESS;
}
