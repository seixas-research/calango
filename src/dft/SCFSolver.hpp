#pragma once

#include "dft/DftTypes.hpp"

#include <cstddef>
#include <deque>
#include <vector>

namespace calango::dft {

/// Density mixing for the self-consistency loop.
///
/// The Kohn-Sham problem is a fixed point: a density gives a potential, the
/// potential gives orbitals, the orbitals give a density. Feeding the output
/// straight back in diverges for anything metallic — long-wavelength charge
/// sloshes between regions with a gain above one — so each iteration is fed a
/// MIXTURE of what went in and what came out.
///
/// Two schemes, and the second is why this class exists:
///
///   * Linear: ρ_in^{k+1} = ρ_in^k + α (ρ_out^k − ρ_in^k). One parameter, no
///     memory, and it converges linearly at best. Small α is stable and slow;
///     large α is fast until it is not.
///   * Pulay (DIIS): treat the residual R^k = ρ_out^k − ρ_in^k as an error
///     vector, and choose the combination of the last m inputs whose residuals
///     cancel best — minimise ‖Σ c_i R^i‖ subject to Σ c_i = 1. That is a
///     small constrained least-squares problem solved every iteration, and it
///     turns tens of iterations into a handful.
///
/// This class implements both, and is deliberately independent of everything
/// else in the module: it mixes vectors of doubles and knows nothing about
/// grids, atoms or functionals. That is what lets it be tested against a
/// synthetic fixed-point problem with a known answer, which is a far sharper
/// test than "the SCF converged on silicon".
class DensityMixer {
public:
    explicit DensityMixer(const Parameters& parameters);

    /// Next input density from the current input and output.
    ///
    /// `input` and `output` must be the same length; a mismatch returns the
    /// input unchanged rather than a truncated mixture.
    std::vector<double> mix(const std::vector<double>& input,
                            const std::vector<double>& output);

    /// Integrated absolute change |ρ_out − ρ_in|, summed with `weights` when
    /// supplied and unweighted otherwise. This is the density convergence
    /// measure the loop tests, and it is reported per iteration because a
    /// residual that stops falling is the signal to change the mixing rather
    /// than to raise the iteration limit.
    static double residualNorm(const std::vector<double>& input,
                               const std::vector<double>& output,
                               const std::vector<double>& weights = {});

    /// Drop the history. Called when the problem changes underneath the mixer
    /// — a moved atom, a changed basis — because extrapolating from residuals
    /// of a different Hamiltonian is worse than not extrapolating at all.
    void reset();

    std::size_t historySize() const { return inputs_.size(); }

private:
    /// Pulay coefficients for the stored residuals, or empty when the
    /// least-squares problem is too ill-conditioned to trust — in which case
    /// the caller falls back to linear mixing rather than to a wild step.
    std::vector<double> pulayCoefficients() const;

    double fraction_ = 0.3;
    std::size_t maxHistory_ = 8;
    std::deque<std::vector<double>> inputs_;
    std::deque<std::vector<double>> residuals_;
};

/// The self-consistency loop.
///
/// STATUS: the loop's structure, its convergence tests and its mixing are
/// implemented and testable; the step that turns a potential into a density is
/// not, because it needs the Hamiltonian assembly and the eigensolver. The
/// loop is expressed against a callback for exactly that reason — it can be
/// driven, and its convergence logic checked, before the physics underneath it
/// exists.
class SCFSolver {
public:
    explicit SCFSolver(Parameters parameters);

    /// One self-consistency step: given an input density, produce the output
    /// density and the energy that density implies.
    ///
    /// Returning an Outcome rather than throwing keeps a failed step
    /// distinguishable from a converged one at the call site.
    using StepFunction =
        Outcome (*)(const std::vector<double>& input,
                    std::vector<double>& output, EnergyBreakdown& energy,
                    void* context);

    struct Report {
        Outcome outcome;
        int iterations = 0;
        double finalResidual = 0.0;
        double finalEnergyChangeEv = 0.0;
        EnergyBreakdown energy;
        /// Residual per iteration, for the convergence plot. A loop that ends
        /// without this is a loop nobody can diagnose.
        std::vector<double> residualHistory;
    };

    /// Run to self-consistency. `step` is called once per iteration with the
    /// current input density.
    Report run(std::vector<double> initialDensity, StepFunction step,
               void* context, const std::vector<double>& weights = {});

    const Parameters& parameters() const { return parameters_; }

private:
    Parameters parameters_;
};

} // namespace calango::dft
