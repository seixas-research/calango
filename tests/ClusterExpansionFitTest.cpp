// Unit tests for the ECI solver (src/core/ClusterExpansionFit).
//
// Every assertion here is pinned to a CLOSED FORM or to a value that was known
// before the solver ran — never to a previous run of this code. What that means
// in practice for a regression solver:
//
//   * exact recovery — data generated from known coefficients with NO noise
//     must give those coefficients back to machine precision;
//   * the two textbook identities under an ORTHONORMAL design, where both
//     estimators collapse to one line each (ESL §3.4.1 and Table 3.4):
//         ridge  β = β_ols / (1 + λ)
//         lasso  β = sign(β_ols)·max(|β_ols| − λ, 0)
//     the second including that the zeros are EXACT zeros;
//   * an exactly rank-deficient design, where the answer is again a closed form
//     (the degenerate pair behaves as one column at half the penalty) and where
//     a normal-equation solver would divide by zero;
//   * cross-validation, checked against the SUPPORT of the model the data was
//     generated from, plus the defining signature of overfitting — training
//     error and CV error moving in opposite directions.
//
// The fixtures use std::mt19937 raw output through a hand-rolled Box-Muller
// rather than <random>'s distributions: the engine is specified bit-for-bit by
// the standard but the DISTRIBUTIONS are not, so libstdc++ and libc++ would
// otherwise generate different data and the CV assertions would be pinned to
// different problems on Linux and macOS.
//
// Exit code 0 = pass.

#include "core/ClusterExpansionFit.hpp"

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace calango::core;

namespace {

int gFailures = 0;

void check(bool condition, const std::string& what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++gFailures;
    }
}

void checkClose(double got, double want, double tol, const std::string& what)
{
    if (!(std::abs(got - want) <= tol) || !std::isfinite(got)) {
        std::fprintf(stderr, "FAIL: %s (got %.17g, want %.17g, tol %.3g)\n",
                     what.c_str(), got, want, tol);
        ++gFailures;
    }
}

/// Portable standard normal: mt19937 is bit-exact across implementations, the
/// distribution objects are not.
struct Normal {
    std::mt19937 rng;
    explicit Normal(unsigned seed) : rng(seed) {}
    double uniform()
    {
        // (0,1) open interval — Box-Muller takes a log of the first draw.
        return (rng() + 0.5) / 4294967296.0;
    }
    double operator()()
    {
        const double u1 = uniform();
        const double u2 = uniform();
        return std::sqrt(-2.0 * std::log(u1))
            * std::cos(6.283185307179586 * u2);
    }
};

std::vector<double> matVec(const std::vector<double>& a, int rows, int cols,
                           const std::vector<double>& x)
{
    std::vector<double> y(static_cast<std::size_t>(rows), 0.0);
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j)
            y[static_cast<std::size_t>(i)] +=
                a[static_cast<std::size_t>(i) * cols + j]
                * x[static_cast<std::size_t>(j)];
    return y;
}

std::vector<double> transposeVec(const std::vector<double>& a, int rows,
                                 int cols, const std::vector<double>& y)
{
    std::vector<double> out(static_cast<std::size_t>(cols), 0.0);
    for (int j = 0; j < cols; ++j)
        for (int i = 0; i < rows; ++i)
            out[static_cast<std::size_t>(j)] +=
                a[static_cast<std::size_t>(i) * cols + j]
                * y[static_cast<std::size_t>(i)];
    return out;
}

/// Modified Gram-Schmidt, twice (re-orthogonalisation), so the result is
/// orthonormal to ~1e-15 and the closed forms below can be pinned tightly.
std::vector<double> orthonormalDesign(int rows, int cols, unsigned seed)
{
    Normal normal(seed);
    std::vector<double> a(static_cast<std::size_t>(rows) * cols);
    for (double& v : a)
        v = normal();
    for (int pass = 0; pass < 2; ++pass)
        for (int j = 0; j < cols; ++j) {
            for (int k = 0; k < j; ++k) {
                double dot = 0.0;
                for (int i = 0; i < rows; ++i)
                    dot += a[static_cast<std::size_t>(i) * cols + j]
                        * a[static_cast<std::size_t>(i) * cols + k];
                for (int i = 0; i < rows; ++i)
                    a[static_cast<std::size_t>(i) * cols + j] -=
                        dot * a[static_cast<std::size_t>(i) * cols + k];
            }
            double norm = 0.0;
            for (int i = 0; i < rows; ++i)
                norm += a[static_cast<std::size_t>(i) * cols + j]
                    * a[static_cast<std::size_t>(i) * cols + j];
            norm = std::sqrt(norm);
            for (int i = 0; i < rows; ++i)
                a[static_cast<std::size_t>(i) * cols + j] /= norm;
        }
    return a;
}

std::vector<std::vector<double>> toRows(const std::vector<double>& a, int rows,
                                        int cols)
{
    std::vector<std::vector<double>> out(static_cast<std::size_t>(rows));
    for (int i = 0; i < rows; ++i)
        out[static_cast<std::size_t>(i)].assign(
            a.begin() + static_cast<std::size_t>(i) * cols,
            a.begin() + static_cast<std::size_t>(i + 1) * cols);
    return out;
}

// -- 1. SVD against known decompositions ------------------------------------
void testSvd()
{
    // A rank-1 matrix of ones: X = 1_m 1_nᵀ, so σ₁ = √(mn) exactly and every
    // other singular value is 0.
    const int m = 7, n = 4;
    std::vector<double> ones(static_cast<std::size_t>(m) * n, 1.0);
    ThinSvd s = thinSvd(ones, m, n);
    check(s.ok, "SVD of the all-ones matrix succeeds");
    check(s.rank == 1, "all-ones matrix has rank 1");
    checkClose(s.singularValues[0], std::sqrt(static_cast<double>(m * n)),
               1e-12, "sigma_1 of the all-ones matrix is sqrt(mn)");
    for (int j = 1; j < 4; ++j)
        checkClose(s.singularValues[static_cast<std::size_t>(j)], 0.0, 1e-12,
                   "trailing singular values of a rank-1 matrix vanish");

    // The 8×8 Hadamard matrix has HᵀH = 8I, so every singular value is √8.
    // Entries are ±1, i.e. exactly representable, and the inner products are
    // exact integer sums — nothing here is a tolerance.
    const int h = 8;
    std::vector<double> had(static_cast<std::size_t>(h) * h, 1.0);
    for (int i = 0; i < h; ++i)
        for (int j = 0; j < h; ++j) {
            int bits = (i & j);
            int parity = 0;
            while (bits) {
                parity ^= (bits & 1);
                bits >>= 1;
            }
            had[static_cast<std::size_t>(i) * h + j] = parity ? -1.0 : 1.0;
        }
    ThinSvd hs = thinSvd(had, h, h);
    check(hs.rank == 8, "the 8x8 Hadamard matrix has full rank");
    for (int j = 0; j < h; ++j)
        checkClose(hs.singularValues[static_cast<std::size_t>(j)],
                   std::sqrt(8.0), 1e-13,
                   "every Hadamard singular value is sqrt(8)");

    // Reconstruction U Σ Vᵀ = X, in both the tall and the WIDE orientation —
    // wide is the case a cluster expansion actually hits (more candidate
    // orbits than affordable configurations) and the case the transpose
    // shortcut in thinSvd() exists for.
    for (int mode = 0; mode < 2; ++mode) {
        const int rows = mode == 0 ? 9 : 4;
        const int cols = mode == 0 ? 4 : 9;
        Normal normal(7 + mode);
        std::vector<double> a(static_cast<std::size_t>(rows) * cols);
        for (double& v : a)
            v = normal();
        ThinSvd d = thinSvd(a, rows, cols);
        const int k = std::min(rows, cols);
        double worst = 0.0;
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j) {
                double v = 0.0;
                for (int q = 0; q < k; ++q)
                    v += d.u[static_cast<std::size_t>(i) * k + q]
                        * d.singularValues[static_cast<std::size_t>(q)]
                        * d.v[static_cast<std::size_t>(j) * k + q];
                worst = std::max(
                    worst,
                    std::abs(v - a[static_cast<std::size_t>(i) * cols + j]));
            }
        checkClose(worst, 0.0, 1e-12,
                   mode == 0 ? "U S V^T reconstructs a tall matrix"
                             : "U S V^T reconstructs a wide matrix");
        // Orthonormality of the retained left/right vectors.
        for (int p = 0; p < k; ++p)
            for (int q = 0; q < k; ++q) {
                double dot = 0.0;
                for (int j = 0; j < cols; ++j)
                    dot += d.v[static_cast<std::size_t>(j) * k + p]
                        * d.v[static_cast<std::size_t>(j) * k + q];
                checkClose(dot, p == q ? 1.0 : 0.0, 1e-12,
                           "right singular vectors are orthonormal");
            }
    }
}

// -- 2. Ridge: exact recovery and the orthonormal shrinkage law -------------
void testRidgeClosedForms()
{
    const int rows = 24, cols = 6;
    const std::vector<double> x = orthonormalDesign(rows, cols, 4242);

    // Sanity on the fixture itself: XᵀX = I to machine precision, otherwise
    // the "closed form" below would be pinned to the wrong problem.
    double worst = 0.0;
    for (int p = 0; p < cols; ++p)
        for (int q = 0; q < cols; ++q) {
            double dot = 0.0;
            for (int i = 0; i < rows; ++i)
                dot += x[static_cast<std::size_t>(i) * cols + p]
                    * x[static_cast<std::size_t>(i) * cols + q];
            worst = std::max(worst, std::abs(dot - (p == q ? 1.0 : 0.0)));
        }
    check(worst < 1e-14, "the test's orthonormal design really is orthonormal");

    Normal normal(99);
    std::vector<double> y(static_cast<std::size_t>(rows));
    for (double& v : y)
        v = normal();
    const std::vector<double> ols = transposeVec(x, rows, cols, y); // XᵀX = I

    for (double lambda : {0.0, 0.1, 1.0, 10.0, 1000.0}) {
        const std::vector<double> beta = ridgeSolve(x, rows, cols, y, lambda);
        for (int j = 0; j < cols; ++j)
            checkClose(beta[static_cast<std::size_t>(j)],
                       ols[static_cast<std::size_t>(j)] / (1.0 + lambda), 1e-12,
                       "ridge under an orthonormal design is beta_ols/(1+lambda)");
    }

    // Exact recovery: noiseless data from known coefficients, lambda = 0.
    Normal gen(2024);
    const int n2 = 40, p2 = 8;
    std::vector<double> design(static_cast<std::size_t>(n2) * p2);
    for (double& v : design)
        v = gen();
    std::vector<double> truth{1.25, -0.5, 3.0, 0.125, -2.75, 0.0625, 1.0, -1.5};
    const std::vector<double> yExact = matVec(design, n2, p2, truth);
    const std::vector<double> recovered =
        ridgeSolve(design, n2, p2, yExact, 0.0);
    for (int j = 0; j < p2; ++j)
        checkClose(recovered[static_cast<std::size_t>(j)],
                   truth[static_cast<std::size_t>(j)], 1e-11,
                   "ridge at lambda=0 recovers noiseless coefficients exactly");
}

// -- 3. LASSO: soft-thresholding, exact zeros, exact recovery ---------------
void testLassoClosedForms()
{
    const int rows = 24, cols = 6;
    const std::vector<double> x = orthonormalDesign(rows, cols, 4242);
    Normal normal(99);
    std::vector<double> y(static_cast<std::size_t>(rows));
    for (double& v : y)
        v = normal();
    const std::vector<double> ols = transposeVec(x, rows, cols, y);

    double maxAbs = 0.0;
    for (double v : ols)
        maxAbs = std::max(maxAbs, std::abs(v));

    for (double frac : {0.0, 0.1, 0.5, 0.9, 1.0, 1.5}) {
        const double lambda = frac * maxAbs;
        LassoOptions opt;
        opt.tolerance = 1e-14;
        const LassoSolution sol =
            lassoSolve(x, rows, cols, y, lambda, opt, nullptr);
        check(sol.converged, "lasso coordinate descent converges");
        int zeros = 0;
        for (int j = 0; j < cols; ++j) {
            const double b = ols[static_cast<std::size_t>(j)];
            const double want =
                std::abs(b) > lambda ? (b > 0 ? b - lambda : b + lambda) : 0.0;
            checkClose(sol.beta[static_cast<std::size_t>(j)], want, 1e-12,
                       "lasso under an orthonormal design is soft-thresholding");
            if (want == 0.0) {
                // The point of the L1 penalty: not small, ZERO. A solver that
                // returns 1e-9 here has not selected any variables.
                check(sol.beta[static_cast<std::size_t>(j)] == 0.0,
                      "lasso drives shrunk coefficients to exactly 0.0");
                ++zeros;
            }
        }
        if (frac >= 1.0)
            check(zeros == cols,
                  "at lambda >= max|x_j^T y| the lasso solution is all zero");
    }

    // Exact recovery at lambda -> 0 (noiseless data, known coefficients).
    Normal gen(2024);
    const int n2 = 40, p2 = 8;
    std::vector<double> design(static_cast<std::size_t>(n2) * p2);
    for (double& v : design)
        v = gen();
    std::vector<double> truth{1.25, -0.5, 3.0, 0.125, -2.75, 0.0625, 1.0, -1.5};
    const std::vector<double> yExact = matVec(design, n2, p2, truth);
    LassoOptions opt;
    opt.tolerance = 1e-15;
    opt.maxIterations = 200000;
    const LassoSolution sol =
        lassoSolve(design, n2, p2, yExact, 1e-12, opt, nullptr);
    for (int j = 0; j < p2; ++j)
        checkClose(sol.beta[static_cast<std::size_t>(j)],
                   truth[static_cast<std::size_t>(j)], 1e-8,
                   "lasso at lambda->0 recovers noiseless coefficients");
}

// -- 4. Exactly collinear columns -------------------------------------------
void testCollinearity()
{
    // X = [x, x, z] with x ⟂ z. Two identical columns is not a contrived case
    // for a cluster expansion — two orbits at the same radius that no computed
    // configuration distinguishes produce it exactly — and XᵀX is singular, so
    // a normal-equation solver divides by zero here.
    //
    // Closed form. Because the duplicated pair enters the fit only through
    // s = b₁ + b₂ while the penalty λ(b₁² + b₂²) is minimised at b₁ = b₂ = s/2,
    // the pair behaves as ONE column carrying HALF the penalty:
    //     s = xᵀy / (‖x‖² + λ/2),  b₁ = b₂ = s/2
    // and, x being orthogonal to z, the third coefficient is untouched:
    //     b₃ = zᵀy / (‖z‖² + λ)
    const int rows = 12;
    std::vector<double> xcol(static_cast<std::size_t>(rows));
    std::vector<double> zcol(static_cast<std::size_t>(rows));
    for (int i = 0; i < rows; ++i) {
        xcol[static_cast<std::size_t>(i)] = (i % 2 == 0) ? 1.0 : -1.0;
        zcol[static_cast<std::size_t>(i)] = (i < rows / 2) ? 1.0 : -1.0;
    }
    double check_orthogonal = 0.0;
    for (int i = 0; i < rows; ++i)
        check_orthogonal += xcol[static_cast<std::size_t>(i)]
            * zcol[static_cast<std::size_t>(i)];
    check(check_orthogonal == 0.0, "collinearity fixture: x is orthogonal to z");

    std::vector<double> design(static_cast<std::size_t>(rows) * 3);
    for (int i = 0; i < rows; ++i) {
        design[static_cast<std::size_t>(i) * 3 + 0] =
            xcol[static_cast<std::size_t>(i)];
        design[static_cast<std::size_t>(i) * 3 + 1] =
            xcol[static_cast<std::size_t>(i)];
        design[static_cast<std::size_t>(i) * 3 + 2] =
            zcol[static_cast<std::size_t>(i)];
    }
    Normal normal(31337);
    std::vector<double> y(static_cast<std::size_t>(rows));
    for (double& v : y)
        v = normal();

    double xty = 0.0, zty = 0.0, xx = 0.0, zz = 0.0;
    for (int i = 0; i < rows; ++i) {
        xty += xcol[static_cast<std::size_t>(i)] * y[static_cast<std::size_t>(i)];
        zty += zcol[static_cast<std::size_t>(i)] * y[static_cast<std::size_t>(i)];
        xx += xcol[static_cast<std::size_t>(i)]
            * xcol[static_cast<std::size_t>(i)];
        zz += zcol[static_cast<std::size_t>(i)]
            * zcol[static_cast<std::size_t>(i)];
    }

    ThinSvd svd = thinSvd(design, rows, 3);
    check(svd.rank == 2, "an exactly duplicated column costs one unit of rank");

    for (double lambda : {0.0, 1e-8, 0.5, 4.0}) {
        const std::vector<double> beta = ridgeSolve(design, rows, 3, y, lambda);
        for (double b : beta)
            check(std::isfinite(b),
                  "ridge stays finite on an exactly singular design");
        checkClose(beta[0], beta[1], 1e-12,
                   "ridge splits degenerate columns evenly");
        checkClose(beta[0] + beta[1], xty / (xx + 0.5 * lambda), 1e-12,
                   "the degenerate pair acts as one column at half the penalty");
        checkClose(beta[2], zty / (zz + lambda), 1e-12,
                   "the independent column is unaffected by the degeneracy");
    }
}

// -- 5. Cross-validation must prefer the true model, not the best fit -------
//
// Two fixtures, because the two things being asserted live in different
// regimes and conflating them produces a test that passes for the wrong reason:
//
//   A. SUPPORT RECOVERY needs a design in which the signal and the noise are
//      cleanly separated, so that the λ cross-validation picks sits in the gap
//      between them. The test asserts that gap explicitly (below), because a
//      lasso support assertion without a stated margin is a coin flip: the
//      CV-optimal λ and the largest spurious correlation both scale as
//      σ√(log p / n), so on an arbitrary fixture they land on top of each other
//      and the exact support comes out right about 60% of the time. That is a
//      property of the lasso, not of this implementation, and pinning it
//      without checking the margin would produce a test that fails on an
//      unrelated change for reasons nobody could diagnose.
//
//   B. THE OVERFITTING SIGNATURE needs p comparable to n — the regime a real
//      cluster expansion is always in, where the affordable configurations run
//      out long before the candidate orbits do. With n ≫ p even the
//      unregularised fit generalises and there is no signature to see.
const std::vector<double> kSparseTruth3{1.5, -1.0, 0.8};

void testCrossValidationSupport()
{
    // n = 48 configurations, 10 candidate orbits, of which 3 are real.
    // The design is orthonormal (columns scaled to unit variance), which is the
    // regime where the lasso's selection is governed by the closed form
    // support = { j : |x_jᵀy|/n > λ } — so the margin below is exactly the
    // statement that CV landed between the noise and the signal.
    const int n = 48, p = 10;
    const std::vector<double> ortho = orthonormalDesign(n, p, 22);
    std::vector<std::vector<double>> rows(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        rows[static_cast<std::size_t>(i)].assign(static_cast<std::size_t>(p),
                                                 0.0);
        for (int j = 0; j < p; ++j)
            rows[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                ortho[static_cast<std::size_t>(i) * p + j] * std::sqrt(1.0 * n);
    }
    std::vector<double> truth(static_cast<std::size_t>(p), 0.0);
    for (int j = 0; j < 3; ++j)
        truth[static_cast<std::size_t>(j)] =
            kSparseTruth3[static_cast<std::size_t>(j)];
    const double trueIntercept = -3.25;
    Normal noise(113);
    std::vector<double> y(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        double v = trueIntercept;
        for (int j = 0; j < p; ++j)
            v += truth[static_cast<std::size_t>(j)]
                * rows[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
        y[static_cast<std::size_t>(i)] = v + 0.05 * noise();
    }

    EciFitOptions opt;
    opt.method = EciMethod::Lasso;
    opt.cvFolds = 0; // leave-one-out, the CE convention
    opt.oneStandardError = true;
    const EciFitResult fit = fitEffectiveClusterInteractions(rows, y, opt);
    check(fit.ok, "the lasso fit succeeds");

    // The margin the support assertion rests on: the CV-selected λ must be
    // above every spurious correlation and far below every real one.
    double yMean = 0.0;
    for (double v : y)
        yMean += v;
    yMean /= n;
    double noiseMax = 0.0, signalMin = 1e30;
    for (int j = 0; j < p; ++j) {
        double c = 0.0;
        for (int i = 0; i < n; ++i)
            c += rows[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)]
                * (y[static_cast<std::size_t>(i)] - yMean);
        c = std::abs(c) / n;
        if (truth[static_cast<std::size_t>(j)] != 0.0)
            signalMin = std::min(signalMin, c);
        else
            noiseMax = std::max(noiseMax, c);
    }
    check(fit.lambda > 1.5 * noiseMax,
          "CV picked a lambda above every spurious correlation (fixture margin)");
    check(fit.lambda < 0.1 * signalMin,
          "CV picked a lambda far below every real correlation (fixture margin)");

    std::vector<int> support;
    for (int j = 0; j < p; ++j)
        if (fit.eci[static_cast<std::size_t>(j)] != 0.0)
            support.push_back(j);
    const bool exact = support == std::vector<int>{0, 1, 2};
    if (!exact) {
        std::fprintf(stderr, "  selected support:");
        for (int j : support)
            std::fprintf(stderr, " %d", j);
        std::fprintf(stderr, " (lambda %.5g, noiseMax %.5g, signalMin %.5g)\n",
                     fit.lambda, noiseMax, signalMin);
    }
    check(exact, "CV selects exactly the orbits the data was generated from");
    check(fit.activeTerms == 3, "and reports three active interactions");

    // The coefficients, up to the shrinkage an L1 penalty necessarily applies
    // (the lasso is biased by construction; that is the price of selection).
    for (int j = 0; j < 3; ++j) {
        const double got = fit.eci[static_cast<std::size_t>(j)];
        const double want = truth[static_cast<std::size_t>(j)];
        check(got * want > 0.0, "a recovered ECI has the right sign");
        check(std::abs(got - want) < 0.1 * std::abs(want),
              "a recovered ECI is within 10% of the truth");
    }
    checkClose(fit.intercept, trueIntercept, 0.02,
               "the unpenalised intercept recovers J0");
    check(fit.cvScore >= fit.rmse,
          "the CV score is never better than the training error");

    // Ridge on the same data: it cannot select, so it keeps every orbit — but
    // it must shrink the spurious ones to the scale of the noise rather than
    // hand them physics. (This contrast is why lasso is the default here.)
    EciFitOptions ridge = opt;
    ridge.method = EciMethod::Ridge;
    ridge.oneStandardError = false;
    const EciFitResult rfit = fitEffectiveClusterInteractions(rows, y, ridge);
    check(rfit.ok, "the ridge fit succeeds");
    check(rfit.activeTerms == p, "ridge keeps every orbit (it cannot select)");
    for (int j = 0; j < 3; ++j)
        check(rfit.eci[static_cast<std::size_t>(j)]
                  * truth[static_cast<std::size_t>(j)]
              > 0.0,
              "ridge gets the sign of the real interactions right");
    for (int j = 3; j < p; ++j)
        check(std::abs(rfit.eci[static_cast<std::size_t>(j)]) < 0.05,
              "ridge shrinks the spurious orbits to the noise scale");
}

void testOverfittingSignature()
{
    // n = 24 configurations, 20 candidate orbits: fewer data than parameters is
    // the everyday state of a cluster expansion.
    const int n = 24, p = 20;
    Normal normal(20260812u);
    std::vector<double> design(static_cast<std::size_t>(n) * p);
    for (double& v : design)
        v = normal();
    std::vector<double> truth(static_cast<std::size_t>(p), 0.0);
    for (int j = 0; j < 3; ++j)
        truth[static_cast<std::size_t>(j)] =
            kSparseTruth3[static_cast<std::size_t>(j)];
    std::vector<double> y = matVec(design, n, p, truth);
    Normal noise(555);
    for (double& v : y)
        v += -3.25 + 0.05 * noise();
    const std::vector<std::vector<double>> rows = toRows(design, n, p);

    EciFitOptions opt;
    opt.method = EciMethod::Lasso;
    opt.oneStandardError = true;
    const EciFitResult fit = fitEffectiveClusterInteractions(rows, y, opt);
    check(fit.ok, "the lasso fit succeeds on an underdetermined design");

    // THE point of the whole module. At the sparse end of the path the model
    // fits its training set roughly three times better and predicts roughly a
    // third worse. Anyone selecting on the training RMSE picks that model.
    const std::size_t last = fit.lambdaPath.size() - 1;
    const std::size_t sel = static_cast<std::size_t>(fit.selectedIndex);
    check(fit.rmsePath[last] < 0.6 * fit.rmsePath[sel],
          "the least-regularised model has a much LOWER training error");
    check(fit.cvPath[last] > 1.15 * fit.cvPath[sel],
          "the least-regularised model has a clearly HIGHER CV error");
    check(fit.activePath[last] > fit.activeTerms,
          "the least-regularised model keeps orbits CV threw away");
    check(fit.activeTerms < p, "the selected model is sparse");

    // No false negatives, and whatever spurious orbits survive carry a
    // negligible fraction of the physics. (Exact support is NOT asserted here:
    // with 17 noise columns and 24 configurations the lasso keeps a few by
    // chance, which is a fact about the estimator, not a defect.)
    for (int j = 0; j < 3; ++j)
        check(fit.eci[static_cast<std::size_t>(j)]
                  * truth[static_cast<std::size_t>(j)]
              > 0.0,
              "every real interaction survives selection");
    double worstSpurious = 0.0;
    for (int j = 3; j < p; ++j)
        worstSpurious =
            std::max(worstSpurious, std::abs(fit.eci[static_cast<std::size_t>(j)]));
    check(worstSpurious < 0.05 * 0.8,
          "no spurious ECI reaches 5% of the smallest real one");

    // Ridge cannot drop anything, so on an underdetermined design it spreads
    // the fit over every orbit and pays for it in cross-validation.
    EciFitOptions ridge = opt;
    ridge.method = EciMethod::Ridge;
    ridge.oneStandardError = false;
    const EciFitResult rfit = fitEffectiveClusterInteractions(rows, y, ridge);
    check(rfit.cvScore > fit.cvScore,
          "ridge predicts worse than lasso when orbits outnumber configurations");
}

// -- 6. ARD --------------------------------------------------------------
void testArd()
{
    // Same fixture A as the support test.
    const int n = 48, p = 10;
    const std::vector<double> ortho = orthonormalDesign(n, p, 22);
    std::vector<std::vector<double>> rows(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        rows[static_cast<std::size_t>(i)].assign(static_cast<std::size_t>(p),
                                                 0.0);
        for (int j = 0; j < p; ++j)
            rows[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                ortho[static_cast<std::size_t>(i) * p + j] * std::sqrt(1.0 * n);
    }
    std::vector<double> truth(static_cast<std::size_t>(p), 0.0);
    for (int j = 0; j < 3; ++j)
        truth[static_cast<std::size_t>(j)] =
            kSparseTruth3[static_cast<std::size_t>(j)];
    Normal noise(113);
    std::vector<double> y(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        double v = -3.25;
        for (int j = 0; j < p; ++j)
            v += truth[static_cast<std::size_t>(j)]
                * rows[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
        y[static_cast<std::size_t>(i)] = v + 0.05 * noise();
    }

    EciFitOptions opt;
    opt.method = EciMethod::Ard;
    const EciFitResult fit = fitEffectiveClusterInteractions(rows, y, opt);
    check(fit.ok, "the ARD fit succeeds");

    // WHAT ARD DOES AND DOES NOT GUARANTEE. It prunes, and it always keeps the
    // real interactions, but it does NOT reduce to exactly the true support and
    // asserting that would be wrong. Under an orthogonal design the relevance
    // criterion reduces to (x_jᵀy)² > ‖x_j‖²σ² — a one-sigma test — so a pure
    // noise column survives with probability P(χ²₁ > 1) ≈ 0.32, independently
    // of how much data there is. Roughly a third of the irrelevant orbits
    // sticking around is the METHOD, not a bug in this implementation; what
    // makes it harmless is that their ECIs are at the noise scale, which is
    // what is asserted.
    for (int j = 0; j < 3; ++j)
        check(fit.eci[static_cast<std::size_t>(j)] != 0.0,
              "ARD keeps every real interaction");
    check(fit.activeTerms < p, "ARD prunes irrelevant orbits");
    double worstSpurious = 0.0;
    for (int j = 3; j < p; ++j)
        worstSpurious =
            std::max(worstSpurious, std::abs(fit.eci[static_cast<std::size_t>(j)]));
    check(worstSpurious < 0.02, "surviving spurious ARD ECIs are at noise scale");

    // ARD does not shrink what it keeps, so its coefficients are tighter than
    // the lasso's on the same data (which was within 10%, above).
    for (int j = 0; j < 3; ++j)
        checkClose(fit.eci[static_cast<std::size_t>(j)],
                   truth[static_cast<std::size_t>(j)],
                   0.03 * std::abs(truth[static_cast<std::size_t>(j)]),
                   "ARD recovers an interaction without lasso-scale shrinkage");
    check(std::isfinite(fit.cvScore) && fit.cvScore > 0.0,
          "ARD still reports a cross-validation score (its only diagnostic)");
}

// -- 7. Constant columns, multiplicities, and the locale trap ---------------
void testReportAndConstantColumn()
{
    // A CE correlation vector normally carries the EMPTY CLUSTER — a column of
    // ones — which is exactly collinear with the intercept. It must not break
    // the fit and must not be handed an ECI of its own.
    const int n = 30, p = 4;
    Normal normal(808);
    std::vector<std::vector<double>> rows(static_cast<std::size_t>(n));
    std::vector<double> y(static_cast<std::size_t>(n));
    const std::vector<double> truth{0.0, 0.4, -0.9, 0.0};
    for (int i = 0; i < n; ++i) {
        rows[static_cast<std::size_t>(i)].assign(static_cast<std::size_t>(p),
                                                 0.0);
        rows[static_cast<std::size_t>(i)][0] = 1.0; // the empty cluster
        for (int j = 1; j < p; ++j)
            rows[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                normal();
        double v = -1.75;
        for (int j = 0; j < p; ++j)
            v += truth[static_cast<std::size_t>(j)]
                * rows[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
        y[static_cast<std::size_t>(i)] = v;
    }

    std::vector<EciColumn> columns{{"empty", 0, 0.0, 1},
                                   {"pair 2.55", 2, 2.55, 12},
                                   {"pair 3.61", 2, 3.61, 6},
                                   {"triplet 2.55", 3, 2.55, 24}};
    EciFitOptions opt;
    opt.method = EciMethod::Lasso;
    const EciFitResult fit =
        fitEffectiveClusterInteractions(rows, y, opt, columns);
    check(fit.ok, "a design containing a constant column still fits");
    check(fit.eci[0] == 0.0,
          "the constant (empty-cluster) column gets no ECI of its own");
    checkClose(fit.intercept, -1.75, 0.05,
               "the constant lands in the intercept, where it belongs");

    // Multiplicity has to survive into the report, and m*J is what ranks.
    bool sawPair = false;
    for (const EciTerm& t : fit.terms)
        if (t.label == "pair 2.55") {
            sawPair = true;
            check(t.multiplicity == 12, "multiplicity is carried through");
            checkClose(t.weightedEci, 12.0 * t.eci, 1e-12,
                       "m*ECI is the multiplicity times the ECI");
        }
    check(sawPair, "a real interaction appears in the reported terms");
    check(fit.cvScore > 0.0 && std::isfinite(fit.cvScore),
          "a CV score is always reported");

    // THE LOCALE TRAP. This machine runs pt_BR.UTF-8, where printf writes
    // "0,123". A report containing a decimal comma is unparseable by anything
    // downstream and has already shipped twice in this repo (see
    // core/LocaleSafeNumber.hpp). Force a comma locale and check the output.
    const char* saved = std::setlocale(LC_NUMERIC, nullptr);
    const std::string savedName = saved ? saved : "C";
    const char* applied = nullptr;
    for (const char* name : {"pt_BR.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8"}) {
        applied = std::setlocale(LC_NUMERIC, name);
        if (applied)
            break;
    }
    const std::string report = formatEciReport(fit);
    std::setlocale(LC_NUMERIC, savedName.c_str());
    if (applied) {
        check(report.find(',') == std::string::npos,
              "the ECI report contains no decimal comma under a comma locale");
        check(report.find('.') != std::string::npos,
              "the ECI report writes decimal points");
    } else {
        std::fprintf(stderr,
                     "  (no comma locale installed; locale check skipped)\n");
    }
    check(report.find("CV score") != std::string::npos,
          "the report leads with the CV score");
    check(report.find("training RMSE") != std::string::npos,
          "the report shows the training RMSE next to it");
}

} // namespace

int main()
{
    testSvd();
    testRidgeClosedForms();
    testLassoClosedForms();
    testCollinearity();
    testCrossValidationSupport();
    testOverfittingSignature();
    testArd();
    testReportAndConstantColumn();

    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("PASS: cluster expansion ECI fit — SVD, ridge shrinkage law, "
                "lasso soft-thresholding with exact zeros, singular-design "
                "closed form, CV support recovery, ARD, locale-safe report\n");
    return 0;
}
