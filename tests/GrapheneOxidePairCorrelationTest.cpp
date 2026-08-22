// GO Pair Correlation (Warren-Cowley) test.
//
// The shell-enumeration counts and every synthetic-decoration alpha below
// are checked against DIRECT, independent recomputation from a real
// pristine sheet's own bonding/geometry -- never against a memorized
// honeycomb lattice-sum formula, per this module's own requirement.

#include "core/GrapheneOxidePairCorrelation.hpp"

#include "core/AngleGeometry.hpp"
#include "core/GrapheneOxideGroupAnalysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using calango::core::Atom;
using calango::core::GrapheneOxideBuilder;
using calango::core::Structure;
using calango::core::WarrenCowleyOptions;
using calango::core::WarrenCowleyResult;
using calango::core::analyzeGrapheneOxideGroups;
using calango::core::analyzeGrapheneOxidePairCorrelation;
using calango::core::computeWarrenCowley;
using calango::core::honeycombShellCutoffs;
using Group = GrapheneOxideBuilder::Group;
using Config = GrapheneOxideBuilder::Config;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition)
        ++failures;
}

/// A large periodic PRIMITIVE-cell pristine sheet: 2 atoms/cell, and
/// crucially, atom index parity (even/odd) is EXACTLY the honeycomb's two
/// sublattices -- the builder appends the primitive cell's two basis atoms
/// in the same order every cell, so index % 2 never depends on which cell
/// an atom came from. Verified directly in the "sublattices are bipartite"
/// check below, not assumed.
Structure primitiveSheet(int n)
{
    Config config;
    config.lattice = GrapheneOxideBuilder::Lattice::Primitive;
    config.supercell[0] = config.supercell[1] = n;
    return GrapheneOxideBuilder::pristine(config);
}

/// A carbon-only structure with the same geometry as `source`, but every
/// atom's atomic number replaced by `label(index)` -- the synthetic
/// "species" WarrenCowley-style tests decorate a lattice with.
Structure relabeled(const Structure& source,
                    const std::vector<int>& labels)
{
    Structure out;
    out.setCell(source.cell());
    for (std::size_t i = 0; i < source.size(); ++i) {
        Atom atom;
        atom.atomicNumber = labels[i];
        atom.position = source.atoms()[i].position;
        out.addAtom(atom);
    }
    return out;
}

int speciesIndex(const WarrenCowleyResult& wc, int z)
{
    for (std::size_t i = 0; i < wc.species.size(); ++i)
        if (wc.species[i] == z)
            return static_cast<int>(i);
    return -1;
}

} // namespace

int main()
{
    std::printf("Honeycomb shell enumeration -- verified against a real "
                "pristine sheet:\n");
    {
        const Structure sheet = primitiveSheet(14);
        const auto& atoms = sheet.atoms();
        // Any atom works as the center: PBC-aware minimum-image distance,
        // not a raw Cartesian one, so the atom's true neighbors are found
        // even when it sits near the tile's own periodic seam (a real bug
        // in an earlier version of honeycombShellCutoffs() itself, caught
        // by exactly this kind of independent recount).
        const std::size_t center = atoms.size() / 2;

        std::vector<double> distances;
        for (std::size_t i = 0; i < atoms.size(); ++i) {
            if (i == center)
                continue;
            const double d =
                calango::core::minimumImageVector(
                    sheet, static_cast<int>(center), static_cast<int>(i), 6.0)
                    .norm();
            if (d < 6.0) // comfortably covers the first several shells
                distances.push_back(d);
        }
        std::sort(distances.begin(), distances.end());

        const auto cutoffs = honeycombShellCutoffs(3);
        check(cutoffs.size() == 3, "honeycombShellCutoffs(3) returns three cutoffs");

        if (cutoffs.size() == 3) {
            int shell1 = 0;
            int shell2 = 0;
            int shell3 = 0;
            for (const double d : distances) {
                if (d < cutoffs[0])
                    ++shell1;
                else if (d < cutoffs[1])
                    ++shell2;
                else if (d < cutoffs[2])
                    ++shell3;
            }
            check(shell1 == 3,
                  "first shell has exactly 3 neighbors, counted directly from "
                  "the pristine sheet");
            check(shell2 == 6,
                  "second shell has exactly 6 neighbors, counted directly "
                  "from the pristine sheet");
            check(shell3 == 3,
                  "third shell has exactly 3 neighbors, counted directly "
                  "from the pristine sheet");
        }
    }

    std::printf("The two sublattices (atom-index parity) are bipartite -- "
                "every nearest neighbor crosses sublattices:\n");
    {
        const Structure sheet = primitiveSheet(10);
        const auto cutoffs = honeycombShellCutoffs(1);
        check(cutoffs.size() == 1, "one cutoff for one shell");
        if (cutoffs.size() == 1) {
            const auto& atoms = sheet.atoms();
            bool allCross = true;
            int pairsChecked = 0;
            for (std::size_t i = 0; i < atoms.size(); ++i) {
                for (std::size_t j = 0; j < atoms.size(); ++j) {
                    if (i == j)
                        continue;
                    if ((atoms[i].position - atoms[j].position).norm()
                        >= cutoffs[0])
                        continue;
                    ++pairsChecked;
                    if (i % 2 == j % 2)
                        allCross = false;
                }
            }
            check(pairsChecked > 0, "at least one nearest-neighbor pair was found");
            check(allCross,
                  "every nearest-neighbor pair has one atom on each "
                  "sublattice (index parity)");
        }
    }

    std::printf("Perfectly alternating decoration -- maximally negative "
                "nearest-shell alpha:\n");
    {
        const Structure sheet = primitiveSheet(10);
        std::vector<int> labels(sheet.size());
        for (std::size_t i = 0; i < sheet.size(); ++i)
            labels[i] = (i % 2 == 0) ? 900 : 901; // species A / species B
        const Structure decorated = relabeled(sheet, labels);

        WarrenCowleyOptions options;
        options.shellCutoffs = honeycombShellCutoffs(1);
        const auto wc = computeWarrenCowley(decorated, options);
        const int a = speciesIndex(wc, 900);
        const int b = speciesIndex(wc, 901);
        check(a >= 0 && b >= 0, "both species are present in the result");
        if (a >= 0 && b >= 0 && !wc.shells.empty()) {
            const double alphaAB = wc.shells[0].alpha[static_cast<std::size_t>(a)]
                                                     [static_cast<std::size_t>(b)];
            check(std::abs(alphaAB - (-1.0)) < 1e-9,
                  "alpha_AB(shell 1) is exactly -1 -- every nearest neighbor "
                  "is the OTHER species");
        }
    }

    std::printf("Random decoration at fixed concentration -- alpha within "
                "the module's own counting error:\n");
    {
        const Structure sheet = primitiveSheet(16); // large N for small error bars
        std::mt19937 rng(42);
        std::bernoulli_distribution coin(0.5);
        std::vector<int> labels(sheet.size());
        for (std::size_t i = 0; i < sheet.size(); ++i)
            labels[i] = coin(rng) ? 900 : 901;
        const Structure decorated = relabeled(sheet, labels);

        WarrenCowleyOptions options;
        options.shellCutoffs = honeycombShellCutoffs(1);
        const auto wc = computeWarrenCowley(decorated, options);
        const int a = speciesIndex(wc, 900);
        const int b = speciesIndex(wc, 901);
        check(a >= 0 && b >= 0, "both species are present");
        if (a >= 0 && b >= 0 && !wc.shells.empty()) {
            const auto& shell = wc.shells[0];
            const double alphaAB =
                shell.alpha[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)];
            // Counting-statistics error bar, from the raw trial counts the
            // new pairCounts/neighborsOfSpecies fields expose -- the SAME
            // formula this module's own error-bar reporting uses:
            // sigma_p = sqrt(p(1-p)/N), sigma_alpha = sigma_p / c_j.
            const double neighborsOfA =
                shell.neighborsOfSpecies[static_cast<std::size_t>(a)];
            const double pAB =
                shell.pairCounts[static_cast<std::size_t>(a)]
                                [static_cast<std::size_t>(b)]
                / neighborsOfA;
            const double cB = wc.concentrations[static_cast<std::size_t>(b)];
            const double sigmaP =
                std::sqrt(std::max(pAB * (1.0 - pAB), 0.0) / neighborsOfA);
            const double sigmaAlpha = sigmaP / cB;
            check(neighborsOfA > 100.0,
                  "enough neighbor pairs were sampled for a meaningful error "
                  "bar");
            check(std::abs(alphaAB) < 5.0 * sigmaAlpha,
                  "a random decoration's alpha is consistent with zero within "
                  "5 sigma of the module's own counting-statistics error bar");
        }
    }

    std::printf("Clustered / striped arrangement -- positive cross-parameter:\n");
    {
        const Structure sheet = primitiveSheet(16);
        // Two large spatial bands along x: everything in the low-x half is
        // one species, everything in the high-x half the other -- large
        // same-species domains with a single boundary, the opposite of the
        // alternating case above.
        const auto& v = sheet.cell().vectors();
        const double xMid = 0.5 * v[0].x; // v[0] is already the FULL tiled cell
        std::vector<int> labels(sheet.size());
        for (std::size_t i = 0; i < sheet.size(); ++i)
            labels[i] = (sheet.atoms()[i].position.x < xMid) ? 900 : 901;
        const Structure decorated = relabeled(sheet, labels);

        WarrenCowleyOptions options;
        options.shellCutoffs = honeycombShellCutoffs(1);
        const auto wc = computeWarrenCowley(decorated, options);
        const int a = speciesIndex(wc, 900);
        const int b = speciesIndex(wc, 901);
        check(a >= 0 && b >= 0, "both species are present");
        if (a >= 0 && b >= 0 && !wc.shells.empty()) {
            const double alphaAB = wc.shells[0].alpha[static_cast<std::size_t>(a)]
                                                     [static_cast<std::size_t>(b)];
            check(alphaAB > 0.5,
                  "a striped/clustered decoration gives a strongly positive "
                  "cross-species alpha -- unlike neighbors are suppressed "
                  "relative to random, away from the one domain boundary");
        }
    }

    std::printf("analyzeGrapheneOxidePairCorrelation() census matches "
                "analyzeGrapheneOxideGroups() on the same structure:\n");
    {
        Config config = Config{};
        config.base = GrapheneOxideBuilder::Base::Nanoflake;
        config.flakeIndex = 4;
        config.setCoverage(Group::Epoxide, 0.15);
        config.setCoverage(Group::Hydroxyl, 0.15);
        config.setCoverage(Group::Carboxyl, 0.2);
        config.seed = 7;
        const Structure s = GrapheneOxideBuilder::build(config);

        const auto groupAnalysis = analyzeGrapheneOxideGroups(s);
        const auto pairAnalysis =
            analyzeGrapheneOxidePairCorrelation(s, honeycombShellCutoffs(1));

        check(pairAnalysis.wc.species.size() == pairAnalysis.speciesNames.size(),
              "species and speciesNames are the same length");
        check(!pairAnalysis.wc.shells.empty(), "at least one shell was computed");

        bool censusMatches = true;
        for (std::size_t i = 0; i < pairAnalysis.speciesNames.size(); ++i) {
            const int instances = [&]() {
                if (pairAnalysis.speciesNames[i] == "Pristine")
                    return groupAnalysis.pristineCarbons;
                for (std::size_t g = 0; g < GrapheneOxideBuilder::kGroupCount; ++g) {
                    std::string name = GrapheneOxideBuilder::name(
                        static_cast<Group>(g));
                    name[0] = static_cast<char>(std::toupper(
                        static_cast<unsigned char>(name[0])));
                    if (name == pairAnalysis.speciesNames[i])
                        return groupAnalysis.groups[g].surfaceCarbons;
                }
                return -1;
            }();
            const double expectedConcentration =
                static_cast<double>(instances) / groupAnalysis.frameworkCarbons;
            censusMatches = censusMatches
                && std::abs(pairAnalysis.wc.concentrations[i]
                           - expectedConcentration)
                    < 1e-9;
        }
        check(censusMatches,
              "every species' concentration matches GO Functional Group "
              "Analysis's own surface-carbon counts on the same structure");
    }

    std::printf(failures == 0
                    ? "\nAll GO Pair Correlation checks passed.\n"
                    : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
