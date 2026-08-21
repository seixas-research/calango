// Benzene-ring / sp2-percolation analysis test.
//
// Every check here is against a closed form or a topological invariant, not
// against a previous run of the code:
//
//   - A defect-free, fully periodic (both in-plane directions) honeycomb
//     sheet of N atoms has EXACTLY N/2 hexagonal faces. This is Euler's
//     formula on a torus (genus 1, chi = 0): V = N, every vertex is
//     3-coordinate so E = 3N/2, and F = E - V = N/2 for a bipartite,
//     girth-6 lattice where every face is a hexagon.
//   - GrapheneOxideBuilder's own nanoflake family is exact: index m gives
//     3m(m-1)+1 fused rings (its own header comment) — benzene at m=1 (one
//     ring), coronene at m=2 (seven).
//   - Disabling periodicity along one in-plane axis of an otherwise
//     unmodified periodic sheet (Structure::cell().setPbc()) turns it into
//     a nanoribbon WITHOUT touching a single atom position: bonds that used
//     to wrap around that axis simply stop being detected, so it must
//     percolate along the axis still periodic and never along the one that
//     is not — independently of any percolation bookkeeping, since
//     Structure::detectBonds() itself never emits a shift on a
//     non-periodic axis.
//
// Ring detection reuses GrapheneOxideBuilder::functionalGroupLabels() for
// intact/disrupted classification — no second chemistry method.

#include "core/GrapheneOxidePercolation.hpp"

#include "core/GrapheneOxideBuilder.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

using calango::core::analyzeRingPercolation;
using calango::core::analyzeRingPercolationTrajectory;
using calango::core::GrapheneOxideBuilder;
using calango::core::RingPercolationResult;
using calango::core::Structure;
using Base = GrapheneOxideBuilder::Base;
using Config = GrapheneOxideBuilder::Config;
using Dosing = GrapheneOxideBuilder::Dosing;
using Group = GrapheneOxideBuilder::Group;
using Lattice = GrapheneOxideBuilder::Lattice;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition)
        ++failures;
}

} // namespace

int main()
{
    std::printf("Pristine periodic sheet — fully intact, percolates both in-plane axes:\n");
    {
        Config config;
        config.lattice = Lattice::Rectangular;
        config.supercell[0] = 4;
        config.supercell[1] = 4;
        const Structure sheet = GrapheneOxideBuilder::pristine(config);
        check(sheet.size() == 64, "4x4 rectangular supercell has 64 carbons");

        const RingPercolationResult result = analyzeRingPercolation(sheet);
        check(result.rings.size() == sheet.size() / 2,
              "ring count is exactly N/2 (Euler's formula on a torus)");
        check(result.intactRingFraction == 1.0, "every ring is intact (no functional groups)");
        check(result.sp2CarbonFraction == 1.0, "every carbon is sp2");
        check(result.periodicAxis[0] && result.periodicAxis[1] && result.periodicAxis[2],
              "builder's periodic sheet is periodic on all three axes (z is vacuum)");
        check(result.percolatesAxis[0] && result.percolatesAxis[1] && !result.percolatesAxis[2],
              "percolates both in-plane axes, not the vacuum axis");
        check(result.domains.size() == 1, "one connected sp2 domain spans the whole sheet");
        check(result.largestDomain == 0
                  && result.domains[0].rings.size() == result.rings.size(),
              "the single domain contains every ring");
    }

    std::printf("\nFully oxidized sheet — percolation destroyed:\n");
    {
        Config config;
        config.lattice = Lattice::Rectangular;
        config.supercell[0] = 4;
        config.supercell[1] = 4;
        config.dosing = Dosing::ExplicitCoverage;
        config.setCoverage(Group::Epoxide, 1.0);
        config.setCoverage(Group::Hydroxyl, 1.0);
        config.seed = 42;
        const Structure sheet = GrapheneOxideBuilder::build(config);

        const RingPercolationResult result = analyzeRingPercolation(sheet);
        check(result.intactRingFraction <= 0.1,
              "requesting full basal coverage leaves at most a handful of intact rings");
        check(!result.percolatesAxis[0] && !result.percolatesAxis[1],
              "saturating basal coverage leaves no percolating sp2 pathway");
    }

    std::printf("\nOne-direction-only percolating stripe:\n");
    {
        // Same builder, same atoms as the pristine-sheet case above — only
        // the cell's own periodicity flags change, from the outside, after
        // the structure already exists. This is the whole point of the
        // check: nothing about ring/bond detection is told "this is a
        // ribbon" directly, it falls out of Structure::detectBonds() no
        // longer wrapping along b.
        Config config;
        config.lattice = Lattice::Rectangular;
        config.supercell[0] = 6;
        config.supercell[1] = 8;
        Structure ribbon = GrapheneOxideBuilder::pristine(config);
        auto cell = ribbon.cell();
        cell.setPbc({true, false, false});
        ribbon.setCell(cell);

        const RingPercolationResult result = analyzeRingPercolation(ribbon);
        check(!result.rings.empty(), "ribbon still contains rings");
        check(result.intactRingFraction == 1.0,
              "every found ring is still unfunctionalized (pristine carbons throughout)");
        check(result.periodicAxis[0] && !result.periodicAxis[1] && !result.periodicAxis[2],
              "periodic along a only");
        check(result.percolatesAxis[0], "percolates along the still-periodic axis");
        check(!result.percolatesAxis[1] && !result.percolatesAxis[2],
              "never percolates a non-periodic axis (no bond ever wraps there)");
        check(result.domains.size() == 1, "ribbon interior is one connected sp2 domain");
    }

    std::printf("\nRing counts on a small flake (GrapheneOxideBuilder's own m -> ring-count "
                "formula, 3m(m-1)+1):\n");
    {
        Config config;
        config.base = Base::Nanoflake;
        config.flakeIndex = 1; // benzene: 3*1*0+1 = 1 ring
        const Structure benzene = GrapheneOxideBuilder::pristine(config);
        const RingPercolationResult result = analyzeRingPercolation(benzene);
        check(result.rings.size() == 1, "benzene (m=1) has exactly one ring");
        check(result.intactRingFraction == 1.0, "benzene's ring is intact");
        check(result.periodicAxis[0] == false && result.periodicAxis[1] == false
                  && result.periodicAxis[2] == false,
              "a flake is not periodic");
        check(!result.percolatesAxis[0] && !result.percolatesAxis[1] && !result.percolatesAxis[2],
              "an aperiodic flake never percolates");
    }
    {
        Config config;
        config.base = Base::Nanoflake;
        config.flakeIndex = 2; // coronene: 3*2*1+1 = 7 rings
        const Structure coronene = GrapheneOxideBuilder::pristine(config);
        const RingPercolationResult result = analyzeRingPercolation(coronene);
        check(result.rings.size() == 7, "coronene (m=2) has exactly seven rings");
        check(result.intactRingFraction == 1.0, "coronene's rings are all intact");
        check(result.domains.size() == 1, "coronene's seven fused rings form one sp2 domain");
    }

    std::printf("\nPer-frame trajectory analysis — a deterministic, seeded progressive-oxidation "
                "sequence (a live MDMC/ASE run is Python-driven and out of scope for a native "
                "Qt-free test; this exercises the same per-frame analysis loop the trajectory "
                "viewer calls):\n");
    {
        std::vector<Structure> frames;
        const double coverages[] = {0.0, 0.05, 0.15, 0.35, 0.7};
        for (const double coverage : coverages) {
            Config config;
            config.lattice = Lattice::Rectangular;
            config.supercell[0] = 4;
            config.supercell[1] = 4;
            config.dosing = Dosing::ExplicitCoverage;
            config.setCoverage(Group::Epoxide, coverage);
            config.seed = 7; // same seed every frame: only the dose changes
            frames.push_back(coverage == 0.0 ? GrapheneOxideBuilder::pristine(config)
                                              : GrapheneOxideBuilder::build(config));
        }

        const std::vector<RingPercolationResult> results
            = analyzeRingPercolationTrajectory(frames);
        check(results.size() == frames.size(), "one result per frame");
        check(results.front().intactRingFraction == 1.0, "frame 0 (pristine) is fully intact");
        check(results.front().percolatesAxis[0] && results.front().percolatesAxis[1],
              "frame 0 percolates both in-plane axes");

        bool nonIncreasing = true;
        for (std::size_t i = 1; i < results.size(); ++i)
            if (results[i].intactRingFraction > results[i - 1].intactRingFraction + 1e-9)
                nonIncreasing = false;
        check(nonIncreasing,
              "intact-ring fraction never increases as the oxidation dose rises");
        check(results.back().intactRingFraction < results.front().intactRingFraction,
              "the last, most-oxidized frame has strictly fewer intact rings than the first");
    }

    std::printf(failures == 0 ? "\nAll ring-percolation checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
