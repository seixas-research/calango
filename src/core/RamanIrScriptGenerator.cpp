#include "core/RamanIrScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"

#include <sstream>
#include <string>

namespace calango::core {

namespace {

/// Python literal for an optional path: a raw string, or None when unset.
std::string pathLiteral(const std::string& path)
{
    if (path.empty())
        return "None";
    return "r\"" + path + "\"";
}

/// The physics, as pure array functions — identical for every engine.
///
/// This block is what makes "three engines" a front-end question rather than
/// three spectroscopy implementations. Each engine block below produces the
/// same four quantities (Hessian, Z*, dalpha/du, and the geometry), and
/// everything after that — the mass-weighted diagonalization, the two
/// contractions, the Stokes prefactor, the broadening and the JSON schema — is
/// this one piece of code.
///
/// Kept as named, side-effect-free functions rather than inline code so each
/// can be exercised against a case with a known analytic answer (see
/// tests/raman_ir_math_test.py, which extracts them from a generated script by
/// AST and checks a diatomic where every number is available in closed form). A
/// dropped 1/sqrt(M) or a transposed einsum produces a plausible spectrum, not
/// an error, so "it ran" is no evidence at all here.
std::string sharedMathBlock()
{
    return R"PY(
# 1 e*A expressed in debye, for the (D/A)^2/amu unit the IR literature quotes.
DEBYE_PER_EA = 4.803204
# ASE's own conversion from a mass-weighted force constant (eV/A^2/amu) to an
# energy in eV.
EV_PER_SQRT_EV_A2_AMU = units._hbar * 1e10 / np.sqrt(units._e * units._amu)


# --- The physics, as pure array functions ---------------------------------

def impose_acoustic_sum_rule(hessian, natoms):
    """Force the translational sum rule Sum_j Phi_{i,a; j,b} = 0 on a Hessian.

    Translating the whole crystal rigidly costs no energy, so the three
    acoustic modes must sit at exactly zero frequency. A finite-difference
    Hessian does not know that: it is assembled from independently converged
    force calculations, and the residual error leaves the acoustic branch
    floating tens of cm^-1 above zero. Measured on MgO at a 4x4x4 grid, the
    uncorrected branch came out at 37, 43 and 51 cm^-1.

    That is not a cosmetic blemish. Everything downstream classifies a mode as
    acoustic by its frequency, and the Stokes intensity divides by omega -- so a
    spurious 51 cm^-1 "optical" mode acquires a large 1/omega weight and plants
    a peak in the Raman spectrum where the physics has none.

    A DFPT Hessian (VASP IBRION=8, Quantum ESPRESSO ph.x) has the same defect
    for the same reason -- the linear response is solved to a finite tolerance
    -- which is why this is applied whatever produced the matrix.

    The standard correction attributes the whole residual to the self-force
    term, which is the one element of each row that finite differences never
    measure directly (it is inferred from the others).
    """
    corrected = np.array(hessian, dtype=float, copy=True)
    for i in range(natoms):
        for a in range(3):
            row = 3 * i + a
            for b in range(3):
                others = sum(corrected[row, 3 * j + b]
                             for j in range(natoms) if j != i)
                corrected[row, 3 * i + b] = -others
    # The correction is applied row-wise and can break the exact symmetry the
    # eigensolver relies on; averaging restores it without undoing the rule.
    return 0.5 * (corrected + corrected.T)


def mass_weighted_modes(hessian, masses):
    """(frequencies in cm^-1, per-mode Cartesian displacements) of a Hessian.

    The eigenvectors of the MASS-WEIGHTED Hessian are what the IR and Raman
    contractions need, and the displacement per unit normal coordinate is that
    eigenvector divided by sqrt(M) again. Diagonalizing here rather than reading
    ASE's own get_mode() makes that convention explicit -- ASE has normalized it
    differently across releases, and the difference is a silent scale error in
    every intensity.
    """
    natoms = len(masses)
    inv_sqrt_m = 1.0 / np.sqrt(np.repeat(np.asarray(masses, dtype=float), 3))
    hessian = impose_acoustic_sum_rule(hessian, natoms)
    dynamical = np.asarray(hessian, dtype=float) \
        * inv_sqrt_m[:, None] * inv_sqrt_m[None, :]
    dynamical = 0.5 * (dynamical + dynamical.T)   # enforce exact symmetry
    omega2, eigenvectors = np.linalg.eigh(dynamical)

    energies_eV = EV_PER_SQRT_EV_A2_AMU * np.sqrt(omega2.astype(complex))
    # An imaginary frequency is a real physical statement (the geometry is not
    # a minimum), so it is reported as a NEGATIVE wavenumber rather than hidden.
    frequencies_cm = np.where(energies_eV.imag > 0,
                              -energies_eV.imag,
                              energies_eV.real) / units.invcm
    displacements = (eigenvectors.T.reshape(-1, natoms, 3)
                     * inv_sqrt_m.reshape(natoms, 3)[None, :, :])
    return np.asarray(frequencies_cm, dtype=float), displacements


def ir_intensities(born, displacements):
    """I_IR(nu) = sum_a |sum_{k,b} Z*_{k,ab} u_{k,b}(nu)|^2, in e^2/amu.

    A rigid translation of the crystal must come out at exactly zero, because
    translating it cannot polarize it -- that is the acoustic sum rule on Z*,
    and it is the sharpest available check on this contraction.
    """
    dipole_derivative = np.einsum('kab,mkb->ma', born, displacements)
    return np.sum(dipole_derivative ** 2, axis=1)


def raman_activities(dalpha, displacements):
    """Placzek activity 45 a'^2 + 7 g'^2 per mode, in A^4/amu.

    `dalpha` is d(alpha_ij)/du_{k,b}, indexed [atom, axis, i, j]; contracting it
    with the mode displacements gives each mode's Raman tensor, and the two
    rotational invariants of that tensor are what survive a powder average.
    """
    dalpha_dq = np.einsum('kbij,mkb->mij', dalpha, displacements)
    mean = np.trace(dalpha_dq, axis1=1, axis2=2) / 3.0
    anisotropy = np.zeros(len(dalpha_dq))
    for m, tensor in enumerate(dalpha_dq):
        diag = np.diag(tensor)
        anisotropy[m] = 0.5 * (
            (diag[0] - diag[1]) ** 2
            + (diag[1] - diag[2]) ** 2
            + (diag[2] - diag[0]) ** 2
        ) + 3.0 * (tensor[0, 1] ** 2 + tensor[1, 2] ** 2 + tensor[0, 2] ** 2)
    return 45.0 * mean ** 2 + 7.0 * anisotropy


def stokes_intensity(frequencies_cm, activity, laser_cm, kT_cm):
    """Measured Stokes intensity from the mode activity.

    The activity is a property of the mode; the intensity additionally carries
    the experiment -- the (omega_L - omega)^4 scattering prefactor, the
    1/omega zero-point factor and the Bose occupation at the sample
    temperature. Both are reported by this script, because comparing bare
    activities against a measured spectrum is a common way to conclude a peak
    is missing when it is only weak.
    """
    optical = frequencies_cm > 1.0     # skip acoustic / imaginary branches
    intensity = np.zeros_like(frequencies_cm)
    with np.errstate(over='ignore'):
        bose = 1.0 / (1.0 - np.exp(-frequencies_cm[optical] / max(kT_cm, 1e-9)))
    intensity[optical] = (
        (laser_cm - frequencies_cm[optical]) ** 4
        * activity[optical] * bose / (30.0 * frequencies_cm[optical])
    )
    return intensity


def lorentzian_spectrum(grid, centres, weights, width):
    """Sum of Lorentzians of half-width `width` at each mode."""
    spectrum = np.zeros_like(np.asarray(grid, dtype=float))
    for centre, weight in zip(centres, weights):
        if weight <= 0.0 or centre <= 0.0:
            continue
        spectrum += weight * width ** 2 / ((grid - centre) ** 2 + width ** 2)
    return spectrum


def verify_hessian_sign(hessian, masses, source):
    """Return `hessian` with the sign convention that the physics requires.

    Force-constant matrices are printed by the external codes in more than one
    convention -- VASP's OUTCAR block is dF/du = -Phi, Quantum ESPRESSO's
    dynamical-matrix file is +Phi -- and the two differ by exactly the sign
    that turns a stable crystal into an entirely imaginary phonon spectrum.

    Nothing downstream can detect that: every intensity is quadratic in the
    eigenvectors, so a globally flipped Hessian yields a full, plausible-looking
    spectrum drawn at negative wavenumbers. Rather than trusting a convention
    that belongs to someone else's release notes, the sign is checked against
    the one thing that cannot be argued with -- a structure the user chose to
    take spectra of has at most a few unstable modes, not a majority of them.
    """
    hessian = np.asarray(hessian, dtype=float)
    frequencies, _ = mass_weighted_modes(hessian, masses)
    imaginary = int(np.sum(frequencies < -1.0))
    if imaginary > len(frequencies) // 2:
        print(f'CALANGO_WARN {source} gave {imaginary} of '
              f'{len(frequencies)} modes as imaginary, which is the signature '
              'of the opposite force-constant sign convention; the matrix has '
              'been negated. If this structure really is that unstable, the '
              'spectrum is not meaningful either way.', flush=True)
        return -hessian
    return hessian
)PY";
}

/// GPAW: finite displacements throughout.
///
/// The Hessian comes from ase.vibrations (6N force evaluations) and the
/// polarizability derivative from the static dielectric tensor at the same 6N
/// displaced geometries. Z* is not computed here at all — it is inherited from
/// a Born Effective Charges run, because GPAW's route to it is a Berry-phase
/// finite difference that costs another 6N self-consistent runs, and making
/// that the price of a Raman spectrum would be charging for a quantity the
/// Raman spectrum does not use.
std::string gpawEngineBlock(const RamanIrConfig& cfg)
{
    std::ostringstream out;
    out << "# --- Inherited inputs ------------------------------------------\n"
           "BASELINE = "
        << pathLiteral(cfg.baselinePath) << "\n"
        << "BORN_CHARGES = " << pathLiteral(cfg.bornChargesPath) << "\n"
        << "OPTICS_REFERENCE = " << pathLiteral(cfg.opticsPath) << "\n\n";

    out << R"PY(from gpaw import GPAW

if BASELINE is None:
    raise RuntimeError(
        'CALANGO_ERROR this workflow restarts from a converged ground state; '
        'no baseline .gpw was configured.')

_baseline = GPAW(BASELINE, txt=None)
atoms = _baseline.get_atoms()
masses = atoms.get_masses()
symbols = atoms.get_chemical_symbols()
natoms = len(atoms)
volume = float(atoms.get_volume()) if atoms.pbc.any() else 0.0
born_source = BORN_CHARGES
REPORTED_DELTA = DELTA
METHOD = ('finite displacements (ase.vibrations) for the force constants; '
          'inherited Berry-phase Z*; finite differences of the static '
          'dielectric tensor for dalpha/du')

_calango_event('start', f'{natoms} atoms, {6 * natoms} displacements')


# --- 1. Gamma-point force constants ---------------------------------------
#
# ase.vibrations displaces every atom by +/- DELTA along each Cartesian axis
# and differences the forces. The calculator is rebuilt from the baseline with
# calc.new(), so every displaced run uses exactly the settings the ground state
# was validated with rather than a second, hand-copied set.
from ase.vibrations import Vibrations

vib_atoms = atoms.copy()
vib_atoms.calc = _baseline.new(symmetry='off', txt='gpaw_vib.txt')
vib = Vibrations(vib_atoms, delta=DELTA, name='vib')
vib.run()

# The Hessian is taken as a matrix rather than reading ASE's own frequencies,
# because the mode vectors have to be MASS-WEIGHTED and unit-normalized for the
# IR and Raman contractions below, and different ASE releases normalize
# get_mode() differently. Diagonalizing here makes the convention explicit.
try:
    hessian = np.asarray(vib.get_vibrations().get_hessian_2d(), dtype=float)
except AttributeError:
    # Pre-3.21 ASE keeps the (already mass-unweighted) matrix on the object.
    hessian = np.asarray(vib.H, dtype=float)


# --- 2. Born effective charges, from the inherited run --------------------
#
# Optional. Without Z* there is no route to an IR intensity in a periodic
# crystal, but there is still a complete phonon spectrum and -- with the Raman
# step below -- a complete Raman spectrum, which is reason enough to run this
# module. A missing Born charges run therefore drops the IR column instead of
# failing the job.
born_tensors = None
if BORN_CHARGES is not None:
    with open(BORN_CHARGES) as handle:
        born_data = json.load(handle)

    born_tensors = np.zeros((natoms, 3, 3))
    seen = set()
    for entry in born_data.get('atoms', []):
        index = int(entry['index'])
        if index >= natoms:
            continue
        born_tensors[index] = np.asarray(entry['tensor'], dtype=float)
        seen.add(index)
    missing = [i for i in range(natoms) if i not in seen]
    if missing:
        # A partial Z* set silently zeroes those atoms' contribution to every
        # mode, which shows up as a plausible-looking spectrum with the wrong
        # intensities -- far worse than refusing. Still fatal, because here the
        # user DID supply Z* and would otherwise get a quiet half-answer.
        raise RuntimeError(
            'CALANGO_ERROR the Born charges run covered only '
            f'{len(seen)} of {natoms} atoms (missing {missing[:8]}...). '
            'Re-run Born Effective Charges over ALL atoms: every atom '
            'contributes to every IR intensity.')


# --- 3. Raman: dalpha/du from the static dielectric response --------------
dalpha = None
raman_meta = {'route': 'finite difference of eps(omega -> 0)'}


def dielectric_tensor(gpw_path, eta, tag):
    """Static (omega -> 0) dielectric tensor, all nine components.

    GPAW's DielectricFunction evaluates epsilon along ONE direction at a time,
    which gives the three diagonal components directly. The off-diagonals come
    from three more evaluations along the (i + j)/sqrt(2) bisectors, since
    n.eps.n for that n is (eps_ii + eps_jj + 2 eps_ij)/2. Six evaluations for
    the full symmetric tensor: the alternative -- keeping only the diagonal --
    drops the 3(a_xy^2 + a_yz^2 + a_xz^2) term of the depolarization invariant
    and silently understates every mode whose Raman tensor is off-diagonal,
    which in a cubic crystal is most of them.
    """
    from gpaw.response.df import DielectricFunction

    # hilbert=False is REQUIRED alongside an explicit frequency list. GPAW's
    # default response path Hilbert-transforms on its own non-linear frequency
    # grid and asserts that descriptor's type, so asking for the single point
    # omega = 0 on the default path aborts with a bare AssertionError deep in
    # chi0_base. Turning the transform off evaluates the requested frequencies
    # directly, which is what a static-limit calculation wants anyway; checked
    # against the full grid on MgO, the two agree to 0.1 %.
    df = DielectricFunction(gpw_path, frequencies=[0.0], hilbert=False, eta=eta,
                            intraband=False, txt=f'gpaw_df_{tag}.txt')
    axes = {}
    for key, direction in (('x', 'x'), ('y', 'y'), ('z', 'z')):
        _, eps_lfc = df.get_dielectric_function(direction=direction)
        axes[key] = float(np.asarray(eps_lfc)[0].real)
    tensor = np.diag([axes['x'], axes['y'], axes['z']])
    inv_sqrt2 = 1.0 / np.sqrt(2.0)
    for (a, b, ka, kb) in ((0, 1, 'x', 'y'),
                           (1, 2, 'y', 'z'),
                           (0, 2, 'x', 'z')):
        direction = [0.0, 0.0, 0.0]
        direction[a] = inv_sqrt2
        direction[b] = inv_sqrt2
        _, eps_lfc = df.get_dielectric_function(direction=direction)
        mixed = float(np.asarray(eps_lfc)[0].real)
        off = mixed - 0.5 * (axes[ka] + axes[kb])
        tensor[a, b] = off
        tensor[b, a] = off
    return tensor


if COMPUTE_RAMAN:
    # Broadening inherited from the Optics run when one was chosen, so the
    # static limit is evaluated with the same eta the user already validated
    # the spectrum with.
    eta = 0.05
    if OPTICS_REFERENCE is not None:
        try:
            with open(OPTICS_REFERENCE) as handle:
                optics_meta = json.load(handle)
            eta = float(optics_meta.get('eta_eV', eta))
        except Exception as exc:
            _calango_event('warning',
                           f'could not read the optics reference: {exc!r}')

    # Empty-band count for the response step. Four times the occupied count is
    # the same rule the Optics workflow uses, with a floor for very small cells
    # where 4x occupied is still only a handful of bands.
    _occupied = max(1, int(round(_baseline.get_number_of_electrons() / 2.0)))
    RESPONSE_BANDS = max(4 * _occupied, 24)
    _calango_event('info',
                   f'response NSCF with {RESPONSE_BANDS} bands '
                   f'({_occupied} occupied)')

    # dchi/du for every atom and axis, by central differences of the static
    # dielectric tensor. chi = (eps - 1)/(4 pi).
    depsilon = np.zeros((natoms, 3, 3, 3))   # [atom, axis, alpha, beta]
    reference = atoms.get_positions().copy()
    total = 6 * natoms
    step = 0
    for atom_index in range(natoms):
        for axis in range(3):
            tensors = []
            for sign in (+1, -1):
                positions = reference.copy()
                positions[atom_index, axis] += sign * DELTA
                moved = atoms.copy()
                moved.set_positions(positions)
                tag = f'{atom_index}_{axis}_{"p" if sign > 0 else "m"}'
                calc = _baseline.new(symmetry='off', txt=f'gpaw_{tag}.txt')
                moved.calc = calc
                moved.get_potential_energy()
                gpw = f'raman_{tag}.gpw'
                calc.write(gpw, mode='all')
                nscf_gpw = f'raman_{tag}_nscf.gpw'
                try:
                    # A fixed-density NSCF step with EXTRA EMPTY BANDS, exactly
                    # as the Optics workflow does. The dielectric function is a
                    # sum over interband transitions into unoccupied states, and
                    # an SCF run carries only the handful of empty bands it
                    # needed to converge the density: on MgO, evaluating the
                    # response straight off the SCF restart gives eps_inf = 3.04
                    # against 3.16 converged, a 4 % error. That error is in a
                    # quantity being DIFFERENCED here, so it does not cancel —
                    # it lands directly in the Raman tensor.
                    nscf = GPAW(gpw).fixed_density(
                        nbands=RESPONSE_BANDS,
                        convergence={'bands': max(RESPONSE_BANDS // 2, 8)},
                        symmetry='off', txt=f'gpaw_nscf_{tag}.txt')
                    nscf.write(nscf_gpw, mode='all')
                    tensors.append(dielectric_tensor(nscf_gpw, eta, tag))
                finally:
                    # Two .gpw per displacement fills a disk quickly.
                    for path in (gpw, nscf_gpw):
                        if os.path.exists(path):
                            os.remove(path)
                step += 1
                _calango_progress(step, total)
            depsilon[atom_index, axis] = (tensors[0] - tensors[1]) / (2.0 * DELTA)

    # chi = (eps - 1) / 4pi, so dchi/du = deps/du / 4pi. The conventional
    # Raman tensor is the derivative of the cell polarizability alpha = V chi.
    dalpha = depsilon * (volume if volume > 0.0 else 1.0) / (4.0 * np.pi)
    raman_meta['eta_eV'] = eta
    raman_meta['off_diagonal'] = True
)PY";
    return out.str();
}

/// VASP: one DFPT run for the Hessian and Z*, finite displacements for the
/// polarizability derivative.
///
/// IBRION=8 with LEPSILON=.TRUE. returns the full force-constant matrix, every
/// ion's Z* and the clamped-ion dielectric tensor from a single linear-response
/// calculation — so the IR half of this module costs ONE run rather than the
/// 6N + 6N the GPAW route needs.
///
/// The Raman half has no such shortcut: VASP computes no Raman tensor, and the
/// established protocol is to differentiate the macroscopic dielectric tensor
/// by displacing the ions. That is 6N further LEPSILON runs, which is why the
/// Raman toggle changes this job's cost by orders of magnitude rather than the
/// factor of a few it changes for GPAW.
std::string vaspEngineBlock(const RamanIrConfig& cfg)
{
    std::ostringstream out;
    out << "import re\n"
           "from ase.io import read\n"
           "from ase.calculators.vasp import Vasp\n"
           "\n";
    if (!cfg.calculator.vaspPotcarPath.empty())
        out << "os.environ['VASP_PP_PATH'] = r\""
            << cfg.calculator.vaspPotcarPath << "\"\n";
    out << "atoms = read('structure.extxyz')\n"
           "masses = atoms.get_masses()\n"
           "symbols = atoms.get_chemical_symbols()\n"
           "natoms = len(atoms)\n"
           "volume = float(atoms.get_volume())\n"
           "born_source = 'VASP LEPSILON (DFPT), same run as the Hessian'\n"
           "REPORTED_DELTA = DELTA if COMPUTE_RAMAN else 0.0\n"
           "METHOD = ('VASP DFPT (IBRION=8, LEPSILON) for the force constants '\n"
           "          'and Z*; finite differences of eps_inf for dalpha/du')\n"
           "\n"
           "\n"
           "def _vasp_parameters(**extra):\n"
           "    \"\"\"One INCAR for every step of this job.\n"
           "\n"
           "    Defined once so the displaced dielectric runs cannot drift from\n"
           "    the DFPT run they are differenced against: a Raman tensor is a\n"
           "    DIFFERENCE of two eps_inf values, so a cutoff or k-mesh that\n"
           "    changed between them would not cancel, it would BE the answer.\n"
           "    \"\"\"\n"
           "    params = dict(\n"
           "        xc='"
        << cfg.calculator.vaspXc << "',\n"
           "        encut="
        << cfg.calculator.planeWaveCutoffEv << ",\n"
           "        kpts=("
        << cfg.calculator.kpts[0] << ", " << cfg.calculator.kpts[1] << ", "
        << cfg.calculator.kpts[2] << "),\n"
           "        ismear=0, sigma=0.05,\n"
           "        # Linear response is a DERIVATIVE of the ground state, so\n"
           "        # its noise is the SCF's noise amplified. EDIFF has to be\n"
           "        # far tighter than a total-energy run would need.\n"
           "        ediff=1e-8,\n"
           "        lreal=False,   # LREAL=Auto is not supported with LEPSILON\n"
           "        lwave=False, lcharg=False,\n"
           "    )\n"
           "    params.update(extra)\n"
           "    return params\n"
           "\n"
           "\n"
           "def _outcar(directory):\n"
           "    path = os.path.join(directory, 'OUTCAR')\n"
           "    if not os.path.exists(path):\n"
           "        raise RuntimeError(f'VASP produced no OUTCAR in {directory}')\n"
           "    return open(path, errors='ignore').read()\n"
           "\n"
           "\n"
           "def parse_vasp_hessian(text, natoms):\n"
           "    \"\"\"Force-constant matrix (3N, 3N) in eV/A^2 from an OUTCAR.\n"
           "\n"
           "    VASP prints it under 'SECOND DERIVATIVES (NOT SYMMETRIZED)' as\n"
           "    dF/du, i.e. MINUS the Hessian, in a table whose rows carry a\n"
           "    '<ion><axis>' label. The negation here states that convention;\n"
           "    verify_hessian_sign() then checks it against the physics rather\n"
           "    than leaving it to be right by assumption.\n"
           "    \"\"\"\n"
           "    marker = 'SECOND DERIVATIVES (NOT SYMMETRIZED)'\n"
           "    if marker not in text:\n"
           "        raise RuntimeError(\n"
           "            'No force constants in OUTCAR. The Hessian is written by\\n'\n"
           "            'the linear-response run (IBRION=8); if VASP stopped\\n'\n"
           "            'early the reason is at the end of that OUTCAR.')\n"
           "    rows = []\n"
           "    for line in text[text.index(marker):].splitlines()[1:]:\n"
           "        fields = line.split()\n"
           "        # A data row is a '3X'-style label followed by 3N numbers;\n"
           "        # the column header above it has 3N fields and no label,\n"
           "        # which is what tells the two apart.\n"
           "        if len(fields) != 3 * natoms + 1:\n"
           "            continue\n"
           "        if not re.fullmatch(r'\\d+[XYZxyz]', fields[0]):\n"
           "            continue\n"
           "        rows.append([float(v) for v in fields[1:]])\n"
           "        if len(rows) == 3 * natoms:\n"
           "            break\n"
           "    if len(rows) != 3 * natoms:\n"
           "        raise RuntimeError('Parsed %d of %d force-constant rows'\n"
           "                           % (len(rows), 3 * natoms))\n"
           "    return -np.asarray(rows, dtype=float)\n"
           "\n"
           "\n"
           "def parse_vasp_born(text, natoms):\n"
           "    \"\"\"Z* tensors (N, 3, 3) in e from an OUTCAR.\"\"\"\n"
           "    marker = 'BORN EFFECTIVE CHARGES'\n"
           "    if marker not in text:\n"
           "        raise RuntimeError(\n"
           "            'No Born charges in OUTCAR. LEPSILON runs need a\\n'\n"
           "            'SEMICONDUCTOR or insulator: for a metal the macroscopic\\n'\n"
           "            'polarization is not defined and VASP writes nothing.')\n"
           "    block = text[text.index(marker):]\n"
           "    tensors = []\n"
           "    for match in re.finditer(\n"
           "            r'ion\\s+\\d+\\s*\\n((?:\\s*[1-3](?:\\s+-?\\d+\\.\\d+){3}"
           "\\s*\\n){3})', block):\n"
           "        tensors.append([[float(v) for v in row.split()[1:4]]\n"
           "                        for row in match.group(1).strip().splitlines()])\n"
           "        if len(tensors) == natoms:\n"
           "            break\n"
           "    if len(tensors) != natoms:\n"
           "        raise RuntimeError('Parsed %d Born tensors for %d atoms'\n"
           "                           % (len(tensors), natoms))\n"
           "    return np.asarray(tensors, dtype=float)\n"
           "\n"
           "\n"
           "def parse_vasp_dielectric(text):\n"
           "    \"\"\"Clamped-ion eps_inf (3, 3) from an OUTCAR.\n"
           "\n"
           "    The FIRST 'including local field effects' block is the DFT one,\n"
           "    which is the electronic response the Raman tensor differentiates.\n"
           "    Matching the bare 'MACROSCOPIC STATIC DIELECTRIC TENSOR' instead\n"
           "    would pick up the 'excluding local field effects' block printed\n"
           "    before it, and on an ionic crystal the two differ by tens of\n"
           "    percent -- a difference that would land whole in dalpha/du.\n"
           "    \"\"\"\n"
           "    match = re.search(\n"
           "        r'MACROSCOPIC STATIC DIELECTRIC TENSOR \\(including local "
           "field effects[^\\n]*\\n\\s*-+\\s*\\n((?:[^\\n]*\\n){3})', text)\n"
           "    if not match:\n"
           "        raise RuntimeError(\n"
           "            'No macroscopic dielectric tensor in OUTCAR -- the\\n'\n"
           "            'LEPSILON step produced no response output.')\n"
           "    return np.asarray([[float(v) for v in row.split()]\n"
           "                       for row in match.group(1).strip().splitlines()],\n"
           "                      dtype=float)\n"
           "\n"
           "\n"
           "# --- 1. One DFPT run: force constants, Z* and eps_inf -------------\n"
           "_calango_event('start', f'{natoms} atoms, VASP linear response')\n"
           "_total = 1 + (6 * natoms if COMPUTE_RAMAN else 0)\n"
           "atoms.calc = Vasp(directory='dfpt', **_vasp_parameters(\n"
           "    ibrion=8,      # DFPT force constants\n"
           "    nsw=1,\n"
           "    nwrite=3,      # NWRITE=3 is what prints the second derivatives\n"
           "    lepsilon=True,  # Z* and eps_inf from the same response\n"
           "))\n"
           "atoms.get_potential_energy()\n"
           "_dfpt = _outcar('dfpt')\n"
           "hessian = verify_hessian_sign(\n"
           "    parse_vasp_hessian(_dfpt, natoms), masses, \"VASP's OUTCAR\")\n"
           "born_tensors = parse_vasp_born(_dfpt, natoms)\n"
           "_calango_progress(1, _total)\n"
           "\n"
           "\n"
           "# --- 2. Raman: dalpha/du by displacing the ions -------------------\n"
           "dalpha = None\n"
           "raman_meta = {'route': 'finite difference of the VASP LEPSILON "
           "eps_inf'}\n"
           "if COMPUTE_RAMAN:\n"
           "    depsilon = np.zeros((natoms, 3, 3, 3))   # [atom, axis, i, j]\n"
           "    reference = atoms.get_positions().copy()\n"
           "    step = 1\n"
           "    for atom_index in range(natoms):\n"
           "        for axis in range(3):\n"
           "            tensors = []\n"
           "            for sign in (+1, -1):\n"
           "                positions = reference.copy()\n"
           "                positions[atom_index, axis] += sign * DELTA\n"
           "                moved = atoms.copy()\n"
           "                moved.set_positions(positions)\n"
           "                tag = f'{atom_index}_{axis}_"
           "{\"p\" if sign > 0 else \"m\"}'\n"
           "                work = f'eps_{tag}'\n"
           "                moved.calc = Vasp(directory=work, **_vasp_parameters(\n"
           "                    ibrion=-1, nsw=0, lepsilon=True))\n"
           "                moved.get_potential_energy()\n"
           "                tensors.append(parse_vasp_dielectric(_outcar(work)))\n"
           "                step += 1\n"
           "                _calango_progress(step, _total)\n"
           "            depsilon[atom_index, axis] = \\\n"
           "                (tensors[0] - tensors[1]) / (2.0 * DELTA)\n"
           "    # chi = (eps - 1)/4pi, and the conventional Raman tensor is the\n"
           "    # derivative of the CELL polarizability alpha = V chi.\n"
           "    dalpha = depsilon * volume / (4.0 * np.pi)\n";
    return out.str();
}

/// Quantum ESPRESSO: everything from one ph.x run.
///
/// ph.x at q = 0 with `epsil = .true.` returns the force constants, Z* and the
/// clamped-ion dielectric tensor; adding `lraman = .true.` returns the Raman
/// tensor as well, computed as an analytic THIRD-order response rather than by
/// differencing a dielectric tensor. That makes QE the only engine here whose
/// full Raman + IR answer costs a single linear-response job, with no
/// displacement amplitude to trade off against SCF noise.
///
/// The price is a real restriction: the 2n+1 machinery behind `lraman` is
/// implemented for NORM-CONSERVING pseudopotentials only. ph.x stops rather
/// than approximating, and the block below turns that into a message naming
/// the cause instead of letting the job fail at the parse step.
std::string espressoEngineBlock(const RamanIrConfig& cfg)
{
    std::ostringstream out;
    out << "import re\n"
           "import subprocess\n"
           "from ase.io import read\n"
           "from ase.calculators.espresso import Espresso, EspressoProfile\n"
           "from ase.units import Bohr, Rydberg\n"
           "\n"
           "atoms = read('structure.extxyz')\n"
           "masses = atoms.get_masses()\n"
           "symbols = atoms.get_chemical_symbols()\n"
           "natoms = len(atoms)\n"
           "volume = float(atoms.get_volume())\n"
           "born_source = 'Quantum ESPRESSO ph.x (DFPT), same run as the "
           "Hessian'\n"
           "# DFPT is an analytic derivative: there is no displacement.\n"
           "REPORTED_DELTA = 0.0\n"
           "METHOD = ('Quantum ESPRESSO DFPT (ph.x, epsil'\n"
           "          + (' + lraman' if COMPUTE_RAMAN else '')\n"
           "          + ') for the force constants, Z*'\n"
           "          + (' and the analytic Raman tensor'\n"
           "             if COMPUTE_RAMAN else ''))\n"
           "\n"
           "_pseudo_dir = r\""
        << cfg.calculator.espressoPseudoDir << "\"\n"
           "_pw = os.environ.get('CALANGO_PW_X', 'pw.x')\n"
           "_ph = os.environ.get('CALANGO_PH_X', 'ph.x')\n"
           "profile = EspressoProfile(command=_pw, pseudo_dir=_pseudo_dir)\n"
           "# EDIT ME: one UPF per element. The guess is the conventional\n"
           "# '<Symbol>.UPF' naming; a real library rarely matches it.\n"
           "#\n"
           "# lraman below is implemented for NORM-CONSERVING pseudopotentials\n"
           "# only, so an ultrasoft or PAW set here is not merely a different\n"
           "# accuracy trade-off — it is the one choice that makes the Raman\n"
           "# half of this job impossible.\n"
           "pseudopotentials = {s: f'{s}.UPF' for s in sorted(set(symbols))}\n"
           "\n"
           "_calango_event('start', f'{natoms} atoms, QE linear response')\n"
           "_calango_progress(0, 3)\n"
           "\n"
           "atoms.calc = Espresso(\n"
           "    profile=profile,\n"
           "    pseudopotentials=pseudopotentials,\n"
           "    input_data={\n"
           "        'control': {'calculation': 'scf', 'prefix': 'calango',\n"
           "                    'outdir': './qe', 'tprnfor': True},\n"
           "        'system': {'ecutwfc': "
        << cfg.calculator.qeEcutwfcRy << ",\n";
    if (cfg.calculator.qeEcutrhoRy > 0.0)
        out << "                   'ecutrho': " << cfg.calculator.qeEcutrhoRy
            << ",\n";
    out << "                   # epsil (and hence Z*) is defined only for an\n"
           "                   # insulator, so the occupations are fixed.\n"
           "                   'occupations': 'fixed'},\n"
           "        # DFPT differentiates the ground state, so the SCF has to be\n"
           "        # converged far past what a total energy would need.\n"
           "        'electrons': {'conv_thr': 1e-12},\n"
           "    },\n"
           "    kpts=("
        << cfg.calculator.kpts[0] << ", " << cfg.calculator.kpts[1] << ", "
        << cfg.calculator.kpts[2] << "),\n"
           ")\n"
           "atoms.get_potential_energy()\n"
           "_calango_progress(1, 3)\n"
           "\n"
           "with open('ph.in', 'w') as handle:\n"
           "    handle.write('Gamma-point phonons, Z* and the Raman tensor\\n')\n"
           "    handle.write('&INPUTPH\\n')\n"
           "    handle.write(\"  prefix = 'calango'\\n\")\n"
           "    handle.write(\"  outdir = './qe'\\n\")\n"
           "    handle.write(\"  fildyn = 'calango.dyn'\\n\")\n"
           "    handle.write('  epsil = .true.\\n')\n"
           "    # Guarded at run time rather than at generation time, like the\n"
           "    # displaced sweeps on the other two engines: the reviewed\n"
           "    # script is editable, and flipping COMPUTE_RAMAN in it has to\n"
           "    # mean the same thing everywhere.\n"
           "    if COMPUTE_RAMAN:\n"
           "        handle.write('  lraman = .true.\\n')\n"
           "    handle.write('  tr2_ph = 1.0d-14\\n')\n"
           "    handle.write('/\\n')\n"
           "    handle.write('0.0 0.0 0.0\\n')\n"
           "with open('ph.in') as _in, open('ph.out', 'w') as _out:\n"
           "    _result = subprocess.run([_ph], stdin=_in, stdout=_out,\n"
           "                             stderr=subprocess.STDOUT)\n"
           "if _result.returncode != 0:\n"
           "    raise RuntimeError('ph.x failed (%s); see ph.out. Set "
           "CALANGO_PH_X\\n'\n"
           "                       'to its full path if it is not on PATH.'\n"
           "                       % _ph)\n"
           "_calango_progress(2, 3)\n"
           "\n"
           "if not os.path.exists('calango.dyn'):\n"
           "    raise RuntimeError(\n"
           "        'ph.x wrote no dynamical-matrix file (calango.dyn); see "
           "ph.out.')\n"
           "_dyn = open('calango.dyn', errors='ignore').read()\n"
           "_phout = open('ph.out', errors='ignore').read()\n"
           "\n"
           "\n"
           "def _blocks_of_three(text, header):\n"
           "    \"\"\"Every `header`-matched block, as a list of 3x3 arrays.\n"
           "\n"
           "    ph.x writes each tensor as three lines of three numbers under a\n"
           "    one-line header, in fixed or exponential notation depending on\n"
           "    the quantity and the release — so the values go through float()\n"
           "    on whitespace-split fields rather than through a number regex\n"
           "    that would have to know which.\n"
           "    \"\"\"\n"
           "    found = []\n"
           "    for match in re.finditer(header, text):\n"
           "        rows = []\n"
           "        for line in text[match.end():].splitlines():\n"
           "            fields = line.split()\n"
           "            if not fields:\n"
           "                continue\n"
           "            try:\n"
           "                values = [float(v) for v in fields]\n"
           "            except ValueError:\n"
           "                break\n"
           "            if len(values) != 3:\n"
           "                break\n"
           "            rows.append(values)\n"
           "            if len(rows) == 3:\n"
           "                break\n"
           "        if len(rows) == 3:\n"
           "            found.append(np.asarray(rows, dtype=float))\n"
           "    return found\n"
           "\n"
           "\n"
           "def parse_qe_force_constants(dyn_text, natoms):\n"
           "    \"\"\"Force constants (3N, 3N) in eV/A^2 from a .dyn file.\n"
           "\n"
           "    The file stores Phi in Ry/bohr^2, UNWEIGHTED by the masses (that\n"
           "    is why the header carries them) and in Cartesian axes. Each\n"
           "    atom pair is a two-integer line followed by three lines of three\n"
           "    complex numbers; at q = 0 the imaginary parts vanish and only\n"
           "    the real ones are read.\n"
           "    \"\"\"\n"
           "    marker = re.search(r'Dynamical\\s+Matrix in cartesian axes',\n"
           "                       dyn_text)\n"
           "    if not marker:\n"
           "        raise RuntimeError(\n"
           "            'calango.dyn holds no dynamical matrix in cartesian "
           "axes.')\n"
           "    body = dyn_text[marker.end():]\n"
           "    for stop in ('Dielectric Tensor', 'Effective Charges',\n"
           "                 'Diagonalizing'):\n"
           "        if stop in body:\n"
           "            body = body[:body.index(stop)]\n"
           "    phi = np.zeros((3 * natoms, 3 * natoms))\n"
           "    seen = set()\n"
           "    lines = body.splitlines()\n"
           "    for index, line in enumerate(lines):\n"
           "        fields = line.split()\n"
           "        # The pair header is exactly two integers on a line of its\n"
           "        # own; the q line and the data rows both fail that test.\n"
           "        if len(fields) != 2 or not all(f.isdigit() for f in fields):\n"
           "            continue\n"
           "        na, nb = int(fields[0]) - 1, int(fields[1]) - 1\n"
           "        if not (0 <= na < natoms and 0 <= nb < natoms):\n"
           "            continue\n"
           "        rows = []\n"
           "        for row_line in lines[index + 1:index + 4]:\n"
           "            values = [float(v) for v in row_line.split()]\n"
           "            if len(values) != 6:\n"
           "                rows = []\n"
           "                break\n"
           "            rows.append(values[0::2])   # real parts\n"
           "        if len(rows) != 3:\n"
           "            continue\n"
           "        phi[3 * na:3 * na + 3, 3 * nb:3 * nb + 3] = rows\n"
           "        seen.add((na, nb))\n"
           "    if len(seen) != natoms * natoms:\n"
           "        raise RuntimeError(\n"
           "            'Parsed %d of %d atom-pair blocks from calango.dyn'\n"
           "            % (len(seen), natoms * natoms))\n"
           "    # Ry/bohr^2 -> eV/A^2.\n"
           "    return phi * Rydberg / Bohr ** 2\n"
           "\n"
           "\n"
           "def parse_qe_born(dyn_text, natoms):\n"
           "    \"\"\"Z* tensors (N, 3, 3) in e, from the .dyn file.\"\"\"\n"
           "    tensors = _blocks_of_three(dyn_text, r'atom\\s+#\\s*\\d+\\s*\\n')\n"
           "    if len(tensors) < natoms:\n"
           "        raise RuntimeError(\n"
           "            'ph.x reported %d effective-charge tensors for %d "
           "atoms.\\n'\n"
           "            'epsil = .true. is legal only for an insulator; for a "
           "metal\\n'\n"
           "            'the macroscopic field is screened out and the quantity "
           "is\\n'\n"
           "            'not defined.' % (len(tensors), natoms))\n"
           "    return np.asarray(tensors[:natoms], dtype=float)\n"
           "\n"
           "\n"
           "def parse_qe_raman(text, natoms):\n"
           "    \"\"\"dalpha/du (N, 3, 3, 3) in A^2, or None when absent.\n"
           "\n"
           "    ph.x prints V/(4 pi) * d(eps_jk)/du_i already in A^2, which is\n"
           "    exactly the `dalpha` this module's contraction expects -- so\n"
           "    there is no volume factor to apply here, and applying one would\n"
           "    scale every Raman activity by the cell volume squared.\n"
           "\n"
           "    Each (atom, polarization) block is a 3x3 in (j, k). d(eps)/du is\n"
           "    symmetric in those two indices, so the block is symmetrized\n"
           "    rather than depending on which of them ph.x varies fastest.\n"
           "    \"\"\"\n"
           "    if 'Raman tensor' not in text:\n"
           "        return None\n"
           "    body = text[text.index('Raman tensor'):]\n"
           "    blocks = _blocks_of_three(\n"
           "        body, r'atom\\s+#\\s*\\d+\\s+pol\\.\\s*\\d+\\s*\\n')\n"
           "    if len(blocks) < 3 * natoms:\n"
           "        raise RuntimeError(\n"
           "            'ph.x printed %d of the %d expected Raman blocks.'\n"
           "            % (len(blocks), 3 * natoms))\n"
           "    tensor = np.zeros((natoms, 3, 3, 3))\n"
           "    for index in range(3 * natoms):\n"
           "        block = blocks[index]\n"
           "        tensor[index // 3, index % 3] = 0.5 * (block + block.T)\n"
           "    return tensor\n"
           "\n"
           "\n"
           "hessian = verify_hessian_sign(\n"
           "    parse_qe_force_constants(_dyn, natoms), masses,\n"
           "    \"ph.x's dynamical-matrix file\")\n"
           "born_tensors = parse_qe_born(\n"
           "    _dyn[_dyn.index('Effective Charges'):], natoms) \\\n"
           "    if 'Effective Charges' in _dyn else None\n"
           "if born_tensors is None:\n"
           "    raise RuntimeError(\n"
           "        'ph.x reported no effective charges even though epsil was\\n'\n"
           "        'requested. That happens for a metal, where the macroscopic\\n'\n"
           "        'field is screened out and Z* is not defined.')\n"
           "\n"
           "dalpha = None\n"
           "raman_meta = {'route': 'analytic third-order response (ph.x "
           "lraman)'}\n"
           "if COMPUTE_RAMAN:\n"
           "    # The dyn file is where write_ramtns puts it; ph.out is checked\n"
           "    # as well because some releases echo it to stdout instead.\n"
           "    dalpha = parse_qe_raman(_dyn, natoms)\n"
           "    if dalpha is None:\n"
           "        dalpha = parse_qe_raman(_phout, natoms)\n"
           "    if dalpha is None:\n"
           "        raise RuntimeError(\n"
           "            'ph.x ran but printed no Raman tensor.\\n\\n'\n"
           "            'lraman = .true. is implemented for NORM-CONSERVING\\n'\n"
           "            'pseudopotentials only — an ultrasoft or PAW set makes "
           "the\\n'\n"
           "            'third-order response unavailable, and ph.x declines "
           "rather\\n'\n"
           "            'than approximating it. Either switch this system to "
           "a\\n'\n"
           "            'norm-conserving library, or turn the Raman spectrum "
           "off\\n'\n"
           "            'and keep the (unaffected) infrared one.\\n\\n'\n"
           "            'See ph.out for what ph.x itself said.')\n"
           "_calango_progress(3, 3)\n";
    return out.str();
}

/// Everything downstream of the engine blocks: one diagonalization, two
/// contractions, the Stokes prefactor, the broadened curves and the JSON.
std::string sharedTailBlock()
{
    return R"PY(
# --- Normal modes ---------------------------------------------------------
frequencies_cm, displacements = mass_weighted_modes(hessian, masses)
_calango_event('info', f'{len(frequencies_cm)} Gamma-point modes')


# --- Infrared intensities from Z* -----------------------------------------
#
# I_IR(nu) = sum_a |sum_{k,b} Z*_{k,ab} u_{k,b}(nu)|^2 with u = e/sqrt(M) the
# Cartesian displacement per unit normal coordinate. The result is in
# e^2/amu; the (D/A)^2/amu duplicate is the unit the spectroscopy literature
# quotes, and 1 e*A = 4.803204 D gives the conversion.
ir_e2_amu = np.zeros(len(frequencies_cm))
ir_debye = np.zeros(len(frequencies_cm))
if born_tensors is None:
    # Without Z* there is no route to an IR intensity in a periodic crystal.
    # Every IR number is written as zero rather than as a plausible-looking
    # wrong value, and `ir.computed` records which happened.
    ir_meta = {'computed': False,
               'reason': 'no Born effective charges supplied'}
    print('CALANGO_WARN no Born effective charges supplied — the phonon '
          'frequencies and the Raman spectrum are computed as usual, but '
          'every IR intensity is reported as zero. Run Electronics -> "Born '
          'Effective Charges..." and re-run this to fill them in.', flush=True)
else:
    ir_e2_amu = ir_intensities(np.asarray(born_tensors, dtype=float),
                               displacements)
    ir_debye = ir_e2_amu * DEBYE_PER_EA ** 2
    ir_meta = {'computed': True, 'reason': '', 'source': born_source}


# --- Raman activities from dalpha/du --------------------------------------
raman_activity = np.zeros(len(frequencies_cm))
raman_meta['computed'] = dalpha is not None
if dalpha is not None:
    raman_activity = raman_activities(np.asarray(dalpha, dtype=float),
                                      displacements)
else:
    _calango_event('info',
                   'Raman disabled: only the IR spectrum is computed')


# --- Stokes intensities and the broadened spectra -------------------------
laser_cm = 1.0e7 / LASER_NM
kT_cm = units.kB * TEMPERATURE_K / units.invcm
raman_intensity = stokes_intensity(frequencies_cm, raman_activity,
                                   laser_cm, kT_cm)

grid = np.linspace(FREQ_MIN_CM, FREQ_MAX_CM, max(2, NPOINTS))
ir_spectrum = lorentzian_spectrum(grid, frequencies_cm, ir_debye,
                                  BROADENING_CM)
raman_spectrum = lorentzian_spectrum(grid, frequencies_cm, raman_intensity,
                                     BROADENING_CM)

modes = []
for index in range(len(frequencies_cm)):
    modes.append({
        'index': index,
        'frequency_cm': float(frequencies_cm[index]),
        'frequency_meV': float(frequencies_cm[index] * units.invcm * 1000.0),
        'ir_intensity_D2_A2_amu': float(ir_debye[index]),
        'ir_intensity_e2_amu': float(ir_e2_amu[index]),
        'raman_activity_A4_amu': float(raman_activity[index]),
        'raman_intensity': float(raman_intensity[index]),
        'acoustic': bool(abs(frequencies_cm[index]) <= 1.0),
    })

summary = {
    'formula': atoms.get_chemical_formula(),
    'engine': ENGINE,
    'method': METHOD,
    'atoms': natoms,
    'symbols': symbols,
    'displacement_A': REPORTED_DELTA,
    'laser_nm': LASER_NM,
    'temperature_K': TEMPERATURE_K,
    'broadening_cm': BROADENING_CM,
    'volume_A3': volume,
    'born_charges_source': born_source,
    'raman': raman_meta,
    'ir': ir_meta,
    'modes': modes,
    'spectrum': {
        'frequency_cm': [float(v) for v in grid],
        'ir': [float(v) for v in ir_spectrum],
        'raman': [float(v) for v in raman_spectrum],
    },
}
with open('raman_ir.json', 'w') as handle:
    json.dump(summary, handle, indent=2)

# max() over an all-zero IR column picks an arbitrary mode and reports it as
# "the strongest", which reads as a result. With no Born charges there is no
# strongest IR mode to name, so the field says so.
_strongest = (max(modes, key=lambda m: m['ir_intensity_D2_A2_amu'])
              if modes and ir_meta['computed'] else None)
_strongest_cm = f'{_strongest["frequency_cm"]:.1f}' if _strongest else 'n/a'
print(f'CALANGO_RESULT raman_ir=raman_ir.json '
      f'modes={len(modes)} '
      f'raman={"yes" if raman_meta["computed"] else "no"} '
      f'ir={"yes" if ir_meta["computed"] else "no"} '
      f'strongest_ir_cm={_strongest_cm}',
      flush=True)
)PY";
}

} // namespace

std::string generateRamanIrScript(const RamanIrConfig& config)
{
    RamanIrConfig cfg = config;
    // The response evaluation integrates over the FULL Brillouin zone, and a
    // displaced geometry has lower symmetry than the equilibrium one anyway —
    // so a symmetry-reduced k-set built for the undisplaced cell would be wrong
    // for every displacement. Same reasoning as the Born-charges generator.
    cfg.calculator.gpawSymmetryOff = true;

    const auto engine = cfg.calculator.calculator;
    const bool gpaw = engine == CalculatorKind::Gpaw;
    const bool vasp = engine == CalculatorKind::Vasp;
    const bool espresso = engine == CalculatorKind::QuantumEspresso;

    std::ostringstream out;
    out << "# Raman and infrared spectra — generated by Calango\n"
           "#\n"
           "# Both spectra describe the SAME Gamma-point phonons and differ\n"
           "# only in which electronic response couples to them:\n"
           "#\n"
           "#   IR    I(nu) ~ sum_a |sum_{k,b} Z*_{k,ab} e_{k,b}(nu)/sqrt(M_k)|^2\n"
           "#         the change in macroscopic POLARIZATION. In a periodic\n"
           "#         crystal there is no molecular dipole to differentiate, so\n"
           "#         the Born effective charges Z* are the only route to it.\n"
           "#\n"
           "#   Raman S(nu) = 45 a'^2 + 7 g'^2 built from dchi/dQ, the change in\n"
           "#         POLARIZABILITY -- the same electronic response the Optics\n"
           "#         process evaluates, taken in the static limit.\n"
           "#\n";
    if (gpaw)
        out << "# GPAW route: FINITE DISPLACEMENTS throughout. The force\n"
               "# constants come from ase.vibrations (6N force evaluations) and\n"
               "# dalpha/du from the static dielectric tensor at those same 6N\n"
               "# geometries. Z* is inherited from a Born Effective Charges run\n"
               "# rather than recomputed: GPAW's route to it is another 6N\n"
               "# self-consistent Berry-phase runs, and the Raman spectrum does\n"
               "# not use it.\n";
    else if (vasp)
        out << "# VASP route: ONE DFPT run (IBRION=8 with LEPSILON) returns the\n"
               "# force constants, every ion's Z* and the clamped-ion dielectric\n"
               "# tensor together. The Raman half has no such shortcut -- VASP\n"
               "# computes no Raman tensor -- so dalpha/du is obtained by\n"
               "# differencing eps_inf over 6N displaced LEPSILON runs. The\n"
               "# Raman toggle therefore changes this job's cost by orders of\n"
               "# magnitude, not by the factor of a few it changes for GPAW.\n";
    else if (espresso)
        out << "# Quantum ESPRESSO route: ONE ph.x run returns everything. At\n"
               "# q = 0, `epsil` gives the force constants, Z* and eps_inf, and\n"
               "# `lraman` adds the Raman tensor as an analytic THIRD-order\n"
               "# response -- no displacement amplitude to trade off against SCF\n"
               "# noise, and no linearity assumption left to check.\n"
               "#\n"
               "# `lraman` is implemented for NORM-CONSERVING pseudopotentials\n"
               "# only. That restriction is real and is reported as such rather\n"
               "# than worked around.\n";
    out << "import json\n"
           "import os\n"
           "\n";
    if (gpaw)
        out << "os.environ.setdefault('GPAW_NEW', '1')\n";
    out << "import numpy as np\n"
           "from ase import units\n"
           "\n"
        << AseScriptGenerator::jsonLoggerPreamble();

    if (!gpaw && !vasp && !espresso) {
        // Refusing up front beats running the whole vibrational job and then
        // failing at the response step.
        out << "raise RuntimeError(\n"
               "    'Raman and IR spectra need a backend that computes both the "
               "force\\n'\n"
               "    'constants and the electronic response: GPAW (finite "
               "displacements),\\n'\n"
               "    'VASP (IBRION=8 + LEPSILON) or Quantum ESPRESSO (ph.x). The "
               "selected\\n'\n"
               "    'engine was \""
            << toString(cfg.calculator.calculator)
            << "\". Re-open the wizard and choose one of\\n'\n"
               "    'those three.')\n";
        return out.str();
    }

    // The settings that describe the SPECTRUM rather than the calculation are
    // shared by every engine, so they are emitted once, above the engine block.
    out << "# --- Settings ---------------------------------------------------\n"
           "ENGINE = \""
        << toString(cfg.calculator.calculator) << "\"\n"
        << "DELTA = " << cfg.displacement << "          # Angstrom\n"
        << "COMPUTE_RAMAN = " << (cfg.computeRaman ? "True" : "False") << "\n"
        << "LASER_NM = " << cfg.laserWavelengthNm << "\n"
        << "TEMPERATURE_K = " << cfg.temperatureK << "\n"
        << "BROADENING_CM = " << cfg.broadeningCm << "\n"
        << "FREQ_MIN_CM = " << cfg.frequencyMinCm << "\n"
        << "FREQ_MAX_CM = " << cfg.frequencyMaxCm << "\n"
        << "NPOINTS = " << cfg.npoints << "\n"
        << sharedMathBlock() << "\n"
        << "\n# --- " << toString(cfg.calculator.calculator)
        << " ---------------------------------------------\n";

    if (gpaw)
        out << gpawEngineBlock(cfg);
    else if (vasp)
        out << vaspEngineBlock(cfg);
    else
        out << espressoEngineBlock(cfg);

    out << sharedTailBlock();
    return out.str();
}

} // namespace calango::core
