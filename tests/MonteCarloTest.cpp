// Integration test for the native swap-atoms Monte Carlo sampler. A 4×4×4
// simple-cubic checkerboard of Cu/Au maximizes unlike neighbor bonds. With a
// positive (clustering-favoring) interaction the Metropolis sampler must lower
// the energy and reduce the unlike-bond fraction while conserving composition.
//
// Exit code 0 = pass.

#include "core/MonteCarlo.hpp"
#include "core/Structure.hpp"
#include "core/UnitCell.hpp"

#include <cstdio>
#include <map>

namespace {
int fail(const char* m)
{
    std::fprintf(stderr, "FAIL: %s\n", m);
    return 1;
}
} // namespace

int main()
{
    using namespace calango;

    const double a = 3.0;
    const int N = 4;
    core::Structure s;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            for (int k = 0; k < N; ++k) {
                const int z = ((i + j + k) % 2 == 0) ? 29 : 79; // Cu / Au
                s.addAtom({z, {i * a, j * a, k * a}});
            }
    s.setCell(core::UnitCell({N * a, 0, 0}, {0, N * a, 0}, {0, 0, N * a},
                             {true, true, true}));

    core::SwapMonteCarloOptions opt;
    opt.temperatureK = 300.0;
    opt.interactionEv = 0.10;   // V > 0 → favor like-pairs (segregation)
    opt.neighborCutoff = 3.2;   // 6 nearest neighbors at 3.0 Å
    opt.steps = 300000;
    opt.snapshotInterval = 30000;
    opt.seed = 1;

    const core::SwapMonteCarloResult res = core::runSwapMonteCarlo(s, opt);
    if (!res.note.empty())
        return fail(res.note.c_str());
    if (res.snapshots.empty())
        return fail("no snapshots recorded");

    // Composition conserved by swaps: 32 Cu + 32 Au.
    std::map<int, int> counts;
    for (const auto& at : res.finalStructure.atoms())
        ++counts[at.atomicNumber];
    if (counts[29] != 32 || counts[79] != 32) {
        std::fprintf(stderr, "FAIL: composition changed Cu=%d Au=%d\n",
                     counts[29], counts[79]);
        return 1;
    }

    // The checkerboard starts fully frustrated (unlike fraction 1.0); with
    // V > 0 the sampler must find a lower-energy, less-mixed configuration.
    if (res.bestEnergy >= res.initialEnergy)
        return fail("energy did not decrease");
    if (!(res.finalUnlikeFraction < 1.0))
        return fail("unlike-bond fraction did not drop from the checkerboard");
    if (res.acceptanceRatio < 0.0 || res.acceptanceRatio > 1.0)
        return fail("acceptance ratio out of range");

    std::printf("PASS: swap MC — E %.4f → %.4f eV (best %.4f), unlike frac "
                "1.000 → %.3f, acceptance %.1f%%\n",
                res.initialEnergy, res.finalEnergy, res.bestEnergy,
                res.finalUnlikeFraction, res.acceptanceRatio * 100.0);
    return 0;
}
