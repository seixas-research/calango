#include "core/ClusterExpansionFit.hpp"

#include "core/LocaleSafeNumber.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
// <cstdint> is NOT unused, whatever clangd says on macOS: libstdc++ from GCC 13
// stopped including it transitively and the Debian package build fails without
// it. Do not let an IDE fix-it remove this line.
#include <cstdint>
#include <functional> // std::greater — not transitive on libstdc++
#include <limits>
#include <locale>     // std::locale::classic — not transitive on libstdc++
#include <numeric>
#include <random>
#include <sstream>
#include <system_error>

namespace calango::core {

namespace {

// ---------------------------------------------------------------------------
// Why this file contains its own linear algebra
// ---------------------------------------------------------------------------
//
// The project has exactly one dense linear-algebra helper, `src/dft/
// LinearAlgebra.{hpp,cpp}` (symmetric/generalised eigenproblems, plus a
// Gaussian-elimination linear solve, optionally backed by LAPACK). It is not
// reused here for three reasons, in order of weight:
//
//   1. DIRECTION. `src/core` is the bottom of the dependency graph — the DFT
//      engine includes core, not the other way round. Making a core module
//      depend on `src/dft` would invert that and drag `DftTypes.hpp` (and,
//      through the LAPACK option, a link-time dependency) into every target
//      that fits an ECI, including the two-object test.
//   2. IT DOES NOT HAVE THE OPERATION. What a ridge path needs is a SINGULAR
//      value decomposition of a rectangular, rank-deficient design matrix.
//      `solveLinearSystem` is Gaussian elimination on a square system, i.e.
//      exactly the normal-equation route this module exists to avoid, and
//      `symmetricEigen` would only get there via XᵀX — squaring the condition
//      number of a matrix that was already the problem.
//   3. SIZE. A CE design matrix is tens of configurations by tens of orbits.
//      An O(n³) method with a bad constant is free at that size, so there is
//      nothing to buy by linking a BLAS.
//
// So: a self-contained ONE-SIDED JACOBI SVD (below) plus, for ARD only, a
// Cholesky factorisation. Jacobi rather than Golub-Reinsch bidiagonalisation
// because it is ~40 lines instead of ~400, is unconditionally backward stable,
// needs no rank decisions partway through, and — the property that matters for
// a near-collinear CE design — computes SMALL singular values to high RELATIVE
// accuracy, where the QR/bidiagonal route loses them into the noise of σ_max.
// Cholesky is used for ARD and only for ARD, because there the matrix being
// factorised is diag(α) + β XᵀX with every α strictly positive: it is SPD by
// construction, not by hope, which is precisely what cannot be said of the raw
// normal equations.

constexpr double kEps = std::numeric_limits<double>::epsilon();

double dotColumns(const double* a, const double* b, int m)
{
    double s = 0.0;
    for (int i = 0; i < m; ++i)
        s += a[i] * b[i];
    return s;
}

// ---------------------------------------------------------------------------
// Cholesky (ARD only)
// ---------------------------------------------------------------------------

/// In-place Cholesky A = L Lᵀ, `a` row-major n×n, lower triangle written.
/// Returns false when a pivot is non-positive, which for the ARD system means
/// the evidence update has diverged and the caller must stop rather than
/// continue with a meaningless posterior.
bool choleskyFactor(std::vector<double>& a, int n)
{
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j <= i; ++j) {
            double sum = a[static_cast<std::size_t>(i) * n + j];
            for (int k = 0; k < j; ++k)
                sum -= a[static_cast<std::size_t>(i) * n + k]
                    * a[static_cast<std::size_t>(j) * n + k];
            if (i == j) {
                if (!(sum > 0.0))
                    return false;
                a[static_cast<std::size_t>(i) * n + i] = std::sqrt(sum);
            } else {
                a[static_cast<std::size_t>(i) * n + j] =
                    sum / a[static_cast<std::size_t>(j) * n + j];
            }
        }
    }
    return true;
}

/// Inverse of an SPD matrix through its Cholesky factor. ARD needs the full
/// inverse, not just a solve: the relevance update reads the DIAGONAL of the
/// posterior covariance, which is what tells one coefficient from another.
bool choleskyInverse(std::vector<double> a, int n, std::vector<double>& inv)
{
    if (!choleskyFactor(a, n))
        return false;
    inv.assign(static_cast<std::size_t>(n) * n, 0.0);
    std::vector<double> col(static_cast<std::size_t>(n));
    for (int c = 0; c < n; ++c) {
        std::fill(col.begin(), col.end(), 0.0);
        col[static_cast<std::size_t>(c)] = 1.0;
        for (int i = 0; i < n; ++i) { // forward substitution, L z = e_c
            double sum = col[static_cast<std::size_t>(i)];
            for (int k = 0; k < i; ++k)
                sum -= a[static_cast<std::size_t>(i) * n + k]
                    * col[static_cast<std::size_t>(k)];
            col[static_cast<std::size_t>(i)] =
                sum / a[static_cast<std::size_t>(i) * n + i];
        }
        for (int i = n - 1; i >= 0; --i) { // back substitution, Lᵀ x = z
            double sum = col[static_cast<std::size_t>(i)];
            for (int k = i + 1; k < n; ++k)
                sum -= a[static_cast<std::size_t>(k) * n + i]
                    * col[static_cast<std::size_t>(k)];
            col[static_cast<std::size_t>(i)] =
                sum / a[static_cast<std::size_t>(i) * n + i];
        }
        for (int i = 0; i < n; ++i)
            inv[static_cast<std::size_t>(i) * n + c] =
                col[static_cast<std::size_t>(i)];
    }
    return true;
}

// ---------------------------------------------------------------------------
// Standardisation
// ---------------------------------------------------------------------------

/// A training set reduced to the form every penalised solver here expects:
/// columns centred (and optionally scaled), response centred, and dead columns
/// removed.
struct Prepared {
    int rows = 0;
    int cols = 0; ///< kept columns
    std::vector<double> x; ///< rows×cols row-major, centred/scaled
    std::vector<double> y; ///< centred
    std::vector<int> keep; ///< original column index of each kept column
    std::vector<double> center;
    std::vector<double> scale;
    double yMean = 0.0;
};

/// Centre/scale the rows in `rowIndex` of `data`.
///
/// TRAP THIS GUARDS AGAINST: a CE correlation vector routinely contains a
/// CONSTANT column — the empty cluster is literally all ones, and at fixed
/// composition the point term is constant too. A constant column is exactly
/// collinear with the intercept, so the model is unidentifiable and XᵀX is
/// singular no matter how many configurations are added. Rather than let the
/// regularisation paper over it (ridge would silently split J₀ between the
/// intercept and that column, making the reported ECI meaningless), such
/// columns are DROPPED here and reported with an ECI of exactly zero; the
/// constant they carried lands in the intercept, which is where it belongs.
Prepared prepare(const std::vector<std::vector<double>>& data,
                 const std::vector<double>& y,
                 const std::vector<int>& rowIndex, int totalCols,
                 bool standardize)
{
    Prepared p;
    const int n = static_cast<int>(rowIndex.size());
    p.rows = n;
    if (n == 0)
        return p;

    std::vector<double> mean(static_cast<std::size_t>(totalCols), 0.0);
    for (int r = 0; r < n; ++r) {
        const std::vector<double>& row =
            data[static_cast<std::size_t>(rowIndex[static_cast<std::size_t>(r)])];
        for (int c = 0; c < totalCols; ++c)
            mean[static_cast<std::size_t>(c)] += row[static_cast<std::size_t>(c)];
    }
    for (double& m : mean)
        m /= n;

    std::vector<double> sd(static_cast<std::size_t>(totalCols), 0.0);
    for (int r = 0; r < n; ++r) {
        const std::vector<double>& row =
            data[static_cast<std::size_t>(rowIndex[static_cast<std::size_t>(r)])];
        for (int c = 0; c < totalCols; ++c) {
            const double d = row[static_cast<std::size_t>(c)]
                - mean[static_cast<std::size_t>(c)];
            sd[static_cast<std::size_t>(c)] += d * d;
        }
    }
    for (int c = 0; c < totalCols; ++c)
        sd[static_cast<std::size_t>(c)] =
            std::sqrt(sd[static_cast<std::size_t>(c)] / n);

    for (int c = 0; c < totalCols; ++c) {
        const double magnitude =
            1.0 + std::abs(mean[static_cast<std::size_t>(c)]);
        if (sd[static_cast<std::size_t>(c)] > 1.0e-12 * magnitude)
            p.keep.push_back(c);
    }
    p.cols = static_cast<int>(p.keep.size());

    p.center.resize(p.keep.size());
    p.scale.resize(p.keep.size());
    for (std::size_t j = 0; j < p.keep.size(); ++j) {
        p.center[j] = mean[static_cast<std::size_t>(p.keep[j])];
        p.scale[j] = standardize ? sd[static_cast<std::size_t>(p.keep[j])] : 1.0;
    }

    p.x.assign(static_cast<std::size_t>(n) * p.keep.size(), 0.0);
    for (int r = 0; r < n; ++r) {
        const std::vector<double>& row =
            data[static_cast<std::size_t>(rowIndex[static_cast<std::size_t>(r)])];
        for (std::size_t j = 0; j < p.keep.size(); ++j)
            p.x[static_cast<std::size_t>(r) * p.keep.size() + j] =
                (row[static_cast<std::size_t>(p.keep[j])] - p.center[j])
                / p.scale[j];
    }

    p.y.resize(static_cast<std::size_t>(n));
    double ysum = 0.0;
    for (int r = 0; r < n; ++r)
        ysum += y[static_cast<std::size_t>(rowIndex[static_cast<std::size_t>(r)])];
    p.yMean = ysum / n;
    for (int r = 0; r < n; ++r)
        p.y[static_cast<std::size_t>(r)] =
            y[static_cast<std::size_t>(rowIndex[static_cast<std::size_t>(r)])]
            - p.yMean;
    return p;
}

/// A fitted model in ORIGINAL column coordinates (undoing centre/scale), which
/// is the only form in which a fold's model can be applied to a held-out row.
struct Model {
    std::vector<double> beta; ///< one per original column
    double intercept = 0.0;
};

Model unstandardize(const Prepared& p, const std::vector<double>& scaled,
                    int totalCols)
{
    Model m;
    m.beta.assign(static_cast<std::size_t>(totalCols), 0.0);
    double shift = 0.0;
    for (std::size_t j = 0; j < p.keep.size(); ++j) {
        const double b = scaled[j] / p.scale[j];
        m.beta[static_cast<std::size_t>(p.keep[j])] = b;
        shift += b * p.center[j];
    }
    m.intercept = p.yMean - shift;
    return m;
}

double predict(const Model& m, const std::vector<double>& row)
{
    double v = m.intercept;
    for (std::size_t c = 0; c < row.size(); ++c)
        v += m.beta[c] * row[c];
    return v;
}

// ---------------------------------------------------------------------------
// ARD / sparse Bayesian regression
// ---------------------------------------------------------------------------

/// Evidence-maximisation (MacKay/Tipping) fit on already centred, scaled data.
///
/// One precision α_j per coefficient; α_j → ∞ means "this orbit is irrelevant"
/// and the coefficient is pruned to exactly zero. The attraction for a cluster
/// expansion is that there is NO regularisation parameter to choose — the data
/// sets the sparsity — which is why Bayesian CE (Mueller & Ceder, PRB 80,
/// 024103 (2009)) exists at all. The cost is that on a small, noisy training
/// set it can prune a real but weak interaction and never look back: unlike a
/// λ path there is no knob with which to inspect the alternative, so the CV
/// score reported for ARD is the only evidence the user gets.
std::vector<double> ardSolve(const std::vector<double>& x, int n, int p,
                             const std::vector<double>& y, int maxIterations,
                             double tolerance)
{
    std::vector<double> beta(static_cast<std::size_t>(p), 0.0);
    if (n == 0 || p == 0)
        return beta;

    // Gram matrix and Xᵀy once; every iteration only gathers submatrices.
    std::vector<double> gram(static_cast<std::size_t>(p) * p, 0.0);
    std::vector<double> xty(static_cast<std::size_t>(p), 0.0);
    for (int a = 0; a < p; ++a) {
        for (int b = a; b < p; ++b) {
            double s = 0.0;
            for (int r = 0; r < n; ++r)
                s += x[static_cast<std::size_t>(r) * p + a]
                    * x[static_cast<std::size_t>(r) * p + b];
            gram[static_cast<std::size_t>(a) * p + b] = s;
            gram[static_cast<std::size_t>(b) * p + a] = s;
        }
        double s = 0.0;
        for (int r = 0; r < n; ++r)
            s += x[static_cast<std::size_t>(r) * p + a]
                * y[static_cast<std::size_t>(r)];
        xty[static_cast<std::size_t>(a)] = s;
    }

    double yy = 0.0;
    for (int r = 0; r < n; ++r)
        yy += y[static_cast<std::size_t>(r)] * y[static_cast<std::size_t>(r)];
    const double yVar = yy / std::max(1, n);
    double noise = yVar > 0.0 ? 1.0 / yVar : 1.0e6; // β, the noise precision

    std::vector<double> alpha(static_cast<std::size_t>(p), 1.0);
    std::vector<int> active(static_cast<std::size_t>(p));
    std::iota(active.begin(), active.end(), 0);

    // α above this is numerically infinite: the corresponding coefficient is
    // smaller than any energy scale in the problem and only slows the solve.
    constexpr double kAlphaMax = 1.0e12;
    const int iterations = std::max(10, std::min(maxIterations, 2000));

    std::vector<double> mu, sigma, a;
    for (int iter = 0; iter < iterations; ++iter) {
        const int k = static_cast<int>(active.size());
        if (k == 0)
            break;
        a.assign(static_cast<std::size_t>(k) * k, 0.0);
        for (int i = 0; i < k; ++i)
            for (int j = 0; j < k; ++j)
                a[static_cast<std::size_t>(i) * k + j] = noise
                    * gram[static_cast<std::size_t>(active[static_cast<std::size_t>(i)]) * p
                           + active[static_cast<std::size_t>(j)]]
                    + (i == j ? alpha[static_cast<std::size_t>(
                           active[static_cast<std::size_t>(i)])]
                              : 0.0);
        if (!choleskyInverse(a, k, sigma))
            break;

        mu.assign(static_cast<std::size_t>(k), 0.0);
        for (int i = 0; i < k; ++i) {
            double s = 0.0;
            for (int j = 0; j < k; ++j)
                s += sigma[static_cast<std::size_t>(i) * k + j]
                    * xty[static_cast<std::size_t>(
                        active[static_cast<std::size_t>(j)])];
            mu[static_cast<std::size_t>(i)] = noise * s;
        }

        // γ_j ∈ [0,1] is how well-determined coefficient j is by the data.
        double gammaSum = 0.0;
        double maxChange = 0.0;
        std::vector<int> survivors;
        std::vector<double> newAlpha(static_cast<std::size_t>(k));
        for (int i = 0; i < k; ++i) {
            const int j = active[static_cast<std::size_t>(i)];
            const double aj = alpha[static_cast<std::size_t>(j)];
            const double sii = sigma[static_cast<std::size_t>(i) * k + i];
            const double gamma = std::clamp(1.0 - aj * sii, 1.0e-12, 1.0);
            gammaSum += gamma;
            const double mu2 = mu[static_cast<std::size_t>(i)]
                * mu[static_cast<std::size_t>(i)];
            const double updated =
                mu2 > 0.0 ? std::min(gamma / mu2, kAlphaMax * 10.0)
                          : kAlphaMax * 10.0;
            newAlpha[static_cast<std::size_t>(i)] = updated;
            maxChange = std::max(maxChange,
                                 std::abs(std::log(updated) - std::log(aj)));
        }

        // Residual for the noise update, computed from μ over the active set.
        double rss = 0.0;
        for (int r = 0; r < n; ++r) {
            double pred = 0.0;
            for (int i = 0; i < k; ++i)
                pred += x[static_cast<std::size_t>(r) * p
                          + active[static_cast<std::size_t>(i)]]
                    * mu[static_cast<std::size_t>(i)];
            const double d = y[static_cast<std::size_t>(r)] - pred;
            rss += d * d;
        }
        const double effective = std::max(1.0e-9, n - gammaSum);
        noise = rss > 0.0 ? effective / rss : 1.0e12;
        noise = std::clamp(noise, 1.0e-12, 1.0e12);

        for (int i = 0; i < k; ++i) {
            const int j = active[static_cast<std::size_t>(i)];
            alpha[static_cast<std::size_t>(j)] =
                newAlpha[static_cast<std::size_t>(i)];
            if (alpha[static_cast<std::size_t>(j)] < kAlphaMax)
                survivors.push_back(j);
        }
        std::fill(beta.begin(), beta.end(), 0.0);
        for (int i = 0; i < k; ++i)
            beta[static_cast<std::size_t>(active[static_cast<std::size_t>(i)])] =
                alpha[static_cast<std::size_t>(active[static_cast<std::size_t>(i)])]
                    < kAlphaMax
                ? mu[static_cast<std::size_t>(i)]
                : 0.0;

        const bool pruned = survivors.size() != active.size();
        active.swap(survivors);
        if (!pruned && maxChange < std::max(tolerance, 1.0e-9))
            break;
    }
    return beta;
}

// ---------------------------------------------------------------------------
// Path solving shared by the final fit and by every cross-validation fold
// ---------------------------------------------------------------------------

std::vector<Model> solvePath(const Prepared& p,
                             const std::vector<double>& lambdas,
                             const EciFitOptions& options, int totalCols)
{
    std::vector<Model> models;
    models.reserve(lambdas.size());
    if (p.cols == 0 || p.rows == 0) {
        // Nothing identifiable beyond the mean; the intercept alone is the model.
        for (std::size_t i = 0; i < lambdas.size(); ++i) {
            Model m;
            m.beta.assign(static_cast<std::size_t>(totalCols), 0.0);
            m.intercept = p.yMean;
            models.push_back(std::move(m));
        }
        return models;
    }

    if (options.method == EciMethod::Ard) {
        const std::vector<double> beta = ardSolve(p.x, p.rows, p.cols, p.y,
                                                  options.maxIterations,
                                                  options.tolerance);
        for (std::size_t i = 0; i < lambdas.size(); ++i)
            models.push_back(unstandardize(p, beta, totalCols));
        return models;
    }

    if (options.method == EciMethod::Ridge) {
        // ONE decomposition for the whole path — the reason ridge CV over a
        // 50-point path and 100 leave-one-out folds costs nothing.
        const ThinSvd svd = thinSvd(p.x, p.rows, p.cols);
        for (double lambda : lambdas) {
            // The public λ is defined against the 1/(2n)-normalised objective
            // so that it means the same thing for a fold of n−1 rows as for the
            // full set of n; the raw solver's objective has no 1/n, hence n·λ.
            const std::vector<double> beta =
                ridgeSolve(svd, p.y, lambda * p.rows);
            models.push_back(unstandardize(p, beta, totalCols));
        }
        return models;
    }

    LassoOptions lo;
    lo.maxIterations = options.maxIterations;
    lo.tolerance = options.tolerance;
    std::vector<double> warm(static_cast<std::size_t>(p.cols), 0.0);
    for (double lambda : lambdas) {
        const LassoSolution sol =
            lassoSolve(p.x, p.rows, p.cols, p.y, lambda * p.rows, lo, &warm);
        warm = sol.beta;
        models.push_back(unstandardize(p, sol.beta, totalCols));
    }
    return models;
}

std::string fixedText(double value, int decimals)
{
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                      std::chars_format::fixed, decimals);
    if (result.ec != std::errc())
        return localeSafeFormat(value);
    return std::string(buffer, result.ptr);
}

std::string scientificText(double value, int decimals)
{
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                      std::chars_format::scientific, decimals);
    if (result.ec != std::errc())
        return localeSafeFormat(value);
    return std::string(buffer, result.ptr);
}

std::string pad(std::string text, std::size_t width)
{
    if (text.size() < width)
        text.append(width - text.size(), ' ');
    return text;
}

} // namespace

// ---------------------------------------------------------------------------
// One-sided Jacobi SVD
// ---------------------------------------------------------------------------

ThinSvd thinSvd(const std::vector<double>& matrix, int rows, int cols)
{
    ThinSvd out;
    out.rows = rows;
    out.cols = cols;
    if (rows <= 0 || cols <= 0
        || matrix.size() != static_cast<std::size_t>(rows) * cols)
        return out;

    // One-sided Jacobi orthogonalises the COLUMNS of the working matrix, so it
    // wants the tall orientation. A CE design matrix is frequently wide (more
    // candidate orbits than configurations), so transpose when it is and swap
    // U and V at the end: (Xᵀ = U'ΣV'ᵀ) ⇒ (X = V'ΣU'ᵀ).
    const bool transposed = rows < cols;
    const int m = transposed ? cols : rows;
    const int n = transposed ? rows : cols;

    // Column-major working copy: rotations sweep whole columns.
    std::vector<double> a(static_cast<std::size_t>(m) * n, 0.0);
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j) {
            const double value = matrix[static_cast<std::size_t>(i) * cols + j];
            if (transposed)
                a[static_cast<std::size_t>(i) * m + j] = value; // (j,i) of Xᵀ
            else
                a[static_cast<std::size_t>(j) * m + i] = value;
        }

    std::vector<double> v(static_cast<std::size_t>(n) * n, 0.0);
    for (int j = 0; j < n; ++j)
        v[static_cast<std::size_t>(j) * n + j] = 1.0;

    // Quadratic convergence means ~6-10 sweeps in practice; the cap only exists
    // so that a pathological input cannot spin forever.
    constexpr int kMaxSweeps = 60;
    const double threshold = 8.0 * kEps;
    for (int sweep = 0; sweep < kMaxSweeps; ++sweep) {
        double worst = 0.0;
        for (int pi = 0; pi < n - 1; ++pi)
            for (int q = pi + 1; q < n; ++q) {
                double* ap = a.data() + static_cast<std::size_t>(pi) * m;
                double* aq = a.data() + static_cast<std::size_t>(q) * m;
                const double alpha = dotColumns(ap, ap, m);
                const double beta = dotColumns(aq, aq, m);
                const double gamma = dotColumns(ap, aq, m);
                if (gamma == 0.0 || alpha <= 0.0 || beta <= 0.0)
                    continue;
                const double off = std::abs(gamma) / std::sqrt(alpha * beta);
                worst = std::max(worst, off);
                if (off <= threshold)
                    continue;
                // Rotation annihilating a_pᵀa_q: the SMALLER root of
                // t² + 2ζt − 1 = 0 keeps |θ| ≤ π/4, which is what makes the
                // sweep converge instead of merely permuting the pair.
                const double zeta = (beta - alpha) / (2.0 * gamma);
                const double sign = zeta >= 0.0 ? 1.0 : -1.0;
                const double t =
                    sign / (std::abs(zeta) + std::sqrt(1.0 + zeta * zeta));
                const double c = 1.0 / std::sqrt(1.0 + t * t);
                const double s = c * t;
                for (int i = 0; i < m; ++i) {
                    const double x = ap[i];
                    const double yv = aq[i];
                    ap[i] = c * x - s * yv;
                    aq[i] = s * x + c * yv;
                }
                double* vp = v.data() + static_cast<std::size_t>(pi) * n;
                double* vq = v.data() + static_cast<std::size_t>(q) * n;
                for (int i = 0; i < n; ++i) {
                    const double x = vp[i];
                    const double yv = vq[i];
                    vp[i] = c * x - s * yv;
                    vq[i] = s * x + c * yv;
                }
            }
        if (worst <= threshold)
            break;
    }

    // Singular values are the norms of the now-orthogonal columns.
    std::vector<int> order(static_cast<std::size_t>(n));
    std::iota(order.begin(), order.end(), 0);
    std::vector<double> norm(static_cast<std::size_t>(n), 0.0);
    for (int j = 0; j < n; ++j)
        norm[static_cast<std::size_t>(j)] = std::sqrt(dotColumns(
            a.data() + static_cast<std::size_t>(j) * m,
            a.data() + static_cast<std::size_t>(j) * m, m));
    std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
        return norm[static_cast<std::size_t>(lhs)]
            > norm[static_cast<std::size_t>(rhs)];
    });

    const int k = std::min(rows, cols);
    std::vector<double> uu(static_cast<std::size_t>(m) * k, 0.0); // m×k
    std::vector<double> vv(static_cast<std::size_t>(n) * k, 0.0); // n×k
    out.singularValues.assign(static_cast<std::size_t>(k), 0.0);
    const double sigmaMax = norm[static_cast<std::size_t>(order[0])];
    const double rankTol = std::max(m, n) * kEps * sigmaMax;
    out.rank = 0;
    for (int j = 0; j < k; ++j) {
        const int src = order[static_cast<std::size_t>(j)];
        const double sigma = norm[static_cast<std::size_t>(src)];
        out.singularValues[static_cast<std::size_t>(j)] = sigma;
        if (sigma > rankTol) {
            ++out.rank;
            for (int i = 0; i < m; ++i)
                uu[static_cast<std::size_t>(i) * k + j] =
                    a[static_cast<std::size_t>(src) * m + i] / sigma;
        }
        for (int i = 0; i < n; ++i)
            vv[static_cast<std::size_t>(i) * k + j] =
                v[static_cast<std::size_t>(src) * n + i];
    }

    if (transposed) {
        out.u = std::move(vv); // rows×k
        out.v = std::move(uu); // cols×k
    } else {
        out.u = std::move(uu);
        out.v = std::move(vv);
    }
    out.ok = true;
    return out;
}

// ---------------------------------------------------------------------------
// Ridge
// ---------------------------------------------------------------------------

std::vector<double> ridgeSolve(const ThinSvd& svd, const std::vector<double>& y,
                               double lambda)
{
    std::vector<double> beta(static_cast<std::size_t>(std::max(0, svd.cols)),
                             0.0);
    if (!svd.ok || y.size() != static_cast<std::size_t>(svd.rows))
        return beta;
    lambda = std::max(0.0, lambda); // a negative penalty would amplify, not damp
    const int k = static_cast<int>(svd.singularValues.size());
    const double sigmaMax = k > 0 ? svd.singularValues[0] : 0.0;
    const double rankTol = std::max(svd.rows, svd.cols) * kEps * sigmaMax;
    for (int j = 0; j < k; ++j) {
        const double sigma = svd.singularValues[static_cast<std::size_t>(j)];
        // Directions at the noise floor are DROPPED rather than damped. With
        // λ = 0 keeping them is a division by ~0 (this is the collinear-orbit
        // case, which is normal in a CE, not exotic); with λ > 0 the term they
        // would contribute is O(σ/λ) and is noise either way. Dropping them is
        // what makes λ = 0 mean "minimum-norm least squares" instead of "NaN".
        if (sigma <= rankTol || sigma <= 0.0)
            continue;
        double uy = 0.0;
        for (int i = 0; i < svd.rows; ++i)
            uy += svd.u[static_cast<std::size_t>(i) * k + j]
                * y[static_cast<std::size_t>(i)];
        const double factor = sigma / (sigma * sigma + lambda) * uy;
        for (int c = 0; c < svd.cols; ++c)
            beta[static_cast<std::size_t>(c)] +=
                svd.v[static_cast<std::size_t>(c) * k + j] * factor;
    }
    return beta;
}

std::vector<double> ridgeSolve(const std::vector<double>& matrix, int rows,
                               int cols, const std::vector<double>& y,
                               double lambda)
{
    return ridgeSolve(thinSvd(matrix, rows, cols), y, lambda);
}

// ---------------------------------------------------------------------------
// LASSO
// ---------------------------------------------------------------------------

LassoSolution lassoSolve(const std::vector<double>& matrix, int rows, int cols,
                         const std::vector<double>& y, double lambda,
                         const LassoOptions& options,
                         const std::vector<double>* warmStart)
{
    LassoSolution out;
    out.beta.assign(static_cast<std::size_t>(std::max(0, cols)), 0.0);
    if (rows <= 0 || cols <= 0
        || matrix.size() != static_cast<std::size_t>(rows) * cols
        || y.size() != static_cast<std::size_t>(rows))
        return out;
    if (warmStart && warmStart->size() == out.beta.size())
        out.beta = *warmStart;
    lambda = std::max(0.0, lambda); // a negative penalty would amplify, not damp

    std::vector<double> normSq(static_cast<std::size_t>(cols), 0.0);
    for (int j = 0; j < cols; ++j) {
        double s = 0.0;
        for (int i = 0; i < rows; ++i) {
            const double v = matrix[static_cast<std::size_t>(i) * cols + j];
            s += v * v;
        }
        normSq[static_cast<std::size_t>(j)] = s;
    }

    std::vector<double> residual(static_cast<std::size_t>(rows));
    auto recomputeResidual = [&]() {
        for (int i = 0; i < rows; ++i) {
            double v = y[static_cast<std::size_t>(i)];
            for (int j = 0; j < cols; ++j)
                v -= matrix[static_cast<std::size_t>(i) * cols + j]
                    * out.beta[static_cast<std::size_t>(j)];
            residual[static_cast<std::size_t>(i)] = v;
        }
    };
    recomputeResidual();

    const double tol = std::max(options.tolerance, 0.0);
    for (int pass = 0; pass < options.maxIterations; ++pass) {
        double maxDelta = 0.0;
        double maxAbs = 0.0;
        for (int j = 0; j < cols; ++j) {
            const double nn = normSq[static_cast<std::size_t>(j)];
            if (nn <= 0.0) {
                out.beta[static_cast<std::size_t>(j)] = 0.0;
                continue;
            }
            const double bOld = out.beta[static_cast<std::size_t>(j)];
            double rho = 0.0;
            for (int i = 0; i < rows; ++i)
                rho += matrix[static_cast<std::size_t>(i) * cols + j]
                    * residual[static_cast<std::size_t>(i)];
            rho += nn * bOld; // partial residual, coefficient j excluded
            // Soft threshold. The max() below is what produces a coefficient
            // that is EXACTLY 0.0, not merely small — the variable selection
            // that makes the lasso the right tool for choosing orbits.
            double bNew = 0.0;
            if (rho > lambda)
                bNew = (rho - lambda) / nn;
            else if (rho < -lambda)
                bNew = (rho + lambda) / nn;
            if (bNew != bOld) {
                const double delta = bNew - bOld;
                for (int i = 0; i < rows; ++i)
                    residual[static_cast<std::size_t>(i)] -=
                        matrix[static_cast<std::size_t>(i) * cols + j] * delta;
                out.beta[static_cast<std::size_t>(j)] = bNew;
                maxDelta = std::max(maxDelta, std::abs(delta));
            }
            maxAbs = std::max(maxAbs, std::abs(bNew));
        }
        out.iterations = pass + 1;
        if (maxDelta <= tol * (1.0 + maxAbs)) {
            out.converged = true;
            break;
        }
        // The residual is updated incrementally for speed, so it drifts; a
        // periodic recomputation stops that drift from setting the accuracy
        // floor when the exact-recovery case asks for ten-plus digits.
        if ((pass % 64) == 63)
            recomputeResidual();
    }
    return out;
}

// ---------------------------------------------------------------------------
// The fit
// ---------------------------------------------------------------------------

EciFitResult fitEffectiveClusterInteractions(
    const std::vector<std::vector<double>>& correlations,
    const std::vector<double>& energies, const EciFitOptions& options,
    const std::vector<EciColumn>& columns)
{
    EciFitResult result;
    const int n = static_cast<int>(correlations.size());
    if (n == 0 || energies.size() != correlations.size()) {
        result.note = "need one energy per configuration";
        return result;
    }
    const int totalCols = static_cast<int>(correlations[0].size());
    if (totalCols == 0) {
        result.note = "the design matrix has no columns";
        return result;
    }
    for (const std::vector<double>& row : correlations)
        if (static_cast<int>(row.size()) != totalCols) {
            result.note = "correlation rows have inconsistent lengths";
            return result;
        }
    if (!columns.empty() && static_cast<int>(columns.size()) != totalCols) {
        result.note = "column descriptions do not match the design matrix";
        return result;
    }

    std::vector<int> allRows(static_cast<std::size_t>(n));
    std::iota(allRows.begin(), allRows.end(), 0);
    const Prepared full =
        prepare(correlations, energies, allRows, totalCols, options.standardize);

    // --- λ path ------------------------------------------------------------
    std::vector<double> lambdas = options.lambdas;
    if (options.method == EciMethod::Ard) {
        lambdas.assign(1, 0.0); // ARD determines its own sparsity
    } else if (lambdas.empty()) {
        // λ_max = the smallest λ at which the lasso solution is entirely zero,
        // i.e. max_j |x_jᵀ y| / n under the 1/(2n) normalisation. It is the
        // natural top of the path for ridge too: everything above it is a model
        // that has been shrunk past the point of saying anything.
        double lambdaMax = 0.0;
        for (int j = 0; j < full.cols; ++j) {
            double s = 0.0;
            for (int i = 0; i < full.rows; ++i)
                s += full.x[static_cast<std::size_t>(i) * full.cols + j]
                    * full.y[static_cast<std::size_t>(i)];
            lambdaMax = std::max(lambdaMax, std::abs(s));
        }
        lambdaMax = full.rows > 0 ? lambdaMax / full.rows : 0.0;
        if (!(lambdaMax > 0.0))
            lambdaMax = 1.0; // constant response: any path is as good
        double ratio = options.lambdaMinRatio;
        if (!(ratio > 0.0))
            ratio = options.method == EciMethod::Ridge ? 1.0e-8 : 1.0e-4;
        const int count = std::max(1, options.lambdaCount);
        lambdas.resize(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            const double f = count == 1 ? 0.0
                                        : static_cast<double>(i) / (count - 1);
            lambdas[static_cast<std::size_t>(i)] =
                lambdaMax * std::pow(ratio, f); // descending — warm starts
        }
    } else {
        // A caller-supplied path is sorted descending for the same reason.
        std::sort(lambdas.begin(), lambdas.end(), std::greater<double>());
    }

    // --- Cross-validation ---------------------------------------------------
    const int nLambda = static_cast<int>(lambdas.size());
    std::vector<double> cvSquared(static_cast<std::size_t>(nLambda), 0.0);
    std::vector<std::vector<double>> foldMse; // per fold, per λ
    int folds = options.cvFolds;
    if (folds <= 1 || folds > n)
        folds = n; // leave-one-out, the CE convention
    const bool canCv = n >= 3;

    if (canCv) {
        std::vector<int> assignment(static_cast<std::size_t>(n), 0);
        if (folds == n) {
            std::iota(assignment.begin(), assignment.end(), 0);
        } else {
            for (int i = 0; i < n; ++i)
                assignment[static_cast<std::size_t>(i)] = i % folds;
            std::mt19937 rng(options.seed);
            std::shuffle(assignment.begin(), assignment.end(), rng);
        }
        foldMse.assign(static_cast<std::size_t>(folds),
                       std::vector<double>(static_cast<std::size_t>(nLambda),
                                           0.0));
        for (int f = 0; f < folds; ++f) {
            std::vector<int> train, test;
            for (int i = 0; i < n; ++i)
                (assignment[static_cast<std::size_t>(i)] == f ? test : train)
                    .push_back(i);
            if (train.empty() || test.empty())
                continue;
            // Centring and scaling are recomputed from the TRAINING rows only.
            // Standardising once on the whole data set and then splitting leaks
            // the held-out energies into the fit and produces a CV score that
            // is optimistic by exactly the amount one is trying to measure.
            const Prepared p = prepare(correlations, energies, train, totalCols,
                                       options.standardize);
            const std::vector<Model> models =
                solvePath(p, lambdas, options, totalCols);
            for (int l = 0; l < nLambda; ++l) {
                double sum = 0.0;
                for (int row : test) {
                    const double d =
                        predict(models[static_cast<std::size_t>(l)],
                                correlations[static_cast<std::size_t>(row)])
                        - energies[static_cast<std::size_t>(row)];
                    sum += d * d;
                    cvSquared[static_cast<std::size_t>(l)] += d * d;
                }
                foldMse[static_cast<std::size_t>(f)][static_cast<std::size_t>(l)] =
                    sum / test.size();
            }
        }
    }

    // --- Final models on all data ------------------------------------------
    const std::vector<Model> models = solvePath(full, lambdas, options, totalCols);
    result.lambdaPath = lambdas;
    result.cvPath.assign(static_cast<std::size_t>(nLambda), 0.0);
    result.rmsePath.assign(static_cast<std::size_t>(nLambda), 0.0);
    result.activePath.assign(static_cast<std::size_t>(nLambda), 0);
    for (int l = 0; l < nLambda; ++l) {
        result.cvPath[static_cast<std::size_t>(l)] =
            canCv ? std::sqrt(cvSquared[static_cast<std::size_t>(l)] / n)
                  : std::numeric_limits<double>::quiet_NaN();
        double sum = 0.0;
        int active = 0;
        for (int i = 0; i < n; ++i) {
            const double d =
                predict(models[static_cast<std::size_t>(l)],
                        correlations[static_cast<std::size_t>(i)])
                - energies[static_cast<std::size_t>(i)];
            sum += d * d;
        }
        for (double b : models[static_cast<std::size_t>(l)].beta)
            if (std::abs(b) > options.zeroTolerance)
                ++active;
        result.rmsePath[static_cast<std::size_t>(l)] = std::sqrt(sum / n);
        result.activePath[static_cast<std::size_t>(l)] = active;
    }

    // --- Selection ----------------------------------------------------------
    int best = 0;
    if (canCv) {
        double bestScore = std::numeric_limits<double>::max();
        for (int l = 0; l < nLambda; ++l)
            if (result.cvPath[static_cast<std::size_t>(l)] < bestScore) {
                bestScore = result.cvPath[static_cast<std::size_t>(l)];
                best = l;
            }
        if (options.oneStandardError && folds > 1) {
            // Standard error of the mean fold MSE at the CV optimum; then the
            // largest λ (earliest on a descending path) whose CV mean is still
            // inside it. ESL §7.10: the CV curve is flat near its minimum and
            // its exact argmin is noise, so paying one standard error for a
            // markedly smaller cluster set is a good trade.
            double mean = 0.0;
            for (int f = 0; f < folds; ++f)
                mean += foldMse[static_cast<std::size_t>(f)]
                               [static_cast<std::size_t>(best)];
            mean /= folds;
            double var = 0.0;
            for (int f = 0; f < folds; ++f) {
                const double d = foldMse[static_cast<std::size_t>(f)]
                                        [static_cast<std::size_t>(best)]
                    - mean;
                var += d * d;
            }
            const double se = folds > 1
                ? std::sqrt(var / (folds - 1)) / std::sqrt(folds)
                : 0.0;
            const double threshold = mean + se;
            for (int l = 0; l < nLambda; ++l) {
                double m = 0.0;
                for (int f = 0; f < folds; ++f)
                    m += foldMse[static_cast<std::size_t>(f)]
                                [static_cast<std::size_t>(l)];
                m /= folds;
                if (m <= threshold) {
                    best = l;
                    break; // path is descending in λ: the first hit is the largest
                }
            }
        }
    }

    const Model& chosen = models[static_cast<std::size_t>(best)];
    result.ok = true;
    result.selectedIndex = best;
    result.lambda = lambdas[static_cast<std::size_t>(best)];
    result.eci = chosen.beta;
    result.intercept = chosen.intercept;
    result.rmse = result.rmsePath[static_cast<std::size_t>(best)];
    result.cvScore = result.cvPath[static_cast<std::size_t>(best)];
    result.activeTerms = result.activePath[static_cast<std::size_t>(best)];
    if (!canCv)
        // With two configurations there is nothing to hold out, so there is no
        // evidence for any particular λ. Reporting the MOST regularised model
        // (index 0 of a descending path) is the conservative reading: better a
        // fit that says almost nothing than ECIs invented from two points.
        result.note = "fewer than three configurations — no cross-validation; "
                      "reporting the most strongly regularised model";

    result.predictions.resize(static_cast<std::size_t>(n));
    result.residuals.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double p = predict(chosen, correlations[static_cast<std::size_t>(i)]);
        result.predictions[static_cast<std::size_t>(i)] = p;
        result.residuals[static_cast<std::size_t>(i)] =
            p - energies[static_cast<std::size_t>(i)];
        result.maxAbsError = std::max(
            result.maxAbsError,
            std::abs(result.residuals[static_cast<std::size_t>(i)]));
    }

    for (int c = 0; c < totalCols; ++c) {
        if (std::abs(result.eci[static_cast<std::size_t>(c)])
            <= options.zeroTolerance)
            continue;
        EciTerm term;
        term.index = c;
        term.eci = result.eci[static_cast<std::size_t>(c)];
        if (!columns.empty()) {
            const EciColumn& col = columns[static_cast<std::size_t>(c)];
            term.label = col.label;
            term.order = col.order;
            term.radius = col.radius;
            term.multiplicity = std::max(1, col.multiplicity);
        } else {
            term.label = "column " + std::to_string(c);
        }
        term.weightedEci = term.multiplicity * term.eci;
        result.terms.push_back(std::move(term));
    }
    std::sort(result.terms.begin(), result.terms.end(),
              [](const EciTerm& a, const EciTerm& b) {
                  return std::abs(a.weightedEci) > std::abs(b.weightedEci);
              });
    return result;
}

std::string formatEciReport(const EciFitResult& result)
{
    std::ostringstream os;
    os.imbue(std::locale::classic()); // belt and braces: no digit grouping
    if (!result.ok) {
        os << "Cluster expansion fit failed: " << result.note << "\n";
        return os.str();
    }
    os << "Cluster expansion fit\n";
    os << "  candidate columns   " << result.eci.size() << "\n";
    os << "  active interactions " << result.activeTerms << "\n";
    os << "  lambda              " << scientificText(result.lambda, 4) << "\n";
    // The two numbers, adjacent and in that order, deliberately: the CV score
    // is the result and the training RMSE is the thing that lies.
    os << "  CV score (RMSE)     " << scientificText(result.cvScore, 4)
       << "   <- the diagnostic\n";
    os << "  training RMSE       " << scientificText(result.rmse, 4) << "\n";
    os << "  max |residual|      " << scientificText(result.maxAbsError, 4)
       << "\n";
    os << "  J0 (constant)       " << fixedText(result.intercept, 6) << "\n";
    if (result.cvScore > 0.0 && result.rmse > 0.0
        && result.cvScore > 3.0 * result.rmse)
        os << "  WARNING: the CV score is more than 3x the training RMSE — the "
              "fit is memorising its training set.\n";
    if (!result.note.empty())
        os << "  note: " << result.note << "\n";
    os << "\n  " << pad("orbit", 24) << pad("order", 7) << pad("radius", 10)
       << pad("mult", 6) << pad("ECI", 14) << "m*ECI\n";
    for (const EciTerm& t : result.terms) {
        os << "  " << pad(t.label, 24)
           << pad(t.order > 0 ? std::to_string(t.order) : std::string("-"), 7)
           << pad(t.radius > 0.0 ? fixedText(t.radius, 3) : std::string("-"), 10)
           << pad(std::to_string(t.multiplicity), 6)
           << pad(fixedText(t.eci, 6), 14) << fixedText(t.weightedEci, 6)
           << "\n";
    }
    return os.str();
}

} // namespace calango::core
