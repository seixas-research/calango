#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Single-point evaluation of a randomly-perturbed ensemble.
///
/// The Random Noise wizard generates N displaced copies of one structure and
/// hands them over as a trajectory. This turns that trajectory into a job:
/// evaluate every member at its own geometry and collect the resulting energy
/// (and force) distribution.
///
/// The distribution is the whole point. A single perturbed energy says nothing;
/// a hundred of them measure the local curvature of the potential-energy
/// surface — which is what tells you whether a "converged" geometry sits in a
/// well or on a shelf, and what an ML potential trained on the ensemble will
/// actually see. So the script's output is a summary (mean, spread, extremes)
/// rather than a single number, and every member is recorded individually so
/// the ensemble can be re-analysed without re-running it.
struct RandomNoiseRunConfig {
    /// Input trajectory of perturbed structures (staged next to run.py).
    std::string inputTrajectory = "configs.extxyz";
    /// Output trajectory, each frame carrying its computed energy and forces.
    std::string outputTrajectory = "noise_singlepoint.extxyz";
    /// Per-member records plus the ensemble statistics.
    std::string resultsJson = "random_noise.json";

    /// The engine and its settings. `task` is ignored — this workflow is
    /// always a single point per member.
    CalculatorConfig calculator;

    /// Record the forces alongside the energy. Cheap for every engine here
    /// (the same evaluation produces both) and the thing that makes the
    /// ensemble usable as ML training data rather than just an energy
    /// histogram.
    bool computeForces = true;
    /// Record the stress tensor. Only meaningful with a cell, and not every
    /// calculator implements it, so it is off by default.
    bool computeStress = false;
    /// Carry on past a member whose evaluation raised, recording it as failed.
    /// One SCF that will not converge should not lose the other ninety-nine.
    bool continueOnFailure = true;
};

class RandomNoiseScriptGenerator {
public:
    /// Full ASE script: reads the ensemble, evaluates each member, and writes
    /// the annotated trajectory plus the results JSON.
    static std::string generate(const RandomNoiseRunConfig& config);
};

} // namespace calango::core
