// Integration test for the SQS generator backend: build a Si0.5Ge0.5
// random alloy on the diamond cubic lattice through the real pipeline
// (core::Structure -> PythonEngine -> SqsBuilder -> core::Structure) and
// verify site occupations and cell vectors. Exercises the embedded
// interpreter exactly like the GUI does (same py::exec scoping).
//
// Exit code 0 = pass. Run via `ctest` or directly.

#include "core/Structure.hpp"
#include "core/UnitCell.hpp"
#include "python_bridge/PythonEngine.hpp"
#include "python_bridge/SqsBuilder.hpp"

#include <cmath>
#include <cstdio>
#include <map>

namespace {

int fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

} // namespace

int main()
{
    using namespace calango;

    pybridge::PythonEngine python;
    if (!python.aseAvailable())
        return fail("ASE not importable in the embedded interpreter");

    // Diamond cubic Si, conventional 8-atom cell, a = 5.43 Å.
    const double a = 5.43;
    core::Structure silicon;
    const core::Vec3 fcc[] = {
        {0.0, 0.0, 0.0}, {0.0, 0.5, 0.5}, {0.5, 0.0, 0.5}, {0.5, 0.5, 0.0}};
    for (const auto& site : fcc) {
        silicon.addAtom({14, {site.x * a, site.y * a, site.z * a}});
        silicon.addAtom({14,
                         {(site.x + 0.25) * a, (site.y + 0.25) * a,
                          (site.z + 0.25) * a}});
    }
    silicon.setCell(core::UnitCell({a, 0, 0}, {0, a, 0}, {0, 0, a},
                                   {true, true, true}));

    pybridge::SqsBuilder::Params params;
    params.nx = params.ny = params.nz = 2; // 64 sites
    params.replaceElement = "Si";
    params.composition = {{"Si", 0.5}, {"Ge", 0.5}};
    params.shell1 = 2.6; // nn = 2.35 Å
    params.shell2 = 4.0; // 2nn = 3.84 Å
    params.steps = 3000;
    params.seed = 7;

    pybridge::SqsBuilder::Result result;
    try {
        result = pybridge::SqsBuilder::generate(silicon, params);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: generate() threw:\n%s\n", e.what());
        return 1;
    }

    // Site occupations: 32 Si + 32 Ge on 64 sites.
    if (result.structure.size() != 64)
        return fail("expected 64 atoms in the 2x2x2 supercell");
    std::map<int, int> counts;
    for (const auto& atom : result.structure.atoms())
        ++counts[atom.atomicNumber];
    if (counts[14] != 32 || counts[32] != 32) {
        std::fprintf(stderr, "FAIL: occupations Si=%d Ge=%d (expected 32/32)\n",
                     counts[14], counts[32]);
        return 1;
    }

    // Cell vectors: 2a along each axis, orthogonal.
    const auto& vectors = result.structure.cell().vectors();
    const double expected[3][3] = {
        {2 * a, 0, 0}, {0, 2 * a, 0}, {0, 0, 2 * a}};
    for (int i = 0; i < 3; ++i) {
        const double v[3] = {vectors[static_cast<std::size_t>(i)].x,
                             vectors[static_cast<std::size_t>(i)].y,
                             vectors[static_cast<std::size_t>(i)].z};
        for (int k = 0; k < 3; ++k)
            if (std::abs(v[k] - expected[i][k]) > 1e-9) {
                std::fprintf(stderr,
                             "FAIL: cell vector %d component %d = %f "
                             "(expected %f)\n",
                             i, k, v[k], expected[i][k]);
                return 1;
            }
    }

    std::printf("PASS: Si0.5Ge0.5 SQS — 32/32 occupations, cell 2a·I, "
                "backend \"%s\", residual objective %.4f\n",
                result.method.c_str(), result.objective);
    return 0;
}
