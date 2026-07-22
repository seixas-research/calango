// Diagnostic integration test for the finite-displacement phonon pipeline,
// using diamond (C). It verifies the three properties called out in the spec:
//   1. The 2x2x2 supercell of the 8-atom conventional diamond cell has 64 C.
//   2. The finite-displacement (± delta, every atom) dynamical matrix of that
//      supercell has 3 * N = 192 vibrational modes.
//   3. The 3 acoustic modes vanish at Gamma (≈ 0 cm⁻¹) and optical branches
//      are present (nonzero high-frequency modes).
//
// The displacements are the same ± all-atom set the in-app builder produces;
// a generic Lennard-Jones potential stands in for a real calculator (EMT has
// no carbon), which is sufficient to exercise counts and the acoustic sum
// rule (translational invariance guarantees the 3 zero modes regardless).
//
// Exit code 0 = pass.

#include "core/Structure.hpp"
#include "core/UnitCell.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <cstdio>
#include <vector>

namespace py = pybind11;

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

    pybridge::PythonEngine python;
    if (!python.aseAvailable())
        return fail("ASE not importable in the embedded interpreter");

    // Conventional diamond cell: two interpenetrating FCC sublattices, 8 atoms.
    const double a = 3.567;
    const double fcc[4][3] = {
        {0.0, 0.0, 0.0}, {0.0, 0.5, 0.5}, {0.5, 0.0, 0.5}, {0.5, 0.5, 0.0}};
    core::Structure diamond;
    for (const auto& s : fcc) {
        diamond.addAtom({6, {s[0] * a, s[1] * a, s[2] * a}});
        diamond.addAtom({6, {(s[0] + 0.25) * a, (s[1] + 0.25) * a,
                             (s[2] + 0.25) * a}});
    }
    diamond.setCell(core::UnitCell({a, 0, 0}, {0, a, 0}, {0, 0, a},
                                   {true, true, true}));
    if (diamond.size() != 8)
        return fail("conventional diamond cell should have 8 atoms");

    // (1) Supercell expansion — 2x2x2 of 8 atoms = 64 carbon atoms.
    const core::Structure super =
        pybridge::AseBridge::makeSupercell(diamond, 2, 2, 2);
    if (super.size() != 64) {
        std::fprintf(stderr, "FAIL: 2x2x2 supercell has %zu atoms (expected 64)\n",
                     super.size());
        return 1;
    }

    // (2)+(3) Finite-displacement dynamical matrix of the supercell.
    int nModes = 0;
    std::vector<double> smallest;
    double maxFreq = 0.0;
    try {
        py::dict scope;
        scope["atoms"] = pybridge::AseBridge::toAtoms(super);
        py::exec(R"PY(
import os, tempfile
import numpy as np
from ase.calculators.lj import LennardJones
from ase.vibrations import Vibrations

# Every atom is displaced by ± delta along x, y, z (6N force evaluations).
atoms.calc = LennardJones(sigma=2.0, epsilon=0.1, rc=6.0)
name = os.path.join(tempfile.mkdtemp(), "phonon_diag")
vib = Vibrations(atoms, delta=0.01, name=name)
vib.run()
freqs = np.asarray(vib.get_frequencies()).astype(complex)  # 3N, cm^-1
vib.clean()

mags = np.sort(np.abs(freqs))
result_modes = int(len(freqs))
result_smallest = [float(m) for m in mags[:3]]
result_max = float(mags[-1])
)PY",
                 scope, scope);
        nModes = scope["result_modes"].cast<int>();
        smallest = scope["result_smallest"].cast<std::vector<double>>();
        maxFreq = scope["result_max"].cast<double>();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: phonon computation threw:\n%s\n", e.what());
        return 1;
    }

    // (2) 3 * N_atoms = 3 * 64 = 192 vibrational modes.
    if (nModes != 192) {
        std::fprintf(stderr, "FAIL: got %d modes (expected 3*64 = 192)\n", nModes);
        return 1;
    }

    // (3) Three acoustic modes vanish at Gamma; optical branches are present.
    if (smallest.size() < 3 || smallest[2] > 30.0)
        return fail("the 3 acoustic modes are not ~0 cm⁻¹ at Gamma");
    if (maxFreq < 50.0)
        return fail("no optical (nonzero high-frequency) branches found");

    std::printf("PASS: diamond phonons — 64-atom supercell, %d modes (3N), "
                "acoustic |ω|₃ = %.3f cm⁻¹, optical up to %.0f cm⁻¹\n",
                nModes, smallest[2], maxFreq);
    return 0;
}
