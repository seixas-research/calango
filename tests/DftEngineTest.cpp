// Native DFT engine scaffold.
//
// Two kinds of check, and the split is the point of the test:
//
//   * The implemented numerics are verified against CLOSED-FORM answers, not
//     against another calculation. A radial quadrature that agrees with a
//     second quadrature proves nothing; one that reproduces ∫r²e^{-2r}dr = 1/4
//     to twelve digits is measured. The same for the radial Poisson solve,
//     which is checked against the analytic Hartree potential of a hydrogen
//     1s density, and for the Pulay mixer, which is checked against a linear
//     fixed point whose solution is known exactly.
//
//   * The pieces with EXACT answers are checked against those answers, not
//     against another program: the eigensolver against a constructed spectrum,
//     the radial solver against the hydrogenic -Z^2/2n^2, the angular rules
//     against the orthonormality of the spherical harmonics, the
//     exchange-correlation potential against a finite difference of its own
//     energy. Where a reference code exists the numbers are cross-checked too
//     (GPAW's all-electron atom converges onto the silicon values pinned
//     below), but the primary check is arithmetic.
//
//   * What is NOT implemented is verified to REFUSE and to leave its output
//     untouched. A stub that returns a default-constructed result is one that
//     will eventually be wired into a UI and report an energy of zero as
//     though it meant it.

#include "core/AseScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "dft/AtomicSolver.hpp"
#include "dft/CalangoDFTEngine.hpp"
#include "dft/HamiltonianAssembler.hpp"
#include "dft/IntegrationGrid.hpp"
#include "dft/ForceCalculator.hpp"
#include "dft/KPointGrid.hpp"
#include "dft/LinearAlgebra.hpp"
#include "dft/NAOBasisSet.hpp"
#include "dft/RadialGrid.hpp"
#include "dft/SCFSolver.hpp"
#include "dft/XcFunctional.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace calango::dft;

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

/// The same, with the number that decided it. Printed for every check rather
/// than only the failures: a tolerance that is passing by a hair is worth
/// seeing before it starts failing.
void check(bool ok, const std::string& what, double value)
{
    std::printf("  %-4s %s  [%.10g]\n", ok ? "ok" : "FAIL", what.c_str(),
                value);
    if (!ok)
        ++failures;
}

void checkClose(double got, double expected, double tolerance,
                const std::string& what)
{
    const bool ok = std::abs(got - expected) <= tolerance;
    std::printf("  %-4s %s", ok ? "ok" : "FAIL", what.c_str());
    if (!ok)
        std::printf("  (got %.12g, expected %.12g)", got, expected);
    std::printf("\n");
    if (!ok)
        ++failures;
}

} // namespace

int main()
{
    std::printf("Radial mesh and quadrature:\n");
    {
        const RadialGrid grid(801, 40.0, 1.0e-5);
        check(grid.size() == 801, "an odd point count is kept as given");
        check(grid.r().front() == 0.0,
              "the mesh starts EXACTLY at the nucleus, where the cusp is");
        checkClose(grid.outerRadius(), 40.0, 1.0e-9,
                   "and reaches the requested outer radius");
        check(grid.r()[1] < grid.r()[2] - grid.r()[1],
              "spacing grows with r — dense at the core, coarse in the tail");

        const RadialGrid even(800, 40.0, 1.0e-5);
        check(even.size() == 801,
              "an even count is rounded up: Simpson needs an even number of "
              "intervals");

        // ∫₀^∞ r² e^{-2r} dr = 2!/2³ = 1/4. The integrand spans nine orders of
        // magnitude across the mesh, which is what the compensated summation
        // is for.
        std::vector<double> exponential(grid.size());
        for (std::size_t i = 0; i < grid.size(); ++i)
            exponential[i] = std::exp(-2.0 * grid.r()[i]);
        checkClose(grid.integrateSpherical(exponential), 0.25, 1.0e-10,
                   "∫r²e^{-2r}dr = 1/4");

        // ∫₀^∞ r⁴ e^{-r} dr = 4! = 24 — a slower decay, so the OUTER end of
        // the mesh is what is being tested here rather than the inner.
        std::vector<double> slower(grid.size());
        for (std::size_t i = 0; i < grid.size(); ++i) {
            const double r = grid.r()[i];
            slower[i] = r * r * std::exp(-r);
        }
        checkClose(grid.integrateSpherical(slower), 24.0, 1.0e-6,
                   "∫r⁴e^{-r}dr = 24");

        // The hydrogen 1s density, normalised: ∫ρ dV = 1 electron.
        std::vector<double> density(grid.size());
        for (std::size_t i = 0; i < grid.size(); ++i)
            density[i] = std::exp(-2.0 * grid.r()[i]) / M_PI;
        checkClose(4.0 * M_PI * grid.integrateSpherical(density), 1.0, 1.0e-10,
                   "the hydrogen 1s density integrates to one electron");

        check(grid.integrate({1.0, 2.0}) == 0.0,
              "a length mismatch integrates to zero rather than reading out "
              "of bounds");
    }

    std::printf("Interpolation:\n");
    {
        const RadialGrid grid(401, 20.0, 1.0e-4);
        std::vector<double> values(grid.size());
        for (std::size_t i = 0; i < grid.size(); ++i)
            values[i] = std::exp(-grid.r()[i]);
        checkClose(grid.interpolate(values, 1.0), std::exp(-1.0), 1.0e-8,
                   "cubic interpolation reproduces e^{-r} at r = 1");
        checkClose(grid.interpolate(values, 0.001), std::exp(-0.001), 1.0e-8,
                   "and close to the nucleus, where the mesh is finest");
        check(grid.interpolate(values, 25.0) == 0.0,
              "beyond the mesh it is exactly zero — a confined basis function "
              "IS zero there");
    }

    std::printf("Radial Poisson solve:\n");
    {
        // The Hartree potential of the hydrogen 1s density has a closed form:
        //     V(r) = 1/r − e^{-2r}(1 + 1/r)
        // finite at the origin (V(0) = 1) and going as 1/r far away. Checking
        // both limits matters: the small-r behaviour is where the 0/0 in the
        // enclosed-charge term lives, and the large-r behaviour is what says
        // the total charge came out right.
        const RadialGrid grid(1601, 40.0, 1.0e-6);
        std::vector<double> density(grid.size());
        for (std::size_t i = 0; i < grid.size(); ++i)
            density[i] = std::exp(-2.0 * grid.r()[i]) / M_PI;

        const std::vector<double> potential = grid.hartreePotential(density);
        check(potential.size() == grid.size(),
              "the potential is returned on the same mesh");
        check(std::isfinite(potential.front()),
              "and is finite at r = 0, where 1/r is not");
        checkClose(potential.front(), 1.0, 1.0e-5,
                   "V(0) = 1 hartree for the hydrogen 1s density");

        const auto analytic = [](double r) {
            return r > 0.0 ? 1.0 / r - std::exp(-2.0 * r) * (1.0 + 1.0 / r)
                           : 1.0;
        };
        double worst = 0.0;
        for (std::size_t i = 0; i < grid.size(); ++i) {
            const double r = grid.r()[i];
            if (r < 1.0e-3 || r > 20.0)
                continue; // the tail is 1e-18; a relative test there is noise
            const double reference = analytic(r);
            if (std::abs(reference) < 1.0e-12)
                continue;
            worst = std::max(
                worst, std::abs(potential[i] - reference) / std::abs(reference));
        }
        check(worst < 1.0e-6,
              "and matches 1/r − e^{-2r}(1 + 1/r) everywhere between "
              "10^-3 and 20 bohr (worst relative error "
                  + std::to_string(worst) + ")");

        check(grid.hartreePotential({1.0, 2.0}).size() == grid.size(),
              "a malformed density gives a zero potential of the right "
              "length, not a short one");
    }

    std::printf("Density mixing:\n");
    {
        // A linear fixed point with a known answer and a deliberately awful
        // gain: x_out = A x_in + b with A having eigenvalues near -1, which is
        // exactly the charge-sloshing regime that makes plain feedback
        // diverge. Linear mixing crawls; Pulay should not.
        const std::size_t n = 6;
        const auto step = [n](const std::vector<double>& in) {
            std::vector<double> out(n, 0.0);
            for (std::size_t i = 0; i < n; ++i) {
                out[i] = 1.0 + 0.9 * in[(i + 1) % n] - 0.85 * in[i];
            }
            return out;
        };

        const auto iterate = [&](int history, int maxIterations) {
            Parameters parameters;
            parameters.mixingFraction = 0.3;
            parameters.mixingHistory = history;
            DensityMixer mixer(parameters);
            std::vector<double> x(n, 0.0);
            for (int iteration = 0; iteration < maxIterations; ++iteration) {
                const std::vector<double> out = step(x);
                if (DensityMixer::residualNorm(x, out) < 1.0e-10)
                    return iteration;
                x = mixer.mix(x, out);
            }
            return maxIterations;
        };

        const int linear = iterate(0, 5000);
        const int pulay = iterate(8, 5000);
        check(linear < 5000, "linear mixing converges on the test problem");
        check(pulay < 5000, "so does Pulay");
        check(pulay < linear / 2,
              "and Pulay gets there in under half the iterations ("
                  + std::to_string(pulay) + " vs " + std::to_string(linear)
                  + ")");

        Parameters parameters;
        DensityMixer mixer(parameters);
        const std::vector<double> a{1.0, 2.0};
        check(mixer.mix(a, {1.0}).size() == a.size(),
              "a length mismatch returns the input unchanged rather than a "
              "truncated mixture");
        mixer.mix(a, {1.5, 2.5});
        check(mixer.historySize() == 1, "the history records one entry");
        mixer.reset();
        check(mixer.historySize() == 0,
              "and reset clears it — extrapolating across a changed "
              "Hamiltonian is worse than not extrapolating");
    }

    std::printf("Self-consistency loop:\n");
    {
        // The loop is driven by a callback, so its convergence logic can be
        // tested without any of the physics existing.
        struct Context {
            int calls = 0;
        };
        Context context;
        const auto step = [](const std::vector<double>& in,
                             std::vector<double>& out, EnergyBreakdown& energy,
                             void* raw) {
            auto* ctx = static_cast<Context*>(raw);
            ++ctx->calls;
            out.assign(in.size(), 0.0);
            for (std::size_t i = 0; i < in.size(); ++i)
                out[i] = 0.5 * in[i] + 1.0; // fixed point at 2
            double sum = 0.0;
            for (const double v : out)
                sum += v;
            energy.total = -sum;
            return Outcome::success();
        };

        Parameters parameters;
        parameters.maxIterations = 200;
        SCFSolver solver(parameters);
        SCFSolver::Report report =
            solver.run(std::vector<double>(4, 0.0), step, &context);
        check(report.outcome.ok(), "a well-behaved fixed point converges");
        check(report.iterations > 1,
              "and never in one iteration — the first has no previous energy "
              "to compare against");
        check(report.residualHistory.size()
                  == static_cast<std::size_t>(report.iterations),
              "with one residual recorded per iteration, for the plot");
        check(report.residualHistory.back() <= report.residualHistory.front(),
              "and the residual fell");

        // A step that never settles must be reported as NotConverged, not as
        // a success with a bad number. The map below has NO fixed point — it
        // walks away by 1 every call — which is a sharper test than a tight
        // tolerance on a convergent map, because Pulay solves a linear fixed
        // point essentially exactly and would meet almost any tolerance.
        const auto runaway = [](const std::vector<double>& in,
                                std::vector<double>& out,
                                EnergyBreakdown& energy, void* raw) {
            auto* ctx = static_cast<Context*>(raw);
            ++ctx->calls;
            out.assign(in.size(), 0.0);
            for (std::size_t i = 0; i < in.size(); ++i)
                out[i] = in[i] + 1.0;
            energy.total = static_cast<double>(ctx->calls);
            return Outcome::success();
        };
        Parameters impatient;
        impatient.maxIterations = 5;
        SCFSolver strict(impatient);
        Context second;
        SCFSolver::Report unconverged =
            strict.run(std::vector<double>(4, 0.0), runaway, &second);
        check(unconverged.outcome.status == Status::NotConverged,
              "an unreachable tolerance reports NotConverged");
        check(unconverged.iterations == 5, "after exactly the iteration limit");

        SCFSolver::Report noStep =
            solver.run(std::vector<double>(4, 0.0), nullptr, nullptr);
        check(noStep.outcome.status == Status::InvalidInput,
              "a missing step function is rejected");
        SCFSolver::Report noDensity = solver.run({}, step, &context);
        check(noDensity.outcome.status == Status::InvalidInput,
              "and so is an empty initial density");
    }

    std::printf("Linear algebra:\n");
    {
        using namespace calango::dft::linalg;
        // A matrix whose spectrum is known by construction: Q D Qᵀ for an
        // explicit orthogonal Q. Checking against arithmetic rather than
        // against another eigensolver, which would only prove they agree.
        const std::size_t n = 4;
        std::vector<double> d = {-3.0, -0.5, 0.25, 7.0};
        std::vector<double> q(n * n, 0.0);
        // Householder reflector I - 2vvᵀ/vᵀv: orthogonal for any v.
        const std::vector<double> v = {1.0, 2.0, -1.0, 0.5};
        double vv = 0.0;
        for (const double x : v)
            vv += x * x;
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j)
                q[i * n + j] = (i == j ? 1.0 : 0.0) - 2.0 * v[i] * v[j] / vv;
        std::vector<double> a(n * n, 0.0);
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j)
                for (std::size_t k = 0; k < n; ++k)
                    a[i * n + j] += q[i * n + k] * d[k] * q[j * n + k];

        std::vector<double> values;
        std::vector<double> vectors;
        check(symmetricEigen(a, n, values, vectors).status == Status::Ok,
              "the symmetric eigensolver runs");
        double worst = 0.0;
        for (std::size_t k = 0; k < n; ++k)
            worst = std::max(worst, std::abs(values[k] - d[k]));
        check(worst < 1.0e-12, "and recovers a constructed spectrum exactly",
              worst);
        // Ascending order is not cosmetic: every occupation rule downstream
        // assumes the lowest state is first.
        check(std::is_sorted(values.begin(), values.end()),
              "with the eigenvalues ascending");
        double orthogonality = 0.0;
        for (std::size_t k = 0; k < n; ++k)
            for (std::size_t l = 0; l < n; ++l) {
                double dot = 0.0;
                for (std::size_t i = 0; i < n; ++i)
                    dot += vectors[i * n + k] * vectors[i * n + l];
                orthogonality =
                    std::max(orthogonality, std::abs(dot - (k == l ? 1.0 : 0.0)));
            }
        check(orthogonality < 1.0e-12, "and orthonormal eigenvectors",
              orthogonality);

        // Generalised: HC = SCE with S = I gives back the ordinary problem,
        // which is the one case with an answer already in hand.
        std::vector<double> identity(n * n, 0.0);
        for (std::size_t i = 0; i < n; ++i)
            identity[i * n + i] = 1.0;
        std::vector<double> gValues;
        std::vector<double> gVectors;
        std::size_t discarded = 0;
        check(solveGeneralized(a, identity, n, gValues, gVectors, &discarded)
                      .status
                  == Status::Ok,
              "the generalised solver runs with a unit overlap");
        worst = 0.0;
        for (std::size_t k = 0; k < n; ++k)
            worst = std::max(worst, std::abs(gValues[k] - d[k]));
        check(worst < 1.0e-11, "and reduces to the ordinary problem", worst);
        check(discarded == 0, "with nothing discarded from a full-rank basis");

        // A DEFECTIVE overlap: two identical basis functions. The whole point
        // of canonical orthogonalisation is that this is survivable — Cholesky
        // would fail or, worse, return noise amplified by 1/sqrt(s).
        const std::size_t m = 3;
        std::vector<double> sing = {1.0, 1.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0};
        std::vector<double> hs = {2.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 0.0, 1.0};
        std::vector<double> sValues;
        std::vector<double> sVectors;
        const Outcome singular =
            solveGeneralized(hs, sing, m, sValues, sVectors, &discarded);
        check(singular.status == Status::Ok,
              "a linearly dependent basis is handled, not rejected");
        check(discarded == 1,
              "with exactly the lost direction removed", double(discarded));
        check(sValues.size() == 2,
              "leaving a problem of the rank the basis actually had");

        // Hermitian: a real matrix embedded as complex must give the same
        // answer. This is the check that the 2n x 2n real embedding, and the
        // every-second-eigenvalue rule that goes with it, are right.
        std::vector<std::complex<double>> ha(n * n);
        std::vector<std::complex<double>> hi(n * n);
        for (std::size_t i = 0; i < n * n; ++i) {
            ha[i] = a[i];
            hi[i] = identity[i];
        }
        std::vector<double> hValues;
        std::vector<std::complex<double>> hVectors;
        check(solveGeneralizedHermitian(ha, hi, n, hValues, hVectors).status
                  == Status::Ok,
              "the Hermitian solver runs");
        worst = 0.0;
        for (std::size_t k = 0; k < n && k < hValues.size(); ++k)
            worst = std::max(worst, std::abs(hValues[k] - d[k]));
        check(hValues.size() == n && worst < 1.0e-11,
              "and a real matrix through the complex path gives the same "
              "spectrum",
              worst);
    }

    std::printf("Exchange-correlation:\n");
    {
        // Exchange has a closed form, and v_x = (4/3) eps_x EXACTLY because
        // rho*eps_x scales as rho^(4/3). That ratio is the cheapest possible
        // check that an exchange implementation is right.
        const XcResult x = Lda::exchange(0.05);
        const double expected = -0.75 * std::cbrt(3.0 / 3.14159265358979323846)
            * std::cbrt(0.05);
        check(std::abs(x.energyPerElectron - expected) < 1.0e-14,
              "Dirac exchange matches its closed form");
        check(std::abs(x.potential - 4.0 / 3.0 * x.energyPerElectron) < 1.0e-14,
              "and v_x is exactly four thirds of eps_x");

        // The three correlation fits are interpolations of the SAME Monte
        // Carlo data, so they have to agree to the accuracy of that data —
        // well under a millihartree per electron across the range that
        // matters. Wide disagreement means one of them is mistyped.
        for (const double density : {1.0e-3, 1.0e-2, 0.1, 1.0}) {
            const double pw =
                Lda::correlation(density, XcFunctional::LdaPw).energyPerElectron;
            const double vwn =
                Lda::correlation(density, XcFunctional::LdaVwn).energyPerElectron;
            const double pz =
                Lda::correlation(density, XcFunctional::LdaPz).energyPerElectron;
            check(std::abs(pw - vwn) < 1.5e-3 && std::abs(pw - pz) < 1.5e-3,
                  "PW92, VWN and PZ correlation agree at rho = "
                      + std::to_string(density),
                  std::max(std::abs(pw - vwn), std::abs(pw - pz)));
        }

        // v_c = eps_c - (r_s/3) d(eps_c)/d(r_s), differentiated analytically in
        // the source. Checked here against a finite difference, which is the
        // only way a sign error in that derivative gets caught.
        for (const auto functional :
             {XcFunctional::LdaPw, XcFunctional::LdaVwn, XcFunctional::LdaPz}) {
            const double density = 0.037;
            const double h = 1.0e-6 * density;
            const auto energy = [functional](double n) {
                return n * Lda::evaluate(n, functional).energyPerElectron;
            };
            const double numeric =
                (energy(density + h) - energy(density - h)) / (2.0 * h);
            const double analytic = Lda::evaluate(density, functional).potential;
            check(std::abs(numeric - analytic) < 1.0e-6,
                  "v_xc is the derivative of rho*eps_xc it claims to be",
                  std::abs(numeric - analytic));
        }

        check(Lda::evaluate(0.0, XcFunctional::LdaPw).potential == 0.0,
              "zero density gives zero, not a NaN from a cube root");
        check(Lda::evaluate(-1.0, XcFunctional::LdaPw).potential == 0.0,
              "and so does a negative one, which a difference density produces");
        check(!Lda::supports(XcFunctional::GgaPbe),
              "PBE is declared but NOT implemented, and says so rather than "
              "quietly substituting LDA");
    }

    std::printf("PBE gradient functional:\n");
    {
        // A GGA is written as f(rho, sigma) with sigma = |grad rho|^2, and the
        // two derivatives are what the matrix element is built from. Both are
        // checked against a finite difference of f itself -- the only test
        // that catches a sign or a missing chain-rule term, and the PBE
        // correlation has one that is easy to miss: A depends on the density
        // through eps_c, so df/drho has a term through A.
        double worstRho = 0.0;
        double worstSigma = 0.0;
        for (const double density : {1.0e-3, 1.0e-2, 0.1, 1.0, 10.0})
            for (const double sigma : {1.0e-2, 1.0, 100.0}) {
                const XcPoint point =
                    Xc::evaluate(density, sigma, XcFunctional::GgaPbe);
                const double hr = 1.0e-6 * density;
                const double numericRho =
                    (Xc::evaluate(density + hr, sigma, XcFunctional::GgaPbe)
                         .energyDensity
                     - Xc::evaluate(density - hr, sigma, XcFunctional::GgaPbe)
                           .energyDensity)
                    / (2.0 * hr);
                const double hs = 1.0e-6 * sigma;
                const double numericSigma =
                    (Xc::evaluate(density, sigma + hs, XcFunctional::GgaPbe)
                         .energyDensity
                     - Xc::evaluate(density, sigma - hs, XcFunctional::GgaPbe)
                           .energyDensity)
                    / (2.0 * hs);
                worstRho = std::max(worstRho,
                                    std::abs(numericRho - point.dfdrho)
                                        / std::max(1.0e-8,
                                                   std::abs(point.dfdrho)));
                // Scaled by the ENERGY CONTRIBUTION sigma*df/dsigma rather
                // than by df/dsigma itself. At high density and small sigma
                // the derivative is of order 1e-13 while f is of order 10, so
                // a finite difference of f cannot resolve it at all and a
                // relative test against it measures this check's own roundoff.
                // What has to be right is the contribution to the energy.
                worstSigma = std::max(
                    worstSigma,
                    std::abs(numericSigma - point.dfdsigma) * sigma
                        / std::max(1.0e-10, std::abs(point.energyDensity)));
            }
        check(worstRho < 1.0e-5, "df/drho is the derivative of f it claims",
              worstRho);
        check(worstSigma < 1.0e-7,
              "and so is df/dsigma, measured through the energy it carries",
              worstSigma);

        // At zero gradient a GGA must reduce EXACTLY to its parent LDA. PBE's
        // parent is PW92, and the enhancement factor is 1 at s = 0.
        double worstLimit = 0.0;
        for (const double density : {1.0e-3, 0.1, 5.0}) {
            const XcPoint gga =
                Xc::evaluate(density, 0.0, XcFunctional::GgaPbe);
            const XcResult lda = Lda::evaluate(density, XcFunctional::LdaPw);
            worstLimit = std::max(
                worstLimit,
                std::abs(gga.energyDensity - density * lda.energyPerElectron)
                    / std::abs(density * lda.energyPerElectron));
        }
        check(worstLimit < 1.0e-14,
              "and at sigma = 0 PBE is EXACTLY LDA-PW92, as its construction "
              "requires",
              worstLimit);

        check(Xc::needsGradients(XcFunctional::GgaPbe),
              "PBE declares that it needs density gradients");
        check(!Xc::needsGradients(XcFunctional::LdaPw),
              "and no LDA does — which is what keeps the local path free of "
              "the memory and time a gradient costs");
        check(Xc::evaluate(0.0, 1.0, XcFunctional::GgaPbe).energyDensity == 0.0,
              "zero density gives zero, not a division by rho^(4/3)");
    }

    std::printf("Basis-function gradients:\n");
    {
        // grad(phi) against a central difference of phi. It caught a real
        // problem: du/dr from a three-point stencil left the SPLIT-VALENCE
        // functions about a percent off, because a split function is already
        // the difference of two similar quantities and loses digits to
        // cancellation before any stencil error is added. The five-point form
        // brings every function to a few parts in ten thousand, where what is
        // left is this check's own finite difference of an interpolant.
        Parameters parameters;
        parameters.confinementRadiusA = 3.0;
        parameters.confinementWidthA = 0.8;
        NAOBasisSet basis(RadialGrid(2001, 50.0, 1.0e-6));
        check(basis.generate({{14, 14.0}}, parameters, 2).status == Status::Ok,
              "a tier-2 silicon basis generates");
        const std::vector<BasisFunctionIndex> functions = basis.enumerate({14});
        std::vector<double> values;
        std::vector<double> kinetic;
        std::vector<double> gradients;
        std::vector<double> plus;
        std::vector<double> minus;
        std::vector<double> scratch;
        double worst = 0.0;
        for (const double radius : {0.3, 0.9, 1.7, 3.0, 4.5})
            for (int axis = 0; axis < 3; ++axis) {
                std::array<double, 3> base{{0.31, -0.57, 0.76}};
                const double norm = std::sqrt(base[0] * base[0]
                                              + base[1] * base[1]
                                              + base[2] * base[2]);
                for (double& component : base)
                    component *= radius / norm;
                std::vector<std::array<double, 3>> at(functions.size(), base);
                basis.evaluateWithGradients(functions, at, values, kinetic,
                                            gradients);
                const double step = 1.0e-5;
                std::vector<std::array<double, 3>> shifted = at;
                for (std::array<double, 3>& d : shifted)
                    d[static_cast<std::size_t>(axis)] += step;
                basis.evaluate(functions, shifted, plus, scratch);
                shifted = at;
                for (std::array<double, 3>& d : shifted)
                    d[static_cast<std::size_t>(axis)] -= step;
                basis.evaluate(functions, shifted, minus, scratch);
                for (std::size_t i = 0; i < functions.size(); ++i) {
                    const double numeric =
                        (plus[i] - minus[i]) / (2.0 * step);
                    const double analytic =
                        gradients[i * 3 + static_cast<std::size_t>(axis)];
                    const double scale = std::max(
                        {std::abs(numeric), std::abs(analytic), 1.0e-3});
                    worst = std::max(worst,
                                     std::abs(numeric - analytic) / scale);
                }
            }
        check(worst < 2.0e-3,
              "grad(phi) matches a finite difference of phi for every function "
              "of a double-zeta-plus-polarisation basis",
              worst);
    }

    std::printf("Atomic solver against exact arithmetic:\n");
    {
        Parameters parameters;
        AtomicSolver solver(RadialGrid(2001, 60.0, 1.0e-6), parameters);
        // With no electrons the potential is the bare nucleus, whose spectrum
        // is -Z^2/2n^2. No functional, no reference code, no tolerance chosen
        // to make it pass: this is the radial integrator against a closed form.
        const std::vector<double> bare(solver.grid().size(), 0.0);
        double worst = 0.0;
        for (int n = 1; n <= 3; ++n)
            for (int l = 0; l < n; ++l) {
                AtomicShell shell;
                shell.principal = n;
                shell.l = l;
                shell.label = AtomicSolver::shellLabel(n, l);
                const Outcome outcome = solver.solveOrbital(14.0, bare, 0.0, shell);
                check(outcome.status == Status::Ok,
                      "the bare-nucleus " + shell.label + " state solves");
                const double exact = -0.5 * 196.0 / (n * n);
                worst = std::max(worst,
                                 std::abs(shell.eigenvalue - exact) / std::abs(exact));
            }
        check(worst < 1.0e-7,
              "and every hydrogenic eigenvalue is exact to seven digits", worst);

        // The self-consistent silicon atom. These numbers are not a snapshot
        // of whatever the code happened to print: GPAW's own all-electron
        // atomic solver converges ONTO them as its radial grid is refined
        // (1s: -65.1817 at its default mesh, -65.18429 at 7500 points, against
        // -65.18430 here), and this side is grid-converged to eight digits.
        const AtomicResult silicon = solver.solve(14);
        check(silicon.outcome.status == Status::Ok,
              "the silicon atom reaches self-consistency",
              silicon.finalResidual);
        check(silicon.shells.size() == 5,
              "with five occupied shells: 1s 2s 2p 3s 3p");
        check(std::abs(silicon.totalEnergy + 288.19374) < 2.0e-4,
              "total energy -288.19374 Ha (LDA-PW92)", silicon.totalEnergy);
        check(std::abs(silicon.shells[0].eigenvalue + 65.18430) < 2.0e-4,
              "1s eigenvalue -65.18430 Ha", silicon.shells[0].eigenvalue);
        check(std::abs(silicon.shells.back().eigenvalue + 0.15331) < 2.0e-4,
              "3p eigenvalue -0.15331 Ha", silicon.shells.back().eigenvalue);
        // The energy decomposition must add up. It is assembled from four
        // independently computed integrals, so this catches a term counted
        // twice or dropped.
        const double sum = silicon.kinetic + silicon.externalEnergy
            + silicon.hartreeEnergy + silicon.xcEnergy;
        check(std::abs(sum - silicon.totalEnergy) < 1.0e-9,
              "and the decomposition sums to the total", sum - silicon.totalEnergy);

        // Aufbau order, which decides which orbitals a basis is built from.
        const std::vector<AtomicShell> iron =
            AtomicSolver::groundStateConfiguration(26);
        check(iron.size() == 7, "iron fills seven shells");
        check(iron[5].label == "4s" && iron[6].label == "3d",
              "with 4s BEFORE 3d, as the Madelung order requires");
    }

    std::printf("Angular quadrature and the multicentre partition:\n");
    {
        // A Lebedev rule of degree d integrates every spherical harmonic
        // product up to that degree exactly. A mistyped weight or a permuted
        // orbit still sums to one and still integrates constants — it fails
        // here and nowhere else, as an energy wrong in the third digit.
        const int degrees[] = {3, 5, 7, 11};
        const int counts[] = {6, 14, 26, 50};
        for (int i = 0; i < 4; ++i) {
            const std::vector<AngularPoint> rule =
                AngularGrid::lebedev(counts[i]);
            check(rule.size() == static_cast<std::size_t>(counts[i]),
                  "Lebedev " + std::to_string(counts[i]) + " has its points");
            double weight = 0.0;
            for (const AngularPoint& point : rule)
                weight += point.weight;
            check(std::abs(weight - 1.0) < 1.0e-14, "with weights summing to 1",
                  weight - 1.0);
            const int lMax = degrees[i] / 2;
            const std::size_t harmonics = AngularGrid::harmonicCount(lMax);
            std::vector<double> gram(harmonics * harmonics, 0.0);
            std::vector<double> row;
            for (const AngularPoint& point : rule) {
                AngularGrid::realSphericalHarmonics(point.x, point.y, point.z,
                                                    lMax, row);
                for (std::size_t a = 0; a < harmonics; ++a)
                    for (std::size_t b = 0; b < harmonics; ++b)
                        gram[a * harmonics + b] +=
                            point.weight * row[a] * row[b] * 4.0
                            * 3.14159265358979323846;
            }
            double error = 0.0;
            for (std::size_t a = 0; a < harmonics; ++a)
                for (std::size_t b = 0; b < harmonics; ++b)
                    error = std::max(error,
                                     std::abs(gram[a * harmonics + b]
                                              - (a == b ? 1.0 : 0.0)));
            check(error < 1.0e-13,
                  "and integrates spherical harmonics exactly to l = "
                      + std::to_string(lMax),
                  error);
        }

        // The Becke partition over a PERIODIC cell. Integrating unity has to
        // give the cell volume: that is the statement that the atom-centred
        // weights, summed over every atom and every image, come to one
        // everywhere. Nothing else in the engine tests the periodic partition
        // on its own.
        const double a = 5.43 * 1.8897261254578281;
        std::vector<IntegrationGrid::Atom> atoms = {{14, {{0.0, 0.0, 0.0}}},
                                                    {14,
                                                     {{a / 4, a / 4, a / 4}}}};
        std::vector<std::array<double, 3>> lattice = {{{0.0, a / 2, a / 2}},
                                                      {{a / 2, 0.0, a / 2}},
                                                      {{a / 2, a / 2, 0.0}}};
        IntegrationGrid grid;
        check(grid.build(atoms, lattice, 60, 29, 8.5).status == Status::Ok,
              "the periodic multicentre grid builds");
        std::vector<double> ones(grid.size(), 1.0);
        const double volume = a * a * a / 4.0;
        const double integrated = grid.integrate(ones);
        check(std::abs(integrated - volume) / volume < 1.0e-3,
              "and integrates unity to the cell volume",
              std::abs(integrated - volume) / volume);
    }

    std::printf("Basis generation:\n");
    {
        NAOBasisSet basis(RadialGrid(1201, 40.0, 1.0e-6));
        check(basis.forSpecies(6) == nullptr,
              "an absent species has no basis");

        const Outcome empty = basis.generate({}, {}, 1);
        check(empty.status == Status::InvalidInput,
              "generating a basis for no species is rejected");
        Parameters bad;
        bad.confinementRadiusA = -1.0;
        check(basis.generate({{6, 6.0}}, bad, 1).status == Status::InvalidInput,
              "as is a non-positive confinement radius");
        check(basis.generate({{6, 6.0}}, {}, 0).status == Status::InvalidInput,
              "as is a tier count below one");
        check(basis.generate({{6, 6.0}}, {}, 4).status == Status::NotImplemented,
              "and a tier beyond the third reports NotImplemented rather than "
              "quietly returning a smaller basis than was asked for");

        Parameters parameters;
        parameters.confinementRadiusA = 3.0;
        check(basis.generate({{14, 14.0}}, parameters, 1).status == Status::Ok,
              "silicon generates");
        const SpeciesBasis* silicon = basis.forSpecies(14);
        check(silicon != nullptr, "and is installed");
        if (silicon != nullptr) {
            check(silicon->functions.size() == 5,
                  "with five radial functions, the CORE INCLUDED — this is an "
                  "all-electron method and there is no pseudopotential "
                  "standing in for 1s");
            check(silicon->functionCount() == 9,
                  "which is 9 basis functions once m is counted",
                  double(silicon->functionCount()));
            // Strict locality is the entire point of confinement: "small"
            // is not "zero", and a tail that is merely small leaves every pair
            // of atoms in a crystal weakly coupled.
            bool zeroOutside = true;
            const std::vector<double>& r = basis.grid().r();
            for (const RadialFunction& function : silicon->functions)
                for (std::size_t i = 0; i < r.size(); ++i)
                    if (r[i] > function.cutoffBohr && function.u[i] != 0.0)
                        zeroOutside = false;
            check(zeroOutside,
                  "and every function is EXACTLY zero beyond its cutoff");
            check(!silicon->freeAtomDensity.empty()
                      && !silicon->neutralAtomPotential.empty()
                      && !silicon->atomicPotential.empty(),
                  "carrying the free-atom density, the neutral-atom potential "
                  "and the potential its own orbitals solve");
            // v^NA is the potential of a NEUTRAL object, so it decays
            // exponentially rather than as 1/r. That single property is what
            // makes the lattice sum in the assembler converge at all.
            const std::size_t far = r.size() - 40;
            check(std::abs(silicon->neutralAtomPotential[far]) < 1.0e-6,
                  "and the neutral-atom potential has decayed to zero far out",
                  silicon->neutralAtomPotential[far]);
        }
    }

    std::printf("Basis tiers:\n");
    {
        // Every basis function must satisfy the identity that defines its
        // stored kinetic array:
        //
        //     <phi|T|phi> = int u*(tu) dr = 1/2 int [ u'^2 + l(l+1)u^2/r^2 ] dr
        //
        // The right-hand side knows nothing about how the function was built —
        // it is the gradient form of the kinetic energy — so this catches a
        // wrong analytic derivative in the split-valence construction, which
        // is otherwise completely silent: the SCF converges, the electron
        // count is exact, and only the total energy is wrong.
        Parameters parameters;
        parameters.confinementRadiusA = 3.0;
        std::vector<std::size_t> counts;
        for (int tier = 1; tier <= 3; ++tier) {
            NAOBasisSet basis(RadialGrid(2001, 50.0, 1.0e-6));
            const Outcome outcome = basis.generate({{14, 14.0}}, parameters, tier);
            check(outcome.status == Status::Ok,
                  "silicon generates at tier " + std::to_string(tier));
            const SpeciesBasis* silicon = basis.forSpecies(14);
            if (silicon == nullptr)
                continue;
            counts.push_back(silicon->functions.size());
            const RadialGrid& grid = basis.grid();
            double worst = 0.0;
            bool localised = true;
            for (const RadialFunction& function : silicon->functions) {
                const std::size_t n = grid.size();
                std::vector<double> stored(n, 0.0);
                std::vector<double> gradient(n, 0.0);
                for (std::size_t i = 0; i < n; ++i)
                    stored[i] = function.u[i] * function.kineticU[i];
                for (std::size_t i = 1; i + 1 < n; ++i) {
                    const double du = 0.5 * (function.u[i + 1] - function.u[i - 1])
                        / grid.drdi()[i];
                    const double r = grid.r()[i];
                    gradient[i] = 0.5
                        * (du * du
                           + function.l * (function.l + 1) * function.u[i]
                               * function.u[i] / (r * r));
                }
                const double a = grid.integrate(stored);
                const double b = grid.integrate(gradient);
                worst = std::max(worst, std::abs(a - b) / std::max(1.0, std::abs(b)));
                for (std::size_t i = 0; i < n; ++i)
                    if (grid.r()[i] > function.cutoffBohr
                        && (function.u[i] != 0.0 || function.kineticU[i] != 0.0))
                        localised = false;
            }
            // 3e-3 is set by the CHECK, not by the arrays: the gradient form
            // above differentiates u by a central difference, and for the d
            // polarisation function the l(l+1)u²/r² term makes that the
            // dominant error. A genuinely wrong analytic derivative in the
            // split construction would miss by order one, not by a thousandth.
            check(worst < 3.0e-3,
                  "and every function's kinetic array matches the gradient "
                  "form",
                  worst);
            check(localised,
                  "with the function AND its kinetic array exactly zero beyond "
                  "the cutoff");
        }
        check(counts.size() == 3 && counts[0] == 5 && counts[1] == 8
                  && counts[2] == 11,
              "the tiers nest: 5 radial functions, then 8 (two split valence "
              "channels plus a d polarisation shell), then 11");
    }

    std::printf("Basis tiers are variational:\n");
    {
        // Adding functions to a basis can only LOWER the energy. That is the
        // whole reason a tier hierarchy is meaningful, and it is the check
        // that catches a new function whose matrix elements are wrong —
        // a broken one lets the energy go anywhere, usually down.
        //
        // The FREE ATOM is the right system for it: the minimal basis is
        // already that atom's own orbitals, so the gain is small and any
        // large drop is a bug rather than physics.
        calango::core::Structure atom;
        calango::core::Atom silicon;
        silicon.atomicNumber = 14;
        silicon.position = {0.0, 0.0, 0.0};
        atom.addAtom(silicon);

        double previous = 0.0;
        for (int tier = 1; tier <= 3; ++tier) {
            Parameters parameters;
            parameters.radialShells = 60;
            parameters.angularPoints = 17;
            parameters.confinementRadiusA = 3.0;
            parameters.basisTiers = tier;
            CalangoDFTEngine engine(parameters);
            const CalangoDFTEngine::Result result = engine.run(atom);
            check(result.outcome.status == Status::Ok,
                  "tier " + std::to_string(tier) + " converges",
                  result.energy.total);
            if (tier > 1) {
                check(result.energy.total <= previous + 1.0e-6,
                      "and does not raise the energy above the tier below",
                      result.energy.total - previous);
                check(previous - result.energy.total < 1.0,
                      "by an amount an already-near-exact atomic basis can "
                      "plausibly gain (under 1 eV)",
                      previous - result.energy.total);
            }
            previous = result.energy.total;
        }
    }

    std::printf("Brillouin-zone symmetry:\n");
    {
        // Group orders and irreducible-wedge sizes are published, checkable
        // numbers, and they were checked against an independent symmetry
        // analyser. They also caught a real bug: the images of the lattice
        // vectors are the COLUMNS of the operation matrix, not the rows, and
        // building them as rows yields the transposed group -- same size,
        // still a group, and completely invisible for a crystal with one atom
        // at the origin. Both single-atom cases below passed while diamond
        // was wrong, which is exactly why diamond is here.
        const double a = 5.43 * 1.8897261254578281;
        const std::vector<std::array<double, 3>> fcc = {
            {{0.0, a / 2, a / 2}}, {{a / 2, 0.0, a / 2}}, {{a / 2, a / 2, 0.0}}};
        const std::vector<std::array<double, 3>> cubic = {
            {{a, 0.0, 0.0}}, {{0.0, a, 0.0}}, {{0.0, 0.0, a}}};
        const std::vector<KPointGrid::Atom> diamond = {
            {14, {{0.0, 0.0, 0.0}}}, {14, {{a / 4, a / 4, a / 4}}}};
        const std::vector<KPointGrid::Atom> single = {{14, {{0.0, 0.0, 0.0}}}};

        check(KPointGrid::latticePointGroup(fcc).size() == 48,
              "the face-centred cubic LATTICE has 48 point operations",
              double(KPointGrid::latticePointGroup(fcc).size()));
        check(KPointGrid::crystalPointGroup(fcc, diamond).size() == 48,
              "and so does the diamond CRYSTAL on it, its two-atom basis "
              "costing none of them",
              double(KPointGrid::crystalPointGroup(fcc, diamond).size()));

        // Break the symmetry and it must fall.
        std::vector<KPointGrid::Atom> displaced = diamond;
        displaced[0].position[0] += 0.2 * 1.8897261254578281;
        const std::size_t broken =
            KPointGrid::crystalPointGroup(fcc, displaced).size();
        check(broken == 8,
              "displacing one atom leaves 8 — the lattice group is untouched, "
              "so this is the BASIS being tested and not just the cell",
              double(broken));

        struct Case {
            const char* name;
            const std::vector<std::array<double, 3>>* lattice;
            const std::vector<KPointGrid::Atom>* atoms;
            int divisions;
            std::size_t expected;
        };
        const Case cases[] = {
            {"diamond 4x4x4", &fcc, &diamond, 4, 8},
            {"diamond 2x2x2", &fcc, &diamond, 2, 3},
            {"fcc 4x4x4", &fcc, &single, 4, 8},
            {"simple cubic 4x4x4", &cubic, &single, 4, 10},
            {"broken 4x4x4", &fcc, &displaced, 4, 18},
        };
        for (const Case& item : cases) {
            KPointGrid grid;
            const Outcome outcome =
                grid.build({item.divisions, item.divisions, item.divisions},
                           *item.lattice, *item.atoms,
                           KPointGrid::Symmetry::PointGroup);
            check(outcome.status == Status::Ok,
                  std::string(item.name) + " reduces");
            check(grid.size() == item.expected,
                  std::string("  to ") + std::to_string(item.expected)
                      + " irreducible points",
                  double(grid.size()));
            double sum = 0.0;
            std::size_t orbits = 0;
            for (const KPoint& point : grid.points()) {
                sum += point.weight;
                orbits += point.orbitSize;
            }
            check(std::abs(sum - 1.0) < 1.0e-13,
                  "  with weights summing to one", sum - 1.0);
            check(orbits == grid.fullMeshSize(),
                  "  and every point of the full mesh claimed exactly once",
                  double(orbits));
        }

        // A mesh that does not have the symmetry of its crystal must not be
        // folded with operations that carry a point off it.
        KPointGrid anisotropic;
        check(anisotropic.build({4, 4, 2}, fcc, diamond,
                                KPointGrid::Symmetry::PointGroup)
                      .status
                  == Status::Ok,
              "an anisotropic mesh still builds");
        check(anisotropic.operations().size() < 48,
              "with the operations incompatible with it discarded",
              double(anisotropic.operations().size()));

        KPointGrid finite;
        finite.build({4, 4, 4}, {}, single, KPointGrid::Symmetry::PointGroup);
        check(finite.size() == 1 && finite.points()[0].weight == 1.0,
              "and a finite system is Gamma with weight one, whatever the "
              "divisions asked for");
    }

    std::printf("Time-reversal folding is exact:\n");
    {
        // The property that makes time reversal safe where the point group is
        // not: |psi_-k|^2 = |psi_k*|^2 = |psi_k|^2 POINTWISE for a real
        // Hamiltonian, so folding k with -k needs no symmetrisation of the
        // density. An odd mesh is required for this to test anything -- every
        // point of a 2x2x2 mesh is its own inverse.
        calango::core::Structure cell;
        calango::core::Atom atom;
        atom.atomicNumber = 14;
        atom.position = {0.0, 0.0, 0.0};
        cell.addAtom(atom);
        const double a = 7.0;
        cell.setCell(calango::core::UnitCell({a, 0.0, 0.0}, {0.0, a, 0.0},
                                             {0.0, 0.0, a}));

        double reference = 0.0;
        std::size_t fullPoints = 0;
        for (int mode = 0; mode <= 1; ++mode) {
            Parameters parameters;
            parameters.radialShells = 40;
            parameters.angularPoints = 17;
            parameters.confinementRadiusA = 2.5;
            parameters.confinementWidthA = 0.8;
            parameters.basisTiers = 1;
            parameters.kGrid = {3, 3, 3};
            parameters.kSymmetry = mode;
            CalangoDFTEngine engine(parameters);
            const CalangoDFTEngine::Result result = engine.run(cell);
            check(result.outcome.status == Status::Ok,
                  mode == 0 ? "the full 3x3x3 mesh converges"
                            : "the time-reversal-folded mesh converges",
                  result.energy.total);
            if (mode == 0) {
                reference = result.energy.total;
                fullPoints = result.bands.size();
            } else {
                check(result.bands.size() < fullPoints,
                      "and is genuinely smaller (27 points fold to 14)",
                      double(result.bands.size()));
                check(std::abs(result.energy.total - reference) < 1.0e-4,
                      "giving the SAME energy — which is the whole claim",
                      result.energy.total - reference);
            }
        }
    }

    std::printf("Forces:\n");
    {
        Parameters parameters;
        parameters.radialShells = 40;
        parameters.angularPoints = 17;
        parameters.confinementRadiusA = 2.5;
        parameters.confinementWidthA = 0.8;
        parameters.basisTiers = 1;

        calango::core::Structure h2;
        calango::core::Atom atom;
        atom.atomicNumber = 1;
        atom.position = {0.0, 0.0, 0.0};
        h2.addAtom(atom);
        atom.position = {0.9, 0.0, 0.0};
        h2.addAtom(atom);

        ForceCalculator calculator(parameters);
        // A derivative does not depend on the step used to take it. This is
        // the check that failed for a long time and was the symptom of an
        // off-centre nuclear pole in the neutral-atom pair energy: the force
        // read −4.5, −7.2 and −12.4 eV/Å at three step sizes, all noise. It
        // now plateaus, which is what made every other line below possible.
        const AtomicForces small = calculator.finiteDifference(h2, 0.005);
        const AtomicForces large = calculator.finiteDifference(h2, 0.02, false);
        check(small.outcome.status == Status::Ok,
              "the finite-difference force converges", small.total[0][0]);
        check(large.outcome.status == Status::Ok,
              "at a four times larger step too", large.total[0][0]);
        check(std::abs(small.total[0][0] - large.total[0][0]) < 0.05,
              "and gives the SAME force — a derivative independent of the "
              "step used to take it",
              std::abs(small.total[0][0] - large.total[0][0]));
        check(small.noiseEstimateEvPerA < 0.05,
              "with the doubled-step guard reporting no residual noise",
              small.noiseEstimateEvPerA);

        double worst = 0.0;
        for (int c = 0; c < 3; ++c) {
            const auto o = static_cast<std::size_t>(c);
            worst = std::max(worst,
                             std::abs(small.total[0][o] + small.total[1][o]));
        }
        check(worst < 1.0e-4,
              "the forces sum to zero over the atoms, as translating the whole "
              "system must require",
              worst);
        // A bond stretched past its minimum pulls inward. Atom 1 sits at +x,
        // so a positive x force on atom 0 is a restoring one.
        check(small.total[0][0] > 0.0,
              "and a stretched bond pulls its atoms together",
              small.total[0][0]);

        // The analytic decomposition now has a trustworthy reference to be
        // measured against, which is the whole reason it is reported. It does
        // NOT yet agree: the Hamiltonian part of Pulay and the Becke-weight
        // derivatives are missing, and on this geometry they dominate.
        const AtomicForces analytic = calculator.analytic(h2, false);
        check(analytic.outcome.status == Status::Ok,
              "the analytic decomposition runs");
        double sums = 0.0;
        for (int c = 0; c < 3; ++c) {
            const auto o = static_cast<std::size_t>(c);
            sums = std::max(sums, std::abs(analytic.hellmannFeynman[0][o]
                                           + analytic.hellmannFeynman[1][o]));
            sums = std::max(sums, std::abs(analytic.pulay[0][o]
                                           + analytic.pulay[1][o]));
        }
        check(sums < 1.0e-6,
              "with each of its terms separately summing to zero over the "
              "atoms",
              sums);
    }

    std::printf("Geometry relaxation:\n");
    {
        Parameters parameters;
        parameters.radialShells = 30;
        parameters.angularPoints = 11;
        parameters.confinementRadiusA = 2.0;
        parameters.confinementWidthA = 0.6;
        parameters.basisTiers = 1;

        calango::core::Structure h2;
        calango::core::Atom atom;
        atom.atomicNumber = 1;
        atom.position = {0.0, 0.0, 0.0};
        h2.addAtom(atom);
        atom.position = {0.95, 0.0, 0.0};
        h2.addAtom(atom);

        // Deliberately few steps: what is asserted is that the optimiser goes
        // DOWNHILL and that the force shrinks, which is the property a broken
        // force destroys. Driving it all the way to the minimum is a study,
        // not a per-commit cost.
        ForceCalculator calculator(parameters);
        const RelaxationResult relaxed = calculator.relax(h2, 0.05, 3);
        check(relaxed.history.size() >= 3, "the relaxation takes steps",
              double(relaxed.history.size()));
        if (relaxed.history.size() >= 3) {
            check(relaxed.history.back().energyEv
                      < relaxed.history.front().energyEv,
                  "and lowers the energy", relaxed.history.back().energyEv
                      - relaxed.history.front().energyEv);
            check(relaxed.history.back().maxForceEvPerA
                      < relaxed.history.front().maxForceEvPerA,
                  "while the largest force shrinks",
                  relaxed.history.back().maxForceEvPerA);
        }
        // The bond shortens towards its minimum; it must not run away.
        if (relaxed.positions.size() == 2) {
            const double length =
                std::abs(relaxed.positions[1][0] - relaxed.positions[0][0]);
            check(length < 0.95 && length > 0.5,
                  "and the bond contracts towards a minimum rather than "
                  "running away",
                  length);
        }
    }

    std::printf("Stress tensor:\n");
    {
        // Cubic symmetry is the sharpest available check and needs no
        // reference: the off-diagonal components of a cubic crystal's stress
        // are zero by symmetry, whatever the lattice constant, and nothing in
        // the calculation knows that.
        Parameters parameters;
        parameters.radialShells = 24;
        parameters.angularPoints = 11;
        parameters.confinementRadiusA = 2.5;
        parameters.confinementWidthA = 0.8;
        parameters.basisTiers = 1;
        parameters.kGrid = {1, 1, 1};
        // A STRAINED cell is harder to converge than the one it came from,
        // and the earlier settings here failed on that rather than on grid
        // quality: gentler mixing and a higher ceiling fixed it outright.
        parameters.maxIterations = 200;
        parameters.mixingFraction = 0.2;

        const double a = 5.43;
        calango::core::Structure silicon;
        silicon.setCell(calango::core::UnitCell({0.0, a / 2, a / 2},
                                                {a / 2, 0.0, a / 2},
                                                {a / 2, a / 2, 0.0}));
        calango::core::Atom atom;
        atom.atomicNumber = 14;
        atom.position = {0.0, 0.0, 0.0};
        silicon.addAtom(atom);
        atom.position = {a / 4, a / 4, a / 4};
        silicon.addAtom(atom);

        ForceCalculator calculator(parameters);
        const StressTensor stress = calculator.stress(silicon, 0.01, false);
        check(stress.outcome.status == Status::Ok, "the stress tensor computes",
              stress.pressureGpa);
        double offDiagonal = 0.0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                if (i != j)
                    offDiagonal = std::max(
                        offDiagonal,
                        std::abs(stress.tensor[static_cast<std::size_t>(i * 3
                                                                        + j)]));
        // Cubic symmetry is the sharpest check available and needs no
        // reference number: nothing in the calculation is told the crystal is
        // cubic, so the off-diagonals vanishing and the three diagonals
        // agreeing are properties the machinery has to earn.
        //
        // The MAGNITUDE is validated elsewhere, at production settings, where
        // the trace gives 55.9 GPa against 52.5 GPa from an independent
        // volume scan — agreeing inside the stress's own noise estimate.
        check(offDiagonal < 1.0e-6,
              "and its off-diagonal components vanish, as cubic symmetry "
              "requires and nothing in the calculation was told",
              offDiagonal);
        double asymmetry = 0.0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                asymmetry = std::max(
                    asymmetry,
                    std::abs(stress.tensor[static_cast<std::size_t>(i * 3 + j)]
                             - stress.tensor[static_cast<std::size_t>(j * 3
                                                                      + i)]));
        check(asymmetry == 0.0, "and the tensor is symmetric");
        const double spread =
            std::max(std::abs(stress.tensor[0] - stress.tensor[4]),
                     std::abs(stress.tensor[4] - stress.tensor[8]));
        check(spread < 1.0e-6,
              "with its three diagonal components equal, which a cubic "
              "crystal requires and nothing here was told",
              spread);
        check(stress.tensor[0] < 0.0,
              "and a compressive sign — this lattice constant is larger than "
              "the engine's own equilibrium",
              stress.tensor[0]);

        calango::core::Structure finite;
        finite.addAtom(atom);
        check(calculator.stress(finite).outcome.status == Status::InvalidInput,
              "and a finite system is refused, strain having no meaning "
              "without a cell");
    }

    std::printf("Engine facade:\n");    std::printf("Engine facade:\n");
    {
        CalangoDFTEngine engine;
        calango::core::Structure empty;
        check(engine.run(empty).outcome.status == Status::InvalidInput,
              "an empty structure is rejected");

        Parameters broken;
        broken.maxIterations = 0;
        engine.setParameters(broken);
        calango::core::Structure hydrogen;
        calango::core::Atom atom;
        atom.atomicNumber = 1;
        atom.position = {0.0, 0.0, 0.0};
        hydrogen.addAtom(atom);
        check(engine.run(hydrogen).outcome.status == Status::InvalidInput,
              "parameters are validated before anything else");

        // PBE used to be the case that REFUSED here. It now runs, so what
        // this checks is the other half of the same principle: a functional
        // the engine claims must actually produce a self-consistent answer,
        // not a plausible-looking one.
        Parameters gradient;
        gradient.xc = XcFunctional::GgaPbe;
        gradient.radialShells = 40;
        gradient.angularPoints = 17;
        gradient.confinementRadiusA = 2.5;
        gradient.confinementWidthA = 0.8;
        gradient.basisTiers = 1;
        engine.setParameters(gradient);
        const CalangoDFTEngine::Result pbe = engine.run(hydrogen);
        check(pbe.outcome.status == Status::Ok,
              "a gradient functional runs to self-consistency",
              pbe.energy.total);
        check(std::abs(pbe.integratedElectrons - 1.0) < 1.0e-6,
              "with the electron still there", pbe.integratedElectrons);
        // Hydrogen is where a GGA departs most from an LDA, because its
        // density has nothing but gradient.
        Parameters local = gradient;
        local.xc = XcFunctional::LdaPw;
        engine.setParameters(local);
        const CalangoDFTEngine::Result lda = engine.run(hydrogen);
        check(lda.outcome.status == Status::Ok, "as does the local one",
              lda.energy.total);
        check(pbe.energy.total < lda.energy.total,
              "and PBE lies below LDA, as it does for every light atom",
              pbe.energy.total - lda.energy.total);

        check(!CalangoDFTEngine::unimplementedSteps().empty(),
              "the missing steps are queryable, so a UI cannot drift from "
              "them");
    }

    std::printf("Calculator integration:\n");
    {
        using calango::core::CalculatorKind;
        using calango::core::CalculatorFamily;
        check(calango::core::calculatorFamily(CalculatorKind::CalangoDft)
                  == CalculatorFamily::AbInitio,
              "the built-in engine is classified as ab initio — that "
              "it runs in process says nothing about the family");

        // The one thing that must never happen: a run on this engine reaching
        // the ASE script generator. There is no Python calculator to build, so
        // a generated script would hand ASE an `atoms` with no calculator and
        // fail at get_potential_energy() with a message naming neither cause.
        calango::core::CalculatorConfig config;
        config.calculator = CalculatorKind::CalangoDft;
        const std::string script =
            calango::core::AseScriptGenerator::generate(config,
                                                        "structure.extxyz");
        check(script.find("raise RuntimeError") != std::string::npos,
              "a script generated for it refuses loudly rather than running "
              "without a calculator");
        check(script.find("runs inside the application") != std::string::npos,
              "and says why, so the mis-route is diagnosable from the script");
        check(script.find("atoms.calc =") == std::string::npos
                  || script.find("raise RuntimeError")
                      < script.find("atoms.calc ="),
              "with nothing pretending to be a calculator before it");
    }

    std::printf(failures == 0 ? "\nAll DFT engine checks passed.\n"
                              : "\n%d DFT engine check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
