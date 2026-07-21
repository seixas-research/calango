#include "core/PhononScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"

#include <sstream>

namespace calango::core {

namespace {

void emitPeriodicPhonons(std::ostringstream& out, const PhononConfig& c)
{
    out << "from ase.phonons import Phonons\n"
           "from ase.units import invcm\n"
           "\n"
        << "supercell = (" << c.supercell[0] << ", " << c.supercell[1] << ", "
        << c.supercell[2] << ")\n"
        << "delta = " << c.deltaAngstrom << "  # Å\n"
           "print(f\"CALANGO_INFO displacements={6 * len(atoms)} supercell={supercell}\","
           " flush=True)\n"
           "\n"
           "ph = Phonons(atoms, atoms.calc, supercell=supercell, delta=delta,\n"
           "             name=\"phonon\")\n"
           "ph.run()\n"
           "\n"
           "# Force constants -> dynamical matrix (acoustic sum rule enforced).\n"
           "ph.read(acoustic=True)\n"
           "ph.clean()\n"
           "\n"
           "# Gamma-point frequencies.\n"
           "gamma = ph.band_structure([[0.0, 0.0, 0.0]])[0]\n"
           "for i, freq in enumerate(gamma):\n"
           "    print(f\"CALANGO_RESULT mode={i:3d} freq_cm1={freq / invcm:10.2f} \"\n"
           "          f\"freq_meV={freq * 1e3:9.3f}\", flush=True)\n"
           "\n"
           "# Dispersion along the ASE-suggested Brillouin-zone path.\n"
        << "path = atoms.cell.bandpath(npoints=" << c.bandPathPoints << ")\n"
        << "print(f\"CALANGO_INFO bandpath={path.path}\", flush=True)\n"
           "bands = ph.band_structure(path.kpts)  # (nk, nmodes) eV\n"
           "xcoords, special_x, labels = path.get_linear_kpoint_axis()\n"
           "with open(\"phonon_bands.csv\", \"w\") as f:\n"
           "    f.write(\"# special points: \"\n"
           "            + \" \".join(f\"{l}@{x:.6f}\" for l, x in zip(labels, special_x))\n"
           "            + \"\\n\")\n"
           "    f.write(\"k_distance,\"\n"
           "            + \",\".join(f\"mode_{j}_cm1\" for j in range(len(bands[0])))\n"
           "            + \"\\n\")\n"
           "    for x, row in zip(xcoords, bands):\n"
           "        f.write(f\"{x:.6f},\" + \",\".join(f\"{w / invcm:.3f}\" for w in row)\n"
           "                + \"\\n\")\n"
           "print(\"CALANGO_INFO wrote phonon_bands.csv\", flush=True)\n"
           "\n"
           "# Phonon density of states.\n"
        << "kgrid = (" << c.dosKptGrid << ", " << c.dosKptGrid << ", "
        << c.dosKptGrid << ")\n"
        << "try:\n"
           "    dos = ph.get_dos(kpts=kgrid).sample_grid(npts=400, width=2.0 * invcm)\n"
           "    dos_energies, dos_weights = dos.get_energies(), dos.get_weights()\n"
           "except AttributeError:  # older ASE releases\n"
           "    dos_energies, dos_weights = ph.dos(kpts=kgrid, npts=400,\n"
           "                                       delta=2.0 * invcm)\n"
           "with open(\"phonon_dos.csv\", \"w\") as f:\n"
           "    f.write(\"energy_cm1,dos\\n\")\n"
           "    for e, d in zip(dos_energies, dos_weights):\n"
           "        f.write(f\"{e / invcm:.3f},{d:.6f}\\n\")\n"
           "print(\"CALANGO_INFO wrote phonon_dos.csv\", flush=True)\n";
}

void emitMolecularVibrations(std::ostringstream& out, const PhononConfig& c)
{
    out << "import numpy as np\n"
           "\n"
           "from ase.vibrations import Vibrations\n"
           "\n"
        << "delta = " << c.deltaAngstrom << "  # Å\n"
           "print(f\"CALANGO_INFO displacements={6 * len(atoms)}\", flush=True)\n"
           "\n"
           "vib = Vibrations(atoms, delta=delta, name=\"vib\")\n"
           "vib.run()\n"
           "vib.summary(log=\"vibrations.txt\")\n"
           "\n"
           "# Frequencies in cm^-1; imaginary modes come out complex.\n"
           "freqs = np.asarray(vib.get_frequencies())\n"
           "for i, freq in enumerate(freqs):\n"
           "    f = complex(freq)\n"
           "    if abs(f.imag) > 1e-6:\n"
           "        print(f\"CALANGO_RESULT mode={i:3d} freq_cm1={f.imag:10.2f}i"
           " imaginary=1\", flush=True)\n"
           "    else:\n"
           "        print(f\"CALANGO_RESULT mode={i:3d} freq_cm1={f.real:10.2f}\","
           " flush=True)\n"
           "\n"
           "# Mode animations (vib.<n>.traj) — open them in Calango to see\n"
           "# each normal mode as a trajectory. Near-zero (translation /\n"
           "# rotation) and imaginary modes are skipped: their amplitude\n"
           "# diverges (1/omega) and would produce NaN frames.\n"
           "for i, freq in enumerate(freqs):\n"
           "    f = complex(freq)\n"
           "    if abs(f.imag) < 1e-6 and f.real > 10.0:  # cm^-1\n"
           "        vib.write_mode(i)\n"
           "vib.clean()\n";
}

} // namespace

std::string PhononScriptGenerator::generate(const PhononConfig& config,
                                            const std::string& structureFile)
{
    std::ostringstream out;
    out << "#!/usr/bin/env python3\n"
           "# Generated by Calango " << CALANGO_VERSION << " — finite-displacement "
        << (config.periodic ? "phonons" : "normal modes") << " with "
        << toString(config.calculator.calculator) << ".\n"
           "# Every atom is displaced by ±delta along x, y and z (6N force\n"
           "# evaluations, no symmetry reduction). This is a plain ASE script:\n"
           "# edit it freely or run it standalone.\n"
           "\n"
           "from ase.io import read\n"
           "\n"
        << "atoms = read(r\"" << structureFile << "\")\n"
        << "print(f\"CALANGO_INFO natoms={len(atoms)}\", flush=True)\n"
           "\n";
    out << AseScriptGenerator::calculatorSnippet(config.calculator);
    out << "\n";
    if (config.periodic)
        emitPeriodicPhonons(out, config);
    else
        emitMolecularVibrations(out, config);
    out << "\nprint(\"CALANGO_DONE\", flush=True)\n";
    return out.str();
}

} // namespace calango::core
