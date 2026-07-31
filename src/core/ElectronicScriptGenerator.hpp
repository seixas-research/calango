#pragma once

#include "core/BandSymmetryScriptGenerator.hpp"
#include "core/CalculatorConfig.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// One orbital-projection channel of a "fatband" plot: a set of atoms and an
/// atomic shell, whose combined weight |⟨φ | ψ_nk⟩|² is carried alongside every
/// band energy so the band can be drawn with a thickness or a colour
/// proportional to it.
///
/// A plain band structure says where the states are; this says what they are
/// made of. It is the difference between "there is a band crossing E_F" and
/// "the band crossing E_F is Fe d, so the magnetism lives there".
struct FatbandProjection {
    /// Display name shown in the viewer's channel list ("C p_z", "Fe d", …).
    /// Generated from the selection when the user does not override it.
    std::string label;
    /// Atom indices contributing to this channel. Empty means every atom of
    /// `element`; if that is empty too, every atom in the cell.
    std::vector<int> atoms;
    /// Chemical symbol filter applied when `atoms` is empty.
    std::string element;
    /// Angular momentum: 0 = s, 1 = p, 2 = d, 3 = f.
    int angularMomentum = 0;
    /// Magnetic quantum number within the shell, or -1 for "sum over all m".
    ///
    /// GPAW's ordering, which is what the index means: for p, m = 0, 1, 2 is
    /// y, z, x; for d, m = 0…4 is xy, yz, 3z²−r², zx, x²−y². The distinction
    /// matters — separating p_z from (p_x, p_y) is exactly what separates the
    /// π bands of a layered material from its σ bands.
    int magnetic = -1;
};

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

    // -- Band symmetry (GPAW) ----------------------------------------------
    /// Classify the bands at the high-symmetry points of the path by the
    /// irreducible representation of the little group they realize, writing
    /// `band_symmetry.json`. See BandSymmetryScriptGenerator.hpp.
    bool bandSymmetry = false;
    BandSymmetryConfig symmetry;

    // -- Orbital-projected bands / "fatbands" (GPAW) ------------------------
    /// Carry per-band, per-k orbital weights alongside the energies, writing
    /// `fatbands.json`. Empty `fatbandProjections` with this on means "one
    /// channel per element and shell present in the structure", which is the
    /// useful default for a first look.
    bool fatbands = false;
    std::vector<FatbandProjection> fatbandProjections;
};

/// Standalone run.py: reads structure.extxyz from the job directory,
/// runs the backend, and writes bands.json (+ pdos.json when available)
/// with CALANGO_PROGRESS markers for the job console.
std::string generateElectronicScript(const ElectronicConfig& config);

} // namespace calango::core
