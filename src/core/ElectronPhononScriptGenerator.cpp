#include "core/ElectronPhononScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"

#include <sstream>

namespace calango::core {

namespace {

/// The commensurability guard, emitted before anything expensive runs.
///
/// A q-point only has a phonon in the finite-difference cache if it is a
/// reciprocal lattice vector of the SUPERCELL — i.e. if q·N is integral for
/// the supercell repetitions N. Stage 3 is where GPAW discovers otherwise, and
/// that is after stages 1 and 2 have been paid for, which on a real system is
/// hours. Checked here instead, in the first seconds of the run.
std::string commensurabilityBlock()
{
    return R"PY(# --- q-mesh must be commensurate with the supercell ----------------
# A phonon at q exists in the finite-difference cache only when q is a
# reciprocal vector of the supercell: displacing the primitive cell inside
# an N1xN2xN3 box samples exactly the q with q_i * N_i integral. Anything
# else has no dynamical matrix to be read back, and GPAW says so in stage 3
# — after the displacement runs, which are the entire cost of this module.
_bad = [i for i in range(3) if SUPERCELL[i] % QGRID[i] != 0
        and QGRID[i] % SUPERCELL[i] != 0]
if any(QGRID[i] > SUPERCELL[i] for i in range(3)):
    raise ValueError(
        f"q-mesh {QGRID} is denser than the supercell {SUPERCELL} can "
        f"represent. The finite-difference cache only knows the phonons "
        f"commensurate with the supercell, so a denser q-mesh has no "
        f"dynamical matrix to read. Either coarsen the q-mesh to divide "
        f"{SUPERCELL}, or enlarge the supercell (which costs 6N+1 more SCF "
        f"runs per added repetition).")
if _bad:
    raise ValueError(
        f"q-mesh {QGRID} is not commensurate with the supercell "
        f"{SUPERCELL}: each q_i*N_i must be integral. Use a q-mesh whose "
        f"entries divide the supercell repetitions.")
print(f"CALANGO_INFO supercell={SUPERCELL} q-mesh={QGRID} "
      f"(commensurate)", flush=True)

)PY";
}

/// The raw arrays Calango's tetrahedron analysis reads.
///
/// The script's job ends here. alpha^2F, lambda and tau are computed in C++
/// (see ElectronPhononAnalysis), which integrates both Fermi-surface deltas
/// with the linear tetrahedron method.
///
/// That split is not tidiness. This block used to compute lambda here with a
/// Gaussian of width sigma_e, and on fcc Al lambda came out 0.009, 0.22, 0.49,
/// 1.55, 4.99, 16.6, 31.0 as sigma_e was widened by 16x — no plateau anywhere,
/// so the number reported was whichever sigma_e happened to be configured. The
/// tetrahedron method has no such parameter, so there is nothing left to
/// converge and the smearing sweep that used to police it is gone with it.
///
/// The biggest array is not rewritten: GPAW's own gsqklnn.npy is already
/// (spins, q, k, modes, bands, bands) complex, which is the exact index order
/// the analysis wants, so the manifest points straight at it. On a production
/// mesh that array is tens of gigabytes and a reformatted copy would be
/// another one.
std::string rawOutputBlock(const ElectronPhononConfig& cfg)
{
    std::ostringstream out;
    out << "# --- Raw output for Calango's tetrahedron analysis --------------\n"
           "#\n"
           "# alpha^2 F(w) = 1/(N(E_F) Nk Nq) *\n"
           "#     sum_{q,nu,k,m,n} |g_mn^nu(k,q)|^2\n"
           "#         delta(e_nk - E_F) delta(e_m,k+q - E_F) delta(w - w_qnu)\n"
           "#\n"
           "# Both electron deltas sit AT the Fermi level, so this is a\n"
           "# Fermi-surface integral and converges with the k-mesh as slowly as\n"
           "# any other one. Calango evaluates them on tetrahedra; this script\n"
           "# only has to hand over the ingredients.\n"
           "\n"
           "# The k-points must be in the order the tetrahedron decomposition\n"
           "# assumes, ((i1*n2)+i2)*n3+i3 with the last index fastest. GPAW\n"
           "# enumerates its Monkhorst-Pack set that way, but this is checked\n"
           "# rather than trusted: if the enumeration ever changed, every\n"
           "# eigenvalue would be attached to the wrong corner of the mesh and\n"
           "# the result would still look like a number.\n"
           "#\n"
           "# Checked RELATIVE to the first point, not against absolute\n"
           "# coordinates: a Monkhorst-Pack set with gamma=False sits at\n"
           "# (2i-n+1)/2n, half a step off i/n. That uniform shift is harmless\n"
           "# — it moves every tetrahedron identically and changes no\n"
           "# neighbour relation — so testing absolute positions would reject\n"
           "# a perfectly good grid. What must hold is the spacing and the\n"
           "# order.\n"
           "_offsets = np.array([[_i / KGRID[0], _j / KGRID[1], _k / KGRID[2]]\n"
           "                     for _i in range(KGRID[0])\n"
           "                     for _j in range(KGRID[1])\n"
           "                     for _k in range(KGRID[2])])\n"
           "# Wrapped into [-0.5, 0.5) so that 0 and 1 both count as zero.\n"
           "_drift = np.mod(_kpts - _kpts[0] - _offsets + 0.5, 1.0) - 0.5\n"
           "if not np.allclose(_drift, 0.0, atol=1e-6):\n"
           "    raise RuntimeError(\n"
           "        \"GPAW's k-point order is not the row-major grid order this \"\n"
           "        \"module's tetrahedron integration assumes. Reorder _eps \"\n"
           "        \"and _kplusq before saving them, or the Fermi-surface \"\n"
           "        \"integration will pair unrelated states.\")\n"
           "\n"
           "# Reciprocal lattice vectors as ROWS, inverse Angstrom. ASE's\n"
           "# reciprocal() omits the 2*pi, and without it every gradient — so\n"
           "# every tetrahedron weight, so N(E_F) and lambda — is off by\n"
           "# (2*pi)^3.\n"
           "_recip = 2.0 * np.pi * np.array(atoms.cell.reciprocal())\n"
           "\n"
           "np.save(\"elph_eigenvalues.npy\",\n"
           "        np.ascontiguousarray(_eps, dtype=float))\n"
           "# The RAW frequencies, negatives included. Calango masks and counts\n"
           "# the imaginary ones itself; substituting a placeholder here would\n"
           "# hide an unstable structure behind a plausible lambda.\n"
           "np.save(\"elph_frequencies.npy\",\n"
           "        np.ascontiguousarray(_omega_raw, dtype=float))\n"
           "np.save(\"elph_kplusq.npy\",\n"
           "        np.ascontiguousarray(_kplusq, dtype=float))\n"
           "\n"
           "with open(\"elph_raw.txt\", \"w\") as _handle:\n"
           "    _handle.write(\"calango.elph.raw 1\\n\")\n"
           "    _handle.write(\"kgrid %d %d %d\\n\" % tuple(KGRID))\n"
           "    _handle.write(\"reciprocal \"\n"
           "                  + \" \".join(repr(float(_v))\n"
           "                              for _v in _recip.reshape(-1)) + \"\\n\")\n"
           "    _handle.write(\"spins %d\\n\" % _nspins)\n"
           "    _handle.write(\"bands %d\\n\" % _eps.shape[2])\n"
           "    _handle.write(\"qcount %d\\n\" % _nq)\n"
           "    _handle.write(\"modes %d\\n\" % _omega_raw.shape[1])\n"
           "    _handle.write(\"fermi %s\\n\" % repr(_efermi))\n"
        << "    _handle.write(\"temperature " << cfg.temperatureK << "\\n\")\n"
        << "    _handle.write(\"smearing " << cfg.phononSmearingEv << "\\n\")\n"
        << "    _handle.write(\"mu_star " << cfg.muStar << "\\n\")\n"
        << "    _handle.write(\"plasma_frequency " << cfg.plasmaFrequencyEv
        << "\\n\")\n"
        << "    _handle.write(\"eigenvalues elph_eigenvalues.npy\\n\")\n"
           "    _handle.write(\"frequencies elph_frequencies.npy\\n\")\n"
           "    _handle.write(\"kplusq elph_kplusq.npy\\n\")\n"
           "    # GPAW's own file, referenced where it lies.\n"
           "    _handle.write(\"gsquared gsqklnn.npy\\n\")\n"
           "print(\"CALANGO_INFO wrote elph_raw.txt; alpha^2F, lambda and tau \"\n"
           "      \"are computed by Calango using linear-tetrahedron \"\n"
           "      \"integration (no Fermi-surface smearing).\", flush=True)\n";
    return out.str();
}

} // namespace

std::string generateElectronPhononScript(const ElectronPhononConfig& cfg)
{
    std::ostringstream out;
    out << "#!/usr/bin/env python3\n"
           "# Electron-phonon coupling (gpaw.elph) — generated by Calango "
        << CALANGO_VERSION
        << "\n"
           "#\n"
           "# Three stages, in order of cost:\n"
           "#   1. DisplacementRunner  — 6N+1 supercell SCF runs, ~all the time\n"
           "#   2. Supercell matrix    — project dV/du onto the LCAO basis\n"
           "#   3. Bloch matrix        — rotate to g_mn^nu(k,q), then derive\n"
           "#                            alpha^2F, lambda and tau(T)\n"
           "#\n"
           "# This is a plain ASE/GPAW script: edit it freely or run it "
           "standalone.\n"
           "\n"
           "import json\n"
           "\n"
           "import numpy as np\n"
           "from ase.io import read\n"
           "\n"
        << AseScriptGenerator::jsonLoggerPreamble()
        << "from gpaw import GPAW, FermiDirac\n"
           "from gpaw.elph import DisplacementRunner, ElectronPhononMatrix, "
           "Supercell\n"
           "from ase.phonons import Phonons\n"
           "\n"
        << "SUPERCELL = (" << cfg.supercell[0] << ", " << cfg.supercell[1]
        << ", " << cfg.supercell[2] << ")\n"
        << "KGRID = (" << cfg.kGrid[0] << ", " << cfg.kGrid[1] << ", "
        << cfg.kGrid[2] << ")\n"
        << "QGRID = (" << cfg.qGrid[0] << ", " << cfg.qGrid[1] << ", "
        << cfg.qGrid[2] << ")\n"
        << "BASIS = \"" << cfg.basis << "\"\n"
        << "DELTA = " << cfg.deltaAngstrom << "  # Angstrom\n"
           "\n"
           "atoms = read(\"structure.extxyz\")\n"
           "_calango_progress(0, 4)\n"
           "\n"
        << commensurabilityBlock()
        << "\n"
           "def _calculator(txt, parallel=None):\n"
           "    \"\"\"The LCAO ground state every stage shares.\n"
           "\n"
           "    LCAO is mandatory, not a speed choice: stage 2 projects the\n"
           "    potential gradients onto BASIS FUNCTIONS, so a plane-wave\n"
           "    ground state has nothing for it to project onto.\n"
           "\n"
           "    point_group symmetry is off throughout. The displaced\n"
           "    supercells have lower symmetry than the ideal crystal, and a\n"
           "    calculator that reduced the k-set by the undisplaced symmetry\n"
           "    would be describing a different system in stage 1 than in\n"
           "    stage 3.\n"
           "    \"\"\"\n"
           "    return GPAW(\n"
           "        mode=\"lcao\",\n"
           "        basis=BASIS,\n"
           "        kpts={\"size\": KGRID, \"gamma\": False},\n"
        << "        xc=\"" << (cfg.calculator.gpawXc.empty() ? std::string("PBE")
                                                        : cfg.calculator.gpawXc)
        << "\",\n"
           "        occupations=FermiDirac("
        << (cfg.calculator.smearingWidthEv > 0.0 ? cfg.calculator.smearingWidthEv
                                                 : 0.05)
        << "),\n"
           "        symmetry={\"point_group\": False},\n"
           "        convergence={\"forces\": 1e-4, \"density\": 1e-7},\n"
           "        parallel=parallel or {},\n"
           "        txt=txt,\n"
           "    )\n"
           "\n"
           "\n"
           "# --- Stage 1: finite displacements ------------------------------\n"
           "# Every atom of the primitive cell, +/- DELTA along x, y and z,\n"
           "# each inside the supercell: 6N+1 SCF runs. This dominates the\n"
           "# wall time of the whole module.\n"
           "print(f\"CALANGO_INFO stage 1/3: {6 * len(atoms) + 1} supercell \"\n"
           "      f\"SCF runs for the displacements\", flush=True)\n"
           "\n"
           "# Clear half-written cache entries before resuming.\n"
           "#\n"
           "# This stage is hours long and therefore GETS interrupted — a\n"
           "# walltime limit, a full disk, Ctrl-C. An interrupted displacement\n"
           "# leaves a ZERO-LENGTH .json behind, and ASE decides which\n"
           "# displacements are already done by which files EXIST, not by\n"
           "# whether they parse. So the empty file counts as done: the rerun\n"
           "# skips that displacement, the cache hands back None for it, and\n"
           "# the force constants are silently built from an incomplete set.\n"
           "# ASE's own Displacement.run docstring says the file 'must be\n"
           "# deleted before restarting the job' and does not delete it.\n"
           "#\n"
           "# strip_empties() is that deletion, and it is safe on a clean\n"
           "# start (nothing to strip) as well as on a resume.\n"
           "from ase.utils.filecache import MultiFileJSONCache\n"
           "_stripped = MultiFileJSONCache(\"elph\").strip_empties()\n"
           "if _stripped:\n"
           "    print(f\"CALANGO_WARN cleared {_stripped} empty displacement \"\n"
           "          f\"cache file(s) left by an interrupted run; those \"\n"
           "          f\"displacements will be recomputed. The completed ones \"\n"
           "          f\"are reused.\", flush=True)\n"
           "\n"
           "_calc1 = _calculator(\"elph_displacements.txt\")\n"
           "atoms.calc = _calc1\n"
           "_elph = DisplacementRunner(atoms, _calc1, supercell=SUPERCELL,\n"
           "                          name=\"elph\", delta=DELTA,\n"
           "                          calculate_forces=True)\n"
           "_elph.run()\n"
           "_calango_progress(1, 4)\n"
           "\n"
           "# --- Stage 2: supercell matrix ----------------------------------\n"
           "# domain/band parallelisation is pinned to 1 because the projection\n"
           "# walks the basis functions of the whole supercell on one rank;\n"
           "# GPAW's own elph fixtures do the same.\n"
           "print(\"CALANGO_INFO stage 2/3: projecting dV/du onto the LCAO \"\n"
           "      \"basis\", flush=True)\n"
           "_calc2 = _calculator(\"elph_supercell.txt\",\n"
           "                     parallel={\"domain\": 1, \"band\": 1})\n"
           "_sc = Supercell(atoms, supercell=SUPERCELL)\n"
           "_sc.calculate_supercell_matrix(_calc2)\n"
           "_calango_progress(2, 4)\n"
           "\n"
           "# --- Stage 3: Bloch-basis matrix elements -----------------------\n"
           "# The ground state the rotation uses must have NO symmetry\n"
           "# reduction at all: bloch_matrix indexes the full k-set, and an\n"
           "# irreducible one would silently pair the wrong states.\n"
           "print(\"CALANGO_INFO stage 3/3: rotating to the Bloch basis\",\n"
           "      flush=True)\n"
           "_calc3 = GPAW(\n"
           "    mode=\"lcao\",\n"
           "    basis=BASIS,\n"
           "    kpts={\"size\": KGRID, \"gamma\": False},\n"
        << "    xc=\"" << (cfg.calculator.gpawXc.empty() ? std::string("PBE")
                                                     : cfg.calculator.gpawXc)
        << "\",\n"
           "    occupations=FermiDirac("
        << (cfg.calculator.smearingWidthEv > 0.0 ? cfg.calculator.smearingWidthEv
                                                 : 0.05)
        << "),\n"
           "    symmetry=\"off\",\n"
           "    convergence={\"density\": 1e-7},\n"
           "    txt=\"elph_groundstate.txt\",\n"
           ")\n"
           "atoms.calc = _calc3\n"
           "atoms.get_potential_energy()\n"
           "\n"
           "# The q-points, as fractions of the reciprocal cell.\n"
           "_qs = np.array([[i / QGRID[0], j / QGRID[1], k / QGRID[2]]\n"
           "                for i in range(QGRID[0])\n"
           "                for j in range(QGRID[1])\n"
           "                for k in range(QGRID[2])])\n"
           "_epm = ElectronPhononMatrix(atoms, \"supercell\", \"elph\")\n"
           "# prefactor=True includes sqrt(hbar/2*M*omega), so g comes back in\n"
           "# eV — the units alpha^2F is defined for. Without it the elements\n"
           "# are bare eV/Angstrom and every derived quantity below is wrong by\n"
           "# a mode-dependent factor.\n"
           "_g = _epm.bloch_matrix(_calc3, k_qc=_qs, savetofile=True,\n"
           "                       prefactor=True)\n"
           "print(f\"CALANGO_INFO g_sqklnn shape = {_g.shape}\", flush=True)\n"
           "_calango_progress(3, 4)\n"
           "\n"
           "# --- Electronic structure on the same mesh ----------------------\n"
           "_efermi = float(_calc3.get_fermi_level())\n"
           "_kpts = np.array(_calc3.get_bz_k_points())\n"
           "_nspins = _calc3.get_number_of_spins()\n"
           "_nk = len(_kpts)\n"
           "_nq = len(_qs)\n"
           "_eps = np.array([[_calc3.get_eigenvalues(kpt=_ik, spin=_s)\n"
           "                  for _ik in range(_nk)]\n"
           "                 for _s in range(_nspins)])\n"
           "\n"
           "# k+q, as an index into the same k-set. Both meshes are\n"
           "# Monkhorst-Pack on the same reciprocal cell, so the sum wraps back\n"
           "# onto a grid point; matching by rounded fractional coordinate is\n"
           "# exact for that and does not depend on the ordering GPAW happens\n"
           "# to enumerate the k-points in.\n"
           "_key = {tuple(np.round(np.mod(_k, 1.0), 6)): _i\n"
           "        for _i, _k in enumerate(_kpts)}\n"
           "_kplusq = np.zeros((_nq, _nk), dtype=int)\n"
           "for _iq, _q in enumerate(_qs):\n"
           "    for _ik, _k in enumerate(_kpts):\n"
           "        _target = tuple(np.round(np.mod(_k + _q, 1.0), 6))\n"
           "        if _target not in _key:\n"
           "            raise RuntimeError(\n"
           "                f\"k+q = {_target} is not on the k-mesh. The \"\n"
           "                f\"q-mesh {QGRID} must divide the k-mesh {KGRID} \"\n"
           "                f\"for every k+q to be a sampled state; otherwise \"\n"
           "                f\"the Fermi-surface delta at k+q has no \"\n"
           "                f\"eigenvalue to evaluate.\")\n"
           "        _kplusq[_iq, _ik] = _key[_target]\n"
           "\n"
           "# --- Phonon frequencies from the same displacement cache --------\n"
           "# Read back rather than recomputed: the dynamical matrix that goes\n"
           "# with these matrix elements is the one their own displacements\n"
           "# produced, and a second phonon run at different settings would\n"
           "# pair g with frequencies it was not computed against.\n"
           "_ph = Phonons(atoms, name=\"elph\", supercell=SUPERCELL,\n"
           "              center_refcell=True)\n"
           "_ph.read(acoustic=True)\n"
           "_omega_raw = np.array(_ph.band_structure(_qs))  # eV\n"
           "# Imaginary (negative) frequencies are a real result — an unstable\n"
           "# structure — but alpha^2F integrates 1/w and cannot represent one.\n"
           "# They are saved AS THEY ARE and excluded downstream by mask.\n"
           "#\n"
           "# Not by writing NaN into the array, which is the obvious spelling\n"
           "# and does not exclude anything: np.max over the result is NaN, so\n"
           "# every derived quantity is NaN and lambda comes out NaN with\n"
           "# tau = inf. Observed exactly that on fcc Al, whose acoustic\n"
           "# branches at a 2x2x2 supercell come out marginally negative —\n"
           "# the ordinary case, not a pathological one.\n"
           "_unstable = int(np.sum(_omega_raw < 0.0))\n"
           "if _unstable:\n"
           "    print(f\"CALANGO_WARN {_unstable} imaginary phonon \"\n"
           "          f\"frequencies: the structure is not at a local minimum. \"\n"
           "          f\"They are excluded from alpha^2F, which therefore \"\n"
           "          f\"describes only the stable modes.\", flush=True)\n"
           "if not np.any(np.isfinite(_omega_raw) & (_omega_raw > 1e-6)):\n"
           "    raise RuntimeError(\n"
           "        \"Every phonon frequency is zero or imaginary, so there is \"\n"
           "        \"no stable mode for electrons to scatter off. Relax the \"\n"
           "        \"structure before computing its electron-phonon \"\n"
           "        \"coupling — alpha^2F integrates 1/omega and is not \"\n"
           "        \"defined on an unstable geometry.\")\n"
           "\n"
        << rawOutputBlock(cfg)
        << "\n"
           "# Run settings, kept beside the arrays so a result found on disk\n"
           "# months later still says what produced it.\n"
           "_provenance = {\n"
           "    \"supercell\": list(SUPERCELL),\n"
           "    \"kgrid\": list(KGRID),\n"
           "    \"qgrid\": list(QGRID),\n"
           "    \"basis\": BASIS,\n"
           "    \"delta_A\": DELTA,\n"
           "    \"fermi_level_eV\": _efermi,\n"
           "    \"imaginary_modes\": _unstable,\n"
           "    \"integration\": \"tetrahedron\",\n"
           "}\n"
           "with open(\"elph_run.json\", \"w\") as handle:\n"
           "    json.dump(_provenance, handle, indent=2)\n"
           "_calango_progress(4, 4)\n"
           "print(\"CALANGO_RESULT elph=elph_raw.txt\", flush=True)\n"
           "_calango_event(\"done\",\n"
           "               f\"g_sqklnn ready on a {KGRID} k-mesh and {QGRID} \"\n"
           "               f\"q-mesh; alpha^2F and tau follow from the \"\n"
           "               f\"tetrahedron analysis\")\n";
    return out.str();
}

} // namespace calango::core
