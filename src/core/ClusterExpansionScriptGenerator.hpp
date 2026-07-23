#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Batch relaxation of a cluster-expansion ensemble.
///
/// The Cluster Expansion *builder* enumerates symmetry-inequivalent decorated
/// configurations and hands them over as a multi-frame trajectory. This
/// generator turns that trajectory into a job: relax every frame with the
/// chosen calculator, collect the ground-state energies, and emit both the
/// optimized trajectory and the data the Results panel needs to draw a
/// formation-energy convex hull.
struct ClusterExpansionRunConfig {
    /// Input trajectory of unrelaxed configurations (staged next to run.py).
    std::string inputTrajectory = "configs.extxyz";
    /// Output trajectory of relaxed ground-state structures.
    std::string outputTrajectory = "optimized_configs.extxyz";

    /// Per-configuration relaxation. The optimizer/fmax/maxSteps fields of
    /// `calculator` drive each individual relaxation; `task` is ignored (this
    /// workflow always optimizes).
    CalculatorConfig calculator;

    /// Skip relaxation and evaluate each configuration at its input geometry.
    /// Useful for a first pass over a large ensemble, or when the builder's
    /// structures are already relaxed.
    bool singlePointOnly = false;

    /// Element whose site fraction is the diagram's concentration axis. Empty
    /// picks the alphabetically-second species present, which for a binary
    /// ensemble is a stable, predictable choice.
    std::string concentrationElement;

    /// Elemental reference energies (eV/atom) for the formation energy. When
    /// `useEnsembleEndpoints` is true these are ignored and the ensemble's own
    /// x = 0 and x = 1 configurations are used instead — the usual choice,
    /// since it cancels calculator-specific offsets exactly.
    bool useEnsembleEndpoints = true;
    double referenceA = 0.0;
    double referenceB = 0.0;

    /// Continue past a configuration whose relaxation raised, recording it as
    /// failed. A single bad decoration should not lose the other 200 runs.
    bool continueOnFailure = true;
};

class ClusterExpansionScriptGenerator {
public:
    /// Full ASE script: reads the ensemble, relaxes each frame, writes the
    /// optimized trajectory plus `cluster_expansion.json` (per-configuration
    /// concentration / energy / formation energy, and the references used).
    static std::string generate(const ClusterExpansionRunConfig& config);
};

} // namespace calango::core
