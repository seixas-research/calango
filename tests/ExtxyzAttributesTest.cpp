// Extended-XYZ per-atom attribute round-trip.
//
// AseBridge::toAtoms() used to carry only symbols, positions and the cell, so
// every trajectory Calango wrote silently dropped the forces and magnetic
// moments it had just read. This test writes a frame with forces, magmoms and
// velocities, pushes it through read -> model -> write, and checks the
// columns survive with their values intact.
//
// It also pins the collinear-magmom promotion the viewport's vector overlay
// depends on: a scalar magmom column must surface as an (N, 3) vector field
// aligned with z.

#include "core/Structure.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <pybind11/embed.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace py = pybind11;
using namespace calango;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

/// Write a 4-atom bcc Fe cell carrying forces, collinear magmoms and
/// velocities, using ASE directly so the input is unquestionably valid.
void writeInput(const std::string& path)
{
    py::dict locals;
    locals["path"] = path;
    py::exec(R"PY(
import numpy as np
from ase.build import bulk
from ase.io import write

atoms = bulk("Fe", "bcc", a=2.87, cubic=True) * (2, 1, 1)
n = len(atoms)
atoms.arrays["forces"] = np.arange(3 * n, dtype=float).reshape(n, 3) * 0.125
atoms.arrays["magmoms"] = np.array([2.25, -2.25] * (n // 2))[:n]
atoms.set_velocities(np.full((n, 3), 0.0125))
write(path, atoms)
)PY",
             locals, locals);
}

} // namespace

int main()
{
    // RAII interpreter, exactly as main() does it. Declared first so every
    // pybind11 object below is destroyed before the interpreter finalizes.
    pybridge::PythonEngine python;

    const auto dir = std::filesystem::temp_directory_path() / "calango_extxyz_test";
    std::filesystem::create_directories(dir);
    const std::string inPath = (dir / "in.extxyz").string();
    const std::string outPath = (dir / "out.extxyz").string();

    try {
        writeInput(inPath);
    } catch (const std::exception& e) {
        std::printf("could not stage the input frame: %s\n", e.what());
        return EXIT_FAILURE;
    }

    std::printf("Import into the model:\n");
    core::Structure structure;
    try {
        structure = pybridge::AseBridge::readStructure(inPath);
    } catch (const std::exception& e) {
        std::printf("  FAIL read: %s\n", e.what());
        return EXIT_FAILURE;
    }
    check(structure.size() == 4, "4 atoms");
    check(structure.vectorFields().count("forces") == 1, "forces imported as vectors");
    check(structure.vectorFields().count("velocities") == 1, "velocities imported");
    check(structure.vectorFields().count("magmoms") == 1,
          "scalar magmoms promoted to (N,3) vectors for the arrow overlay");
    if (structure.vectorFields().count("magmoms")) {
        const auto& m = structure.vectorFields().at("magmoms");
        const bool alongZ = std::abs(m[0].x) < 1e-12 && std::abs(m[0].y) < 1e-12
            && std::abs(std::abs(m[0].z) - 2.25) < 1e-9;
        check(alongZ, "promoted moment is (0, 0, m) with |m| = 2.25");
        check(m[0].z * m[1].z < 0.0, "antiferromagnetic sign alternation kept");
    }

    std::printf("Export back out:\n");
    try {
        std::vector<std::shared_ptr<core::Structure>> frames{
            std::make_shared<core::Structure>(structure),
            std::make_shared<core::Structure>(structure)};
        pybridge::AseBridge::writeTrajectory(frames, outPath, "extxyz");
    } catch (const std::exception& e) {
        std::printf("  FAIL write: %s\n", e.what());
        return EXIT_FAILURE;
    }

    // Inspect the written file with ASE rather than the bridge, so a bug that
    // is symmetric in both directions cannot hide.
    //
    // Note where each property lands on read-back: ASE's extended-XYZ reader
    // treats `forces` and `magmoms` as *computed* properties and routes them
    // into a SinglePointCalculator, while `momenta` and `initial_magmoms` are
    // per-atom state and stay in atoms.arrays. Asserting against arrays alone
    // would wrongly report a correct file as broken.
    py::dict locals;
    locals["path"] = outPath;
    py::exec(R"PY(
import numpy as np
from ase.io import read

frames = read(path, index=":")
n_frames = len(frames)
first = frames[0]
results = dict(first.calc.results) if first.calc is not None else {}
# Every column the file actually declares, from the header line.
header = open(path).read().split("\n")[1]
declared = [f.split(":")[0] for f in header.split("Properties=")[1].split()[0].split(":")[0::3]]
columns = sorted(set(list(first.arrays.keys()) + list(results.keys())))

has_forces = "forces" in results
has_magmoms = "magmoms" in results
has_momenta = "momenta" in first.arrays
force_max = float(np.abs(results["forces"]).max()) if has_forces else -1.0
magmom_abs = float(np.abs(results["magmoms"]).max()) if has_magmoms else -1.0
velocity = float(np.abs(first.get_velocities()).max()) if has_momenta else -1.0
# The derived magnitude fields must NOT leak into the file: "|forces|" is not
# expressible as an extended-XYZ column name.
piped = [c for c in declared if "|" in c]
)PY",
             locals, locals);

    check(locals["n_frames"].cast<int>() == 2, "both frames written");
    check(locals["has_forces"].cast<bool>(),
          "forces column survived the round trip (as a calculator result)");
    check(locals["has_magmoms"].cast<bool>(), "magmoms column survived");
    check(locals["has_momenta"].cast<bool>(),
          "velocities survived (as ASE momenta)");
    // Largest force written was 0.125 * 11 = 1.375 eV/A.
    check(std::abs(locals["force_max"].cast<double>() - 1.375) < 1e-6,
          "force values are numerically intact");
    check(std::abs(locals["magmom_abs"].cast<double>() - 2.25) < 1e-6,
          "magmom values are numerically intact");
    check(std::abs(locals["velocity"].cast<double>() - 0.0125) < 1e-9,
          "velocity values are numerically intact");
    check(locals["piped"].cast<py::list>().empty(),
          "derived |name| magnitude fields are not emitted as columns");

    std::printf("%s\n", locals["columns"].cast<py::list>().size() > 0
                            ? "  ..  columns present in the written frame:"
                            : "");
    for (const auto& c : locals["columns"].cast<py::list>())
        std::printf("        %s\n", c.cast<std::string>().c_str());

    // -- MD trajectory: every frame must reach the viewport with the vectors
    // the "Vector overlay" draws. This is the regression that motivated the
    // fix: md.traj/md.extxyz frames used to arrive with positions only.
    std::printf("MD trajectory frames:\n");
    {
        const std::string mdPath = (dir / "md.extxyz").string();
        py::dict md;
        md["path"] = mdPath;
        try {
            py::exec(R"PY(
import numpy as np
from ase import units
from ase.build import bulk
from ase.calculators.emt import EMT
from ase.io import write
from ase.calculators.singlepoint import SinglePointCalculator
from ase.md.langevin import Langevin
from ase.md.velocitydistribution import MaxwellBoltzmannDistribution

# Rattle first: a PERFECT lattice has exactly zero forces by symmetry, so a
# pristine cell would make this test pass or fail for the wrong reason.
atoms = bulk("Cu", "fcc", a=3.6, cubic=True) * (2, 2, 2)
atoms.rattle(stdev=0.02, seed=7)
atoms.calc = EMT()
MaxwellBoltzmannDistribution(atoms, temperature_K=300)
dyn = Langevin(atoms, timestep=1.0 * units.fs, temperature_K=300,
               friction=0.01 / units.fs)
open(path, "w").close()

# Mirrors the generated MD script's _dump_extxyz observer exactly.
def dump():
    snapshot = atoms.copy()
    snapshot.set_velocities(atoms.get_velocities())
    snapshot.calc = SinglePointCalculator(
        snapshot, energy=atoms.get_potential_energy(), forces=atoms.get_forces())
    write(path, snapshot, format="extxyz", append=True)

dump()
dyn.attach(lambda: dump() if dyn.nsteps > 0 else None, interval=1)
dyn.run(5)
)PY",
                     md, md);
        } catch (const std::exception& e) {
            std::printf("  FAIL could not run the MD fixture: %s\n", e.what());
            return EXIT_FAILURE;
        }

        const auto frames = pybridge::AseBridge::readTrajectory(mdPath);
        check(frames.size() == 6, "6 frames (t=0 seed + 5 steps)");
        std::size_t withForces = 0;
        std::size_t withVelocities = 0;
        double largestForce = 0.0;
        for (const auto& frame : frames) {
            const auto& vectors = frame.vectorFields();
            if (vectors.count("forces")) {
                ++withForces;
                for (const auto& f : vectors.at("forces"))
                    largestForce = std::max(largestForce, f.norm());
            }
            if (vectors.count("velocities"))
                ++withVelocities;
        }
        check(withForces == frames.size(),
              "every frame exposes a \"forces\" vector field to the viewport");
        check(withVelocities == frames.size(),
              "every frame exposes a \"velocities\" vector field");
        check(largestForce > 1e-6,
              "forces are physically non-zero on the rattled cell");
        std::printf("      max |F| across the run: %.6f eV/A\n", largestForce);
    }

    std::printf(failures == 0 ? "\nAll extxyz attribute checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
