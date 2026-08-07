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

    if (c.loToSplitting()) {
        out << "\n"
               "# --- LO-TO splitting -------------------------------------\n"
               "#\n"
               "# The force constants above come from a finite supercell, which\n"
               "# is charge-neutral and so cannot host the macroscopic electric\n"
               "# field a long-wavelength LO mode sets up. Without that field\n"
               "# the LO and TO branches are degenerate at Gamma, which for a\n"
               "# polar crystal is simply wrong. The field is added back\n"
               "# analytically from the Born effective charges Z* and the\n"
               "# electronic dielectric tensor eps_inf.\n"
               "born_charges_file = r\"" << c.bornChargesFile << "\"\n"
               "with open(born_charges_file) as _handle:\n"
               "    _born_data = json.load(_handle)\n"
               "born_tensors = np.zeros((len(atoms), 3, 3))\n"
               "_seen = set()\n"
               "for _entry in _born_data[\"atoms\"]:\n"
               "    born_tensors[int(_entry[\"index\"])] = np.asarray(\n"
               "        _entry[\"tensor\"], dtype=float)\n"
               "    _seen.add(int(_entry[\"index\"]))\n"
               "if len(_seen) != len(atoms):\n"
               "    raise SystemExit(\n"
               "        f\"CALANGO_ERROR the Born charges run covers "
               "{len(_seen)} atom(s) \"\n"
               "        f\"but this structure has {len(atoms)}. LO-TO "
               "splitting needs a \"\n"
               "        f\"Z* tensor for every atom - re-run Born Effective "
               "Charges over \"\n"
               "        f\"the whole cell, on this same geometry.\")\n"
               "\n"
               "# Z* must sum to zero over the cell (the acoustic sum rule):\n"
               "# translating the whole crystal cannot create a dipole. A\n"
               "# residual here is the Born run's own convergence error, and\n"
               "# left in it puts a spurious dipole on the acoustic modes.\n"
               "_residual = born_tensors.sum(axis=0)\n"
               "_residual_max = float(np.abs(_residual).max())\n"
               "born_tensors -= _residual / len(atoms)\n"
               "print(f\"CALANGO_INFO born_asr_residual_e={_residual_max:.4f}\",\n"
               "      flush=True)\n"
               "\n"
               "dielectric_tensor = np.array([\n";
        for (int row = 0; row < 3; ++row) {
            out << "    [" << c.dielectric[row][0] << ", " << c.dielectric[row][1]
                << ", " << c.dielectric[row][2] << "],\n";
        }
        out << "])\n"
               "# 14.399652 eV*A is e^2/(4*pi*eps_0) - the unit conversion\n"
               "# phonopy expects for Z* in |e| and distances in Angstrom.\n"
               "phonon.nac_params = {\"born\": born_tensors,\n"
               "                     \"dielectric\": dielectric_tensor,\n"
               "                     \"factor\": 14.399652}\n"
               "print(\"CALANGO_INFO lo_to_splitting=on \"\n"
               "      f\"eps_inf_diag={np.diag(dielectric_tensor).tolist()}\",\n"
               "      flush=True)\n"
               "\n"
               "\n"
               "def _gamma_direction(index, points):\n"
               "    \"\"\"Direction the path approaches Gamma from.\n"
               "\n"
               "    The correction is a limit, not a value: at exactly q = 0 the\n"
               "    dipole field depends on which way you came in, so the LO\n"
               "    frequency at Gamma differs along different directions. That\n"
               "    is the physics, not an artifact - and it is why phonopy\n"
               "    needs a direction rather than just a q-point.\n"
               "    \"\"\"\n"
               "    for step in (1, -1):\n"
               "        probe = index + step\n"
               "        while 0 <= probe < len(points):\n"
               "            candidate = np.asarray(points[probe], dtype=float)\n"
               "            if np.linalg.norm(candidate) > 1e-8:\n"
               "                return candidate.tolist()\n"
               "            probe += step\n"
               "    return [1.0, 0.0, 0.0]  # a path that is nothing but Gamma\n"
               "\n"
               "\n";
    }
    out << "\n"
           "# Gamma-point frequencies (THz from phonopy -> cm^-1), with the\n"
           "# irreducible representation of each branch from the eigenvector\n"
           "# subspace projection (phonopy's eigenvectors are the\n"
           "# mass-weighted columns _gamma_irreps expects).\n"
           "THZ_TO_CM1 = 33.35641\n"
           "_g_omega, _g_vectors = phonon.get_frequencies_with_eigenvectors(\n"
           "    [0.0, 0.0, 0.0])\n"
           "gamma = np.asarray(_g_omega)\n"
           "gamma_irreps = _gamma_irreps(atoms, gamma * THZ_TO_CM1,\n"
           "                             np.asarray(_g_vectors))\n"
           "n_acoustic = int(np.sum(np.abs(gamma * THZ_TO_CM1) < 1.0))\n"
           "for i, freq in enumerate(gamma):\n"
           "    _ir = gamma_irreps[i] if gamma_irreps else \"\"\n"
           "    print(f\"CALANGO_RESULT mode={i:3d} \"\n"
           "          f\"freq_cm1={freq * THZ_TO_CM1:10.2f} \"\n"
           "          f\"freq_meV={freq * 4.135667:9.3f}\"\n"
           "          + (f\" irrep={_ir}\" if _ir else \"\"), flush=True)\n"
           "\n"        << (c.loToSplitting()
                ? "# The Gamma frequencies above are the TRANSVERSE ones: with "
                  "no\n"
                  "# direction supplied, phonopy leaves q = 0 uncorrected. Read "
                  "the\n"
                  "# longitudinal set as the limit along x, and report the "
                  "splitting\n"
                  "# itself - the one number this whole correction exists to "
                  "produce.\n"
                  "phonon.run_qpoints([[0.0, 0.0, 0.0]], "
                  "nac_q_direction=[1.0, 0.0, 0.0])\n"
                  "gamma_lo = np.asarray(\n"
                  "    phonon.get_qpoints_dict()[\"frequencies\"])[0]\n"
                  "for i, (_to, _lo) in enumerate(zip(gamma, gamma_lo)):\n"
                  "    print(f\"CALANGO_RESULT mode={i:3d} \"\n"
                  "          f\"TO_cm1={_to * THZ_TO_CM1:10.2f} \"\n"
                  "          f\"LO_cm1={_lo * THZ_TO_CM1:10.2f} \"\n"
                  "          f\"split_cm1={(_lo - _to) * THZ_TO_CM1:9.2f}\",\n"
                  "          flush=True)\n"
                  "_split = float(np.max((gamma_lo - gamma)) * THZ_TO_CM1)\n"
                  "print(f\"CALANGO_RESULT lo_to_split_cm1={_split:.2f}\", "
                  "flush=True)\n"
                  "\n"
                : "")
        << 
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
           "freqs_thz = np.asarray(phonon.get_qpoints_dict()[\"frequencies\"])\n"        << (c.loToSplitting()
                ? "# phonopy cannot apply the correction at exactly q = 0 "
                  "without\n"
                  "# being told which way the path arrives, so those points come "
                  "back\n"
                  "# uncorrected. Redo each of them with the direction of "
                  "approach:\n"
                  "# this is what puts the LO branch above the TO branch at "
                  "Gamma\n"
                  "# instead of leaving them degenerate.\n"
                  "for _i, _q in enumerate(qpoints):\n"
                  "    if np.linalg.norm(_q) > 1e-8:\n"
                  "        continue\n"
                  "    _direction = _gamma_direction(_i, qpoints)\n"
                  "    phonon.run_qpoints([_q], nac_q_direction=_direction)\n"
                  "    freqs_thz[_i] = np.asarray(\n"
                  "        phonon.get_qpoints_dict()[\"frequencies\"])[0]\n"
                  "    print(f\"CALANGO_INFO gamma_nac_direction={_direction}\",\n"
                  "          flush=True)\n"
                : "")
        << 
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
        << "phonon.run_mesh([" << c.dosKptGrid[0] << ", " << c.dosKptGrid[1]
        << ", " << c.dosKptGrid[2] << "])\n"
           "# RAW, unbroadened: the mesh frequencies binned finely, with no\n"
           "# Gaussian. sigma is applied by the viewer, which makes it a slider\n"
           "# instead of a decision this run is committed to — and a phonon\n"
           "# calculation is expensive enough that re-running it to try a\n"
           "# different broadening is not something anyone does, so the width\n"
           "# used to be chosen once and never questioned.\n"
           "_mesh = phonon.get_mesh_dict()\n"
           "_freq = np.asarray(_mesh[\"frequencies\"]) * THZ_TO_CM1  # (nq, nb)\n"
           "_qw = np.asarray(_mesh[\"weights\"], dtype=float)\n"
           "_qw = _qw / _qw.sum()\n"
        << "_nbins = " << c.dosPoints << "\n"
           "# Start at 0 and pad the top: the acoustic branches go to zero at\n"
           "# Gamma, and a negative floor would put the imaginary modes of an\n"
           "# unstable structure off the grid instead of on it where they can\n"
           "# be seen.\n"
           "_lo = min(0.0, float(_freq.min())) - 10.0\n"
           "_hi = float(_freq.max()) + 10.0\n"
           "_bin = (_hi - _lo) / (_nbins - 1)\n"
           "_hist = np.zeros(_nbins)\n"
           "for _iq in range(_freq.shape[0]):\n"
           "    _idx = np.clip(((_freq[_iq] - _lo) / _bin).astype(int), 0,\n"
           "                    _nbins - 1)\n"
           "    _hist += np.bincount(_idx,\n"
           "                          weights=np.full(_freq.shape[1], _qw[_iq]),\n"
           "                          minlength=_nbins)\n"
           "dos_json = {\n"
           "    \"unit\": \"cm^-1\",\n"
           "    \"frequencies\": [float(_lo + _i * _bin) for _i in range(_nbins)],\n"
           "    \"dos\": [float(v) for v in _hist],\n"
           "    \"broadened\": False,\n"
           "    \"bin_width\": float(_bin),\n"
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
           "    entry = {\n"
           "        \"label\": str(label),\n"
           "        \"q\": q,\n"
           "        \"frequencies\": [float(w * THZ_TO_CM1) for w in omega],\n"
           "        \"eigenvectors\": [\n"
           "            [{\"re\": [float(v) for v in columns[3 * a:3 * a + 3, b].real],\n"
           "              \"im\": [float(v) for v in columns[3 * a:3 * a + 3, b].imag]}\n"
           "             for a in range(natoms)]\n"
           "            for b in range(columns.shape[1])],\n"
           "    }\n"
           "    # Irrep labels exist only at Gamma: away from the zone center\n"
           "    # the factor group is not the little group of q.\n"
           "    if np.linalg.norm(q) < 1e-8 and gamma_irreps:\n"
           "        entry[\"irreps\"] = list(gamma_irreps)\n"
           "    mode_qpoints.append(entry)\n"
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
           "# Gamma-point frequencies (3 acoustic modes -> 0), with the\n"
           "# irreducible representation of each branch. ASE hands back\n"
           "# displacement patterns u = e / sqrt(m); restore the mass\n"
           "# weighting so the projection runs on orthonormal eigenvectors.\n"
           "_g_omega, _g_modes = ph.band_structure([[0.0, 0.0, 0.0]],\n"
           "                                       modes=True)\n"
           "gamma = np.asarray(_g_omega)[0]\n"
           "_g_columns = np.array([\n"
           "    (np.sqrt(atoms.get_masses())[:, None]\n"
           "     * np.asarray(mode).real).reshape(-1)\n"
           "    for mode in np.asarray(_g_modes)[0]]).T\n"
           "gamma_irreps = _gamma_irreps(atoms, gamma / invcm, _g_columns)\n"
           "n_acoustic = int(np.sum(np.abs(gamma / invcm) < 1.0))\n"
           "for i, freq in enumerate(gamma):\n"
           "    _ir = gamma_irreps[i] if gamma_irreps else \"\"\n"
           "    print(f\"CALANGO_RESULT mode={i:3d} freq_cm1={freq / invcm:10.2f} \"\n"
           "          f\"freq_meV={freq * 1e3:9.3f}\"\n"
           "          + (f\" irrep={_ir}\" if _ir else \"\"), flush=True)\n"
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
        << "kgrid = (" << c.dosKptGrid[0] << ", " << c.dosKptGrid[1] << ", "
        << c.dosKptGrid[2] << ")\n"
        << "_nbins = " << c.dosPoints << "\n"
           "# RAW, unbroadened. ase.phonons.Phonons.get_dos() returns the mesh\n"
           "# frequencies and their weights BEFORE any sampling — sample_grid()\n"
           "# is what applies the Gaussian — so taking the RawDOSData directly\n"
           "# is what leaves sigma to the viewer.\n"
           "_raw_ok = True\n"
           "try:\n"
           "    _raw = ph.get_dos(kpts=kgrid)\n"
           "    _f = np.asarray(_raw.get_energies()) / invcm\n"
           "    _w = np.asarray(_raw.get_weights(), dtype=float)\n"
           "except Exception:  # older ASE releases expose ph.dos(...)\n"
           "    # This path can only return an ALREADY-SAMPLED curve: ph.dos()\n"
           "    # applies its own Gaussian and there is no way to ask it not\n"
           "    # to. Broadened at one bin — the resolution floor, so the\n"
           "    # viewer's sigma still dominates — and flagged below so it is\n"
           "    # never convolved a second time.\n"
           "    _raw_ok = False\n"
           "    _f, _w = ph.dos(kpts=kgrid, npts=_nbins, delta=1e-4)\n"
           "    _f = np.asarray(_f) / invcm\n"
           "    _w = np.asarray(_w, dtype=float)\n"
           "_lo = min(0.0, float(_f.min())) - 10.0\n"
           "_hi = float(_f.max()) + 10.0\n"
           "_bin = (_hi - _lo) / (_nbins - 1)\n"
           "_idx = np.clip(((_f - _lo) / _bin).astype(int), 0, _nbins - 1)\n"
           "_hist = np.bincount(_idx, weights=_w, minlength=_nbins)\n"
           "dos_json = {\n"
           "    \"unit\": \"cm^-1\",\n"
           "    \"frequencies\": [float(_lo + _i * _bin) for _i in range(_nbins)],\n"
           "    \"dos\": [float(v) for v in _hist],\n"
           "    \"broadened\": not _raw_ok,\n"
           "    \"bin_width\": float(_bin),\n"
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
           "    entry = {\n"
           "        \"label\": str(label),\n"
           "        \"q\": q,\n"
           "        \"frequencies\": [float(w / invcm) for w in omega[0]],\n"
           "        \"eigenvectors\": [[[float(c) for c in atom] for atom in branch]\n"
           "                         for branch in np.asarray(vectors[0]).real],\n"
           "    }\n"
           "    # Irrep labels exist only at Gamma: away from the zone center\n"
           "    # the factor group is not the little group of q.\n"
           "    if np.linalg.norm(q) < 1e-8 and gamma_irreps:\n"
           "        entry[\"irreps\"] = list(gamma_irreps)\n"
           "    mode_qpoints.append(entry)\n"
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

    // Mulliken labels for the Γ modes. Same class-sum (Burnside) character
    // table construction as the in-app Raman analysis
    // (python_bridge/RamanAnalysis.cpp) — duplicated here because this copy
    // must run inside the job environment, not the embedded interpreter —
    // followed by a projection of each degenerate frequency cluster's
    // eigenvector subspace onto the irreps. Best-effort by design: any
    // failure returns None and the run proceeds unlabeled.
    out << R"PY(
def _gamma_irreps(atoms, freqs_cm1, eigvec_columns):
    """Irreducible-representation label of every Gamma-point branch.

    eigvec_columns: complex (3N, 3N) array whose column b is the
    mass-weighted eigenvector of branch b. Returns a list of labels
    ('' where no clean assignment exists), or None when the analysis
    is unavailable (no spglib, non-primitive cell, ...).
    """
    try:
        import spglib

        cell = (atoms.cell[:], atoms.get_scaled_positions(), atoms.numbers)
        sym = spglib.get_symmetry(cell, symprec=1e-4)
        if sym is None:
            return None
        rots = [np.array(r) for r in sym["rotations"]]
        trans = [np.array(t) for t in sym["translations"]]
        order = len(rots)
        key = lambda m: tuple(int(x) for x in np.rint(m).flatten())
        index = {key(r): i for i, r in enumerate(rots)}
        if len(index) != order:
            # Centering translations: the cell is not primitive, so Gamma
            # carries folded modes a factor-group label would mislabel.
            print("CALANGO_INFO gamma_irreps=skipped (non-primitive cell)",
                  flush=True)
            return None
        identity = index[key(np.eye(3))]
        mult = [[index[key(rots[i] @ rots[j])] for j in range(order)]
                for i in range(order)]
        inv = [mult[i].index(identity) for i in range(order)]

        class_of = [-1] * order
        classes = []
        for i in range(order):
            if class_of[i] >= 0:
                continue
            members = sorted({mult[mult[g][i]][inv[g]] for g in range(order)})
            for m in members:
                class_of[m] = len(classes)
            classes.append(members)
        nclasses = len(classes)

        # Character table from the class-sum algebra (Burnside).
        a = np.zeros((nclasses, nclasses, nclasses))
        for i, ci in enumerate(classes):
            for j, cj in enumerate(classes):
                for x in ci:
                    for y in cj:
                        a[i, j, class_of[mult[x][y]]] += 1.0
        for l, cl in enumerate(classes):
            a[:, :, l] /= len(cl)
        rng = np.random.default_rng(12345)
        combo = np.tensordot(rng.random(nclasses), a, axes=(0, 0))
        _, vectors = np.linalg.eig(combo)
        characters = []
        for col in vectors.T:
            lam = col / col[class_of[identity]]
            dim = np.sqrt(order / np.sum(np.abs(lam) ** 2
                                         / [len(c) for c in classes]))
            characters.append(dim * lam / [len(c) for c in classes])
        characters = np.array(characters)

        used = [False] * len(characters)
        irreps = []  # (chi_real, dim, paired)
        for i, chi in enumerate(characters):
            if used[i]:
                continue
            if np.max(np.abs(chi.imag)) < 1e-6:
                irreps.append((chi.real,
                               int(round(chi[class_of[identity]].real)),
                               False))
                used[i] = True
                continue
            for j in range(i + 1, len(characters)):
                if not used[j] and np.max(np.abs(characters[j]
                                                 - chi.conj())) < 1e-6:
                    summed = (chi + characters[j]).real
                    irreps.append((summed,
                                   int(round(summed[class_of[identity]])),
                                   True))
                    used[i] = used[j] = True
                    break
            else:
                return None

        # Mulliken labels (same heuristic as Calango's Raman analysis).
        basis = np.array(atoms.cell[:]).T
        to_cart = np.linalg.inv(basis)
        rot_cart = [basis @ np.array(rots[c[0]], dtype=float) @ to_cart
                    for c in classes]
        dets = [int(round(np.linalg.det(m))) for m in rot_cart]
        traces = [float(np.trace(m)) for m in rot_cart]

        def rotation_order(cls):
            if dets[cls] < 0:
                return 0
            theta = np.arccos(np.clip((traces[cls] - 1.0) / 2.0, -1.0, 1.0))
            return 1 if theta < 1e-6 else int(round(2 * np.pi / theta))

        def axis_vector(matrix, eig):
            vals, vecs = np.linalg.eig(matrix.astype(float))
            for v, vec in zip(vals, vecs.T):
                if abs(v - eig) < 1e-6:
                    return np.real(vec)
            return None

        orders = [rotation_order(c) for c in range(nclasses)]
        proper_max = max(orders)
        principal = orders.index(proper_max)
        principal_axis = axis_vector(rot_cart[principal], 1.0)
        inversion = next((c for c in range(nclasses)
                          if np.allclose(rot_cart[c], -np.eye(3), atol=1e-6)),
                         None)

        def is_perp(u, v):
            return u is not None and v is not None \
                and abs(np.dot(u, v)) < 1e-4 * np.linalg.norm(u) \
                * np.linalg.norm(v) + 1e-6

        c2prime = next((c for c in range(nclasses)
                        if orders[c] == 2 and c != principal
                        and is_perp(axis_vector(rot_cart[c], 1.0),
                                    principal_axis)), None)
        sigma_h = None
        sigma_v = None
        for c in range(nclasses):
            if dets[c] < 0 and abs(traces[c] - 1.0) < 1e-6:
                normal = axis_vector(rot_cart[c], -1.0)
                if principal_axis is not None and normal is not None \
                        and abs(abs(np.dot(normal, principal_axis))
                                - np.linalg.norm(normal)
                                * np.linalg.norm(principal_axis)) < 1e-4:
                    sigma_h = c
                elif sigma_v is None:
                    sigma_v = c

        labels = []
        # Several equivalent principal axes = a cubic group, whose 1D irreps
        # are all A by convention (no B labels exist in T..Oh).
        cubic = len(classes[principal]) > 2
        for chi, dim, _ in irreps:
            letter = {1: "A", 2: "E", 3: "T"}.get(dim, f"G{dim}")
            if dim == 1 and proper_max > 1 and not cubic \
                    and chi[principal] < -0.5:
                letter = "B"
            parity = ""
            if inversion is not None:
                parity = "g" if chi[inversion] > 0 else "u"
            prime = ""
            if inversion is None and sigma_h is not None:
                prime = "'" if chi[sigma_h] > 0 else "''"
            labels.append([letter, parity, prime, chi])
        from collections import defaultdict

        groups = defaultdict(list)
        for i, (letter, parity, prime, chi) in enumerate(labels):
            groups[(letter, parity, prime)].append(i)
        final = [None] * len(labels)
        for (letter, parity, prime), members in groups.items():
            if len(members) == 1:
                final[members[0]] = letter + parity + prime
                continue

            def sort_key(i):
                chi = labels[i][3]
                aux = c2prime if irreps[i][1] == 1 else principal
                if aux is None:
                    aux = sigma_v if sigma_v is not None else principal
                # Totally symmetric first (A1 by definition), then more +1
                # characters rank earlier — deterministic and matching the
                # textbook subscript convention. Rounded throughout: the
                # characters carry ~1e-15 eigendecomposition noise, and
                # unrounded keys let it outrank the real ±1 distinctions.
                symmetric = round(sum(len(classes[c]) * chi[c]
                                      for c in range(nclasses)))
                return (-symmetric,
                        -round(chi[aux], 6) if aux is not None else 0.0,
                        tuple(-round(chi[c], 6) for c in range(nclasses)), i)

            for rank, i in enumerate(sorted(members, key=sort_key), start=1):
                final[i] = f"{letter}{rank}{parity}{prime}"

        # Atom permutation of every operation, so the 3N-dimensional action
        # S e = (W ⊗ permutation) e can be applied to the eigenvectors.
        pos = atoms.get_scaled_positions()
        numbers = atoms.numbers
        perms = []
        for R, t in zip(rots, trans):
            mapped = (pos @ np.array(R).T) + t
            perm = np.full(len(pos), -1, dtype=int)
            for a_i, x in enumerate(mapped):
                d = pos - x
                d -= np.rint(d)
                j = int(np.argmin(np.linalg.norm(d, axis=1)))
                if np.linalg.norm(d[j]) > 1e-3 or numbers[j] != numbers[a_i]:
                    return None
                perm[a_i] = j
            perms.append(perm)
        rot_cart_op = [basis @ np.array(R, dtype=float) @ to_cart
                      for R in rots]

        # Degenerate clusters by frequency, then project each cluster's
        # subspace character chi(R) = sum_m <e_m | S(R) e_m> onto the irreps.
        # The label is only committed when the multiplicities exactly account
        # for the cluster's dimension — an accidental degeneracy shows up as
        # a joined label (e.g. "A1g+Eg") rather than a wrong single one.
        freqs = np.asarray(freqs_cm1, dtype=float)
        nmodes = len(freqs)
        vec = np.asarray(eigvec_columns, dtype=complex)
        for b in range(vec.shape[1]):
            norm = np.linalg.norm(vec[:, b])
            if norm > 0:
                vec[:, b] /= norm
        clusters = []
        start = 0
        for b in range(1, nmodes + 1):
            if b == nmodes or abs(freqs[b] - freqs[start]) > 1.0:  # cm^-1
                clusters.append(list(range(start, b)))
                start = b
        out = [""] * nmodes
        natoms = len(pos)
        for cluster in clusters:
            chi_ops = np.zeros(order, dtype=complex)
            for op in range(order):
                W = rot_cart_op[op]
                perm = perms[op]
                for b in cluster:
                    e = vec[:, b].reshape(natoms, 3)
                    Se = np.zeros_like(e)
                    Se[perm] = e @ W.T
                    chi_ops[op] += np.vdot(vec[:, b], Se.reshape(-1))
            counts = []
            for chi, dim, paired in irreps:
                per_op = np.array([chi[class_of[i]] for i in range(order)])
                n = np.sum(chi_ops * per_op).real / order
                counts.append(n / (2.0 if paired else 1.0))
            sel = [(i, int(round(n))) for i, n in enumerate(counts)
                   if int(round(n)) >= 1 and abs(n - round(n)) < 0.1]
            if sel and sum(irreps[i][1] * m for i, m in sel) == len(cluster):
                text = "+".join((final[i] if m == 1 else f"{m}{final[i]}")
                                for i, m in sel)
                for b in cluster:
                    out[b] = text
        return out
    except Exception as error:  # labels are best-effort, never fatal
        print(f"CALANGO_INFO gamma_irreps_failed={error!r}", flush=True)
        return None

)PY";

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

    // LO-TO splitting only exists on the phonopy path: the correction is a
    // property of the dynamical matrix, and ase.phonons has no hook to modify
    // it. Requesting the correction therefore selects that driver regardless
    // of the symmetry-reduction checkbox — running the ASE driver instead
    // would silently produce a dispersion with no splitting in it, which looks
    // like a converged answer and is not one.
    const bool needsPhonopy = c.symmetryReducedDisplacements || c.loToSplitting();
    if (!needsPhonopy) {
        out << "run_ase_displacements()\n";
        return;
    }

    std::ostringstream symBlock;
    emitSymmetryReducedPhonons(symBlock, c);
    out << "def run_symmetry_reduced_displacements():\n"
        << indentBlock(symBlock.str(), "    ") << "\n\n"
        << "# phonopy (and its spglib dependency) live in the job environment,\n"
           "# so availability is decided here rather than when the script was\n"
           "# generated.\n"
           "try:\n"
           "    import phonopy  # noqa: F401\n"
           "except ImportError:\n"
        << (c.loToSplitting()
                ? "    # No silent fallback here. The ASE driver cannot apply "
                  "the\n"
                  "    # non-analytical correction, so it would return a "
                  "dispersion with\n"
                  "    # the LO-TO splitting simply absent - indistinguishable "
                  "from a\n"
                  "    # correct result for a non-polar crystal. Better to stop "
                  "and say\n"
                  "    # so than to hand back a plot that is quietly wrong.\n"
                  "    raise SystemExit(\n"
                  "        \"CALANGO_ERROR LO-TO splitting needs phonopy, which "
                  "is not \"\n"
                  "        \"importable in this environment (pip install "
                  "phonopy). \"\n"
                  "        \"Install it, or clear the Born charges selection to "
                  "run \"\n"
                  "        \"without the correction.\")\n"
                : "    print(\"CALANGO_WARNING phonopy not importable — falling "
                  "back to \"\n"
                  "          \"the full 6N ASE displacement set (pip install "
                  "phonopy)\",\n"
                  "          flush=True)\n"
                  "    run_ase_displacements()\n")
        << "else:\n"
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

std::string PhononScriptGenerator::generate(const PhononConfig& inputConfig,
                                            const std::string& structureFile)
{
    // Finite-displacement phonons move atoms off their symmetric sites by
    // construction — that IS the method. GPAW detects the symmetry group once
    // from the undisplaced geometry and then validates every later set of
    // positions against it, so leaving the reduction on kills the run on the
    // first displacement with "Broken symmetry!" (GPAW 25) or
    // SymmetryBrokenError (GPAW 26). Verified on both.
    //
    // Forced here rather than left to the wizard: there is no configuration in
    // which a symmetry-reduced k-point set survives this, so it is not a
    // choice. (Symmetry-REDUCED displacements, the phonopy path, are a
    // different thing and stay under user control.)
    PhononConfig config = inputConfig;
    config.calculator.gpawSymmetryOff = true;

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
