#pragma once

#include "dft/DftTypes.hpp"

#include <complex>
#include <cstddef>
#include <vector>

namespace calango::dft::linalg {

/// Dense symmetric/Hermitian linear algebra for the Kohn-Sham eigenproblem.
///
/// The generalised problem HC = SCE is the one step of an SCF cycle that
/// cannot be written down as a quadrature, and it is where a hand-rolled
/// routine is most likely to be quietly wrong: an eigensolver that returns
/// unsorted, non-orthogonal or subtly inaccurate vectors still produces a
/// density, still converges, and still prints an energy.
///
/// Two backends, one interface:
///
///   * LAPACK (`dsyev`/`dsygv`) when CMake finds it — Accelerate on macOS,
///     the system reference or OpenBLAS elsewhere. This is the production
///     path and what the engine uses when available.
///   * A cyclic Jacobi rotation solver built in here, always compiled.
///     Jacobi is the slowest respectable symmetric eigensolver (O(n³) with a
///     large constant) and the most trustworthy: it is unconditionally
///     backward stable, it needs no tridiagonal reduction to get wrong, and
///     it delivers small eigenvalues to high RELATIVE accuracy, which
///     tridiagonal QL does not. At the sizes a minimal numerical-atomic-orbital
///     basis produces — tens to a few hundred functions — its cost is
///     irrelevant next to the grid quadrature.
///
/// Keeping both is not indecision. The Jacobi path is what makes the LAPACK
/// path testable: `dft_linalg` runs the same problems through each and
/// requires them to agree, so a LAPACK that is missing, mis-linked, or
/// following a different convention is caught by a test rather than by a
/// wrong band structure.
///
/// Everything here is row-major and dense. Sparsity belongs to the assembler.
bool haveLapack();

/// Eigen-decomposition of a real symmetric matrix.
///
/// `matrix` is n×n row-major and is not modified. On success `eigenvalues` is
/// ascending and `eigenvectors` holds one eigenvector per COLUMN — that is,
/// eigenvectors[i * n + k] is component i of eigenvector k, which is the
/// layout the density matrix Σ_k f_k C_ik C_jk wants.
Outcome symmetricEigen(const std::vector<double>& matrix, std::size_t n,
                       std::vector<double>& eigenvalues,
                       std::vector<double>& eigenvectors);

/// Solve HC = SCE for a real symmetric H and a symmetric positive-definite S.
///
/// Done by CANONICAL ORTHOGONALISATION rather than by the Cholesky reduction
/// LAPACK's `dsygv` uses, and the difference matters for this basis. Atomic
/// orbitals on nearby atoms are not linearly independent — squeeze two atoms
/// together, or add diffuse functions, and S acquires eigenvalues at the
/// 10⁻⁶ level and below. Cholesky of such an S is a factorisation of a matrix
/// that is numerically singular, and the resulting orbitals are noise
/// amplified by 1/√s. Canonical orthogonalisation instead diagonalises S,
/// DISCARDS the directions whose eigenvalue is below `overlapThreshold`, and
/// works in the surviving subspace — the basis silently shrinks by exactly
/// the rank it had lost anyway.
///
/// `discarded` reports how many directions were dropped. A caller that sees a
/// nonzero count has a basis problem, not an eigensolver problem, and this is
/// the only place that can tell it so.
Outcome solveGeneralized(const std::vector<double>& hamiltonian,
                         const std::vector<double>& overlap, std::size_t n,
                         std::vector<double>& eigenvalues,
                         std::vector<double>& eigenvectors,
                         std::size_t* discarded = nullptr,
                         double overlapThreshold = 1.0e-8);

/// Solve HC = SCE for a Hermitian H(k) and S(k), the form every k-point other
/// than a real one takes.
///
/// Implemented by the standard real embedding: a Hermitian A + iB with A
/// symmetric and B antisymmetric maps to the 2n×2n real symmetric
///
///     [  A  -B ]
///     [  B   A ]
///
/// whose spectrum is that of the Hermitian matrix, each eigenvalue twice. So
/// the complex problem is solved by the real solver above, with every second
/// eigenvalue kept — no separate complex eigensolver to write, to test, or to
/// get wrong, at the cost of a factor of eight in a step that is not the
/// bottleneck.
///
/// Matrices are row-major n×n; eigenvectors come back one per column.
Outcome solveGeneralizedHermitian(
    const std::vector<std::complex<double>>& hamiltonian,
    const std::vector<std::complex<double>>& overlap, std::size_t n,
    std::vector<double>& eigenvalues,
    std::vector<std::complex<double>>& eigenvectors,
    std::size_t* discarded = nullptr, double overlapThreshold = 1.0e-8);

/// Solve the small dense linear system A x = b by Gaussian elimination with
/// partial pivoting. Used by the Pulay/DIIS extrapolation and by the
/// least-squares fits in the atomic solver; `false` means A was singular to
/// working precision, which for DIIS is a normal event at convergence and not
/// an error.
bool solveLinearSystem(std::vector<double> a, std::vector<double> b,
                       std::size_t n, std::vector<double>& x);

} // namespace calango::dft::linalg
