// Integration test for the Raman factor-group analysis: diamond-Si must
// yield exactly one optical irrep — the triply degenerate T2g, Raman
// active and not IR active — plus the T1u acoustic branch. Runs through
// the real pipeline (core::Structure -> PythonEngine -> spglib ->
// numerical character table).
//
// Usage: calango_raman_test <path/to/Si_diamond.vasp>

#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PythonEngine.hpp"
#include "python_bridge/RamanAnalysis.hpp"

#include <cstdio>

int main(int argc, char* argv[])
{
    using namespace calango;

    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <Si_diamond.vasp>\n", argv[0]);
        return 2;
    }

    pybridge::PythonEngine python;
    if (!python.aseAvailable()) {
        std::fprintf(stderr, "FAIL: ASE not importable\n");
        return 1;
    }

    core::Structure structure;
    try {
        structure = pybridge::AseBridge::readStructure(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: could not read %s:\n%s\n", argv[1], e.what());
        return 1;
    }

    const auto result = pybridge::RamanAnalysis::analyze(structure);
    if (!result.error.empty()) {
        std::fprintf(stderr, "FAIL: %s\n", result.error.c_str());
        return 1;
    }
    if (result.spaceGroupNumber != 227 || result.atomsPrimitive != 2) {
        std::fprintf(stderr, "FAIL: expected Fd-3m with 2-atom primitive, "
                             "got #%d with %d atoms\n",
                     result.spaceGroupNumber, result.atomsPrimitive);
        return 1;
    }

    const pybridge::RamanAnalysis::Mode* optical = nullptr;
    const pybridge::RamanAnalysis::Mode* acoustic = nullptr;
    for (const auto& mode : result.modes) {
        if (mode.opticalCount > 0)
            optical = optical ? optical : &mode;
        if (mode.acousticCount > 0)
            acoustic = acoustic ? acoustic : &mode;
    }
    if (!optical || !acoustic || result.modes.size() != 2) {
        std::fprintf(stderr, "FAIL: expected exactly one optical and one "
                             "acoustic irrep, got %zu modes\n",
                     result.modes.size());
        return 1;
    }
    if (optical->label != "T2g" || optical->degeneracy != 3
        || !optical->ramanActive || optical->irActive
        || optical->opticalCount != 1) {
        std::fprintf(stderr,
                     "FAIL: optical mode %s (dim %d, raman=%d, ir=%d, n=%d) "
                     "— expected T2g, dim 3, Raman-only, n=1\n",
                     optical->label.c_str(), optical->degeneracy,
                     optical->ramanActive, optical->irActive,
                     optical->opticalCount);
        return 1;
    }
    if (acoustic->label != "T1u" || acoustic->degeneracy != 3) {
        std::fprintf(stderr, "FAIL: acoustic irrep %s (dim %d) — expected "
                             "T1u, dim 3\n",
                     acoustic->label.c_str(), acoustic->degeneracy);
        return 1;
    }

    std::printf("PASS: Si diamond Γ = T2g (Raman) + T1u (acoustic), "
                "point group %s\n",
                result.pointGroup.c_str());
    return 0;
}
