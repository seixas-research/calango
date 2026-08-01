#include "core/NonlinearOpticsScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace calango::core {

const char* toString(NlOpticsGauge gauge)
{
    return gauge == NlOpticsGauge::Velocity ? "vg" : "lg";
}

namespace {

/// The requested components, reduced to the ones GPAW can be handed.
///
/// A third-rank component is exactly three letters from xyz — `get_shg` turns
/// each into an axis index with `'xyz'.index(c)` and raises a bare ValueError
/// on anything else, halfway through a job that has already paid for the
/// ground state and the matrix elements. Filtering here means a typo costs
/// nothing.
std::vector<std::string> validComponents(const NonlinearOpticsConfig& cfg)
{
    std::vector<std::string> kept;
    for (std::string component : cfg.components) {
        // Case is a spelling detail, not a choice.
        std::transform(component.begin(), component.end(), component.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        if (component.size() != 3)
            continue;
        if (component.find_first_not_of("xyz") != std::string::npos)
            continue;
        if (std::find(kept.begin(), kept.end(), component) == kept.end())
            kept.push_back(component);
    }
    if (kept.empty())
        kept.push_back("yyy");
    return kept;
}

/// Python list literal of those components.
std::string componentsLiteral(const std::vector<std::string>& components)
{
    std::string out = "[";
    for (std::size_t i = 0; i < components.size(); ++i)
        out += (i ? ", '" : "'") + components[i] + "'";
    return out + "]";
}

/// The post-processing, as pure functions of the arrays GPAW hands back.
///
/// Separated from the pipeline so `tests/nlopt_parser_test.py` can extract
/// them by AST and drive them with arrays of exactly the shape `get_shg`,
/// `get_shift` and `get_chi_tensor` return, without GPAW anywhere. The unit
/// conversions are the part worth pinning: everything GPAW returns is in SI
/// base units (m/V for χ⁽²⁾, A/V² for the shift current), the literature
/// quotes pm/V and nm²/V, and a factor of 1e6 in either direction produces a
/// plot that looks entirely reasonable.
std::string observablesBlock()
{
    return R"PY(
def sheet_thickness_m(cell_lengths_A, vacuum_axis):
    """Vacuum-direction cell length in metres, or None for a bulk cell.

    A supercell chi^(2) is diluted by whatever vacuum was used: double the
    vacuum and the number halves, so it is not a property of the sheet.
    Multiplying it back by the cell length gives the SHEET susceptibility,
    which is.
    """
    if vacuum_axis is None or vacuum_axis < 0:
        return None
    return float(cell_lengths_A[vacuum_axis]) * 1e-10


def shg_observables(freqs_eV, chi_l, thickness_m=None):
    """chi^(2) spectra from one get_shg() result.

    `chi_l` is the second row of what get_shg returns (and of the .npy it
    saves): complex chi^(2) in SI units of m/V, one entry per requested photon
    energy. 1 m/V = 1e12 pm/V, which is the unit bulk chi^(2) is quoted in.

    For a sheet the reportable quantity is chi^(2) * L, in m^2/V; 1 m^2 = 1e18
    nm^2, giving the nm^2/V of the GPAW tutorial and of the 2D literature.

    The frequency row arrives COMPLEX: get_shg stacks the real grid on top of
    the complex susceptibility, so numpy promotes the whole array. Taking .real
    before the float cast is what the tutorial does too -- casting straight to
    float raises a ComplexWarning today and is slated to become an error.
    """
    freqs_eV = np.asarray(freqs_eV).real.astype(float)
    chi = np.asarray(chi_l, dtype=complex)
    out = {
        'energy_eV': [float(v) for v in freqs_eV],
        'chi2_re_pm_V': [float(v) for v in chi.real * 1e12],
        'chi2_im_pm_V': [float(v) for v in chi.imag * 1e12],
        'chi2_abs_pm_V': [float(v) for v in np.abs(chi) * 1e12],
    }
    if thickness_m:
        sheet = chi * thickness_m
        out['chi2_sheet_re_nm2_V'] = [float(v) for v in sheet.real * 1e18]
        out['chi2_sheet_im_nm2_V'] = [float(v) for v in sheet.imag * 1e18]
        out['chi2_sheet_abs_nm2_V'] = [float(v) for v in np.abs(sheet) * 1e18]
    return out


def shift_observables(freqs_eV, sigma_l, thickness_m=None):
    """Shift-current spectra from one get_shift() result.

    get_shift returns a REAL conductivity in A/V^2 (it takes the real part of
    the band sum itself), so unlike chi^(2) there is no phase here. The sheet
    quantity is sigma * L in A*m/V^2, reported in A*nm/V^2.

    Both rows go through .real before the float cast for the same reason as in
    shg_observables: get_shift stacks them into one array, and whether that
    array is complex depends on what was stacked.
    """
    freqs_eV = np.asarray(freqs_eV).real.astype(float)
    sigma = np.asarray(sigma_l).real.astype(float)
    out = {
        'energy_eV': [float(v) for v in freqs_eV],
        'sigma_A_V2': [float(v) for v in sigma],
    }
    if thickness_m:
        out['sigma_sheet_A_nm_V2'] = [float(v)
                                      for v in sigma * thickness_m * 1e9]
    return out


def linear_observables(freqs_eV, chi_vvl):
    """chi^(1) components, and the dielectric function they imply.

    get_chi_tensor returns a (3, 3, nw) complex array carrying GPAW's own
    4*pi/eps_0 prefactor, in the convention where eps = 1 + chi. The diagonal
    eps is derived here rather than left to the reader, because that is the
    curve the nonlinear spectra have to be read against: the chi^(2) features
    of a semiconductor sit at the absorption edge and at half of it, and a
    resonance assigned to the wrong one of those is the standard way to
    misread an SHG spectrum.
    """
    freqs_eV = np.asarray(freqs_eV).real.astype(float)
    chi = np.asarray(chi_vvl, dtype=complex)
    axes = 'xyz'
    out = {'energy_eV': [float(v) for v in freqs_eV]}
    for i in range(3):
        for j in range(3):
            key = axes[i] + axes[j]
            out['chi1_' + key + '_re'] = [float(v) for v in chi[i, j].real]
            out['chi1_' + key + '_im'] = [float(v) for v in chi[i, j].imag]
        out['eps_' + axes[i] + axes[i] + '_1'] = [
            float(v) for v in 1.0 + chi[i, i].real]
        out['eps_' + axes[i] + axes[i] + '_2'] = [
            float(v) for v in chi[i, i].imag]
    return out


def has_inversion_symmetry(cell, scaled, numbers, tol=1e-3):
    """Does the cell map onto itself under r -> 2c - r for some centre c?

    chi^(2) and the shift current are odd-rank tensors: in a centrosymmetric
    crystal every component vanishes IDENTICALLY, by symmetry rather than by
    smallness. GPAW will still return a spectrum -- the residue of an
    incomplete cancellation over a finite k-mesh -- and it looks like a
    result. That is the single most common way this module is misused, so it
    is checked before the expensive part rather than left to be noticed.

    An inversion centre must lie halfway between some atom and the image of
    atom 0, so only those candidates are tried; `tol` is in Angstrom.
    """
    scaled = np.asarray(scaled, dtype=float) % 1.0
    numbers = np.asarray(numbers)
    cell = np.asarray(cell, dtype=float)
    if len(scaled) == 0:
        return False
    for j in np.where(numbers == numbers[0])[0]:
        centre = 0.5 * (scaled[0] + scaled[j])
        image = (2.0 * centre - scaled) % 1.0
        matched = True
        for i in range(len(scaled)):
            delta = (image[i] - scaled) % 1.0
            # Nearest periodic image, then into Cartesian so the tolerance is
            # a distance rather than a fraction of whichever cell this is.
            delta = np.where(delta > 0.5, delta - 1.0, delta)
            distance = np.linalg.norm(delta @ cell, axis=1)
            if not np.any((distance < tol) & (numbers == numbers[i])):
                matched = False
                break
        if matched:
            return True
    return False


def spectrum_peak(energies, values):
    """(energy, value) of the largest |value|, for the one-line report."""
    values = np.abs(np.asarray(values, dtype=float))
    if values.size == 0:
        return (0.0, 0.0)
    index = int(np.argmax(values))
    return (float(np.asarray(energies).real.astype(float)[index]),
            float(values[index]))
)PY";
}

} // namespace

std::string generateNonlinearOpticsScript(const NonlinearOpticsConfig& config)
{
    NonlinearOpticsConfig cfg = config;

    // Three ground-state settings are requirements of the method, not
    // preferences, so they are imposed here rather than left to the wizard:
    //
    //  * point-group symmetry OFF. make_nlodata asserts it, and an assert
    //    fired after the SCF has run is the most expensive possible way to
    //    learn about a checkbox. Time reversal is deliberately KEPT — the
    //    weaker `symmetry="off"` would double the k-points for nothing.
    //  * nbands="nao". The sums run over intermediate states; GPAW's default
    //    band count covers occupation and little else, and a truncated sum is
    //    not a slightly-worse spectrum but a differently-shaped one.
    //  * convergence bands. The SCF converges occupied states; the empty
    //    manifold it leaves behind is unconverged noise, and here it is
    //    summed over.
    cfg.calculator.gpawSymmetryOff = false;
    cfg.calculator.gpawPointGroupOff = true;
    if (cfg.calculator.gpawNbands.empty())
        cfg.calculator.gpawNbands = "nao";
    if (cfg.calculator.gpawConvergeBands == 0)
        cfg.calculator.gpawConvergeBands = -10;

    const std::vector<std::string> components = validComponents(cfg);
    const int npoints = cfg.npoints > 1 ? cfg.npoints : 2;

    std::ostringstream out;
    out << "# Nonlinear optical response — generated by Calango\n"
           "#\n"
           "# Evaluated with GPAW's nonlinear-optics module (gpaw.nlopt),\n"
           "# following the structure of the official tutorial: converge a\n"
           "# ground state, build the momentum matrix elements once, then sum\n"
           "# over bands for each requested response.\n"
           "#\n"
           "#   SHG            chi^(2)(-2w; w, w), the second-harmonic\n"
           "#                  susceptibility, in m/V (reported as pm/V).\n"
           "#   Shift current  sigma^(2)(0; w, -w), the ballistic photocurrent\n"
           "#                  response of the bulk photovoltaic effect, in\n"
           "#                  A/V^2.\n"
           "#   chi^(1)        the full linear susceptibility tensor, for the\n"
           "#                  absorption edge the nonlinear features have to\n"
           "#                  be read against.\n"
           "#\n"
           "# The expensive step is make_nlodata (the matrix elements), and it\n"
           "# is done ONCE: every component and every response below reuses the\n"
           "# same nlodata object. Asking for a second tensor component costs a\n"
           "# band sum, not another ground state.\n"
           "#\n"
           "# This module does NOT inherit a Single-Point baseline, unlike the\n"
           "# linear Optics one. make_nlodata asserts that point-group symmetry\n"
           "# is off, and the band sums need a converged empty manifold — an\n"
           "# ordinary baseline satisfies neither, and the first fails as a\n"
           "# bare AssertionError after the run has already been paid for.\n"
           "import json\n"
           "import os\n"
           "\n"
           "os.environ.setdefault('GPAW_NEW', '1')\n"
           "import numpy as np\n"
           "from ase.io import read\n"
           "\n"
        << AseScriptGenerator::jsonLoggerPreamble();

    if (cfg.calculator.calculator != CalculatorKind::Gpaw) {
        out << "raise RuntimeError(\n"
               "    'The nonlinear-optics workflow is built on GPAW's "
               "gpaw.nlopt\\n'\n"
               "    'module, which has no counterpart in the other engines. "
               "The\\n'\n"
               "    'selected engine was \""
            << toString(cfg.calculator.calculator)
            << "\". Re-open the wizard and choose GPAW.')\n";
        return out.str();
    }

    out << "# --- Settings ---------------------------------------------------\n"
        << "ETA = " << cfg.broadeningEv << "          # Lorentzian broadening, eV\n"
        << "OMEGA_MIN = " << cfg.omegaMinEv << "\n"
        << "OMEGA_MAX = " << cfg.omegaMaxEv << "\n"
        << "NPOINTS = " << npoints << "\n"
        << "GAUGE = '" << toString(cfg.gauge) << "'\n"
        << "ESHIFT = " << cfg.scissorsEv << "        # scissors shift, eV\n"
        << "COMPONENTS = " << componentsLiteral(components) << "\n"
        << "BAND_FIRST = " << cfg.bandsFirst << "\n"
        << "BAND_LAST = " << cfg.bandsLast << "      # 0 = up to the last band\n"
        << "VACUUM_AXIS = " << cfg.vacuumAxis << "     # -1 = bulk\n"
        << "COMPUTE_SHG = " << (cfg.computeShg ? "True" : "False") << "\n"
        << "COMPUTE_SHIFT = " << (cfg.computeShift ? "True" : "False") << "\n"
        << "COMPUTE_LINEAR = " << (cfg.computeLinear ? "True" : "False") << "\n"
        << observablesBlock() << "\n";

    out << "\n"
           "atoms = read('structure.extxyz')\n"
           "_calango_progress(0, 4)\n"
           "\n"
           "# --- Is chi^(2) allowed at all? ----------------------------------\n"
           "# Checked before the ground state rather than after, because the\n"
           "# answer decides whether the rest of the job means anything.\n"
           "_centro = bool(has_inversion_symmetry(np.asarray(atoms.get_cell()),\n"
           "                                      atoms.get_scaled_positions(),\n"
           "                                      atoms.get_atomic_numbers()))\n"
           "if _centro and (COMPUTE_SHG or COMPUTE_SHIFT):\n"
           "    print('CALANGO_WARN this cell has an inversion centre. Both '\n"
           "          'the second-harmonic susceptibility and the shift '\n"
           "          'current are odd-rank tensors, so every component '\n"
           "          'vanishes IDENTICALLY by symmetry -- whatever comes back '\n"
           "          'is the residue of an incomplete cancellation over a '\n"
           "          'finite k-mesh, not a spectrum. Break the symmetry '\n"
           "          '(a surface, a strain, a different polymorph) or compute '\n"
           "          'the linear response instead.', flush=True)\n"
           "\n"
           "# --- 1. Ground state ---------------------------------------------\n"
        << AseScriptGenerator::gpawImports(cfg.calculator)
        << "\n"
           "calc = GPAW(\n"
        << AseScriptGenerator::gpawKeywordArguments(cfg.calculator, "    ")
        << "    # make_nlodata gathers the wavefunctions to one rank, so the\n"
           "    # real-space domain must not be split across them.\n"
           "    parallel={'domain': 1},\n"
           "    txt='gpaw_gs.txt',\n"
           ")\n"
           "atoms.calc = calc\n"
           "_energy = float(atoms.get_potential_energy())\n"
           "print(f'CALANGO_INFO scf_energy_eV={_energy:.6f}', flush=True)\n"
           "calc.write('gs.gpw', mode='all')\n"
           "_calango_progress(1, 4)\n"
           "\n"
           "# --- 2. Momentum matrix elements ---------------------------------\n"
           "# The one expensive step that is NOT per-component: every spectrum\n"
           "# below is a sum over these. Saved to mml.npz as an artifact of the\n"
           "# run, so a later job can reload it with NLOData.load() instead of\n"
           "# rebuilding it.\n"
           "#\n"
           "# Handed the .gpw PATH rather than the live calculator: given a\n"
           "# path, make_nlodata reopens it with the parallelization its own\n"
           "# gather step needs, which is what the tutorial does.\n"
           "from gpaw.nlopt.matrixel import make_nlodata\n"
           "\n"
           "_band_kwargs = {}\n"
           "if BAND_FIRST:\n"
           "    _band_kwargs['ni'] = BAND_FIRST\n"
           "if BAND_LAST:\n"
           "    _band_kwargs['nf'] = BAND_LAST\n"
           "try:\n"
           "    nlodata = make_nlodata('gs.gpw', **_band_kwargs)\n"
           "except AssertionError as exc:\n"
           "    # The one assert make_nlodata makes is about the point group,\n"
           "    # and it arrives as a bare AssertionError with no message.\n"
           "    raise RuntimeError(\n"
           "        'gpaw.nlopt rejected the ground state: '\n"
           "        f'{exc if str(exc) else \"point-group symmetry is on\"}.\\n'\n"
           "        'The nonlinear response needs the unfolded point group; '\n"
           "        'this script asks for it with\\n'\n"
           "        \"symmetry={'point_group': False, 'time_reversal': True}, \"\n"
           "        'so a failure here means that\\n'\n"
           "        'line was edited out of the calculator above.') from exc\n"
           "nlodata.write('mml.npz')\n"
           "_calango_progress(2, 4)\n"
           "\n"
           "# The photon-energy grid every response shares.\n"
           "freqs = np.linspace(OMEGA_MIN, OMEGA_MAX, NPOINTS)\n"
           "print(f'CALANGO_INFO photon-energy grid {freqs[0]:.3f}..'\n"
           "      f'{freqs[-1]:.3f} eV in {len(freqs)} points', flush=True)\n"
           "\n"
           "_thickness = sheet_thickness_m(atoms.cell.lengths(), VACUUM_AXIS)\n"
           "results = {\n"
           "    'engine': 'GPAW',\n"
           "    'module': 'gpaw.nlopt',\n"
           "    'energy_eV': [float(v) for v in freqs],\n"
           "    'eta_eV': ETA,\n"
           "    'gauge': GAUGE,\n"
           "    'eshift_eV': ESHIFT,\n"
           "    'components': list(COMPONENTS),\n"
           "    'centrosymmetric': _centro,\n"
           "    'vacuum_axis': VACUUM_AXIS,\n"
           "    'L_z_A': (float(atoms.cell.lengths()[VACUUM_AXIS])\n"
           "              if VACUUM_AXIS >= 0 else 0.0),\n"
           "    'formula': atoms.get_chemical_formula(),\n"
           "    'shg': {},\n"
           "    'shift': {},\n"
           "}\n"
           "\n"
           "# --- 3. The response spectra --------------------------------------\n"
           "# Every call below reuses the SAME nlodata. Its distribute() builds\n"
           "# a fresh per-rank view and leaves the object alone, which is what\n"
           "# makes one set of matrix elements serve every component.\n";

    if (cfg.computeShg)
        out << "if COMPUTE_SHG:\n"
               "    from gpaw.nlopt.shg import get_shg\n"
               "    for _pol in COMPONENTS:\n"
               "        _out = f'shg_{_pol}_{GAUGE}.npy'\n"
               "        _shg = get_shg(nlodata, freqs=freqs, eta=ETA, pol=_pol,\n"
               "                       eshift=ESHIFT, gauge=GAUGE,\n"
               "                       out_name=_out)\n"
               "        # Row 0 is the frequency grid, row 1 the complex\n"
               "        # susceptibility -- the layout get_shg both returns and\n"
               "        # saves, so the .npy on disk and this are the same data.\n"
               "        results['shg'][_pol] = shg_observables(\n"
               "            _shg[0], _shg[1], _thickness)\n"
               "        _at, _peak = spectrum_peak(\n"
               "            _shg[0].real, results['shg'][_pol]['chi2_abs_pm_V'])\n"
               "        print(f'CALANGO_RESULT shg_{_pol} '\n"
               "              f'peak_abs_pm_V={_peak:.4g} at {_at:.3f} eV',\n"
               "              flush=True)\n"
               "\n";
    if (cfg.computeShift)
        out << "if COMPUTE_SHIFT:\n"
               "    from gpaw.nlopt.shift import get_shift\n"
               "    for _pol in COMPONENTS:\n"
               "        _out = f'shift_{_pol}.npy'\n"
               "        _shift = get_shift(nlodata, freqs=freqs, eta=ETA,\n"
               "                           pol=_pol, eshift=ESHIFT,\n"
               "                           out_name=_out)\n"
               "        results['shift'][_pol] = shift_observables(\n"
               "            _shift[0], _shift[1], _thickness)\n"
               "        _at, _peak = spectrum_peak(\n"
               "            _shift[0].real, results['shift'][_pol]['sigma_A_V2'])\n"
               "        print(f'CALANGO_RESULT shift_{_pol} '\n"
               "              f'peak_A_V2={_peak:.4g} at {_at:.3f} eV',\n"
               "              flush=True)\n"
               "\n";
    if (cfg.computeLinear)
        out << "if COMPUTE_LINEAR:\n"
               "    from gpaw.nlopt.linear import get_chi_tensor\n"
               "    _chi = get_chi_tensor(nlodata, freqs=freqs, eta=ETA,\n"
               "                          eshift=ESHIFT,\n"
               "                          out_name='chi_linear.npy')\n"
               "    results['linear'] = linear_observables(freqs, _chi)\n"
               "\n";

    out << "_calango_progress(3, 4)\n"
           "\n"
           "if not results['shg'] and not results['shift'] \\\n"
           "        and 'linear' not in results:\n"
           "    raise RuntimeError(\n"
           "        'No response was requested, so nothing was computed. '\n"
           "        'Select at least one of SHG, shift current or the linear '\n"
           "        'susceptibility tensor.')\n"
           "\n"
           "with open('nlopt.json', 'w') as handle:\n"
           "    json.dump(results, handle)\n"
           "_calango_progress(4, 4)\n"
           "print(f'CALANGO_RESULT nlopt=nlopt.json '\n"
           "      f'shg={len(results[\"shg\"])} '\n"
           "      f'shift={len(results[\"shift\"])} '\n"
           "      f'linear={\"yes\" if \"linear\" in results else \"no\"}',\n"
           "      flush=True)\n";
    return out.str();
}

} // namespace calango::core
