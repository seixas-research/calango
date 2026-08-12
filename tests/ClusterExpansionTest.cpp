// Integration test for the native cluster-expansion configuration generator.
// A 2×1×1 supercell of a one-atom simple-cubic Cu cell has two active sites;
// decorating them with {Cu, Au} gives four occupations, of which the two
// mixed ones (CuAu / AuCu) are symmetry-equivalent — so the fingerprint dedup
// must return exactly three inequivalent configurations.
//
// Exit code 0 = pass.

#include "core/ClusterExpansion.hpp"
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
    return 0;
}
