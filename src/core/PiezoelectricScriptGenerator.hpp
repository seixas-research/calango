#pragma once

#include "core/CalculatorConfig.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace calango::core {

/// Parameters for a piezoelectric-tensor calculation, filled in by the
/// Piezoelectric wizard and consumed by generatePiezoelectricScript().
/// Deliberately UI-free, matching BornChargesConfig.
///
/// The piezoelectric tensor
///
///     e_i,alpha = dP_i / d(eps_alpha)      (i = 1..3, alpha = 1..6 Voigt)
///
/// is measured here by the finite-difference strain-polarization method:
/// apply a small homogeneous strain to a relaxed reference cell, evaluate
/// the total polarization P by the modern (Berry-phase) theory at each
/// strained point, and differentiate. It reuses exactly the GPAW
/// Berry-phase machinery BornChargesScriptGenerator differentiates over
/// ATOMIC DISPLACEMENTS (see PolarizationScriptHelpers) — here the same
/// polarization evaluation is differentiated over CELL STRAIN instead.
struct PiezoelectricConfig {
    /// Engine + backend knobs. Only read when `baselinePath` is empty; with
    /// a baseline the whole calculator is restored from it instead. Only
    /// GPAW can evaluate the Berry-phase polarization this method
    /// differentiates — the generated script says so plainly otherwise.
    CalculatorConfig calculator;

    /// Absolute path to a completed Single-Point Calculation's `.gpw`.
    /// MANDATORY in the GUI: it supplies the relaxed reference geometry the
    /// strains are applied to, and the calculator every strained run is
    /// rebuilt from (calc.new()), exactly as in BornChargesConfig.
    std::string baselinePath;

    /// Which Voigt strain components (0-based, 0..5) to include. Empty means
    /// all six — the caller (wizard) is expected to pass the user's
    /// selection or the full set; this generator does not itself decide
    /// which components symmetry makes redundant (see the class doc in
    /// PiezoelectricTensor.hpp for what the symmetry step DOES do: refuse
    /// centrosymmetric structures outright, and clean/zero the assembled
    /// tensor — not skip individual strain directions ahead of time, which
    /// remains a documented follow-up).
    std::vector<int> voigtComponents;

    /// Out-of-plane vacuum axis for a 2D/monolayer structure (0=x, 1=y,
    /// 2=z), or -1 for an ordinary bulk 3D system — the same sentinel
    /// convention as OpticsConfig::vacuumAxis / NonlinearOpticsConfig::
    /// vacuumAxis. Set by the wizard from core::guessVacuumAxis()'s
    /// geometric read of the structure (a large fractional gap along one
    /// axis), OR-ed with an explicit `pbc=False` on that axis — a monolayer
    /// built with `pbc=[True,True,True]` and a vacuum gap (the ordinary ASE
    /// slab convention) needs the geometric signal, since pbc alone says
    /// nothing.
    ///
    /// When set (>= 0):
    ///  - an empty `voigtComponents` defaults to core::inPlaneVoigtComponents
    ///    (in-plane only) rather than all six, and any explicitly-requested
    ///    component that touches the vacuum axis is dropped — straining a
    ///    cell dimension that is not a real lattice parameter is not a
    ///    physical strain, so this holds regardless of what voigtComponents
    ///    asked for;
    ///  - the out-of-plane Cartesian polarization row (index == vacuumAxis)
    ///    is left NaN in every assembled tensor: the periodic Berry phase
    ///    along a vacuum direction integrates almost entirely empty space
    ///    and is not a well-defined polarization for a slab (unlike every
    ///    in-plane row, which the dense in-plane k-mesh makes physical);
    ///  - the reported e_ij additionally includes a 2D/sheet form (C/m),
    ///    the vacuum-independent quantity obtained by multiplying the
    ///    ordinary C/m^2 value by the vacuum axis's own cell length — see
    ///    the "2D coefficients" comment in the .cpp for the derivation.
    int vacuumAxis = -1;

    /// Strain magnitude of the smallest sample point (dimensionless, e.g.
    /// 0.005 = 0.5%). Squeezed between the same two errors as
    /// BornChargesConfig::displacement: large enough that the polarization
    /// change clears SCF/Berry-phase noise, small enough that F = I + eps
    /// stays in the linear response regime the method assumes.
    double strainMagnitude = 0.005;

    /// Sample points per component: 2 evaluates only +-strainMagnitude (the
    /// exact central difference); 4 also evaluates +-2*strainMagnitude and
    /// fits all points (plus the zero-strain reference) by least squares —
    /// the "more points for a better fit" option, at roughly double the
    /// cost per component.
    int pointsPerComponent = 2;

    /// Relax internal (ionic) coordinates at each strained cell before
    /// evaluating the polarization, instead of leaving them at the
    /// strain-scaled ("clamped-ion") positions ase.Atoms.set_cell(...,
    /// scale_atoms=True) produces. Clamped-ion is the cheaper base case and
    /// is always computed; relaxed-ion adds one geometry optimization of
    /// positions (cell fixed) per strain point.
    bool relaxIons = false;
    double relaxFmaxEvPerA = 0.02;
    int relaxMaxSteps = 100;

    /// Use spglib point-group symmetry: refuse outright (before running
    /// anything) when the reference structure is centrosymmetric — where
    /// e_ij is forbidden to be nonzero everywhere — and symmetrize the
    /// assembled tensor afterwards. See
    /// core::symmetrizePiezoelectricTensor / containsInversion.
    bool useSymmetry = true;
    double symmetryTolerance = 1e-4; // matches BandSymmetryScriptGenerator's symprec

    /// Optional elastic stiffness tensor C (Voigt, GPa) supplied by the
    /// user. When present, the generated script also reports the
    /// piezoelectric STRAIN tensor d_i,alpha = sum_beta e_i,beta * S_beta,
    /// alpha with S = C^-1 (core::invert6x6).
    std::optional<std::array<std::array<double, 6>, 6>> elasticStiffnessGpa;
};

/// Turns a PiezoelectricConfig into a standalone ASE/GPAW script that writes
/// `piezoelectric.json` into the job directory and emits the
/// `CALANGO_RESULT piezoelectric=piezoelectric.json` marker the controller
/// watches for.
std::string generatePiezoelectricScript(const PiezoelectricConfig& cfg);

} // namespace calango::core
