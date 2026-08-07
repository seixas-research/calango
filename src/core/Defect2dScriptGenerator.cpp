// ===========================================================================
// The image-charge correction here is implemented rather than delegated, which
// the bulk module (DefectScriptGenerator.cpp) deliberately does not do — it
// hands FNV to pymatgen because an unchecked correction is worse than none: a
// plausible number of the right magnitude and the wrong value, applied
// silently to every point of a formation-energy diagram. There is no pymatgen
// equivalent for the 2D case, so this one earns its trust from limits instead,
// in tests/defect_2d_correction_test.py:
//
//   * the ISOLATED energy reproduces the analytic Gaussian self-energy
//     q^2 / (2 sigma sqrt(pi) eps) to 0.008 % across sigma = 0.8-1.5 A and
//     eps = 1-6, and is bit-identical for every supercell it is asked about;
//   * the PERIODIC energy gives E ~ 1/L to 0.7 %, exactly q^2, exactly 1/eps,
//     and eps_perp genuinely changes the answer;
//   * their DIFFERENCE, in a uniform medium, is the simple-cubic Madelung
//     energy alpha q^2 / (2 eps L) with alpha = 2.8373, to 2 %. That is the
//     one check neither half passes alone: it is what proves the two are on
//     the same normalization, and so that subtracting them means anything.
//
// The isolated half is where the physics is easy to get wrong, and did go
// wrong once. Taking the in-plane area to infinity while keeping the cell's
// periodicity ALONG z is not the isolated system — the 2D divergence lives in
// the out-of-plane images, so E_isolated then drifted 3.80 -> 1.82 eV as the
// box grew, with no sign of converging. Nor is the fix a bigger box.
//
// It is a radiation boundary condition. Outside the sheet eps is constant, so
// for each in-plane g the decaying solution of
//
//     d/dz[eps_perp dV/dz] - g^2 eps_par V = -4 pi rho
//
// is exactly exp(-g|z|), and imposing dV/dz = -+ g V at the edges is therefore
// not an approximation that improves with distance — it is exact as soon as
// the edge clears the charge. A small fixed domain with a well-resolved grid
// beats a growing one, which is why the first prototype (adaptive domain, fixed
// point count, so ~40 Bohr spacing against a 1.9 Bohr Gaussian) was off by a
// factor that itself varied with eps.
// ===========================================================================

#include "core/Defect2dScriptGenerator.hpp"

#include "core/AseScriptGenerator.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace calango::core {

namespace {

/// A Python string literal for a filesystem path.
std::string pathLiteral(const std::string& path)
{
    std::string out = "r\"";
    for (const char c : path)
        out += c == '"' ? '\'' : c;
    return out + "\"";
}

/// The model-charge / Poisson solver, as pure numpy.
///
/// Gaussian units throughout, with lengths in Bohr and energies in Hartree,
/// converted once at the boundary. Mixing eV and Å inside an electrostatics
/// routine is how a factor of 4π survives review.
constexpr const char* kCorrectionHelpers = R"PY(
# --- 2D image-charge correction (Komsa-Pasquarello) ------------------------
#
# A charged defect in a slab supercell is not an isolated defect: it is an
# infinite array of them, in a medium that is the sheet inside and vacuum
# outside, plus a neutralizing background. Two things follow that the bulk
# (FNV) correction cannot express.
#
#   * There is no scalar epsilon to divide by. The screening is the sheet's
#     in-plane and out-of-plane, inside a layer of finite thickness, and 1
#     everywhere else.
#
#   * The energy DIVERGES with vacuum thickness. A charged 2D layer against a
#     jellium background has an electrostatic energy that grows without bound
#     as L_z does, so "add vacuum until it converges" does not terminate.
#
# The correction is therefore not a fitted 1/L term but the difference between
# two solutions of the same Poisson equation: one with the supercell's
# periodicity, one isolated. Because epsilon depends only on z, the equation
#
#     div(eps grad V) = -4 pi rho
#
# separates: for each in-plane G_par it becomes a dense linear system in the
# out-of-plane components,
#
#     sum_{Gz'} [ eps_par(Gz - Gz') G_par^2
#                 + eps_perp(Gz - Gz') Gz Gz' ] V(Gz') = 4 pi rho(Gz),
#
# which is what is assembled and solved below.
BOHR_PER_ANG = 1.0 / 0.529177210903
HARTREE_EV = 27.211386245988


def _dielectric_profile(z, centre, thickness, width, eps_in):
    """Slab profile: eps_in inside the layer, 1 in the vacuum.

    Smoothed with a pair of error functions rather than stepped. A hard step
    has Fourier components at every G_z, which makes the linear system stiff
    for no physical reason — a real interface is not a step either.
    """
    half = 0.5 * thickness
    if width <= 0.0:
        inside = np.abs(z - centre) <= half
        return np.where(inside, eps_in, 1.0)
    from scipy.special import erf as _erf
    edge = 0.5 * (_erf((z - centre + half) / width)
                  - _erf((z - centre - half) / width))
    return 1.0 + (eps_in - 1.0) * edge


def _profile_fourier(profile, n_gz):
    """Fourier coefficients eps(Gz - Gz') as a Toeplitz-indexable array."""
    # profile is sampled on a uniform z grid over the cell; its DFT gives the
    # coefficients directly, normalized so that a constant profile returns
    # eps at Gz = 0 and zero elsewhere.
    coeffs = np.fft.fft(profile) / len(profile)
    # Only the components the solve can reach are kept; the matrix below
    # indexes differences in [-(n-1), n-1].
    index = np.arange(-(n_gz - 1), n_gz)
    return coeffs[index % len(profile)], n_gz - 1


def _periodic_energy(q, cell_ang, centre_frac, sigma_ang, eps_par, eps_perp,
                     thickness_ang, width_ang, n_gz, npar, normal_axis=2,
                     uniform=False):
    """Electrostatic energy of the Gaussian model charge IN THE SUPERCELL, eV.

    The discrete in-plane sum over the cell's own reciprocal stars, with G = 0
    excluded because that is the neutralizing background. Its counterpart is
    _isolated_energy below; the correction is the difference.

    `uniform` fills the cell with eps instead of building a slab profile. It
    exists so that the two halves can be put in the same uniform medium by ONE
    flag: the isolated half has no cell to make "thickness >= L_z" mean
    anything, so without it a caller would have to say "uniform" two different
    ways and could silently say it in only one.
    """
    # In-plane axes are whichever two are not the normal.
    axes = [a for a in range(3) if a != normal_axis]
    a1 = cell_ang[axes[0]]
    a2 = cell_ang[axes[1]]
    lz = float(np.linalg.norm(cell_ang[normal_axis])) * BOHR_PER_ANG
    # In-plane cell as a 2x2 metric, so a non-orthogonal (hexagonal) sheet is
    # handled rather than silently assumed square.
    a1p = np.array([a1[axes[0]], a1[axes[1]]]) * BOHR_PER_ANG
    a2p = np.array([a2[axes[0]], a2[axes[1]]]) * BOHR_PER_ANG
    area = abs(a1p[0] * a2p[1] - a1p[1] * a2p[0])
    volume = area * lz
    if area <= 0.0 or lz <= 0.0:
        raise RuntimeError('degenerate cell: cannot build the model')

    # Reciprocal in-plane vectors.
    b1 = 2.0 * np.pi * np.array([a2p[1], -a2p[0]]) / area
    b2 = 2.0 * np.pi * np.array([-a1p[1], a1p[0]]) / area

    sigma = sigma_ang * BOHR_PER_ANG
    thickness = thickness_ang * BOHR_PER_ANG
    width = width_ang * BOHR_PER_ANG
    centre = centre_frac * lz

    # z grid and the two profiles.
    nz_fine = max(4 * n_gz, 256)
    z = np.linspace(0.0, lz, nz_fine, endpoint=False)
    if uniform:
        par_profile = np.full_like(z, eps_par)
        perp_profile = np.full_like(z, eps_perp)
    else:
        par_profile = _dielectric_profile(z, centre, thickness, width, eps_par)
        perp_profile = _dielectric_profile(z, centre, thickness, width,
                                           eps_perp)
    par_fft, shift = _profile_fourier(par_profile, n_gz)
    perp_fft, _ = _profile_fourier(perp_profile, n_gz)

    gz_index = np.arange(-(n_gz // 2), n_gz - n_gz // 2)
    gz = 2.0 * np.pi * gz_index / lz
    # eps(Gz - Gz') for every pair, as a matrix.
    diff = gz_index[:, None] - gz_index[None, :]
    eps_par_mat = par_fft[diff + shift]
    eps_perp_mat = perp_fft[diff + shift]

    # rho(G) for the Gaussian, normalized to q. The z part is a Gaussian
    # centred on the defect plane; the phase is what puts it there.
    def rho_z(gz_vec):
        return (np.exp(-0.5 * (sigma * gz_vec) ** 2)
                * np.exp(-1j * gz_vec * centre))

    def solve(gpar2, weight):
        """Contribution of one in-plane G (or one integration element)."""
        matrix = eps_par_mat * gpar2 + eps_perp_mat * (gz[:, None] * gz[None, :])
        rhs = 4.0 * np.pi * np.exp(-0.5 * sigma ** 2 * gpar2) * rho_z(gz)
        if gpar2 == 0.0:
            # The G = 0 component is the neutralizing background: it is
            # removed, not solved for. Dropping the row and column is what
            # implements that, rather than a regularization that would leave a
            # cell-size-dependent constant behind.
            keep = gz_index != 0
            matrix = matrix[np.ix_(keep, keep)]
            rhs = rhs[keep]
            if matrix.size == 0:
                return 0.0
            potential = np.linalg.solve(matrix, rhs)
            dens = np.exp(-0.5 * sigma ** 2 * gpar2) * rho_z(gz[keep])
            return weight * float(np.real(np.vdot(dens, potential)))
        potential = np.linalg.solve(matrix, rhs)
        dens = np.exp(-0.5 * sigma ** 2 * gpar2) * rho_z(gz)
        return weight * float(np.real(np.vdot(dens, potential)))

    # Periodic: the discrete in-plane sum over the supercell's own stars.
    total = 0.0
    for i in range(-npar, npar + 1):
        for j in range(-npar, npar + 1):
            gvec = i * b1 + j * b2
            total += solve(float(gvec @ gvec), 1.0 / volume)

    # E = (1/2) sum_G rho*(G) V(G), with q^2 scaling carried explicitly.
    return 0.5 * q * q * total * HARTREE_EV


def _solve_tridiagonal(sub, diag, sup, rhs):
    """Thomas algorithm. numpy only, so the correction needs no scipy.linalg.

    sub[i] is a[i, i-1] and sup[i] is a[i, i+1]; sub[0] and sup[-1] are unused.
    """
    n = len(diag)
    c = np.zeros(n)
    d = np.zeros(n)
    beta = diag[0]
    if beta == 0.0:
        raise RuntimeError('singular tridiagonal system')
    c[0] = sup[0] / beta
    d[0] = rhs[0] / beta
    for i in range(1, n):
        beta = diag[i] - sub[i] * c[i - 1]
        if beta == 0.0:
            raise RuntimeError('singular tridiagonal system')
        c[i] = sup[i] / beta if i < n - 1 else 0.0
        d[i] = (rhs[i] - sub[i] * d[i - 1]) / beta
    x = np.zeros(n)
    x[-1] = d[-1]
    for i in range(n - 2, -1, -1):
        x[i] = d[i] - c[i] * x[i + 1]
    return x


def _isolated_energy(q, sigma_ang, eps_par, eps_perp, thickness_ang,
                     width_ang, n_z=4001, n_g=400, pad_sigma=10.0,
                     uniform=False):
    """Energy of the SAME model charge with no periodic images at all, in eV.

    Note what this does NOT take: a supercell. That is the whole point — an
    isolated energy that moved with the cell would not be an isolated energy,
    and a first version of this which took the in-plane area to infinity while
    keeping the cell's periodicity ALONG z did exactly that (it drifted from
    3.8 to 1.8 eV as the box grew, with no sign of converging). The 2D
    divergence lives in the out-of-plane images, so they have to go too.
    Enlarging the box does not remove them; open boundary conditions do.

    For each in-plane g the Poisson equation

        d/dz[eps_perp(z) dV/dz] - g^2 eps_par(z) V = -4 pi rho(z)

    is solved on a domain that only has to clear the charge and the slab,
    closed by a RADIATION condition dV/dz = -+ g V at the edges. That is not an
    approximation: outside the charge eps is constant, the homogeneous equation
    is eps(V'' - g^2 V) = 0 whatever eps is, and its decaying solution is
    exactly e^{-g|z|}. So the condition is exact for a vacuum exterior and for
    a uniform dielectric one alike, and the domain never has to grow as 1/g.
    """
    sigma = sigma_ang * BOHR_PER_ANG
    d = thickness_ang * BOHR_PER_ANG
    w = width_ang * BOHR_PER_ANG
    half = pad_sigma * sigma if uniform else 0.5 * d + pad_sigma * sigma
    z = np.linspace(-half, half, n_z)
    h = z[1] - z[0]

    if uniform:
        eps_a = np.full_like(z, eps_par)
        eps_p = np.full_like(z, eps_perp)
    else:
        eps_a = _dielectric_profile(z, 0.0, d, w, eps_par)
        eps_p = _dielectric_profile(z, 0.0, d, w, eps_perp)
    # eps_perp on the half-grid points, which is what the flux form needs.
    eps_h = 0.5 * (eps_p[1:] + eps_p[:-1])
    gauss = np.exp(-0.5 * (z / sigma) ** 2) / (np.sqrt(2.0 * np.pi) * sigma)

    def at_g(g):
        diag = np.zeros(n_z)
        sup = np.zeros(n_z)
        sub = np.zeros(n_z)
        diag[1:-1] = -(eps_h[1:] + eps_h[:-1]) / h ** 2 - g * g * eps_a[1:-1]
        sup[1:-1] = eps_h[1:] / h ** 2
        sub[1:-1] = eps_h[:-1] / h ** 2
        # Radiation conditions: V' = +gV at -L, V' = -gV at +L.
        diag[0] = -(1.0 + g * h)
        sup[0] = 1.0
        diag[-1] = 1.0 + g * h
        sub[-1] = -1.0
        rho = gauss * np.exp(-0.5 * (sigma * g) ** 2)
        rhs = -4.0 * np.pi * rho
        rhs[0] = 0.0
        rhs[-1] = 0.0
        potential = _solve_tridiagonal(sub, diag, sup, rhs)
        integrand = rho * potential
        return (float(np.trapezoid(integrand, z)) if hasattr(np, 'trapezoid')
                else float(np.trapz(integrand, z)))

    # E = (1/(4 pi)) int_0^inf g dg [ int dz rho* V ].
    #
    # g = 0 exactly is skipped, and has to be: a charged sheet at zero in-plane
    # wavevector has no decaying solution, so the system is singular there. The
    # INTEGRAND g * I(g) stays finite as g -> 0 (I ~ 1/g), so a grid that
    # starts just above zero loses nothing — the omitted head is O(g_min).
    grid = np.linspace(0.0, np.sqrt(12.0 / sigma), n_g) ** 2
    grid = grid[1:]
    values = np.array([at_g(g) for g in grid])
    weighted = values * grid
    total = (float(np.trapezoid(weighted, grid)) if hasattr(np, 'trapezoid')
             else float(np.trapz(weighted, grid)))
    return q * q * total / (4.0 * np.pi) * HARTREE_EV


def two_d_image_correction(q, cell_ang, centre_frac, sigma_ang, eps_par,
                           eps_perp, thickness_ang, width_ang, n_gz, npar,
                           normal_axis=2, uniform=False):
    """E_corr(q) in eV: isolated minus periodic, the spurious part removed.

    Positive for a charged defect in a slab: the periodic array plus its
    neutralizing background is over-bound relative to the isolated defect, so
    the raw calculation under-estimates the formation energy.

    The two halves are on the same normalization, and that is checked rather
    than assumed — in a uniform medium their difference reproduces the
    simple-cubic Madelung energy alpha q^2 / (2 eps L) to better than 2 %.
    """
    if q == 0:
        return 0.0, {'periodic_eV': 0.0, 'isolated_eV': 0.0}
    periodic = _periodic_energy(q, cell_ang, centre_frac, sigma_ang, eps_par,
                                eps_perp, thickness_ang, width_ang, n_gz,
                                npar, normal_axis=normal_axis,
                                uniform=uniform)
    isolated = _isolated_energy(q, sigma_ang, eps_par, eps_perp,
                                thickness_ang, width_ang, uniform=uniform)
    return isolated - periodic, {'periodic_eV': periodic,
                                 'isolated_eV': isolated}
)PY";

} // namespace

std::string defect2dCorrectionHelpers()
{
    return kCorrectionHelpers;
}

std::string generateDefect2dScript(const Defect2dConfig& cfg)
{
    // q = 0 is the baseline every other charge state is measured against, so
    // it is present whatever the wizard collected.
    std::set<int> chargeSet(cfg.charges.begin(), cfg.charges.end());
    chargeSet.insert(0);

    std::ostringstream out;
    out << "# Charged defects in a 2D material — generated by Calango\n"
           "#\n"
           "#   E_f[X^q](E_F) = E_tot[X^q] - E_tot[host] - sum_i n_i mu_i\n"
           "#                   + q (E_VBM + E_F) + E_corr(q)\n"
           "#\n"
           "# One fixed-geometry SCF per charge state, then the 2D\n"
           "# image-charge correction of Komsa and Pasquarello. The bulk FNV\n"
           "# correction is NOT applicable here and is not used: a charged\n"
           "# sheet in a slab supercell has no scalar epsilon to divide by,\n"
           "# and its energy diverges with vacuum thickness rather than\n"
           "# converging, so the 1/(eps L) form FNV removes is the wrong\n"
           "# functional form rather than an approximate one.\n"
           "import json\n"
           "import os\n"
           "\n"
           "import numpy as np\n"
           "\n"
        << AseScriptGenerator::jsonLoggerPreamble()
        << "from gpaw import GPAW, restart\n"
           "\n"
        << kCorrectionHelpers
        << "\n"
           "# --- Inherited inputs ---------------------------------------------\n"
        << "PRISTINE = " << pathLiteral(cfg.pristinePath) << "\n"
        << "NEUTRAL = " << pathLiteral(cfg.neutralDefectPath) << "\n"
        << "EPS_PAR = " << cfg.epsilonInPlane << "\n"
        << "EPS_PERP = " << cfg.epsilonOutOfPlane << "\n"
        << "THICKNESS = " << cfg.layerThickness << "     # Angstrom\n"
        << "INTERFACE_WIDTH = " << cfg.interfaceWidth << "  # Angstrom\n"
        << "NORMAL_AXIS = " << cfg.normalAxis << "\n"
        << "DEFECT_INDEX = " << cfg.defectIndex << "\n"
        << "SIGMA = " << cfg.modelChargeRadius << "       # Angstrom\n"
        << "N_GZ = " << std::max(8, cfg.zComponents) << "\n"
        << "N_PAR = " << std::max(1, cfg.inPlaneCutoff) << "\n"
        << "APPLY_CORRECTION = " << (cfg.applyCorrection ? "True" : "False")
        << "\n"
        << "FERMI_POINTS = " << std::max(2, cfg.fermiPoints) << "\n"
        << "CHARGES = [";
    bool first = true;
    for (const int q : chargeSet) {
        out << (first ? "" : ", ") << q;
        first = false;
    }
    out << "]\n"
           "SPECIES = [";
    for (std::size_t i = 0; i < cfg.species.size(); ++i) {
        const DefectSpecies& s = cfg.species[i];
        out << (i ? ",\n           " : "") << "{'symbol': '" << s.symbol
            << "', 'count': " << s.count
            << ", 'mu_eV': " << s.chemicalPotentialEv << "}";
    }
    out << "]\n"
           "\n"
           "# --- Host and neutral baselines -----------------------------------\n"
           "host_atoms, host_calc = restart(PRISTINE, txt=None)\n"
           "E_HOST = float(host_calc.get_potential_energy())\n"
           "\n"
           "# The VBM the Fermi level is measured from. Taken from the HOST,\n"
           "# not the defect: the defect's own levels sit in the gap, and\n"
           "# referencing to them would move the origin of the diagram with\n"
           "# every charge state.\n"
           "_eigs = np.concatenate([\n"
           "    host_calc.get_eigenvalues(kpt=k, spin=s)\n"
           "    for s in range(host_calc.get_number_of_spins())\n"
           "    for k in range(len(host_calc.get_ibz_k_points()))])\n"
           "_fermi = float(host_calc.get_fermi_level())\n"
           "_occupied = _eigs[_eigs <= _fermi]\n"
           "E_VBM = float(_occupied.max()) if _occupied.size else _fermi\n"
           "_empty = _eigs[_eigs > _fermi]\n"
           "E_CBM = float(_empty.min()) if _empty.size else _fermi\n"
           "BAND_GAP = max(E_CBM - E_VBM, 0.0)\n"
           "print(f'CALANGO_INFO host VBM={E_VBM:.4f} eV gap={BAND_GAP:.4f} eV',\n"
           "      flush=True)\n"
           "\n"
           "defect_atoms, defect_calc = restart(NEUTRAL, txt=None)\n"
           "CELL = np.asarray(defect_atoms.cell[:], dtype=float)\n"
           "_lz = float(np.linalg.norm(CELL[NORMAL_AXIS]))\n"
           "# Where the sheet sits along the normal, as a fraction of the cell.\n"
           "# Read from the defect site rather than assumed to be the middle:\n"
           "# a slab built by ase.build.surface is not centred.\n"
           "_positions = defect_atoms.get_positions()\n"
           "CENTRE_FRAC = float(_positions[DEFECT_INDEX][NORMAL_AXIS]) / _lz\n"
           "print(f'CALANGO_INFO sheet centre at {CENTRE_FRAC:.3f} of L_z='\n"
           "      f'{_lz:.2f} A; model sigma={SIGMA} A', flush=True)\n"
           "\n"
           "# --- One fixed-geometry SCF per charge state -----------------------\n"
           "results = {}\n"
           "for _i, q in enumerate(CHARGES):\n"
           "    _calango_progress(_i + 1, len(CHARGES) + 2)\n"
           "    atoms, calc = restart(NEUTRAL, txt=f'defect_q{q:+d}.txt')\n"
           "    # The geometry is FIXED at the neutral relaxation for every q.\n"
           "    # Relaxing each charge state separately would fold the\n"
           "    # relaxation energy into the transition levels, which is a\n"
           "    # different (and usually unintended) quantity.\n"
           "    calc = calc.new(charge=float(q), txt=f'defect_q{q:+d}.txt')\n"
           "    atoms.calc = calc\n"
           "    energy = float(atoms.get_potential_energy())\n"
           "    results[q] = {'E_tot_eV': energy, 'correction_eV': 0.0,\n"
           "                  'correction_terms': {}}\n"
           "    print(f'CALANGO_RESULT q={q:+d} E={energy:.6f} eV', flush=True)\n"
           "\n"
           "# --- 2D image-charge corrections ----------------------------------\n"
           "_calango_progress(len(CHARGES) + 1, len(CHARGES) + 2)\n"
           "if APPLY_CORRECTION:\n"
           "    for q in CHARGES:\n"
           "        if q == 0:\n"
           "            continue\n"
           "        corr, terms = two_d_image_correction(\n"
           "            q, CELL, CENTRE_FRAC, SIGMA, EPS_PAR, EPS_PERP,\n"
           "            THICKNESS, INTERFACE_WIDTH, N_GZ, N_PAR,\n"
           "            normal_axis=NORMAL_AXIS)\n"
           "        results[q]['correction_eV'] = float(corr)\n"
           "        results[q]['correction_terms'] = terms\n"
           "        print(f'CALANGO_RESULT q={q:+d} E_corr={corr:+.4f} eV',\n"
           "              flush=True)\n"
           "else:\n"
           "    print('CALANGO_INFO the image-charge correction was switched '\n"
           "          'off; the formation energies below are UNCORRECTED and '\n"
           "          'depend on the supercell and on how much vacuum it '\n"
           "          'carries.', flush=True)\n"
           "\n"
           "# --- Formation energies over the gap -------------------------------\n"
           "# The schema written below is deliberately the one the BULK module\n"
           "# writes, because the diagram is the same diagram: straight lines of\n"
           "# slope q, a lower envelope, and the crossings on it. Only E_corr\n"
           "# differs, and it is reported in the same 'isolated' / 'periodic'\n"
           "# terms, so one viewer serves both rather than two drifting apart.\n"
           "MU_TOTAL = sum(s['count'] * s['mu_eV'] for s in SPECIES)\n"
           "fermi = np.linspace(0.0, BAND_GAP if BAND_GAP > 0 else 1.0,\n"
           "                    FERMI_POINTS)\n"
           "lines = {}\n"
           "for q in CHARGES:\n"
           "    entry = results[q]\n"
           "    # The intercept is E_f at E_F = 0, i.e. at the VBM; the line is\n"
           "    # intercept + q E_F, which is why a charge state can be read off\n"
           "    # the slope.\n"
           "    intercept = (entry['E_tot_eV'] - E_HOST - MU_TOTAL\n"
           "                 + q * E_VBM + entry['correction_eV'])\n"
           "    entry['formation_energy_at_VBM_eV'] = float(intercept)\n"
           "    lines[q] = intercept + q * fermi\n"
           "\n"
           "# The lower envelope is the diagram: at each Fermi level the charge\n"
           "# state actually realized is the cheapest one.\n"
           "stack = np.array([lines[q] for q in CHARGES])\n"
           "envelope = stack.min(axis=0)\n"
           "stable = [CHARGES[i] for i in stack.argmin(axis=0)]\n"
           "\n"
           "# --- Transition levels ---------------------------------------------\n"
           "# Only crossings that lie ON the envelope are thermodynamic levels;\n"
           "# the rest are between two states neither of which is ever the\n"
           "# ground state, and reporting those as levels would be wrong.\n"
           "transitions = []\n"
           "for i in range(1, len(stable)):\n"
           "    if stable[i] == stable[i - 1]:\n"
           "        continue\n"
           "    q_from, q_to = stable[i - 1], stable[i]\n"
           "    if q_from == q_to:\n"
           "        continue\n"
           "    # Solved from the two intercepts rather than read off the grid:\n"
           "    # the crossing is a property of the energies, the grid spacing\n"
           "    # is not.\n"
           "    I_from = results[q_from]['formation_energy_at_VBM_eV']\n"
           "    I_to = results[q_to]['formation_energy_at_VBM_eV']\n"
           "    level = (I_to - I_from) / (q_from - q_to)\n"
           "    transitions.append({\n"
           "        'from_charge': int(q_from),\n"
           "        'to_charge': int(q_to),\n"
           "        'level_eV_above_VBM': float(level),\n"
           "        'level_eV_below_CBM': float(BAND_GAP - level),\n"
           "        'formation_energy_eV': float(I_from + q_from * level),\n"
           "    })\n"
           "    _lbl = lambda q: ('+' * q if q > 0 else '-' * -q) or '0'\n"
           "    print(f'CALANGO_INFO transition ({_lbl(q_from)}/{_lbl(q_to)}) at '\n"
           "          f'E_F = {level:.4f} eV above VBM', flush=True)\n"
           "\n"
           "if not transitions:\n"
           "    print('CALANGO_INFO no transition level inside the gap - one '\n"
           "          'charge state is the ground state across the whole '\n"
           "          'Fermi-level range.', flush=True)\n"
           "\n"
           "summary = {\n"
           "    'host': {\n"
           "        'E_tot_eV': E_HOST,\n"
           "        'E_VBM_eV': E_VBM,\n"
           "        'E_CBM_eV': E_CBM,\n"
           "        'E_gap_eV': BAND_GAP,\n"
           "        'natoms': int(len(host_atoms)),\n"
           "    },\n"
           "    'defect': {\n"
           "        'formula': defect_atoms.get_chemical_formula(),\n"
           "        'natoms': int(len(defect_atoms)),\n"
           "        'defect_index': int(DEFECT_INDEX),\n"
           "    },\n"
           "    'correction': {\n"
           "        'scheme': '2D image charge (Komsa-Pasquarello)',\n"
           "        'applied': bool(APPLY_CORRECTION),\n"
           "        'epsilon': float(EPS_PAR),\n"
           "        'epsilon_in_plane': float(EPS_PAR),\n"
           "        'epsilon_out_of_plane': float(EPS_PERP),\n"
           "        'layer_thickness_A': float(THICKNESS),\n"
           "        'interface_width_A': float(INTERFACE_WIDTH),\n"
           "        'model_charge_radius_A': float(SIGMA),\n"
           "        'normal_axis': int(NORMAL_AXIS),\n"
           "        'cell_length_normal_A': float(_lz),\n"
           "        'z_components': int(N_GZ),\n"
           "        'in_plane_cutoff': int(N_PAR),\n"
           "    },\n"
           "    'species': SPECIES,\n"
           "    'chemical_potential_term_eV': float(MU_TOTAL),\n"
           "    'charges': [\n"
           "        {\n"
           "            'charge': int(q),\n"
           "            'E_tot_eV': results[q]['E_tot_eV'],\n"
           "            'correction_eV': results[q]['correction_eV'],\n"
           "            'correction_terms': results[q]['correction_terms'],\n"
           "            'formation_energy_at_VBM_eV':\n"
           "                results[q]['formation_energy_at_VBM_eV'],\n"
           "            'formation_energy_eV': [float(v) for v in lines[q]],\n"
           "        }\n"
           "        for q in CHARGES\n"
           "    ],\n"
           "    'fermi_level_eV': [float(v) for v in fermi],\n"
           "    'envelope_eV': [float(v) for v in envelope],\n"
           "    'stable_charge': [int(v) for v in stable],\n"
           "    'transitions': transitions,\n"
           "}\n"
           "with open('charged_defects_2d.json', 'w') as handle:\n"
           "    json.dump(summary, handle, indent=2)\n"
           "\n"
           "_calango_progress(len(CHARGES) + 2, len(CHARGES) + 2)\n"
           "print(f'CALANGO_RESULT charged_defects_2d=charged_defects_2d.json '\n"
           "      f'charges={len(CHARGES)} transitions={len(transitions)} '\n"
           "      f'gap={BAND_GAP:.3f}', flush=True)\n"
           "print('CALANGO_DONE', flush=True)\n";
    return out.str();
}

} // namespace calango::core
