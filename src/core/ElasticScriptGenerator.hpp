#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Which finite-strain observable the elastic constants are fit from.
///
/// STRESS-STRAIN (sigma_i = C_ij * eps_j, fit directly from the stress
/// tensor at each strained point) is the PRIMARY method: it needs only
/// strainMagnitude worth of points to fill an entire matrix COLUMN at once
/// (stress is already a 6-vector), and does not need mixed strains for the
/// off-diagonal terms. ENERGY-STRAIN (C_jj = d^2E/deps_j^2 / V0, from a
/// quadratic fit of the total energy) is the FALLBACK for a calculator that
/// does not implement get_stress() at all — every classical/ML potential
/// exposes energies, only some implement a virial/stress. AUTO tries stress
/// at the reference point first and falls back to energy for the WHOLE run
/// if it is not implemented, rather than mixing methods across components.
enum class ElasticMethod {
    Auto,
    StressStrain,
    EnergyStrain,
};

/// Parameters for an elastic-constants calculation, filled in by the Elastic
/// Properties wizard and consumed by generateElasticScript().
///
/// The elastic stiffness tensor
///
///     C_ij  (i, j = 1..6 Voigt)
///
/// is measured here by the finite-strain method: apply a small homogeneous
/// strain to a relaxed reference cell and read off either the stress
/// (STRESS-STRAIN, primary) or the energy (ENERGY-STRAIN, fallback) at each
/// strained point, then fit. This reuses exactly the strain-generation core
/// (core/StrainVoigt.hpp, core/StrainScriptHelpers.hpp) the piezoelectric
/// module differentiates POLARIZATION over — here the SAME strained
/// structures feed a stress or energy readout instead, so the strain
/// stencil, the 2D vacuum-axis handling, and the point-group symmetrization
/// step are shared code, not a parallel reimplementation.
///
/// Unlike PiezoelectricConfig, `calculator` is not restricted to GPAW: the
/// Berry phase piezoelectric needs is GPAW-only, but stress and energy are
/// available from ANY ASE calculator (EMT, MACE, VASP, Quantum ESPRESSO,
/// ...) — the method actually used still depends on whether the chosen
/// engine implements get_stress() (see ElasticMethod::Auto above).
struct ElasticConfig {
    /// Engine + backend knobs. Read directly (not restricted to GPAW) when
    /// `baselinePath` is empty; with a baseline the whole calculator is
    /// restored from it instead, exactly as in PiezoelectricConfig.
    CalculatorConfig calculator;

    /// Absolute path to a completed GPAW Single-Point/Geometry-Optimization
    /// `.gpw`, OPTIONAL (unlike PiezoelectricConfig::baselinePath, which is
    /// mandatory): elastic constants do not need a converged-density
    /// restart to make physical sense the way the Berry phase does, so a
    /// plain structure + fresh calculator (any engine) is equally valid.
    /// When set, supplies the relaxed reference geometry and the calculator
    /// every strained point is rebuilt from (calc.new()), as in
    /// PiezoelectricConfig; when empty, the reference is read from
    /// `structure.extxyz` and the calculator built fresh from `calculator`.
    std::string baselinePath;

    /// Which Voigt strain components (0-based, 0..5) to include. Empty means
    /// all six for bulk, or the in-plane set for a 2D structure — see
    /// `vacuumAxis` below, identical convention to PiezoelectricConfig.
    std::vector<int> voigtComponents;

    /// Out-of-plane vacuum axis for a 2D/monolayer structure (0=x, 1=y,
    /// 2=z), or -1 for bulk — same sentinel and detection convention as
    /// PiezoelectricConfig::vacuumAxis. When set: strain generation is
    /// restricted to the in-plane Voigt set (as in Piezoelectric), AND
    /// every Cij entry whose ROW or COLUMN touches the vacuum axis is left
    /// NaN — a slab's out-of-plane stress components (sigma_zz, sigma_yz,
    /// sigma_xz for vacuumAxis=2) are exactly as ill-defined as the
    /// piezoelectric module's out-of-plane polarization row, and for the
    /// same reason (the periodic cell integrates however much vacuum the
    /// user chose to pad it with). The surviving in-plane 3x3 block is
    /// additionally reported in N/m (area-normalized) alongside the
    /// ordinary GPa value — see the "2D coefficients" comment in the .cpp.
    int vacuumAxis = -1;

    ElasticMethod method = ElasticMethod::Auto;

    /// Strain magnitude of the smallest sample point (dimensionless, e.g.
    /// 0.005 = 0.5%) — same squeeze as PiezoelectricConfig::strainMagnitude.
    double strainMagnitude = 0.005;

    /// Sample points per Voigt component: 2 evaluates only
    /// +-strainMagnitude; 4 also evaluates +-2*strainMagnitude. Stress-
    /// strain fits a LINE (2 points is the exact central difference);
    /// energy-strain fits a PARABOLA (2 points cannot determine a curvature
    /// at all without the eps=0 reference — 4 total points is the practical
    /// minimum for a robust fit) — the wizard defaults this higher when
    /// energy-strain is selected.
    int pointsPerComponent = 2;

    /// Relax internal (ionic) coordinates at each strained cell before
    /// reading off stress/energy, instead of leaving them at the
    /// strain-scaled ("clamped-ion") positions — identical convention and
    /// terminology to PiezoelectricConfig::relaxIons.
    bool relaxIons = false;
    double relaxFmaxEvPerA = 0.02;
    int relaxMaxSteps = 100;

    /// Use spglib point-group symmetry to symmetrize the assembled tensor
    /// (zeroing whatever Cij symmetry forbids) and to pick the closed-form
    /// Born stability criterion by crystal class. Unlike the piezoelectric
    /// module there is no centrosymmetric refusal: every rank-4 elastic
    /// tensor is inversion-invariant, so centrosymmetry never forces C to
    /// zero the way it forces the (rank-3) piezoelectric tensor to zero.
    bool useSymmetry = true;
    double symmetryTolerance = 1e-4;
};

/// Turns an ElasticConfig into a standalone ASE script that writes
/// `elastic.json` into the job directory and emits the
/// `CALANGO_RESULT elastic=elastic.json` marker the controller watches for.
std::string generateElasticScript(const ElasticConfig& cfg);

} // namespace calango::core
