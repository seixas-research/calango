#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Backends for electronic band-structure / PDOS jobs. FreeElectrons is
/// the always-available reference backend (empty-lattice bands, no
/// projections); GPAW adds real DFT bands plus element/orbital-resolved
/// PDOS when the package is importable; Espresso generates a pw.x
/// workflow that requires user-supplied pseudopotentials; Siesta and Vasp
/// emit editable ASE-calculator DFT band templates for those codes.
enum class ElectronicBackend {
    FreeElectrons,
    Gpaw,
    Espresso,
    Siesta,
    Vasp,
};

struct ElectronicConfig {
    ElectronicBackend backend = ElectronicBackend::FreeElectrons;
    /// High-symmetry path string ("GXWKG", "GMKG", ...); empty lets ASE
    /// suggest the path for the structure's Bravais lattice.
    std::string kpath;
    /// k-points sampled along the whole path. Derived from the k-path
    /// builder's "points per segment" times the number of segments — the
    /// wizard no longer offers a second, independent control that could
    /// disagree with it.
    int npoints = 80;
    int nvalence = 4;   ///< electrons per cell (FreeElectrons backend)
    // -- SCF settings (GPAW / Espresso) --
    double ecutEv = 500.0;  ///< plane-wave cutoff (eV)
    int scfKpts = 7;        ///< Monkhorst-Pack k-grid (n x n x n)
    /// Full GPAW parameter set (mode, xc, eigensolver, mixer, convergence,
    /// smearing, k-grid), shared with Geometry Optimization and Single-point
    /// so the same wizard controls drive all three. Only read by the Gpaw
    /// backend; `ecutEv`/`scfKpts` above remain the knobs the other DFT
    /// templates use.
    CalculatorConfig gpaw;
    /// Filename (relative to the job directory) of a baseline GPAW restart file
    /// (`.gpw`, written with mode="all") produced by a prior Single-Point
    /// Calculation. When non-empty the GPAW backend loads this converged charge
    /// density and runs the bands/PDOS strictly non-self-consistently
    /// (calc.fixed_density) instead of re-running the SCF cycle inline. Empty
    /// keeps the legacy self-contained SCF+NSCF script.
    std::string baselineDensityPath;
    /// Re-diagonalize the band energies with spin-orbit coupling included
    /// (GPAW's `gpaw.spinorbit.soc_eigenstates`, applied non-perturbatively to
    /// the converged states along the path).
    ///
    /// This is what splits the degeneracies a scalar-relativistic calculation
    /// leaves in place — the Γ-point valence band of a III-V semiconductor, the
    /// Rashba splitting of a heavy-element surface state, the band inversion of
    /// a topological insulator. For light elements it changes the bands by
    /// meV and costs an extra diagonalization; for 5d/6p systems it is the
    /// difference between the right answer and the wrong one.
    bool spinOrbit = false;
    // -- PDOS (GPAW) --
    bool pdos = true;
    double pdosWidthEv = 0.1;   ///< Gaussian smearing σ (eV)
    int pdosPoints = 401;       ///< number of energy sampling points
    /// Fixed-density k-mesh for the projected DOS, typically denser than the
    /// baseline SCF grid (the wizard defaults it to 2× along non-vacuum axes).
    /// The PDOS is re-evaluated at this mesh via calc.fixed_density.
    int pdosKpts[3] = {14, 14, 14};
};

/// Standalone run.py: reads structure.extxyz from the job directory,
/// runs the backend, and writes bands.json (+ pdos.json when available)
/// with CALANGO_PROGRESS markers for the job console.
std::string generateElectronicScript(const ElectronicConfig& config);

} // namespace calango::core
