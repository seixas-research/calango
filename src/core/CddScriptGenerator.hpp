#pragma once

#include "core/CalculatorConfig.hpp" // densityFiles::

#include <string>
#include <vector>

namespace calango::core {

/// Charge Density Difference: Δρ = ρ(A+B) − ρ(A) − ρ(B).
///
/// The quantity that shows where charge actually went when two fragments were
/// brought together — the accumulation in a bond, the depletion behind an
/// adsorbate, the polarization of a substrate. Each of the three densities on
/// its own is dominated by the atomic cores and shows nothing; only the
/// difference is interpretable, and only if all three are computed the same
/// way on the same grid.
///
/// "The same way" is the whole difficulty, and it is why this restarts from
/// the parent run's `.gpw` rather than re-asking the user for parameters. The
/// exact calculator that produced ρ(A+B) is read back out of it and rebuilt
/// for each fragment, so cutoff, grid spacing, XC, k-points, smearing and
/// convergence cannot drift between the three terms. The fragments keep the
/// parent's cell and boundary conditions for the same reason: two densities on
/// different grids cannot be subtracted at all, and two in different cells
/// would differ by their electrostatics rather than by their bonding.
///
/// Nothing is relaxed. Δρ is defined at ONE geometry — the parent's — and
/// letting a fragment relax would make the difference a mixture of charge
/// transfer and structural rearrangement, which is not a quantity anyone can
/// read off an isosurface.
struct CddRunConfig {
    /// Engine and its parameters.
    ///
    /// GPAW needs almost nothing here: it restarts from the parent's `.gpw`
    /// and reads the exact calculator back out of it, which is the strongest
    /// possible guarantee that the three terms were computed the same way.
    ///
    /// VASP and Quantum ESPRESSO have no such restart. Their fragment runs
    /// have to be RE-SPECIFIED, so the settings are carried here and — the
    /// part that actually matters — the FFT grid is pinned to the parent's
    /// (NGXF/NGYF/NGZF for VASP, nr1/nr2/nr3 for QE). Two densities sampled on
    /// different grids cannot be subtracted at all, and both codes will
    /// happily choose a different grid for a cell with fewer atoms in it.
    CalculatorConfig calculator;

    /// Directory holding the parent single-point's density. GPAW: its `.gpw`.
    /// VASP: CHGCAR, or AECCAR0 + AECCAR2 when `allElectron` is set. QE: the
    /// `.save` directory, from which pp.x exports a cube. Absolute: the CDD
    /// job runs in its own directory and reaches back into the baseline's.
    std::string baselineDir;

    /// All-electron density (`gridrefinement=2`) or the smooth pseudodensity.
    ///
    /// All-electron is the physically complete field but puts enormous nuclear
    /// cusps in all three terms; they cancel to machine precision only because
    /// the geometry is identical, which is exactly why the fragments must not
    /// move. The pseudodensity is smoother and easier to plot, and for a
    /// bonding analysis the valence redistribution it shows is the whole story.
    bool allElectron = true;

    /// 0-based indices of the atoms forming subsystem B. Every other atom is
    /// subsystem A, so the two partitions are exhaustive and disjoint by
    /// construction — a nameless third fragment silently left out would break
    /// the identity the difference relies on.
    std::vector<int> subsystemB;

    std::string outputCube = densityFiles::kChargeDensityDifference;
    std::string resultsJson = "cdd.json";
};

class CddScriptGenerator {
public:
    /// Full ASE/GPAW script: restarts the parent calculation, recomputes each
    /// fragment with the same parameters and cell, writes the difference as a
    /// cube plus a small JSON summary (integrated charge moved, extrema).
    static std::string generate(const CddRunConfig& config);
};

} // namespace calango::core
