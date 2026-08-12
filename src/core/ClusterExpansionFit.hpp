#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace calango::core {

/// Effective Cluster Interactions: the fit that turns a set of computed
/// configurations into a cluster expansion.
///
/// `ClusterExpansion.hpp` produces the configurations and their cluster
/// correlations; a DFT (or MLIP) run produces one energy per configuration.
/// What is left is the linear model
///
///     E(σ) / atom  =  J₀  +  Σ_α  m_α  J_α  Φ_α(σ)
///
/// whose coefficients J_α are the ECIs. Everything in this header is that
/// least-squares problem and, more importantly, its REGULARISATION and its
/// VALIDATION — because for a cluster expansion the unregularised fit is
/// almost always both computable and wrong.
///
/// WHY A PLAIN LEAST-SQUARES SOLVE IS A TRAP HERE. A CE design matrix is
/// systematically ill-conditioned by construction: orbits at nearly the same
/// radius have nearly the same correlation on every configuration you can
/// afford to compute, so their columns are near-parallel; the empty cluster is
/// exactly parallel to the intercept; and the number of candidate orbits grows
/// faster with the cutoff than the number of configurations one is willing to
/// run, so the system is frequently underdetermined outright. Forming the
/// normal equations XᵀX squares the condition number of an already borderline
/// matrix and then inverts it. The failure is not a crash — it is a fit with a
/// perfect training residual and ECIs of alternating sign and absurd magnitude
/// that predicts nothing. Every solver here therefore goes through an SVD or
/// through coordinate descent, never through an explicit inverse.
///
/// WHAT "THE ANSWER" IS. It is not the training RMSE. A cluster expansion is
/// judged by its CROSS-VALIDATION score — the RMS error on configurations the
/// fit never saw (van de Walle & Ceder, J. Phase Equilib. 23, 348 (2002), the
/// paper that made the LOO CV score the standard CE diagnostic). The two
/// numbers moving in opposite directions as the regularisation is relaxed is
/// the definition of overfitting, so `EciFitResult` reports both and the caller
/// is expected to show both.

// ---------------------------------------------------------------------------
// Linear-algebra primitives
// ---------------------------------------------------------------------------
//
// These are exposed rather than hidden in the .cpp because they are what the
// closed-form tests pin. They are deliberately raw: no centring, no scaling, no
// intercept, and an explicitly stated objective, so that the textbook identities
// (ESL §3.4.1: ridge shrinkage under an orthonormal design; ESL Table 3.4:
// lasso soft-thresholding) apply to them verbatim. The convenience layer below
// is the one that centres, scales and cross-validates.

/// Thin singular value decomposition X = U Σ Vᵀ of a row-major `rows`×`cols`
/// matrix.
///
/// k = min(rows, cols). `singularValues` is length k, descending. `u` is
/// rows×k row-major, `v` is cols×k row-major — that is, the SINGULAR VECTORS
/// ARE THE COLUMNS of both, so u[i*k + j] is component i of the j-th left
/// singular vector.
///
/// Directions whose singular value underflows to zero have an arbitrary (zero)
/// left vector; they carry no information and every consumer here skips them.
struct ThinSvd {
    int rows = 0;
    int cols = 0;
    int rank = 0; ///< singular values above max(rows,cols)·eps·σ_max
    std::vector<double> singularValues;
    std::vector<double> u;
    std::vector<double> v;
    bool ok = false;
};

/// One-sided Jacobi SVD. See the .cpp for why Jacobi and not Golub-Reinsch.
ThinSvd thinSvd(const std::vector<double>& matrix, int rows, int cols);

/// Ridge / Tikhonov solution of
///
///     minimise ‖y − X b‖²  +  λ ‖b‖²
///
/// exactly as written: no 1/n, no 1/2, no intercept, no standardisation. Under
/// an orthonormal design (XᵀX = I) this is b = Xᵀy / (1 + λ), which is what the
/// test pins.
///
/// λ = 0 is legal and yields the MINIMUM-NORM least-squares solution, not a
/// division by zero: a rank-deficient X (two identical orbits, say) splits the
/// weight evenly between the degenerate columns instead of blowing up. That
/// property is the entire reason this routine is SVD-based.
std::vector<double> ridgeSolve(const std::vector<double>& matrix, int rows,
                               int cols, const std::vector<double>& y,
                               double lambda);

/// Same, reusing a decomposition. A whole λ path costs one SVD — the reason
/// ridge cross-validation over 50 λ values and 100 leave-one-out folds is
/// essentially free.
std::vector<double> ridgeSolve(const ThinSvd& svd, const std::vector<double>& y,
                               double lambda);

struct LassoOptions {
    int maxIterations = 100000; ///< coordinate-descent passes over the columns
    double tolerance = 1.0e-12; ///< max coefficient change ending a pass
};

struct LassoSolution {
    std::vector<double> beta;
    int iterations = 0;
    bool converged = false;
};

/// LASSO by cyclic coordinate descent with soft-thresholding, solving
///
///     minimise ½‖y − X b‖²  +  λ ‖b‖₁
///
/// again exactly as written (note the ½ — it is what makes the threshold λ and
/// not λ/2). Under an orthonormal design this is the closed form
///
///     b_j = sign(x_jᵀy) · max(|x_jᵀy| − λ, 0)
///
/// and the zeros it produces are EXACT zeros, bit for bit, not small numbers.
/// That is the whole point of the L1 penalty for a cluster expansion: it is a
/// variable selector, and choosing which orbits to keep is the actual
/// difficulty of the method. A "lasso" that only shrinks is a broken lasso, so
/// the test asserts `beta[j] == 0.0`.
///
/// `warmStart`, when non-null and correctly sized, seeds b. Descending a λ path
/// with warm starts is both far faster and far better conditioned than solving
/// each λ cold.
LassoSolution lassoSolve(const std::vector<double>& matrix, int rows, int cols,
                         const std::vector<double>& y, double lambda,
                         const LassoOptions& options = {},
                         const std::vector<double>* warmStart = nullptr);

// ---------------------------------------------------------------------------
// The fit
// ---------------------------------------------------------------------------

enum class EciMethod {
    /// L2. Keeps every orbit, shrinks them all. Well behaved and never sparse:
    /// use it when the orbit set is already small and physically chosen.
    Ridge,
    /// L1. Selects orbits. The default, because a CE's difficulty is deciding
    /// which clusters carry the physics, and this is the method that decides.
    Lasso,
    /// Automatic Relevance Determination (sparse Bayesian regression / RVM).
    /// Learns one precision per orbit by evidence maximisation and prunes the
    /// irrelevant ones outright, with NO regularisation parameter to choose —
    /// so there is no λ path, and the CV score reported is that of the single
    /// self-determined model. Sharper support recovery than lasso when it
    /// works; slower, and it can prune too hard on very small data sets.
    Ard
};

struct EciFitOptions {
    EciMethod method = EciMethod::Lasso;

    /// Explicit regularisation path (descending is fastest — warm starts).
    /// Empty means "generate one automatically" from λ_max, the smallest λ at
    /// which the lasso solution is entirely zero.
    std::vector<double> lambdas;
    int lambdaCount = 50;
    /// Smallest λ on the automatic path, as a fraction of λ_max. 0 = per-method
    /// default (1e-4 for lasso/ARD, 1e-8 for ridge, whose useful λ range runs
    /// much closer to zero because it never zeroes anything).
    double lambdaMinRatio = 0.0;

    /// 0 (or ≥ the number of configurations) means LEAVE-ONE-OUT, which is the
    /// CE convention and is what "the CV score" means in the CE literature.
    /// A positive k < n means k-fold, useful once n is in the hundreds.
    int cvFolds = 0;
    unsigned seed = 20260812; ///< fold assignment for k-fold only

    /// Select the largest λ whose CV score is within one standard error of the
    /// best (ESL §7.10). Deliberately biases toward FEWER orbits: the CV
    /// minimum is flat and noisy on the sparse side, and its exact location
    /// routinely keeps two or three spurious long-range clusters that the 1-SE
    /// rule drops at no cost in CV score.
    bool oneStandardError = false;

    /// Centre and scale columns to unit variance before fitting (and undo it
    /// after). Required for a penalty to mean anything when correlations of
    /// different cluster orders have wildly different magnitudes — an L1 or L2
    /// penalty is not scale-invariant, so without this the units of an orbit
    /// decide how hard it is penalised.
    bool standardize = true;

    int maxIterations = 100000;
    double tolerance = 1.0e-12;
    /// |ECI| at or below this is reported as inactive. Only cosmetic for lasso
    /// (which produces exact zeros) but needed for ridge and for the
    /// unstandardised rescaling.
    double zeroTolerance = 1.0e-12;
};

/// Optional description of one design-matrix COLUMN.
///
/// Deliberately not `ClusterOrbitSummary`: the correlation vector produced by
/// `generateClusterExpansion` is longer than the orbit list (point terms, and
/// one bucket per species tuple per orbit), so the column→orbit map is the
/// caller's business, not this module's. Keeping it a plain description also
/// keeps this translation unit free of `Structure` and of everything it drags
/// in — the fit test links exactly two objects.
struct EciColumn {
    std::string label;      ///< e.g. "pair 2.55 Å"
    int order = 0;          ///< 2 = pair, 3 = triplet, … 0 = unspecified
    double radius = 0.0;    ///< Å
    int multiplicity = 1;   ///< clusters per site in the orbit
};

/// One fitted interaction, ready to show a user.
struct EciTerm {
    int index = 0;
    std::string label;
    int order = 0;
    double radius = 0.0;
    int multiplicity = 1;
    double eci = 0.0;
    /// m·J — what the orbit actually contributes to the energy. A large ECI on
    /// a multiplicity-1 orbit and a small one on a multiplicity-24 orbit can be
    /// the same physics, and ranking by the bare ECI hides that.
    double weightedEci = 0.0;
};

struct EciFitResult {
    bool ok = false;
    std::string note;

    std::vector<double> eci;   ///< one per design-matrix column
    double intercept = 0.0;    ///< J₀ (never penalised)
    double lambda = 0.0;       ///< the selected regularisation strength
    int activeTerms = 0;       ///< non-zero ECIs

    /// THE diagnostic: RMS error over held-out configurations.
    double cvScore = 0.0;
    /// Training RMS error. Always ≤ cvScore in any healthy fit; a large gap is
    /// overfitting, and cvScore ≈ rmse ≫ 0 is an underpowered cluster set.
    double rmse = 0.0;
    double maxAbsError = 0.0;

    /// The whole path, so a caller can draw the CV-versus-λ curve that makes
    /// the trade-off visible instead of asserting it.
    std::vector<double> lambdaPath;
    std::vector<double> cvPath;
    std::vector<double> rmsePath;
    std::vector<int> activePath;
    int selectedIndex = -1; ///< index into the paths

    std::vector<EciTerm> terms;      ///< active terms, largest |m·J| first
    std::vector<double> predictions; ///< model energy per input configuration
    std::vector<double> residuals;   ///< predicted − supplied
};

/// Fit ECIs to `energies` (one per configuration, in whatever unit the caller
/// uses — normally eV/atom) from `correlations` (one row per configuration, one
/// column per orbit; every row must be the same length).
///
/// `columns`, when supplied, must have one entry per column and is used only to
/// label and rank the reported terms.
EciFitResult fitEffectiveClusterInteractions(
    const std::vector<std::vector<double>>& correlations,
    const std::vector<double>& energies, const EciFitOptions& options = {},
    const std::vector<EciColumn>& columns = {});

/// Human-readable summary: the two error numbers side by side, then the active
/// orbits with their multiplicities. Locale-independent — every number goes
/// through std::to_chars, never printf.
std::string formatEciReport(const EciFitResult& result);

} // namespace calango::core
