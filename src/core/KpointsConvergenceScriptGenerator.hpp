#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// K-point convergence: the same single-point calculation repeated over an
/// ascending sequence of Monkhorst-Pack meshes, on the same geometry.
///
/// The sibling of CutoffConvergenceScriptGenerator, and judged the same way:
/// the run at the DENSEST mesh is the reference, and every other point is
/// recorded as its distance from that run in total energy per atom and in
/// the maximum force magnitude. Unlike the cutoff — where more is always
/// better along one axis — a mesh is three numbers, so the sweep carries the
/// full (k1, k2, k3) of every step: a slab sweep holds its vacuum axis at 1,
/// and collapsing that to a single "k" would misdescribe what actually ran.
struct KpointsConvergenceRunConfig {
    /// Structure the sweep evaluates (staged next to run.py).
    std::string structureFile = "structure.extxyz";
    /// Per-mesh records plus the reference summary.
    std::string resultsJson = "kpoints_convergence.json";

    struct Mesh {
        int kpts[3];   ///< the Monkhorst-Pack grid actually run
        int kPerAxis;  ///< the swept scalar n the grid was built from
    };
    /// Ascending density; the last entry is the reference. The generator
    /// keeps the order it is given — the wizard builds it ascending, and a
    /// (2,2,1) vs (1,1,4) mesh has no total order to sort by here.
    std::vector<Mesh> meshes;

    /// The engine and its settings. `task` is ignored (always a single point
    /// per mesh); the GPAW discretization is honoured as configured — k-point
    /// convergence is as real in FD or LCAO as in plane waves.
    CalculatorConfig calculator;

    /// Carry on past a mesh whose SCF raised, recording it as failed.
    bool continueOnFailure = true;
};

class KpointsConvergenceScriptGenerator {
public:
    /// Full ASE/GPAW script: reads the structure, runs one fixed-geometry SCF
    /// per mesh (fresh calculator each time), and writes the results JSON
    /// with the densest-mesh run as the convergence reference.
    static std::string generate(const KpointsConvergenceRunConfig& config);
};

} // namespace calango::core
