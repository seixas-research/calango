// Integration test for the native cluster-expansion configuration generator.
// A 2×1×1 supercell of a one-atom simple-cubic Cu cell has two active sites;
// decorating them with {Cu, Au} gives four occupations, of which the two
// mixed ones (CuAu / AuCu) are symmetry-equivalent — so the fingerprint dedup
// must return exactly three inequivalent configurations.
//
// Exit code 0 = pass.

#include "core/ClusterExpansion.hpp"
#include "core/ClusterExpansionFit.hpp"
#include "core/Structure.hpp"
#include "core/UnitCell.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

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

    // Simple cubic Cu, a = 3.0 Å, one atom.
    const double a = 3.0;
    core::Structure parent;
    parent.addAtom({29, {0, 0, 0}});
    parent.setCell(core::UnitCell({a, 0, 0}, {0, a, 0}, {0, 0, a},
                                  {true, true, true}));

    core::ClusterExpansionOptions opt;
    opt.activeZ = 29;          // Cu
    opt.speciesZ = {29, 79};   // Cu, Au
    opt.supercell[0] = 2;
    opt.supercell[1] = 1;
    opt.supercell[2] = 1;
    opt.pairCutoff = 3.5;      // the two sites (3.0 Å apart) are neighbors
    opt.maxConfigs = 100;

    const core::ClusterExpansionResult res =
        core::generateClusterExpansion(parent, opt);

    if (res.activeSites != 2)
        return fail("expected 2 active sites in the 2x1x1 supercell");
    if (res.enumerated != 4)
        return fail("expected 4 decorations enumerated (2 sites, 2 species)");
    if (res.configs.size() != 3) {
        std::fprintf(stderr,
                     "FAIL: expected 3 inequivalent configs, got %zu\n",
                     res.configs.size());
        return 1;
    }

    // Every config has two atoms, each Cu or Au; compositions are 2:0, 1:1, 0:2.
    std::map<int, int> compHisto; // count of Au -> number of configs
    for (const auto& cfg : res.configs) {
        if (cfg.structure.size() != 2)
            return fail("each config must have 2 atoms");
        int au = 0;
        for (const auto& at : cfg.structure.atoms()) {
            if (at.atomicNumber != 29 && at.atomicNumber != 79)
                return fail("active site decorated with an unexpected species");
            if (at.atomicNumber == 79)
                ++au;
        }
        ++compHisto[au];
        // Degeneracy (g_j — Eq. 6-7 in EGQCA's paper, core::EgqcaCluster::
        // degeneracy): the pure configs (0 or 2 Au) have exactly one
        // decoration each; the mixed one (1 Au) is CuAu and AuCu collapsing
        // onto the same fingerprint, degeneracy 2.
        const int expectedDegeneracy = (au == 1) ? 2 : 1;
        if (cfg.degeneracy != expectedDegeneracy) {
            std::fprintf(stderr,
                         "FAIL: Au-count %d config has degeneracy %d, "
                         "expected %d\n",
                         au, cfg.degeneracy, expectedDegeneracy);
            return 1;
        }
    }
    if (compHisto[0] != 1 || compHisto[1] != 1 || compHisto[2] != 1)
        return fail("expected exactly one config each of Au-count 0, 1, 2");

    // Determinism: a second run yields the same count.
    const auto res2 = core::generateClusterExpansion(parent, opt);
    if (res2.configs.size() != res.configs.size())
        return fail("generation is not deterministic");

    // -- Correlation column labels -------------------------------------------
    //
    // The fingerprint is K point terms plus a species-tuple HISTOGRAM per
    // orbit, NOT one number per orbit. A labeller that assumed one-per-orbit
    // would mislabel every column after the first pair, and an ECI attributed
    // to the wrong cluster reads as physics. The label count matching the
    // fingerprint length exactly is the invariant that prevents it.
    {
        const auto labels = core::clusterCorrelationLabels(
            res, static_cast<int>(opt.speciesZ.size()));
        const std::size_t columns = res.configs.front().correlation.size();
        if (labels.size() != columns)
            return fail("correlation labels do not match the fingerprint "
                        "length");
        for (const auto& l : labels)
            if (l.empty())
                return fail("a correlation column has a blank label");
        if (labels.size() <= opt.speciesZ.size())
            return fail("labels cover only the point terms — the per-orbit "
                        "histograms are missing");
        std::printf("PASS: %zu correlation columns, all labelled\n", columns);
    }

    std::printf("PASS: cluster expansion — 4 decorations → 3 inequivalent "
                "configs (Au-count 0/1/2), %zu pair orbit(s)\n",
                res.orbits.size());

    // -- Ternary (Task 6): the same 2-site cell, three species ---------------
    //
    // K^M = 3^2 = 9 decorations; the two sites are still symmetry-equivalent
    // (swapping them is the same lattice translation as before), so
    // inequivalent configs is the number of unordered pairs drawn WITH
    // replacement from 3 species, K(K+1)/2 = 6 — exactly the same formula
    // the binary case above is one instance of (K=2: 2*3/2 = 3).
    {
        core::ClusterExpansionOptions opt3;
        opt3.activeZ = 29;              // Cu
        opt3.speciesZ = {29, 79, 47};   // Cu, Au, Ag
        opt3.supercell[0] = 2;
        opt3.supercell[1] = 1;
        opt3.supercell[2] = 1;
        opt3.pairCutoff = 3.5;
        opt3.maxConfigs = 100;

        const core::ClusterExpansionResult res3 =
            core::generateClusterExpansion(parent, opt3);

        if (res3.activeSites != 2)
            return fail("ternary: expected 2 active sites");
        if (res3.enumerated != 9)
            return fail("ternary: expected 9 decorations enumerated (2 "
                        "sites, 3 species = 3^2)");
        if (res3.configs.size() != 6) {
            std::fprintf(stderr,
                         "FAIL: ternary: expected 6 inequivalent configs "
                         "(K(K+1)/2 for K=3), got %zu\n",
                         res3.configs.size());
            return 1;
        }

        // Every one of the 6 unordered {Cu,Au,Ag} pairs appears exactly
        // once: (Cu,Cu), (Cu,Au), (Cu,Ag), (Au,Au), (Au,Ag), (Ag,Ag).
        std::map<std::pair<int, int>, int> pairHisto;
        for (const auto& cfg : res3.configs) {
            if (cfg.structure.size() != 2)
                return fail("ternary: each config must have 2 atoms");
            std::vector<int> z;
            for (const auto& at : cfg.structure.atoms())
                z.push_back(at.atomicNumber);
            std::sort(z.begin(), z.end());
            ++pairHisto[{z[0], z[1]}];
        }
        if (pairHisto.size() != 6)
            return fail("ternary: expected all 6 distinct species pairs, "
                        "some are missing or duplicated");
        for (const auto& [key, count] : pairHisto)
            if (count != 1)
                return fail("ternary: a species-pair type appeared more "
                            "than once — the symmetry dedup is wrong");

        // Correlation fingerprint length: K point terms + K(K+1)/2 pair-orbit
        // buckets per orbit (no triplet/quad orbits requested here).
        const auto labels3 = core::clusterCorrelationLabels(
            res3, static_cast<int>(opt3.speciesZ.size()));
        const std::size_t expectedColumns =
            3 + res3.orbits.size() * 6; // K=3 point terms, 6 buckets/pair orbit
        if (labels3.size() != expectedColumns
            || res3.configs.front().correlation.size() != expectedColumns)
            return fail("ternary: correlation fingerprint length does not "
                        "match K point terms + K(K+1)/2 per pair orbit");

        std::printf("PASS: ternary cluster expansion — 9 decorations → 6 "
                    "inequivalent configs (one per {Cu,Au,Ag} pair type), "
                    "%zu correlation column(s)\n",
                    expectedColumns);
    }

    // -- Ternary fit round trip (Task 6): known ECIs -> synthetic energies
    //    -> refit -> recovered ECIs -----------------------------------------
    //
    // A bigger cell than the 2-site one above: enough inequivalent configs
    // that the pair-bucket columns are not trivially collinear with each
    // other (the 2-site case has too few configs for that). The ground
    // truth is deliberately SPARSE and confined to two pair-bucket columns
    // (AB, AC) with every point-term and every other pair-bucket ECI set to
    // EXACTLY zero — the point-term columns always sum to the (fixed) site
    // count, so they are collinear with the intercept by construction, and a
    // ground truth that put independent weight there would not be uniquely
    // recoverable by ANY method, ridge or lasso, because two ECI vectors
    // differing by that null direction produce identical energies. Confined
    // to a genuinely identifiable subspace, lasso should recover both the
    // active coefficients and the true zeros to near machine precision.
    {
        core::ClusterExpansionOptions opt4;
        opt4.activeZ = 29;
        opt4.speciesZ = {29, 79, 47}; // Cu, Au, Ag
        opt4.supercell[0] = 4;
        opt4.supercell[1] = 1;
        opt4.supercell[2] = 1;
        opt4.pairCutoff = 3.5; // nearest-neighbour only, one pair orbit
        opt4.maxConfigs = 500;
        opt4.maxEnumeration = 500000;

        const core::ClusterExpansionResult res4 =
            core::generateClusterExpansion(parent, opt4);
        if (res4.configs.size() < 10) {
            std::fprintf(stderr,
                         "FAIL: fit round trip: only %zu ternary configs — "
                         "too few to identify the pair-bucket subspace\n",
                         res4.configs.size());
            return 1;
        }

        // Bucket order for K=3 pairs (see clusterExpansionPairBucket()):
        // 0=AA(Cu-Cu) 1=AB(Cu-Au) 2=AC(Cu-Ag) 3=BB 4=BC 5=CC. Column layout
        // is [3 point terms][6 pair buckets] (one pair orbit here).
        const std::size_t columns = res4.configs.front().correlation.size();
        if (columns != 9)
            return fail("fit round trip: expected 9 columns (3 point + 6 "
                        "pair buckets) for a single pair orbit");
        std::vector<double> trueEci(columns, 0.0);
        const double trueAB = 0.035, trueAC = -0.021;
        trueEci[3 + 1] = trueAB; // point terms occupy columns 0..2
        trueEci[3 + 2] = trueAC;

        std::vector<std::vector<double>> correlations;
        std::vector<double> energies;
        for (const auto& cfg : res4.configs) {
            correlations.push_back(cfg.correlation);
            double e = 0.0;
            for (std::size_t c = 0; c < columns; ++c)
                e += cfg.correlation[c] * trueEci[c];
            energies.push_back(e);
        }

        core::EciFitOptions fitOpt;
        fitOpt.method = core::EciMethod::Lasso;
        // A short, low path reaching close to zero: this is a noise-free
        // exact-recovery check, not a search for the best-generalising
        // model, so the smallest lambda on the path is what should be
        // selected — cvScore is monotonically best there with no noise to
        // overfit to.
        fitOpt.lambdaCount = 60;
        fitOpt.lambdaMinRatio = 1e-6;

        const core::EciFitResult fit = core::fitEffectiveClusterInteractions(
            correlations, energies, fitOpt);
        if (!fit.ok)
            return fail("fit round trip: the ternary fit did not converge");
        if (fit.rmse >= 1e-6)
            return fail("fit round trip: training RMSE is not ~0 on "
                        "noise-free data");
        if (std::abs(fit.eci[3 + 1] - trueAB) >= 1e-4)
            return fail("fit round trip: did not recover the true AB pair "
                        "ECI");
        if (std::abs(fit.eci[3 + 2] - trueAC) >= 1e-4)
            return fail("fit round trip: did not recover the true AC pair "
                        "ECI");
        double worstOffTarget = 0.0;
        for (std::size_t c = 0; c < columns; ++c) {
            if (c == 3 + 1 || c == 3 + 2)
                continue;
            worstOffTarget = std::max(worstOffTarget, std::abs(fit.eci[c]));
        }
        if (worstOffTarget >= 1e-3)
            return fail("fit round trip: a column NOT in the ground truth "
                        "(including a collinear point term) did not come "
                        "back near zero");
        std::printf("PASS: ternary fit round trip — %zu configs, rmse=%.2e, "
                    "recovered AB=%.5f (true %.5f), AC=%.5f (true %.5f)\n",
                    res4.configs.size(), fit.rmse, fit.eci[3 + 1], trueAB,
                    fit.eci[3 + 2], trueAC);
    }

    // -- Guaranteed pure endpoints under random sampling ---------------------
    //
    // A real alloy's supercell is almost always too large to enumerate
    // exhaustively, so generateClusterExpansion() falls back to random
    // sampling of the occupation space (options.sampled == true below). Left
    // to chance, a pure-species decoration (x=0 or x=1) has probability
    // K^(1-M) of being drawn — for M=20 sites that is roughly one in a
    // million — so without an explicit guarantee, formation energies
    // referenced against "the ensemble's own endpoints"
    // (ClusterExpansionScriptGenerator) would almost never be exactly zero at
    // the pristine compositions. This is the case a real Au-Pd run actually
    // hits.
    {
        core::Structure bigParent;
        bigParent.addAtom({29, {0, 0, 0}});
        bigParent.setCell(core::UnitCell({a, 0, 0}, {0, a, 0}, {0, 0, a},
                                         {true, true, true}));

        core::ClusterExpansionOptions opt5;
        opt5.activeZ = 29;         // Cu
        opt5.speciesZ = {29, 79};  // Cu, Au
        opt5.supercell[0] = 5;
        opt5.supercell[1] = 2;
        opt5.supercell[2] = 2;     // 20 active sites: 2^20 >> maxEnumeration
        opt5.pairCutoff = 3.5;
        opt5.maxConfigs = 200;
        opt5.maxEnumeration = 5000; // small on purpose: force random sampling

        const core::ClusterExpansionResult res5 =
            core::generateClusterExpansion(bigParent, opt5);
        if (res5.activeSites != 20)
            return fail("endpoint guarantee: expected 20 active sites");
        if (!res5.sampled)
            return fail("endpoint guarantee: expected the random-sampling "
                        "branch to have been taken");

        bool foundPureCu = false, foundPureAu = false;
        for (const auto& cfg : res5.configs) {
            if (cfg.speciesCounts[0] == 20 && cfg.speciesCounts[1] == 0) {
                foundPureCu = true;
                if (cfg.degeneracy != 1)
                    return fail("endpoint guarantee: pure Cu should have "
                                "degeneracy 1");
            }
            if (cfg.speciesCounts[1] == 20 && cfg.speciesCounts[0] == 0) {
                foundPureAu = true;
                if (cfg.degeneracy != 1)
                    return fail("endpoint guarantee: pure Au should have "
                                "degeneracy 1");
            }
        }
        if (!foundPureCu || !foundPureAu)
            return fail("endpoint guarantee: a randomly-sampled ensemble did "
                        "not contain both pure single-species endpoints — "
                        "the formation-energy reference used by "
                        "ClusterExpansionScriptGenerator's \"reference the "
                        "ensemble's own endpoints\" option would not be "
                        "exactly zero at x=0/x=1");
        std::printf("PASS: pure endpoints guaranteed present under random "
                    "sampling (%zu configs, %lld decorations examined)\n",
                    res5.configs.size(), res5.enumerated);
    }

    return 0;
}
