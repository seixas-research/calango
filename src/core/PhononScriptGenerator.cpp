#include "core/PhononScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"

#include <sstream>
#include <string>

namespace calango::core {

namespace {

/// Re-indent an emitted Python block so it can be nested inside a `def`.
/// Blank lines stay empty rather than collecting trailing whitespace.
std::string indentBlock(const std::string& text, const char* prefix)
{
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    bool atLineStart = true;
    for (const char ch : text) {
        if (atLineStart && ch != '\n')
            out += prefix;
        out += ch;
        atLineStart = ch == '\n';
    }
    return out;
}

/// A Calculator wrapper that subtracts the reference geometry's residual
/// forces from every evaluation. Emitted only when the user asks for residual
/// removal; it keeps the subtraction inside the calculator so BOTH the ASE and
/// the phonopy displacement drivers get clean forces without either of them
/// knowing about it.
void emitResidualFreeCalculator(std::ostringstream& out)
{
    out << "# ---------------------------------------------------------------\n"
           "# Residual force removal.\n"
           "# A relaxation stops at a finite fmax, so the reference geometry\n"
           "# still carries small forces. Finite differences of contaminated\n"
           "# forces put a spurious linear term into the force constants, which\n"
           "# surfaces as non-zero 'acoustic' frequencies at Gamma. Subtracting\n"
           "# the reference forces from every displaced evaluation removes it.\n"
           "# (A central +/-delta difference cancels a CONSTANT residual on its\n"
           "# own; this also removes the part that varies with configuration,\n"
           "# and is what makes phonopy's single-sided displacements usable.)\n"
           "from ase.calculators.calculator import Calculator, all_changes\n"
           "\n"
           "\n"
           "class ResidualFreeCalculator(Calculator):\n"
           "    implemented_properties = [\"energy\", \"forces\"]\n"
           "\n"
           "    def __init__(self, base, residual, **kwargs):\n"
           "        super().__init__(**kwargs)\n"
           "        self.base = base\n"
           "        self.residual = np.asarray(residual)\n"
           "\n"
           "    def calculate(self, atoms=None, properties=(\"energy\",),\n"
           "                  system_changes=all_changes):\n"
           "        super().calculate(atoms, properties, system_changes)\n"
           "        probe = atoms.copy()\n"
           "        probe.calc = self.base\n"
           "        self.results[\"energy\"] = probe.get_potential_energy()\n"
           "        forces = probe.get_forces()\n"
           "        # Shapes differ when the driver evaluates a cell other than\n"
           "        # the one the baseline was measured on; leave those alone\n"
           "        # rather than broadcasting a meaningless correction.\n"
           "        if forces.shape == self.residual.shape:\n"
           "            forces = forces - self.residual\n"
           "        self.results[\"forces\"] = forces\n"
           "\n"
           "\n";
}

/// Symmetry-reduced finite displacements through phonopy (which uses spglib to
/// find the space group and reduce the displacement set to the irreducible
/// representations). Falls back to the plain ASE 6N driver when phonopy is not
/// importable, so the generated script always runs.
void emitSymmetryReducedPhonons(std::ostringstream& out, const PhononConfig& c)
{
    out << "# Symmetry-reduced finite displacements (spglib via phonopy).\n"
           "# The naive scheme displaces every atom by +/-delta along x, y and\n"
           "# z: 6N force evaluations. Most of those are related by the crystal\n"
           "# space group, so only the symmetry-irreducible displacements carry\n"
           "# new information — phonopy asks spglib for the space group and the\n"
           "# site-symmetry irreps, generates that reduced set, and rebuilds the\n"
           "# full force-constant matrix by symmetry. For a high-symmetry cell\n"
           "# this is often an order of magnitude fewer force evaluations.\n"
           "import phonopy\n"
           "from phonopy import Phonopy\n"
           "from phonopy.structure.atoms import PhonopyAtoms\n"
           "\n"
           "unitcell = PhonopyAtoms(symbols=atoms.get_chemical_symbols(),\n"
           "                        cell=atoms.get_cell(),\n"
           "                        scaled_positions=atoms.get_scaled_positions())\n"
        << "supercell_matrix = np.diag([" << c.supercell[0] << ", "
        << c.supercell[1] << ", " << c.supercell[2] << "])\n"
           "phonon = Phonopy(unitcell, supercell_matrix)\n"
        << "phonon.generate_displacements(distance=delta)\n"
           "supercells = phonon.supercells_with_displacements\n"
           "sym = phonon.symmetry\n"
           "print(f\"CALANGO_INFO spacegroup={sym.get_international_table()} \"\n"
           "      f\"displacements_irreducible={len(supercells)} \"\n"
           "      f\"displacements_naive={6 * len(phonon.supercell)}\", flush=True)\n"
           "\n"
           "# Forces on each irreducible displaced supercell.\n"
           "from ase import Atoms as _Atoms\n"
           "\n"
           "\n"
           "def _to_ase(pa):\n"
           "    return _Atoms(symbols=pa.symbols, cell=pa.cell,\n"
           "                  scaled_positions=pa.scaled_positions, pbc=True)\n"
           "\n"
           "\n";
    if (c.removeResidualForces)
        out << "# Baseline: forces on the UN-displaced supercell.\n"
               "reference = _to_ase(phonon.supercell)\n"
               "reference.calc = base_calc\n"
               "residual = reference.get_forces()\n"
               "print(f\"CALANGO_INFO residual_fmax=\"\n"
               "      f\"{np.max(np.linalg.norm(residual, axis=1)):.6f}\", flush=True)\n"
               "\n";
    out << "force_sets = []\n"
           "for index, scell in enumerate(supercells):\n"
           "    image = _to_ase(scell)\n"
           "    image.calc = base_calc\n"
           "    forces = image.get_forces()\n";
    if (c.removeResidualForces)
        out << "    forces = forces - residual\n";
    out << "    force_sets.append(forces)\n"
           "    print(f\"CALANGO_PROGRESS displacement={index + 1}/\"\n"
           "          f\"{len(supercells)}\", flush=True)\n"
           "\n"
           "phonon.forces = np.asarray(force_sets)\n"
           "phonon.produce_force_constants()\n";
    if (c.acousticSumRule)
        out << "phonon.symmetrize_force_constants()  # acoustic sum rule + "
               "space-group symmetry\n";
    out << "\n"
           "# Gamma-point frequencies (THz from phonopy -> cm^-1).\n"
           "THZ_TO_CM1 = 33.35641\n"
           "gamma = np.asarray(phonon.get_frequencies([0.0, 0.0, 0.0]))\n"
           "n_acoustic = int(np.sum(np.abs(gamma * THZ_TO_CM1) < 1.0))\n"
           "for i, freq in enumerate(gamma):\n"
           "    print(f\"CALANGO_RESULT mode={i:3d} \"\n"
           "          f\"freq_cm1={freq * THZ_TO_CM1:10.2f} \"\n"
           "          f\"freq_meV={freq * 4.135667:9.3f}\", flush=True)\n"
           "\n"
           "# Dispersion along the requested (or ASE-suggested) BZ path. The\n"
           "# q-points come from ASE's bandpath so the path string, labels and\n"
           "# linear x-axis match every other Calango band plot exactly.\n"
        << "path_str = " << (c.kpath.empty() ? "None" : "\"" + c.kpath + "\"")
        << "\n"
        << "path = atoms.cell.bandpath(path_str, npoints=" << c.bandPathPoints
        << ")\n"
           "print(f\"CALANGO_INFO bandpath={path.path}\", flush=True)\n"
           "qpoints = [list(map(float, q)) for q in path.kpts]\n"
           "phonon.run_qpoints(qpoints)\n"
           "freqs_thz = np.asarray(phonon.get_qpoints_dict()[\"frequencies\"])\n"
           "xcoords, special_x, labels = path.get_linear_kpoint_axis()\n"
           "band_json = {\n"
           "    \"unit\": \"cm^-1\",\n"
           "    \"x\": [float(x) for x in xcoords],\n"
           "    \"special_x\": [float(x) for x in special_x],\n"
           "    \"special_labels\": [str(l) for l in labels],\n"
           "    \"frequencies\": [[float(w * THZ_TO_CM1) for w in row]\n"
           "                    for row in freqs_thz],\n"
           "}\n"
           "with open(\"phonon_band.json\", \"w\") as f:\n"
           "    json.dump(band_json, f)\n"
           "print(\"CALANGO_INFO wrote phonon_band.json\", flush=True)\n"
           "\n"
           "# Phonon density of states on a Monkhorst-Pack mesh.\n"
        << "phonon.run_mesh([" << c.dosKptGrid << ", " << c.dosKptGrid << ", "
        << c.dosKptGrid << "])\n"
        << "phonon.run_total_dos(sigma=" << c.dosWidthCm << " / THZ_TO_CM1)\n"
           "dos_dict = phonon.get_total_dos_dict()\n"
           "dos_json = {\n"
           "    \"unit\": \"cm^-1\",\n"
           "    \"frequencies\": [float(e * THZ_TO_CM1)\n"
           "                    for e in dos_dict[\"frequency_points\"]],\n"
           "    \"dos\": [float(d) for d in dos_dict[\"total_dos\"]],\n"
           "}\n"
           "with open(\"phonon_dos.json\", \"w\") as f:\n"
           "    json.dump(dos_json, f)\n"
           "print(\"CALANGO_INFO wrote phonon_dos.json\", flush=True)\n"
           "\n"
           "# Eigenvectors at the path's high-symmetry points (Vibrational\n"
           "# Analysis). phonopy returns complex eigenvectors as columns of a\n"
           "# (3N x 3N) matrix, so column b reshaped to (N, 3) is branch b.\n"
           "mode_qpoints = []\n"
           "for label, qx in zip(labels, special_x):\n"
           "    if not label:\n"
           "        continue\n"
           "    index = int(np.argmin(np.abs(np.asarray(xcoords) - qx)))\n"
           "    q = [float(c) for c in qpoints[index]]\n"
           "    omega, vectors = phonon.get_frequencies_with_eigenvectors(q)\n"
           "    columns = np.asarray(vectors)\n"
           "    natoms = columns.shape[0] // 3\n"
           "    mode_qpoints.append({\n"
           "        \"label\": str(label),\n"
           "        \"q\": q,\n"
           "        \"frequencies\": [float(w * THZ_TO_CM1) for w in omega],\n"
           "        \"eigenvectors\": [\n"
           "            [{\"re\": [float(v) for v in columns[3 * a:3 * a + 3, b].real],\n"
           "              \"im\": [float(v) for v in columns[3 * a:3 * a + 3, b].imag]}\n"
           "             for a in range(natoms)]\n"
           "            for b in range(columns.shape[1])],\n"
           "    })\n"
           "if mode_qpoints:\n"
           "    with open(\"phonon_modes.json\", \"w\") as f:\n"
           "        json.dump({\"unit\": \"cm^-1\", \"qpoints\": mode_qpoints}, f)\n"
           "    print(\"CALANGO_INFO wrote phonon_modes.json\", flush=True)\n"
           "max_cm1 = float(np.max(freqs_thz) * THZ_TO_CM1) if freqs_thz.size else 0.0\n"
           "print(f\"CALANGO_RESULT branches={freqs_thz.shape[1]} \"\n"
           "      f\"acoustic_at_gamma={n_acoustic} max_cm1={max_cm1:.1f}\", flush=True)\n";
}

/// The classic ASE driver: +/-delta along x, y and z for every atom in the
/// supercell (6N force evaluations, no symmetry reduction).
void emitAsePhonons(std::ostringstream& out, const PhononConfig& c)
{
    out << "from ase.phonons import Phonons\n"
           "\n"
           "print(f\"CALANGO_INFO natoms_primitive={len(atoms)} \"\n"
           "      f\"supercell={supercell} displacements={6 * len(atoms)}\",\n"
           "      flush=True)\n"
           "\n"
           "ph = Phonons(atoms, phonon_calc, supercell=supercell, delta=delta,\n"
           "             name=\"phonon\")\n"
           "# Clear any stale displacement cache FIRST: ase.phonons.Phonons.run()\n"
           "# skips displacements whose force file already exists, so a partial\n"
           "# or mismatched prior run would silently leave an incomplete ±delta\n"
           "# set across the supercell and corrupt the force constants. Cleaning\n"
           "# guarantees every displaced supercell configuration is regenerated.\n"
           "ph.clean()\n"
           "ph.run()\n"
           "\n"
           "# Force constants -> dynamical matrix.\n"
        << "ph.read(acoustic=" << (c.acousticSumRule ? "True" : "False") << ")\n"
           "\n"
           "# Gamma-point frequencies (3 acoustic modes -> 0).\n"
           "gamma = np.asarray(ph.band_structure([[0.0, 0.0, 0.0]])[0])\n"
           "n_acoustic = int(np.sum(np.abs(gamma / invcm) < 1.0))\n"
           "for i, freq in enumerate(gamma):\n"
           "    print(f\"CALANGO_RESULT mode={i:3d} freq_cm1={freq / invcm:10.2f} \"\n"
           "          f\"freq_meV={freq * 1e3:9.3f}\", flush=True)\n"
           "\n"
           "# Dispersion along the requested (or ASE-suggested) BZ path.\n"
        << "path_str = " << (c.kpath.empty() ? "None" : "\"" + c.kpath + "\"")
        << "\n"
        << "path = atoms.cell.bandpath(path_str, npoints=" << c.bandPathPoints
        << ")\n"
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
        << "dos_width = " << c.dosWidthCm << "  # Gaussian sigma, cm^-1\n"
        << "try:\n"
           "    dos = ph.get_dos(kpts=kgrid).sample_grid(npts=400,\n"
           "                                             width=dos_width * invcm)\n"
           "    dos_energies, dos_weights = dos.get_energies(), dos.get_weights()\n"
           "except Exception:  # older ASE releases expose ph.dos(...)\n"
           "    dos_energies, dos_weights = ph.dos(kpts=kgrid, npts=400,\n"
           "                                       delta=dos_width * invcm)\n"
           "dos_json = {\n"
           "    \"unit\": \"cm^-1\",\n"
           "    \"frequencies\": [float(e / invcm) for e in dos_energies],\n"
           "    \"dos\": [float(d) for d in dos_weights],\n"
           "}\n"
           "with open(\"phonon_dos.json\", \"w\") as f:\n"
           "    json.dump(dos_json, f)\n"
           "print(\"CALANGO_INFO wrote phonon_dos.json\", flush=True)\n"
           "\n"
           "# Eigenvectors at the path's high-symmetry points, for the\n"
           "# Vibrational Analysis mode animation. Only the special points are\n"
           "# exported: the full path would be a large file of which the viewer\n"
           "# shows a handful of q, and these are the ones with names.\n"
           "mode_qpoints = []\n"
           "for label, qx in zip(labels, special_x):\n"
           "    if not label:\n"
           "        continue\n"
           "    index = int(np.argmin(np.abs(np.asarray(xcoords) - qx)))\n"
           "    q = [float(c) for c in path.kpts[index]]\n"
           "    omega, vectors = ph.band_structure([q], modes=True)\n"
           "    # vectors[kpt][branch][atom][xyz] — ASE returns real modes.\n"
           "    mode_qpoints.append({\n"
           "        \"label\": str(label),\n"
           "        \"q\": q,\n"
           "        \"frequencies\": [float(w / invcm) for w in omega[0]],\n"
           "        \"eigenvectors\": [[[float(c) for c in atom] for atom in branch]\n"
           "                         for branch in np.asarray(vectors[0]).real],\n"
           "    })\n"
           "if mode_qpoints:\n"
           "    with open(\"phonon_modes.json\", \"w\") as f:\n"
           "        json.dump({\"unit\": \"cm^-1\", \"qpoints\": mode_qpoints}, f)\n"
           "    print(\"CALANGO_INFO wrote phonon_modes.json\", flush=True)\n"
           "ph.clean()\n"
           "max_cm1 = float(np.max(bands / invcm)) if bands.size else 0.0\n"
           "print(f\"CALANGO_RESULT branches={bands.shape[1]} \"\n"
           "      f\"acoustic_at_gamma={n_acoustic} max_cm1={max_cm1:.1f}\", flush=True)\n";
}

/// Shared preamble + dispatch between the symmetry-reduced (phonopy/spglib)
/// and the plain ASE 6N displacement drivers. Both are emitted as functions so
/// the choice can fall back at RUN time: whether phonopy is importable is a
/// property of the job environment, not of the machine that wrote the script.
void emitPeriodicPhonons(std::ostringstream& out, const PhononConfig& c)
{
    out << "import json\n"
           "import numpy as np\n"
           "from ase.units import invcm\n"
           "\n"
        << "supercell = (" << c.supercell[0] << ", " << c.supercell[1] << ", "
        << c.supercell[2] << ")\n"
        << "delta = " << c.deltaAngstrom << "  # Å\n"
           "base_calc = atoms.calc\n"
           "\n";

    if (c.removeResidualForces)
        emitResidualFreeCalculator(out);

    // Build each driver into its own buffer, then nest it in a function. The
    // baseline evaluation lives INSIDE each driver: the two use different
    // supercell atom orderings (ASE's repeat vs phonopy's), and hoisting it
    // would also spend a force evaluation on whichever path does not run.
    std::ostringstream aseBlock;
    if (c.removeResidualForces)
        aseBlock << "reference = atoms * supercell\n"
                    "reference.calc = base_calc\n"
                    "residual = reference.get_forces()\n"
                    "print(f\"CALANGO_INFO residual_fmax=\"\n"
                    "      f\"{np.max(np.linalg.norm(residual, axis=1)):.6f}\",\n"
                    "      flush=True)\n"
                    "phonon_calc = ResidualFreeCalculator(base_calc, residual)\n"
                    "\n";
    else
        aseBlock << "phonon_calc = base_calc\n\n";
    emitAsePhonons(aseBlock, c);
    out << "def run_ase_displacements():\n"
        << indentBlock(aseBlock.str(), "    ") << "\n\n";

    if (!c.symmetryReducedDisplacements) {
        out << "run_ase_displacements()\n";
        return;
    }

    std::ostringstream symBlock;
    emitSymmetryReducedPhonons(symBlock, c);
    out << "def run_symmetry_reduced_displacements():\n"
        << indentBlock(symBlock.str(), "    ") << "\n\n"
        << "# phonopy (and its spglib dependency) live in the job environment,\n"
           "# so availability is decided here rather than when the script was\n"
           "# generated. Without it the physics is unchanged — only the number\n"
           "# of force evaluations grows back to the full 6N.\n"
           "try:\n"
           "    import phonopy  # noqa: F401\n"
           "except ImportError:\n"
           "    print(\"CALANGO_WARNING phonopy not importable — falling back to \"\n"
           "          \"the full 6N ASE displacement set (pip install phonopy)\",\n"
           "          flush=True)\n"
           "    run_ase_displacements()\n"
           "else:\n"
           "    run_symmetry_reduced_displacements()\n";
}

void emitMolecularVibrations(std::ostringstream& out, const PhononConfig& c)
{
    out << "import numpy as np\n"
           "\n"
           "from ase.vibrations import Vibrations\n"
           "\n"
        << "delta = " << c.deltaAngstrom << "  # Å\n"
           "base_calc = atoms.calc\n"
           "print(f\"CALANGO_INFO displacements={6 * len(atoms)}\", flush=True)\n"
           "\n";
    if (c.removeResidualForces) {
        emitResidualFreeCalculator(out);
        out << "# Baseline forces on the relaxed reference geometry. For a\n"
               "# molecule these are the residual translations/rotations left by\n"
               "# an incompletely converged relaxation; subtracting them keeps\n"
               "# the 6 zero modes near zero instead of drifting imaginary.\n"
               "residual = atoms.get_forces()\n"
               "print(f\"CALANGO_INFO residual_fmax=\"\n"
               "      f\"{np.max(np.linalg.norm(residual, axis=1)):.6f}\",\n"
               "      flush=True)\n"
               "atoms.calc = ResidualFreeCalculator(base_calc, residual)\n"
               "\n";
    }
    out << "vib = Vibrations(atoms, delta=delta, name=\"vib\")\n"
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
        << (config.symmetryReducedDisplacements
                ? "# Displacements are reduced to the symmetry-irreducible set\n"
                  "# (spglib space group via phonopy), with the full force-constant\n"
                  "# matrix rebuilt by symmetry. Falls back to the plain 6N set\n"
                  "# when phonopy is not installed in the job environment.\n"
                : "# Every atom is displaced by ±delta along x, y and z (6N force\n"
                  "# evaluations, no symmetry reduction).\n")
        << (config.removeResidualForces
                ? "# Forces on the un-displaced reference geometry are subtracted\n"
                  "# from every displaced evaluation (residual force removal).\n"
                : "")
        << "# This is a plain ASE script: edit it freely or run it standalone.\n"
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
