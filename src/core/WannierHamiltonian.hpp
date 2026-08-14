#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include <array>
#include <complex>
#include <string>
#include <vector>

namespace calango::core {

/// Small dense complex linear algebra, shared by everything that works in the
/// Wannier basis.
///
/// Self-contained rather than LAPACK: `src/core` links no LAPACK today (only
/// the native-DFT target does), these matrices are the size of a Wannier basis
/// — a handful to a few tens — and Jacobi is backward stable, so the
/// dependency would buy nothing but a link-order problem.
namespace linalg {

using Cplx = std::complex<double>;
using CMatrix = std::vector<std::vector<Cplx>>;

CMatrix identity(std::size_t n);
CMatrix multiply(const CMatrix& a, const CMatrix& b);
/// Gauss-Jordan inverse with partial pivoting. Throws on a singular matrix.
CMatrix invert(CMatrix a);
/// Eigen-decomposition of a Hermitian matrix by cyclic Jacobi. Eigenvalues
/// ascending; the COLUMNS of `vectors` are the eigenvectors.
void hermitianEigen(CMatrix a, std::vector<double>& values, CMatrix& vectors);

} // namespace linalg

/// A tight-binding Hamiltonian in a maximally-localized Wannier basis, and the
/// interpolation machinery that goes with it.
///
/// NATIVE, AND DELIBERATELY SO. Wannier90 / postw90 are the reference for the
/// conventions below — the H(R) layout, the Wang-Yates-Souza-Vanderbilt
/// interpolation formulas — but nothing here calls them, links them, or needs
/// any file they produce. The only input is H(R) plus a cell, which Calango
/// parses itself; a user who already has a `_hr.dat` can supply one, and a
/// user who does not can build the model in memory.
///
/// CONVENTIONS
///
///   H(k) = Σ_R e^{i k·R} H(R),   k·R = 2π (k_frac · n)
///
/// with R = Σ_i n_i a_i and n the integer lattice triple. The phase therefore
/// needs only the fractional k and the integer n; the Cartesian cell enters
/// only through the GRADIENT, where R must be a real length.
///
///   ∂H/∂k_α = Σ_R i R_α e^{i k·R} H(R)          (Wannier gauge, eV·Å)
///
/// Taken analytically rather than by finite differences on the eigenvalues.
/// That is not an optimisation: at a band crossing the eigenvalue branches
/// swap, so a finite difference of sorted eigenvalues reports a velocity that
/// jumps discontinuously and can have the wrong sign entirely. The matrix
/// gradient has no such problem — the degeneracy lives in the eigenvectors,
/// where it belongs.
///
/// UNITS. H in eV, cell in Å, so ∂H/∂k is eV·Å and a band velocity is
/// (1/ħ)∂ε/∂k. `kVelocitySI` converts eV·Å to m/s.
class WannierHamiltonian {
public:
    /// (1/ħ) × 1 eV·Å, in m/s: multiply ∂ε/∂k [eV·Å] by this to get a velocity.
    static constexpr double kVelocitySI = 1.5193e5;

    /// One real-space block H(R), as a wannier90 `_hr.dat` lists them.
    struct HoppingBlock {
        /// Lattice vector R in integer (fractional) units.
        std::array<int, 3> lattice{0, 0, 0};
        /// Row-major n×n block of H(R) in eV.
        std::vector<double> matrix;
        /// Imaginary part, row-major, same size. Empty means a real block,
        /// which is the usual case for a time-reversal-symmetric system.
        std::vector<double> imaginary;
    };

    /// Eigenvalues, eigenvectors and band velocities at one k.
    struct Bands {
        std::vector<double> energies;          ///< eV, ascending
        linalg::CMatrix vectors;               ///< columns are states
        /// [band][alpha] in eV·Å — that is ∂ε_n/∂k, NOT yet divided by ħ.
        std::vector<std::array<double, 3>> gradients;
    };

    WannierHamiltonian() = default;
    WannierHamiltonian(std::size_t orbitals,
                       std::array<std::array<double, 3>, 3> cell,
                       std::vector<HoppingBlock> hoppings);

    std::size_t orbitals() const { return orbitals_; }
    const std::array<std::array<double, 3>, 3>& cell() const { return cell_; }
    const std::vector<HoppingBlock>& hoppings() const { return hoppings_; }
    /// Cell volume in Å³.
    double volume() const;
    /// Reciprocal lattice vectors (2π convention), Å⁻¹.
    std::array<std::array<double, 3>, 3> reciprocal() const;

    /// H(k) at fractional k, Hermitised.
    linalg::CMatrix hamiltonian(const std::array<double, 3>& kFractional) const;
    /// ∂H/∂k_α at fractional k, in the Wannier gauge, eV·Å. One matrix per
    /// Cartesian direction.
    std::array<linalg::CMatrix, 3> gradient(
        const std::array<double, 3>& kFractional) const;

    /// Diagonalise, and (when `withVelocities`) contract the gradient into
    /// band velocities: ∂ε_n/∂k_α = ⟨n| ∂H/∂k_α |n⟩, the Hellmann-Feynman
    /// result, exact for a non-degenerate band and the correct band-resolved
    /// average within a degenerate multiplet.
    Bands bands(const std::array<double, 3>& kFractional,
                bool withVelocities = true) const;

    /// Regular Γ-centred mesh of fractional k-points.
    static std::vector<std::array<double, 3>> monkhorstPack(
        const std::array<int, 3>& mesh);

    /// Parse a wannier90-format `_hr.dat`. Calango's own parser — the file is
    /// a plain text table, and reading one the user already has is not a
    /// dependency on the code that wrote it.
    static WannierHamiltonian fromHrDat(
        const std::string& path, std::array<std::array<double, 3>, 3> cell,
        std::string* error = nullptr);

private:
    std::size_t orbitals_ = 0;
    std::array<std::array<double, 3>, 3> cell_{
        {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    std::vector<HoppingBlock> hoppings_;
};

} // namespace calango::core
