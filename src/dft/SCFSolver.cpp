#include "dft/SCFSolver.hpp"

#include <algorithm>
#include <cmath>

namespace calango::dft {

namespace {

/// Solve the small dense system A x = b by Gaussian elimination with partial
/// pivoting, returning false when the matrix is too ill-conditioned to trust.
///
/// "Too ill-conditioned" is the important half. The Pulay B-matrix becomes
/// singular exactly when the loop has converged — the residuals are all
/// nearly zero and therefore nearly parallel — so a solver that pushes on
/// regardless produces enormous coefficients and throws away a converged
/// answer on the last iteration.
bool solveSmallSystem(std::vector<double> a, std::vector<double> b,
                      std::vector<double>& x)
{
    const std::size_t n = b.size();
    if (n == 0 || a.size() != n * n)
        return false;
    x.assign(n, 0.0);

    for (std::size_t column = 0; column < n; ++column) {
        std::size_t pivot = column;
        double best = std::abs(a[column * n + column]);
        for (std::size_t row = column + 1; row < n; ++row) {
            const double candidate = std::abs(a[row * n + column]);
            if (candidate > best) {
                best = candidate;
                pivot = row;
            }
        }
        if (!(best > 1.0e-14))
            return false;
        if (pivot != column) {
            for (std::size_t k = 0; k < n; ++k)
                std::swap(a[column * n + k], a[pivot * n + k]);
            std::swap(b[column], b[pivot]);
        }
        const double diagonal = a[column * n + column];
        for (std::size_t row = column + 1; row < n; ++row) {
            const double factor = a[row * n + column] / diagonal;
            if (factor == 0.0)
                continue;
            for (std::size_t k = column; k < n; ++k)
                a[row * n + k] -= factor * a[column * n + k];
            b[row] -= factor * b[column];
        }
    }
    for (std::size_t row = n; row-- > 0;) {
        double sum = b[row];
        for (std::size_t k = row + 1; k < n; ++k)
            sum -= a[row * n + k] * x[k];
        x[row] = sum / a[row * n + row];
    }
    return true;
}

} // namespace

DensityMixer::DensityMixer(const Parameters& parameters)
    : fraction_(std::clamp(parameters.mixingFraction, 1.0e-4, 1.0))
    , maxHistory_(parameters.mixingHistory > 0
                      ? static_cast<std::size_t>(parameters.mixingHistory)
                      : 0)
{
}

void DensityMixer::reset()
{
    inputs_.clear();
    residuals_.clear();
}

double DensityMixer::residualNorm(const std::vector<double>& input,
                                  const std::vector<double>& output,
                                  const std::vector<double>& weights)
{
    if (input.size() != output.size())
        return 0.0;
    const bool weighted = weights.size() == input.size();
    double sum = 0.0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const double difference = std::abs(output[i] - input[i]);
        sum += weighted ? weights[i] * difference : difference;
    }
    return sum;
}

std::vector<double> DensityMixer::pulayCoefficients() const
{
    const std::size_t m = residuals_.size();
    if (m < 2)
        return {};

    // Minimise ‖Σ c_i R_i‖² subject to Σ c_i = 1, as the bordered system
    //
    //     [ B  1 ] [ c ]   [ 0 ]
    //     [ 1ᵀ 0 ] [ λ ] = [ 1 ],      B_ij = R_i · R_j
    const std::size_t n = m + 1;
    std::vector<double> a(n * n, 0.0);
    std::vector<double> b(n, 0.0);
    for (std::size_t i = 0; i < m; ++i) {
        for (std::size_t j = 0; j < m; ++j) {
            double dot = 0.0;
            const std::vector<double>& ri = residuals_[i];
            const std::vector<double>& rj = residuals_[j];
            const std::size_t count = std::min(ri.size(), rj.size());
            for (std::size_t k = 0; k < count; ++k)
                dot += ri[k] * rj[k];
            a[i * n + j] = dot;
        }
        a[i * n + m] = 1.0;
        a[m * n + i] = 1.0;
    }
    b[m] = 1.0;

    std::vector<double> solution;
    if (!solveSmallSystem(std::move(a), std::move(b), solution))
        return {};
    solution.resize(m);

    // A coefficient set with a huge norm is an extrapolation far outside the
    // span of what has been sampled. It is the classic Pulay failure: the
    // least-squares problem is technically solvable and the answer is
    // nonsense. Rejecting it here means the caller falls back to a linear
    // step, which is slow but cannot destroy a converging run.
    double magnitude = 0.0;
    for (const double c : solution)
        magnitude += std::abs(c);
    if (!(magnitude < 1.0e3))
        return {};
    return solution;
}

std::vector<double> DensityMixer::mix(const std::vector<double>& input,
                                      const std::vector<double>& output)
{
    if (input.size() != output.size() || input.empty())
        return input;

    std::vector<double> residual(input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        residual[i] = output[i] - input[i];

    if (maxHistory_ > 0) {
        inputs_.push_back(input);
        residuals_.push_back(residual);
        while (inputs_.size() > maxHistory_) {
            inputs_.pop_front();
            residuals_.pop_front();
        }

        if (const std::vector<double> c = pulayCoefficients(); !c.empty()) {
            // The extrapolated input, plus a linear step along the
            // extrapolated residual. The second term matters: the pure Pulay
            // combination lies in the span of previous inputs and cannot
            // reach anything outside it, so without it the loop stalls once
            // the true solution leaves that span.
            std::vector<double> mixed(input.size(), 0.0);
            for (std::size_t k = 0; k < c.size(); ++k) {
                const std::vector<double>& in = inputs_[k];
                const std::vector<double>& res = residuals_[k];
                for (std::size_t i = 0; i < mixed.size() && i < in.size(); ++i)
                    mixed[i] += c[k] * (in[i] + fraction_ * res[i]);
            }
            return mixed;
        }
    }

    std::vector<double> mixed(input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        mixed[i] = input[i] + fraction_ * residual[i];
    return mixed;
}

SCFSolver::SCFSolver(Parameters parameters)
    : parameters_(std::move(parameters))
{
}

SCFSolver::Report SCFSolver::run(std::vector<double> initialDensity,
                                 StepFunction step, void* context,
                                 const std::vector<double>& weights)
{
    Report report;
    if (!step) {
        report.outcome = Outcome::invalid("no self-consistency step was given");
        return report;
    }
    if (initialDensity.empty()) {
        report.outcome = Outcome::invalid("the initial density is empty");
        return report;
    }

    DensityMixer mixer(parameters_);
    std::vector<double> input = std::move(initialDensity);
    std::vector<double> output;
    double previousEnergy = 0.0;
    bool havePrevious = false;

    for (int iteration = 1; iteration <= parameters_.maxIterations;
         ++iteration) {
        report.iterations = iteration;
        EnergyBreakdown energy;
        const Outcome stepOutcome = step(input, output, energy, context);
        if (!stepOutcome.ok()) {
            report.outcome = stepOutcome;
            return report;
        }
        if (output.size() != input.size()) {
            report.outcome = Outcome::invalid(
                "the self-consistency step returned a density of a different "
                "length than it was given");
            return report;
        }
        report.energy = energy;

        const double residual =
            DensityMixer::residualNorm(input, output, weights);
        report.finalResidual = residual;
        report.residualHistory.push_back(residual);
        const double energyChange =
            havePrevious ? std::abs(energy.total - previousEnergy)
                         : std::abs(energy.total);
        report.finalEnergyChangeEv = energyChange;

        // BOTH tests, and only from the second iteration. The energy is
        // variational, so it is second-order in the density error: it can look
        // settled to a microelectronvolt while the density is still moving,
        // and a run stopped on that basis gives forces and a dipole that are
        // wrong at first order.
        if (havePrevious && residual < parameters_.densityToleranceElectrons
            && energyChange < parameters_.energyToleranceEv) {
            report.outcome = Outcome::success();
            return report;
        }
        previousEnergy = energy.total;
        havePrevious = true;
        input = mixer.mix(input, output);
    }

    report.outcome = {Status::NotConverged,
                      "the self-consistency loop reached its iteration limit "
                      "without meeting both the density and energy "
                      "tolerances"};
    return report;
}

} // namespace calango::dft
