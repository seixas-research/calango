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

    /// Also measure the intraband (Drude) plasma frequency ω_p at every mesh,
    /// as a convergence target beside the energy and the forces. GPAW only.
    ///
    /// This is a different quantity from the other three, and the reason the
    /// sweep needs it. ΔE and the forces are integrals over ALL occupied
    /// states, so they average the Brillouin zone and converge quickly and
    /// monotonically. ω_p is a FERMI-SURFACE integral — only partially
    /// occupied bands contribute (`PlasmaFrequencyIntegrand._band_summation`
    /// returns `nocc1, nocc2`) — so it converges far more slowly, and NOT
    /// monotonically. Measured on FCC Au under point integration: 15.2, 16.2,
    /// 11.8, 11.8, 9.6 eV over 4³…12³, where the 8³ and 10³ runs agree to
    /// 0.6 % while both are 30 % from the answer.
    ///
    /// That is the trap this metric exists to expose: a mesh converged to
    /// 1 meV/atom in energy can still be wrong by tens of percent in ω_p, and
    /// therefore in every low-energy optical property that follows from it. A
    /// user who converged on the energy panel and moved on to the Optics
    /// wizard had no way to see that from inside this module.
    ///
    /// Silently zero on a gapped system: GPAW gates the intraband term on
    /// `gs.metallic`, so the script records the reason rather than plotting a
    /// flat zero line that reads like perfect convergence.
    bool plasmaFrequency = false;

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
