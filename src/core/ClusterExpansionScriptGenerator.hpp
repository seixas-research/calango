#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>
#include <vector>

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

    /// The DESIGN MATRIX: one cluster-correlation vector per configuration, in
    /// the same frame order as `inputTrajectory`.
    ///
    /// Carried here rather than recomputed in Python because the builder has
    /// already computed it (`ClusterExpansionConfig::correlation`) from the
    /// orbit definitions it enumerated. Re-deriving correlations on the Python
    /// side would be a SECOND implementation of cluster enumeration, and two
    /// implementations of the same thing are one more thing that can disagree
    /// — silently, since a wrong design matrix still fits and still produces
    /// plausible ECIs.
    ///
    /// Empty means the ensemble predates this field; the script then emits no
    /// `correlation` key and the ECI fitter refuses rather than fitting
    /// against nothing.
    std::vector<std::vector<double>> correlations;
    /// Human-readable label per correlation column, e.g. "pair r=2.55 m=12".
    /// Same length as each row of `correlations`.
    std::vector<std::string> orbitLabels;

    /// g_j per configuration (core::ClusterExpansionConfig::degeneracy),
    /// same frame order as `inputTrajectory` — how many raw decorations of
    /// the active sublattice collapsed onto this one during enumeration.
    /// The ECI fit does not use it (empty is fine there); EGQCA does, since
    /// it is exactly the cluster degeneracy the theory is written in terms
    /// of. Empty means the ensemble predates this field, same convention as
    /// `correlations`.
    std::vector<int> degeneracies;
};

class ClusterExpansionScriptGenerator {
public:
    /// Full ASE script: reads the ensemble, relaxes each frame, writes the
    /// optimized trajectory plus `cluster_expansion.json` (per-configuration
    /// concentration / energy / formation energy, and the references used).
    static std::string generate(const ClusterExpansionRunConfig& config);
};

} // namespace calango::core
