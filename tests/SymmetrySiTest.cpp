// Integration test for symmetry detection: load the Si_diamond.vasp
// example through the real pipeline (ase.io -> core::Structure ->
// AseBridge::symmetryInfo -> spglib) and require space group Fd-3m
// (#227). Guards the py::exec scoping fix — with split globals/locals
// dicts the query died with "NameError: name 'dataset' is not defined".
//
// Usage: calango_symmetry_test <path/to/Si_diamond.vasp>

#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PythonEngine.hpp"

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
    if (structure.size() != 8) {
        std::fprintf(stderr, "FAIL: expected 8 atoms, got %zu\n",
                     structure.size());
        return 1;
    }

    const auto info = pybridge::AseBridge::symmetryInfo(structure);
    if (!info.error.empty()) {
        std::fprintf(stderr, "FAIL: symmetry query errored: %s\n",
                     info.error.c_str());
        return 1;
    }
    if (info.spaceGroupNumber != 227 || info.spaceGroupSymbol != "Fd-3m"
        || info.pointGroup.empty() || info.crystalSystem != "cubic") {
        std::fprintf(stderr,
                     "FAIL: got %s (#%d), point group '%s', system '%s' — "
                     "expected Fd-3m (#227), cubic\n",
                     info.spaceGroupSymbol.c_str(), info.spaceGroupNumber,
                     info.pointGroup.c_str(), info.crystalSystem.c_str());
        return 1;
    }

    std::printf("PASS: Si diamond -> %s (#%d), point group %s, %s\n",
                info.spaceGroupSymbol.c_str(), info.spaceGroupNumber,
                info.pointGroup.c_str(), info.crystalSystem.c_str());
    return 0;
}
