#include "core/PhononScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"

#include <sstream>

namespace calango::core {

namespace {

void emitPeriodicPhonons(std::ostringstream& out, const PhononConfig& c)
{
    out << "import json\n"
           "import numpy as np\n"
           "from ase.phonons import Phonons\n"
           "from ase.units import invcm\n"
           "\n"
        << "supercell = (" << c.supercell[0] << ", " << c.supercell[1] << ", "
        << c.supercell[2] << ")\n"
        << "delta = " << c.deltaAngstrom << "  # Å\n"
           "print(f\"CALANGO_INFO natoms_primitive={len(atoms)} supercell={supercell} \"\n"
           "      f\"displacements={6 * len(atoms)}\", flush=True)\n"
           "\n"
           "ph = Phonons(atoms, atoms.calc, supercell=supercell, delta=delta,\n"
           "             name=\"phonon\")\n"
           "# Clear any stale displacement cache FIRST: ase.phonons.Phonons.run()\n"
           "# skips displacements whose force file already exists, so a partial\n"
           "# or mismatched prior run would silently leave an incomplete ±delta\n"
           "# set across the supercell and corrupt the force constants. Cleaning\n"
           "# guarantees every displaced supercell configuration is regenerated.\n"
           "ph.clean()\n"
           "ph.run()\n"
           "\n"
           "# Force constants -> dynamical matrix (acoustic sum rule enforced).\n"
           "ph.read(acoustic=True)\n"
           "\n"
           "# Gamma-point frequencies (3 acoustic modes -> 0).\n"
           "gamma = np.asarray(ph.band_structure([[0.0, 0.0, 0.0]])[0])\n"
           "n_acoustic = int(np.sum(np.abs(gamma / invcm) < 1.0))\n"
           "for i, freq in enumerate(gamma):\n"
           "    print(f\"CALANGO_RESULT mode={i:3d} freq_cm1={freq / invcm:10.2f} \"\n"
           "          f\"freq_meV={freq * 1e3:9.3f}\", flush=True)\n"
           "\n"
           "# Dispersion along the ASE-suggested Brillouin-zone path.\n"
        << "path = atoms.cell.bandpath(npoints=" << c.bandPathPoints << ")\n"
        << "print(f\"CALANGO_INFO bandpath={path.path}\", flush=True)\n"
           "bands = np.asarray(ph.band_structure(path.kpts))  # (nk, nmodes) eV\n"
           "xcoords, special_x, labels = path.get_linear_kpoint_axis()\n"
           "band_json = {\n"
           "    \"unit\": \"cm^-1\",\n"
           "    \"x\": [float(x) for x in xcoords],\n"
           "    \"special_x\": [float(x) for x in special_x],\n"
           "    \"special_labels\": [str(l) for l in labels],\n"
           "    # frequencies[kpoint][mode] in cm^-1 (imaginary modes < 0)\n"
           "    \"frequencies\": [[float(w / invcm) for w in row] for row in bands],\n"
           "}\n"
           "with open(\"phonon_band.json\", \"w\") as f:\n"
           "    json.dump(band_json, f)\n"
           "print(\"CALANGO_INFO wrote phonon_band.json\", flush=True)\n"
           "\n"
           "# Phonon density of states.\n"
        << "kgrid = (" << c.dosKptGrid << ", " << c.dosKptGrid << ", "
        << c.dosKptGrid << ")\n"
        << "try:\n"
           "    dos = ph.get_dos(kpts=kgrid).sample_grid(npts=400, width=2.0 * invcm)\n"
           "    dos_energies, dos_weights = dos.get_energies(), dos.get_weights()\n"
           "except Exception:  # older ASE releases expose ph.dos(...)\n"
           "    dos_energies, dos_weights = ph.dos(kpts=kgrid, npts=400,\n"
           "                                       delta=2.0 * invcm)\n"
           "dos_json = {\n"
           "    \"unit\": \"cm^-1\",\n"
           "    \"frequencies\": [float(e / invcm) for e in dos_energies],\n"
           "    \"dos\": [float(d) for d in dos_weights],\n"
           "}\n"
           "with open(\"phonon_dos.json\", \"w\") as f:\n"
           "    json.dump(dos_json, f)\n"
           "print(\"CALANGO_INFO wrote phonon_dos.json\", flush=True)\n"
           "ph.clean()\n"
           "max_cm1 = float(np.max(bands / invcm)) if bands.size else 0.0\n"
           "print(f\"CALANGO_RESULT branches={bands.shape[1]} \"\n"
           "      f\"acoustic_at_gamma={n_acoustic} max_cm1={max_cm1:.1f}\", flush=True)\n";
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
