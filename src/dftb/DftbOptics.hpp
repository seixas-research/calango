#pragma once

#include "core/Vec3.hpp"
#include "dft/DftTypes.hpp"
#include "dftb/DftbHamiltonian.hpp"
#include "dftb/DftbScf.hpp"

#include <array>
#include <complex>
#include <vector>

/// Linear optical absorption from the DFTB states: independent-particle
/// Kubo-Greenwood / RPA, velocity gauge — the SAME level of approximation
/// GPAW's own DielectricFunction and VASP's LOPTICS use (no local-field
/// effects, no excitonic/BSE corrections — see the module doc below for
/// exactly what that means).
///
/// THE VELOCITY OPERATOR, non-orthogonal basis. Diagonalizing H(k)C = E(k)SC
/// at a fixed k says nothing about how E or C move with k; the standard,
/// gauge-invariant generalized-Hellmann-Feynman result for a non-orthogonal
/// tight-binding basis (used across the DFTB/TB optical-absorption
/// literature) is
///
///     v_alpha,nm = <n(k)| dH/dk_alpha - E_m(k) dS/dk_alpha |m(k)>
///
/// (atomic units, hbar = 1), well-defined for n == m (the ordinary group
/// velocity, by the Hellmann-Feynman theorem for a normalized eigenstate —
/// which E_n or E_m appears does not matter there) AND for n != m (the
/// interband matrix element Kubo-Greenwood needs), where it does NOT matter
/// which of E_n/E_m is used either, by the two states' own eigenvalue
/// equations — a property DftbTest.cpp checks directly rather than assumes.
///
/// dH/dk and dS/dk are obtained by CENTRAL FINITE DIFFERENCE of
/// DftbHamiltonianBuilder::blochMatrices() with respect to each k component
/// — not a fresh analytic k-derivative of the Slater-Koster angular
/// transform, for the same reason DftbForces.hpp gives for choosing finite
/// difference over a hand-differentiated Hamiltonian: a second,
/// independently-derived analytic gradient is a real source of sign/algebra
/// bugs, and this one is cheap (H(k)/S(k) evaluation, not a full SCF solve).
///
/// THE ABSORPTION FORMULA (one Cartesian direction alpha, atomic units):
///
///     eps_alpha(w) = 1 + (4*pi/V) sum_k w_k sum_{n,m} (f_m - f_n)
///                    |v_alpha,nm(k)|^2 / [w_nm^2 * (w_nm - w - i*eta)]
///
/// with w_nm = E_n(k) - E_m(k) — a single complex sum giving BOTH eps1
/// (real part) and eps2 (imaginary part) directly, no separate
/// Kramers-Kronig step. The (f_m - f_n) sign (not (f_n - f_m), which a
/// first draft of this module used, and which a REAL run caught: it gives
/// a negative eps2 at positive frequency, violating the passive-medium
/// positivity a Sokhotski-Plemelj derivation shows must hold) is what
/// makes n = empty/m = occupied resonate positively at the physical
/// absorption edge w_nm = w — see DftbOptics.cpp's own comment at the sum
/// for the full re-derivation. V is the cell volume for a 3D-periodic
/// system; for
/// a 2D one (see below) this module reports the per-AREA response instead,
/// exactly as the other 2D-aware optics modules in this codebase do (2D
/// Bands' own k-grid handling, and OpticsScriptGenerator's
/// twoDObservablesBlock for GPAW/VASP).
///
/// SCOPE: diagonal components only (eps_xx, eps_yy, eps_zz) — the full
/// off-diagonal tensor is FUTURE.md. No local-field effects (independent-
/// particle RPA, i.e. the density response to the EXTERNAL field only, not
/// the induced Hartree/XC field a full TDDFT or BSE treatment would add) —
/// stated explicitly here because it is the same approximation level
/// GPAW/VASP's own optics generators are already documented as using, and
/// nothing about a tight-binding basis makes it any less or more valid.
namespace calango::dftb {

struct DftbOpticsOptions {
    std::vector<double> frequenciesEv;
    /// Lorentzian broadening (eV) — the eta above, keeping every w_nm - w
    /// denominator away from an exact pole.
    double broadeningEv = 0.1;
    /// 0 = x, 1 = y, 2 = z.
    int direction = 0;
    /// Set for a 2D-periodic structure (see DftbEngine's own 2D detection):
    /// when > 0, results are reported as the per-area 2D observables
    /// (alpha_2D, absorbance, sigma_2D) INSTEAD of a bulk dielectric
    /// function, using this cell thickness (Angstrom) exactly the way
    /// OpticsScriptGenerator.cpp's twoDObservablesBlock() already does for
    /// GPAW/VASP.
    double vacuumThicknessAngstrom = 0.0;
};

struct DftbOpticsResult {
    std::vector<double> frequenciesEv;
    std::vector<double> eps1;
    std::vector<double> eps2;
    /// The same "seven per-direction spectra from complex eps(w)" formula
    /// set OpticsScriptGenerator.cpp's derived_spectra() already defines
    /// for GPAW/VASP — copied verbatim (refractive index N = n + ik from
    /// sqrt(eps), absorption via 2*(w/hbar*c)*k in cm^-1, reflectivity
    /// ((n-1)^2+k^2)/((n+1)^2+k^2), loss = -Im(1/eps)) so a DFTB optics
    /// panel is comparable, formula for formula, with a GPAW/VASP one —
    /// the exact reason that function's own doc comment gives for reusing
    /// one formula set across engines rather than each deriving its own.
    std::vector<double> n;
    std::vector<double> k;
    std::vector<double> absorptionInverseCm;
    std::vector<double> reflectivity;
    std::vector<double> loss;
    /// Populated only when vacuumThicknessAngstrom > 0 (see the options
    /// doc) — EXACTLY OpticsScriptGenerator.cpp's own twod_observables()
    /// formulas (alpha_2D = L_z/(4*pi)*(eps-1) in Angstrom, absorbance =
    /// (omega/hbar*c)*L_z*eps2, sigma_2D in e^2/h via alpha_2D*2*pi/alpha_fs),
    /// reused verbatim rather than re-derived, so a DFTB 2D-optics result
    /// and a GPAW/VASP one read the identical relation between eps and the
    /// sheet observables.
    std::vector<double> alpha2DReAngstrom;
    std::vector<double> alpha2DImAngstrom;
    std::vector<double> absorbance;
    std::vector<double> sigma2DRe;
    std::vector<double> sigma2DIm;
};

/// v_alpha,nm(k) in the eigenbasis (dimension x dimension, row-major) —
/// exposed on its own so DftbTest.cpp can validate it directly (against the
/// finite-difference group velocity of the eigenvalues themselves, and
/// against the n==m/n!=m gauge-invariance property) before it is folded
/// into an absorption spectrum.
/// `latticeVectorsBohr` are the structure's own real-space lattice vectors
/// (Bohr) — needed to convert a CARTESIAN wavevector step (this function's
/// own `stepInverseBohr`) into the fractional-k perturbation
/// blochMatrices() takes: dk_frac,i = (a_i . e_direction) / (2*pi) * step,
/// which is what makes the returned matrix elements true Cartesian
/// (energy * length) velocities rather than "per fractional-k-unit" ones.
std::vector<std::complex<double>> dftbVelocityMatrix(
    const DftbHamiltonianBuilder& hamiltonian,
    const std::array<core::Vec3, 3>& latticeVectorsBohr,
    const std::array<double, 3>& kFrac, const std::vector<double>& shift,
    const std::vector<std::complex<double>>& eigenvectors,
    const std::vector<double>& eigenvaluesHartree, int direction,
    double stepInverseBohr = 1.0e-4);

/// `shift` is the SAME converged SCC potential shift every other post-
/// processing step (bands, unfolding) reuses — needed here too, since the
/// velocity operator's own finite-difference k-points must be evaluated
/// with the identical (fixed) Hamiltonian the converged eigenstates in
/// `scf.kpoints` themselves came from.
dft::Outcome computeDftbOptics(const DftbScfResult& scf,
                                const DftbHamiltonianBuilder& hamiltonian,
                                const std::array<core::Vec3, 3>& latticeVectorsBohr,
                                double cellVolumeBohr3,
                                const std::vector<double>& shift,
                                const DftbOpticsOptions& options,
                                DftbOpticsResult& out);

} // namespace calango::dftb
