#include "dft/LinearAlgebra.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#ifdef CALANGO_DFT_HAVE_LAPACK
extern "C" {
// Reference LAPACK, the same symbols Accelerate and OpenBLAS export.
void dsyev_(const char* jobz, const char* uplo, const int* n, double* a,
            const int* lda, double* w, double* work, const int* lwork,
            int* info);
}
#endif

namespace calango::dft::linalg {
namespace {

/// Cyclic Jacobi eigenvalue iteration for a real symmetric matrix.
///
/// Repeatedly annihilates the largest off-diagonal element by a plane
/// rotation. Each rotation is orthogonal, so the accumulated transform is
/// orthogonal to machine precision by construction — there is no
/// re-orthogonalisation step that could be skipped and no tridiagonal
/// intermediate whose deflation could go wrong. It stops when the off-diagonal
/// Frobenius norm is at the rounding level of the diagonal, which is the only
/// place it CAN stop.
///
/// `a` is modified in place (becomes diagonal); `v` accumulates eigenvectors
/// in columns.
bool jacobiEigen(std::vector<double>& a, std::size_t n, std::vector<double>& v)
{
    v.assign(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        v[i * n + i] = 1.0;
    if (n <= 1)
        return true;

    const auto offDiagonalNorm = [&a, n] {
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = i + 1; j < n; ++j)
                sum += a[i * n + j] * a[i * n + j];
        return std::sqrt(2.0 * sum);
    };
    double diagonalScale = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        diagonalScale = std::max(diagonalScale, std::abs(a[i * n + i]));
    // A matrix of all zeros has no scale of its own; anchor the tolerance so
    // the loop still terminates rather than chasing an unreachable relative
    // target.
    const double tolerance =
        1.0e-14 * std::max(diagonalScale, offDiagonalNorm()) + 1.0e-300;

    // 100 sweeps is far beyond the 6-10 a cyclic sweep needs for quadratic
    // convergence; it exists so a pathological input fails as `false` rather
    // than as a hang.
    for (int sweep = 0; sweep < 100; ++sweep) {
        if (offDiagonalNorm() <= tolerance)
            return true;
        for (std::size_t p = 0; p < n - 1; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                const double apq = a[p * n + q];
                if (std::abs(apq) <= 1.0e-300)
                    continue;
                const double app = a[p * n + p];
                const double aqq = a[q * n + q];
                // t = tan of the rotation angle, from the stable root of
                // t² + 2θt − 1 = 0 — the smaller root, which keeps the
                // rotation below 45° and the transform well conditioned.
                const double theta = 0.5 * (aqq - app) / apq;
                const double t = (theta >= 0.0 ? 1.0 : -1.0)
                    / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0);
                const double s = t * c;
                for (std::size_t k = 0; k < n; ++k) {
                    const double akp = a[k * n + p];
                    const double akq = a[k * n + q];
                    a[k * n + p] = c * akp - s * akq;
                    a[k * n + q] = s * akp + c * akq;
                }
                for (std::size_t k = 0; k < n; ++k) {
                    const double apk = a[p * n + k];
                    const double aqk = a[q * n + k];
                    a[p * n + k] = c * apk - s * aqk;
                    a[q * n + k] = s * apk + c * aqk;
                }
                for (std::size_t k = 0; k < n; ++k) {
                    const double vkp = v[k * n + p];
                    const double vkq = v[k * n + q];
                    v[k * n + p] = c * vkp - s * vkq;
                    v[k * n + q] = s * vkp + c * vkq;
                }
            }
        }
    }
    return false;
}

/// Sort eigenpairs ascending. Jacobi produces them in no particular order and
/// every caller here assumes "occupied first".
void sortAscending(std::vector<double>& values, std::vector<double>& vectors,
                   std::size_t n)
{
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(),
              [&values](std::size_t x, std::size_t y) {
                  return values[x] < values[y];
              });
    std::vector<double> sortedValues(n);
    std::vector<double> sortedVectors(n * n);
    for (std::size_t k = 0; k < n; ++k) {
        sortedValues[k] = values[order[k]];
        for (std::size_t i = 0; i < n; ++i)
            sortedVectors[i * n + k] = vectors[i * n + order[k]];
    }
    values.swap(sortedValues);
    vectors.swap(sortedVectors);
}

} // namespace

bool haveLapack()
{
#ifdef CALANGO_DFT_HAVE_LAPACK
    return true;
#else
    return false;
#endif
}

Outcome symmetricEigen(const std::vector<double>& matrix, std::size_t n,
                       std::vector<double>& eigenvalues,
                       std::vector<double>& eigenvectors)
{
    if (n == 0 || matrix.size() != n * n)
        return Outcome::invalid("symmetricEigen: matrix is not n x n");

    eigenvalues.assign(n, 0.0);

#ifdef CALANGO_DFT_HAVE_LAPACK
    {
        // LAPACK is column-major; a symmetric matrix is its own transpose, so
        // the row-major buffer can be handed over unchanged. The eigenvectors
        // come back in LAPACK's columns, which are this code's rows — hence
        // the transpose on the way out.
        std::vector<double> a(matrix);
        const int order = static_cast<int>(n);
        int info = 0;
        double probe = 0.0;
        int lwork = -1;
        dsyev_("V", "U", &order, a.data(), &order, eigenvalues.data(), &probe,
               &lwork, &info);
        if (info == 0) {
            lwork = std::max(static_cast<int>(probe), 3 * order);
            std::vector<double> work(static_cast<std::size_t>(lwork));
            dsyev_("V", "U", &order, a.data(), &order, eigenvalues.data(),
                   work.data(), &lwork, &info);
        }
        if (info == 0) {
            eigenvectors.assign(n * n, 0.0);
            for (std::size_t k = 0; k < n; ++k)
                for (std::size_t i = 0; i < n; ++i)
                    eigenvectors[i * n + k] = a[k * n + i];
            return Outcome::success(); // dsyev already sorts ascending
        }
        // Falls through to Jacobi: a LAPACK failure is rare enough to be worth
        // a second opinion rather than an abort.
    }
#endif

    std::vector<double> work(matrix);
    if (!jacobiEigen(work, n, eigenvectors))
        return {Status::NumericalFailure,
                "symmetric eigenproblem did not converge in 100 Jacobi sweeps"};
    for (std::size_t i = 0; i < n; ++i)
        eigenvalues[i] = work[i * n + i];
    sortAscending(eigenvalues, eigenvectors, n);
    return Outcome::success();
}

Outcome solveGeneralized(const std::vector<double>& hamiltonian,
                         const std::vector<double>& overlap, std::size_t n,
                         std::vector<double>& eigenvalues,
                         std::vector<double>& eigenvectors,
                         std::size_t* discarded, double overlapThreshold)
{
    if (n == 0 || hamiltonian.size() != n * n || overlap.size() != n * n)
        return Outcome::invalid("solveGeneralized: matrices are not n x n");

    // 1. Diagonalise S.
    std::vector<double> sValues;
    std::vector<double> sVectors;
    const Outcome sOutcome = symmetricEigen(overlap, n, sValues, sVectors);
    if (!sOutcome.ok())
        return sOutcome;

    // 2. Keep only the well-conditioned directions. A negative eigenvalue in
    //    an overlap matrix is not a small basis problem, it is a broken
    //    quadrature — S is a Gram matrix and cannot have one — so it is worth
    //    the separate diagnosis.
    const double largest = sValues.back();
    if (largest <= 0.0)
        return {Status::NumericalFailure,
                "overlap matrix is not positive definite (largest eigenvalue "
                "is not positive) — the integration grid is not resolving the "
                "basis functions"};
    std::vector<std::size_t> kept;
    kept.reserve(n);
    for (std::size_t k = 0; k < n; ++k) {
        if (sValues[k] > overlapThreshold * largest)
            kept.push_back(k);
    }
    if (discarded)
        *discarded = n - kept.size();
    if (kept.empty())
        return {Status::NumericalFailure,
                "every overlap eigenvalue is below the linear-dependence "
                "threshold"};
    const std::size_t m = kept.size();

    // 3. X = U s^{-1/2} restricted to the kept directions: an n×m matrix whose
    //    columns are an orthonormal basis of the surviving subspace.
    std::vector<double> x(n * m, 0.0);
    for (std::size_t c = 0; c < m; ++c) {
        const double scale = 1.0 / std::sqrt(sValues[kept[c]]);
        for (std::size_t i = 0; i < n; ++i)
            x[i * m + c] = sVectors[i * n + kept[c]] * scale;
    }

    // 4. H' = Xᵀ H X, an ordinary symmetric eigenproblem in m dimensions.
    std::vector<double> hx(n * m, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t c = 0; c < m; ++c) {
            double sum = 0.0;
            for (std::size_t j = 0; j < n; ++j)
                sum += hamiltonian[i * n + j] * x[j * m + c];
            hx[i * m + c] = sum;
        }
    std::vector<double> hPrime(m * m, 0.0);
    for (std::size_t a = 0; a < m; ++a)
        for (std::size_t b = a; b < m; ++b) {
            double sum = 0.0;
            for (std::size_t i = 0; i < n; ++i)
                sum += x[i * m + a] * hx[i * m + b];
            hPrime[a * m + b] = sum;
            hPrime[b * m + a] = sum;
        }

    std::vector<double> primeVectors;
    const Outcome hOutcome =
        symmetricEigen(hPrime, m, eigenvalues, primeVectors);
    if (!hOutcome.ok())
        return hOutcome;

    // 5. Back-transform: C = X C'. The result satisfies CᵀSC = I on the kept
    //    subspace, which is what makes the density matrix below trace to the
    //    electron count.
    eigenvectors.assign(n * m, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = 0; k < m; ++k) {
            double sum = 0.0;
            for (std::size_t c = 0; c < m; ++c)
                sum += x[i * m + c] * primeVectors[c * m + k];
            eigenvectors[i * m + k] = sum;
        }
    return Outcome::success();
}

Outcome solveGeneralizedHermitian(
    const std::vector<std::complex<double>>& hamiltonian,
    const std::vector<std::complex<double>>& overlap, std::size_t n,
    std::vector<double>& eigenvalues,
    std::vector<std::complex<double>>& eigenvectors, std::size_t* discarded,
    double overlapThreshold)
{
    if (n == 0 || hamiltonian.size() != n * n || overlap.size() != n * n)
        return Outcome::invalid(
            "solveGeneralizedHermitian: matrices are not n x n");

    const std::size_t big = 2 * n;
    const auto embed = [n, big](const std::vector<std::complex<double>>& src,
                                std::vector<double>& dst) {
        dst.assign(big * big, 0.0);
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j) {
                const double re = src[i * n + j].real();
                const double im = src[i * n + j].imag();
                dst[i * big + j] = re;
                dst[(i + n) * big + (j + n)] = re;
                dst[i * big + (j + n)] = -im;
                dst[(i + n) * big + j] = im;
            }
    };
    std::vector<double> h;
    std::vector<double> s;
    embed(hamiltonian, h);
    embed(overlap, s);

    std::vector<double> bigValues;
    std::vector<double> bigVectors;
    std::size_t bigDiscarded = 0;
    const Outcome outcome = solveGeneralized(h, s, big, bigValues, bigVectors,
                                             &bigDiscarded, overlapThreshold);
    if (!outcome.ok())
        return outcome;

    // Every eigenvalue of the embedding is doubly degenerate, the pair being
    // (x, y) and (-y, x) — the same complex vector times i. Taking every
    // second one after the ascending sort recovers the Hermitian spectrum;
    // the discarded count halves for the same reason.
    const std::size_t bigKept = bigVectors.size() / big;
    const std::size_t m = bigKept / 2;
    if (discarded)
        *discarded = bigDiscarded / 2;
    eigenvalues.assign(m, 0.0);
    eigenvectors.assign(n * m, {});
    for (std::size_t k = 0; k < m; ++k) {
        eigenvalues[k] = bigValues[2 * k];
        for (std::size_t i = 0; i < n; ++i) {
            eigenvectors[i * m + k] =
                std::complex<double>(bigVectors[i * bigKept + 2 * k],
                                     bigVectors[(i + n) * bigKept + 2 * k]);
        }
        // Fix the arbitrary global phase so two runs of the same calculation
        // produce the same coefficients: rotate the largest component real
        // and positive. Nothing physical depends on it; reproducible output
        // does.
        std::size_t pivot = 0;
        double best = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double magnitude = std::abs(eigenvectors[i * m + k]);
            if (magnitude > best) {
                best = magnitude;
                pivot = i;
            }
        }
        if (best > 1.0e-12) {
            const std::complex<double> phase =
                std::conj(eigenvectors[pivot * m + k]) / best;
            for (std::size_t i = 0; i < n; ++i)
                eigenvectors[i * m + k] *= phase;
        }
    }
    return Outcome::success();
}

bool solveLinearSystem(std::vector<double> a, std::vector<double> b,
                       std::size_t n, std::vector<double>& x)
{
    if (n == 0 || a.size() != n * n || b.size() != n)
        return false;
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        double best = std::abs(a[col * n + col]);
        for (std::size_t row = col + 1; row < n; ++row) {
            const double candidate = std::abs(a[row * n + col]);
            if (candidate > best) {
                best = candidate;
                pivot = row;
            }
        }
        if (best < 1.0e-14)
            return false;
        if (pivot != col) {
            for (std::size_t k = 0; k < n; ++k)
                std::swap(a[col * n + k], a[pivot * n + k]);
            std::swap(b[col], b[pivot]);
        }
        const double diagonal = a[col * n + col];
        for (std::size_t row = col + 1; row < n; ++row) {
            const double factor = a[row * n + col] / diagonal;
            if (factor == 0.0)
                continue;
            for (std::size_t k = col; k < n; ++k)
                a[row * n + k] -= factor * a[col * n + k];
            b[row] -= factor * b[col];
        }
    }
    x.assign(n, 0.0);
    for (std::size_t i = n; i-- > 0;) {
        double sum = b[i];
        for (std::size_t k = i + 1; k < n; ++k)
            sum -= a[i * n + k] * x[k];
        x[i] = sum / a[i * n + i];
    }
    return true;
}

} // namespace calango::dft::linalg
