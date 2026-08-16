#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "core/WannierHamiltonian.hpp"

#include <complex>
#include <vector>

namespace calango::core {

/// Excitons from the Bethe-Salpeter equation (BSE), in the basis of
/// Wannier-interpolated valence and conduction states.
///
/// NATIVE, AND DELIBERATELY SO, exactly like WannierHamiltonian,
/// BoltzmannTransport and BerryPhase above it. Wannier90/postw90, WanTiBEXOS
/// (a published Wannier-tight-binding BSE/exciton code, the closest
/// published relative of what is implemented here), Yambo and BerkeleyGW are
/// references for the formalism and the conventional quantities to report —
/// nothing here invokes them, links them, or requires a file any of them
/// produced. The only input is a WannierHamiltonian (H(k) from H(R), the
/// same object Boltzmann Transport and Berry Phase already consume) plus a
/// screening model.
///
/// THE PHYSICS, TAMM-DANCOFF APPROXIMATION (the default and, in this
/// version, the ONLY mode — full BSE beyond TDA, coupling resonant and
/// anti-resonant blocks, is a documented follow-up in FUTURE.md).
///
/// The electron-hole basis is |vck>: v ranges over `nValence` valence bands
/// below (and including) `valenceBandTop`, c over `nConduction` conduction
/// bands above it, k over a user Monkhorst-Pack mesh. The resonant-block BSE
/// Hamiltonian is
///
///     H_BSE(vck, v'c'k') = (E_c(k) - E_v(k)) delta_{vv'} delta_{cc'} delta_{kk'}
///                          + K(vck, v'c'k'),
///
/// with the electron-hole kernel K = -W (screened direct) + 2*v (bare
/// exchange, singlets only; 0 for triplets). Diagonalizing gives the exciton
/// energies E_S and eigenvectors (envelope amplitudes) A_S(vck).
///
/// THE APPROXIMATION, STATED PLAINLY (mirroring CrpaSolver's own "the price
/// of that choice" section, since the same honesty is owed here). A genuine
/// ab-initio BSE kernel needs the plane-wave (or real-space Wannier
/// FUNCTION, not just centre) exchange-correlation-density matrix elements
/// <ck|e^{iqr}|c'k'> — data this module does not have (Calango's Wannier
/// construction reports centres, spreads and cube files, not the full
/// analytic overlap machinery a plane-wave BSE code builds from GW output).
/// This solver instead uses the standard "point charge at the Wannier
/// centre" / envelope-function simplification: each valence-conduction
/// PAIR at k is treated as a single point dipole, and the kernel couples
/// pairs of the SAME band character (v==v', c==c') at different k through a
/// MODEL reciprocal-space Coulomb potential v(q):
///
///     3D:  v(q) = 4*pi*ke2 / (epsilon_inf * q^2)              (StaticEpsilonInfinity)
///     2D:  v(q) = 2*pi*ke2 / (A_cell * q * (1 + r0*q))        (RytovaKeldysh, Task 4)
///
/// (ke2 = e^2/(4 pi eps0) = 14.399645 eV.A, the standard Gaussian-unit
/// Coulomb constant). This is EXACTLY the "effective-mass"/Wannier-Mott
/// limit of the BSE for a single, well-isolated valence-conduction pair —
/// which is also precisely the analytically solvable model the module is
/// unit-tested against (see BseSolverTest.cpp): with a single valence and a
/// single conduction band, no exchange, and this direct kernel, the
/// discrete lattice sum converges, as the k-mesh densifies, to the
/// continuum hydrogenic (3D) / non-hydrogenic Rytova-Keldysh (2D) exciton
/// series a textbook effective-mass treatment predicts. For a REAL,
/// multi-band material this is a documented, coarser approximation than a
/// full ab-initio W (no local-field effects, no genuine orbital-overlap
/// exchange density) — the pragmatic native approach the task instructions
/// call for, not a claim of GW-BSE-grade accuracy.
///
/// UNITS. Energies in eV, lengths in Angstrom, k in fractional reciprocal
/// coordinates (converted to Cartesian A^-1 internally via
/// WannierHamiltonian::reciprocal()).
class BseSolver {
public:
    enum class Spin { Singlet, Triplet };
    enum class Dimensionality { Bulk3D, Slab2D };

    struct Options {
        /// Monkhorst-Pack mesh the electron-hole basis is built over. Exciton
        /// binding converges SLOWLY with this — the standard caveat, shown
        /// in the UI, not hidden in a tooltip only.
        std::array<int, 3> kmesh{8, 8, 8};
        /// 0-based index (ascending-energy convention, at the mesh's Gamma
        /// point) of the highest valence band included.
        int valenceBandTop = 0;
        /// How many bands below (and including) valenceBandTop, and above
        /// it, span the electron-hole basis.
        int nValence = 1;
        int nConduction = 1;

        Spin spin = Spin::Singlet;

        Dimensionality dimensionality = Dimensionality::Bulk3D;
        /// 3D (StaticEpsilonInfinity): the macroscopic dielectric constant
        /// the direct term is screened by. Ignored for Slab2D.
        double epsilonInfinity = 1.0;
        /// 2D (RytovaKeldysh, Task 4): the 2D screening length r0 = Angstrom,
        /// commonly obtained from the layer's 2D polarizability
        /// (r0 = 2*pi*alpha_2D). r0 = 0 recovers the bare (unscreened) 2D
        /// Coulomb potential. Ignored for Bulk3D.
        double keldyshR0Angstrom = 0.0;
        /// Effective substrate/environment dielectric constant, the mean of
        /// the media above and below the sheet: epsilon_env =
        /// (epsilon_above + epsilon_below) / 2, folded into the
        /// Rytova-Keldysh denominator as an additional screening background.
        /// 1.0 (vacuum on both sides) is the default, freestanding case.
        double environmentEpsilon = 1.0;

        /// How many of the lowest exciton states to report.
        int lowestExcitons = 10;
        /// Broadening (Gaussian sigma, eV) for the absorption spectra.
        double broadeningEv = 0.05;
        /// Energy window for the absorption spectra, relative to the
        /// smallest transition energy in the basis.
        double spectrumWindowEv = 3.0;
        int spectrumPoints = 400;

        /// Above this basis dimension, solve() uses the iterative Lanczos
        /// path (only `lowestExcitons` states) instead of full dense
        /// diagonalization.
        std::size_t denseSizeLimit = 400;
        int lanczosIterations = 120;
    };

    struct Exciton {
        /// Total energy E_S, eV (this IS the eigenvalue of H_BSE; since the
        /// diagonal already carries E_c(k)-E_v(k), no extra offset is added).
        double energy = 0.0;
        /// E_S minus the minimum direct gap in the electron-hole basis —
        /// negative for a bound state, the conventional sign.
        double bindingEnergy = 0.0;
        /// Relative oscillator strength (see the class doc's "oscillator
        /// strengths" note below for exactly what is and is not calibrated).
        double oscillatorStrength = 0.0;
        /// BSE amplitude A_S(vck), same (v,c,k) ordering as basisStates().
        std::vector<std::complex<double>> amplitude;
    };

    struct BasisState {
        int valenceBand = 0;
        int conductionBand = 0;
        std::array<double, 3> kFractional{0.0, 0.0, 0.0};
        double energy = 0.0; ///< E_c(k) - E_v(k), eV
    };

    struct Spectrum {
        std::vector<double> energiesEv;
        std::vector<double> excitonic;           ///< with electron-hole binding
        std::vector<double> independentParticle; ///< the same transitions, unbound
    };

    struct Result {
        std::vector<Exciton> excitons; ///< ascending energy, up to lowestExcitons
        double minimumDirectGapEv = 0.0;
        Spectrum spectrum;
        std::size_t basisDimension = 0;
        bool usedIterativeSolver = false;
        double estimatedDenseMemoryMiB = 0.0;
    };

    BseSolver(WannierHamiltonian hamiltonian, Options options);

    /// The electron-hole basis this Options selects, without diagonalizing
    /// anything — what the UI's size/memory warning is computed from before
    /// the (possibly expensive) solve() runs.
    std::vector<BasisState> basisStates() const;

    /// Estimated dense-matrix memory for the current basis, MiB (16 bytes
    /// per complex entry, N^2 entries).
    double estimatedDenseMemoryMiB() const;

    /// Build H_BSE and diagonalize it (dense or Lanczos, per
    /// Options::denseSizeLimit), then assemble the exciton list and both
    /// absorption spectra.
    Result solve() const;

    /// The dense H_BSE matrix itself, for diagnostics and tests — in
    /// particular, checking Hermiticity directly (h[i][j] == conj(h[j][i]))
    /// on a small synthetic basis, which solve()'s own dense/Lanczos
    /// eigensolvers cannot distinguish from a non-Hermitian input (both
    /// silently return real numbers regardless). Same construction solve()
    /// itself uses internally — not a second, parallel implementation that
    /// could drift from it.
    std::vector<std::vector<std::complex<double>>> hamiltonianForTesting() const;

    /// Oscillator-strength-weighted transition dipole magnitude squared for
    /// one basis state, |p_vc(k)|^2 summed over the 3 Cartesian directions
    /// (eV^2.A^2) -- exposed for testing the interband-dipole machinery
    /// (WannierHamiltonian::gradient()) independent of the full BSE.
    double transitionDipoleSquared(const BasisState& state) const;

    const WannierHamiltonian& hamiltonian() const { return hamiltonian_; }
    const Options& options() const { return options_; }

private:
    std::vector<std::array<std::complex<double>, 3>> computeDipoles(
        const std::vector<BasisState>& states) const;
    std::vector<std::vector<std::complex<double>>> buildHamiltonian(
        const std::vector<BasisState>& states,
        const std::vector<std::array<std::complex<double>, 3>>& dipoles) const;

    WannierHamiltonian hamiltonian_;
    Options options_;
};

/// The kernel matrix elements alone, exposed as free functions so
/// BseSolverTest.cpp can check Hermiticity and the singlet/triplet switch
/// directly, without going through a full solve().
namespace bse_detail {

/// e^2/(4 pi eps0), the Gaussian-unit Coulomb constant, in eV.Angstrom.
inline constexpr double kCoulombEvAngstrom = 14.399645351950548;

/// 3D model-screened direct potential v(q) = 4*pi*ke2/(epsilon*q^2),
/// eV.Angstrom^3 -- divide by (Nk * cellVolume) for the BSE matrix element.
/// `qMinAngstromInverse` regularizes the q -> 0 divergence (the intra-k
/// diagonal term): the standard practical fix of evaluating v at the
/// smallest resolvable |q| on the mesh rather than at literal zero.
double staticScreenedPotential3D(double qAngstromInverse, double epsilonInfinity,
                                 double qMinAngstromInverse);

/// 2D Rytova-Keldysh potential v(q) = 2*pi*ke2/(A_cell*q*(1+r0*q)*epsilon_env),
/// eV (already area-normalized -- A_cell is the in-plane cell area, A^2).
double rytovaKeldyshPotential2D(double qAngstromInverse, double r0Angstrom,
                                double environmentEpsilon, double cellAreaAngstrom2,
                                double qMinAngstromInverse);

} // namespace bse_detail

} // namespace calango::core
