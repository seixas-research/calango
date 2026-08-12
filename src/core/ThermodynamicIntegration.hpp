#pragma once

// <cstdint> must stay even when clangd calls it unused: libstdc++ from GCC 13
// no longer pulls the fixed-width integer types in transitively, so removing
// it breaks the Linux .deb build while the macOS build stays green.
#include <cstdint>
#include <string>
#include <vector>

namespace calango::core {

/// Thermodynamic integration (TI): ABSOLUTE free energies of liquids (and, with
/// the Einstein reference, of solids) from molecular dynamics.
///
/// A simulation measures energy differences, never an absolute free energy —
/// F is not a mechanical average, it is the log of a partition function. TI
/// recovers it by building a reversible path from a reference system whose F is
/// known in CLOSED FORM to the system of interest:
///
///     U(λ) = (1 − λ) U_ref + λ U_target ,     λ ∈ [0, 1]
///     ∂F/∂λ = ⟨∂U/∂λ⟩_λ = ⟨U_target − U_ref⟩_λ
///     ΔF = ∫₀¹ ⟨U_target − U_ref⟩_λ dλ
///     F_target = F_ref + ΔF ,   G = F + PV
///
/// Everything in this file is one of four things: the closed-form F_ref of a
/// reference system, the quadrature that evaluates the integral, the statistics
/// that put an error bar on it, or the diagnostics that say when the answer is
/// not trustworthy.
///
/// UNITS. Energies in eV, lengths in Å, masses in amu, temperature in K,
/// pressure in GPa — the units the rest of Calango and ASE work in.
///
/// PER CELL, NOT PER ATOM. Every energy returned here is for the WHOLE
/// simulation cell, matching core::PhononThermodynamics (whose per-cell
/// convention has already been mistaken for per-atom once). TiAssembly reports
/// both, spelled out in the field names, so a consumer never has to guess.
///
/// This file is Qt-free by construction: src/core carries no Qt dependency and
/// its tests link none.

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

namespace ti_constants {

/// Boltzmann constant, eV/K (CODATA 2018 exact value 1.380649e-23 J/K divided
/// by the exact elementary charge).
inline constexpr double kBoltzmannEvPerK = 8.617333262e-5;

/// ħ² / (2 m_u), in eV·Å², where m_u is the atomic mass unit.
///
/// Derived rather than measured: ħ²/(2 m_e) = 3.8099821161 eV·Å² is the
/// constant every electronic-structure code carries, and m_u/m_e =
/// 1822.888486209, so ħ²/(2 m_u) = 3.8099821161 / 1822.888486209.
/// Written this way because the eV·Å² form is the one that keeps the thermal
/// de Broglie wavelength free of any SI round trip.
inline constexpr double kHbarSqOverTwoAmuEvA2 = 2.0900796e-3;

/// 1 GPa expressed in eV/Å³. 1 eV/Å³ = e / 1e-30 m³ = 1.602176634e11 Pa =
/// 160.2176634 GPa, exact because the elementary charge is. Needed for the PV
/// term of G = F + PV.
inline constexpr double kGpaInEvPerA3 = 1.0 / 160.2176634;

} // namespace ti_constants

// ---------------------------------------------------------------------------
// Reference systems
// ---------------------------------------------------------------------------

/// Which analytic reference the path starts from.
///
/// APPEND-ONLY: the value is written into the generated script and into the
/// run's JSON, so renumbering would reinterpret every stored run.
enum class TiReference {
    /// The ideal gas — no interactions at all. The natural reference for a
    /// LIQUID, and the only one of the three whose free energy is exact with no
    /// caveat whatsoever (Sackur-Tetrode).
    ///
    /// It is also the one that makes the endpoint singularity worst: at λ → 0
    /// the particles are free to overlap, and any target with a hard repulsive
    /// core then contributes an unbounded ⟨U_target⟩. See
    /// endpointDiagnostics() and TiLambdaSchedule.
    IdealGas,
    /// The Einstein crystal — every atom in an independent harmonic well
    /// centred on its lattice site. The standard SOLID reference (Frenkel &
    /// Ladd, J. Chem. Phys. 81, 3188 (1984)); exact in closed form, and free of
    /// the endpoint singularity because the tethered atoms never overlap.
    EinsteinCrystal,
    /// A Lennard-Jones fluid at the same (ρ, T).
    ///
    /// THERE IS NO EXACT CLOSED FORM FOR THIS ONE, which is why it is handled
    /// differently from the two above — see lennardJonesFreeEnergy() for what
    /// is and is not computed, and what has to be supplied instead.
    LennardJonesFluid,
};

/// Everything a reference system needs beyond the thermodynamic state.
struct TiReferenceParameters {
    /// Einstein crystal: the spring constant α of the tether, eV/Å².
    ///
    /// A real choice, not a detail. Too soft and the tethered crystal melts
    /// (the path stops being reversible); too stiff and ⟨U_target − U_ref⟩
    /// becomes enormous at λ → 1 and the quadrature loses precision. The usual
    /// recipe is to match the mean-squared displacement of the target crystal.
    double einsteinSpringEvPerA2 = 1.0;
    /// Whether the MD holds the centre of mass fixed (ASE's FixCom).
    ///
    /// It normally must: an untethered translation of the whole crystal costs
    /// nothing in the target but is quadratically penalized by the springs, so
    /// the two Hamiltonians differ by a soft mode that the tether kills. Fixing
    /// the CM removes it, at the price of a finite-size correction that
    /// einsteinCrystalFreeEnergy() then has to apply.
    bool einsteinFixedCenterOfMass = true;

    /// Lennard-Jones reference: ε (eV) and σ (Å) of the pair potential.
    double ljEpsilonEv = 0.0104;  ///< ≈ 120 K, argon-like
    double ljSigmaA = 3.4;        ///< argon-like
    /// The EXCESS (over ideal gas) free energy per atom of the LJ reference,
    /// eV, when it is known from somewhere else — a previous ideal-gas→LJ
    /// integration run in this very module, or a literature equation of state
    /// the user chose and is willing to stand behind.
    ///
    /// Zero means "not supplied", and the reference then refuses to produce a
    /// number outside the low-density regime where the virial expansion is
    /// quantitative. See lennardJonesFreeEnergy().
    double ljExcessFreeEnergyEvPerAtom = 0.0;
    bool ljExcessSupplied = false;
};

/// The thermodynamic state the reference free energy is evaluated at.
struct TiSystem {
    int atomCount = 0;
    double volumeA3 = 0.0;
    double temperatureK = 300.0;
    double pressureGPa = 0.0;
    /// One mass per atom, amu. A mixture needs them: the ideal-gas free energy
    /// carries one thermal wavelength PER SPECIES, and collapsing a mixture
    /// onto a single average mass silently changes the answer.
    std::vector<double> massesAmu;
    /// Used for every atom when `massesAmu` is empty.
    double uniformMassAmu = 1.0;
};

/// The result of evaluating a reference system's closed form.
struct TiReferenceFreeEnergy {
    double freeEnergyEv = 0.0;      ///< F_ref for the WHOLE cell
    double freeEnergyEvPerAtom = 0.0;
    bool valid = false;             ///< false ⇒ freeEnergyEv means nothing
    std::string description;        ///< what was evaluated, for the report
    std::vector<std::string> warnings;
};

/// Thermal de Broglie wavelength Λ = h / √(2π m k_B T), in Å.
///
/// Written as Λ² = 2πħ²/(m k_B T) = 4π·[ħ²/(2m)]/(k_B T) so it is evaluated
/// entirely in eV·Å² and never touches SI. Returns 0 for a non-positive mass
/// or temperature (the classical limit has no meaning there).
double thermalDeBroglieWavelengthA(double massAmu, double temperatureK);

/// Ideal-gas Helmholtz free energy of the whole cell, eV.
///
///     F_id = k_B T Σ_s N_s [ ln(ρ_s Λ_s³) − 1 ] ,     ρ_s = N_s / V
///
/// THE FACTORIAL IS THE POINT. The canonical partition function of N
/// indistinguishable particles is Q = V^N / (N! Λ^{3N}); Stirling's
/// ln N! ≈ N ln N − N is what turns −k_BT[N ln V − 3N ln Λ] into the extensive
/// N k_B T[ln(ρΛ³) − 1]. Drop the N! and you get N k_B T[ln(V/Λ³)]·(−1), which
/// is off by N k_B T(ln N − 1) — a term that looks like a plausible free energy,
/// scales almost right, and is wrong. That is the classic failure this function
/// exists to make unrepeatable.
///
/// The sum runs over SPECIES: atoms are grouped by mass, each group getting its
/// own Λ and its own N!. A single-species cell reduces to the familiar
/// Sackur-Tetrode form.
TiReferenceFreeEnergy idealGasFreeEnergy(const TiSystem& system);

/// Classical Einstein crystal: N independent 3D harmonic oscillators of spring
/// constant α, each tethered to its own site.
///
///     F_Ein = 3 N k_B T ln(β ħ ω) ,    ω = √(α/m)
///
/// Equivalently (3N k_BT/2)·ln(βαΛ²/2π) — the same number, and the identity is
/// pinned by a test, because the two forms come from different places (the
/// oscillator partition function and the Gaussian configurational integral) and
/// agreeing is evidence that neither dropped a 2π.
///
/// NO 1/N! HERE, deliberately. Einstein atoms are tethered to distinguishable
/// lattice sites; they cannot exchange, so there is no indistinguishability
/// factor to divide out. This is the opposite of the ideal-gas case above and
/// is exactly where a copy-paste between the two goes wrong.
///
/// When `fixedCenterOfMass` is set, the finite-size correction for the
/// constrained ensemble is applied — see
/// einsteinFixedCenterOfMassCorrection().
TiReferenceFreeEnergy einsteinCrystalFreeEnergy(
    const TiSystem& system, double springConstantEvPerA2,
    bool fixedCenterOfMass);

/// Correction, eV, converting the unconstrained Einstein crystal into the
/// fixed-centre-of-mass one that an MD run with FixCom actually samples, and
/// back out to the free-CM crystal.
///
/// Two exact pieces, both derived rather than quoted:
///
///  1. Constraining the CM removes three degrees of freedom from the Gaussian
///     configurational integral:
///        ∫ δ³(Σ Δr_i) Π e^{−βα Δr_i²/2} d³Δr = (2π/βα)^{3(N−1)/2} N^{−3/2}
///     against (2π/βα)^{3N/2} unconstrained, so
///        ΔA₁ = +(3 k_BT/2) [ ln(2π/βα) + ln N ].
///     (The 1D N = 2 case, ∫δ(x₁+x₂)e^{−βα(x₁²+x₂²)/2} = √(π/βα), is what the
///     test checks this against.)
///
///  2. Restoring the free translation of the whole crystal. Writing
///     r_i = R + s_i with Σs_i = 0 gives dr^N = V N³ ds^N δ³(Σs), so
///        ΔA₂ = −k_BT ln(V N³ / Λ³)
///     where Λ is the thermal wavelength that makes the argument dimensionless.
///     Note there is NO N! : a crystal samples ONE permutation of atoms onto
///     sites, and the N! of the partition function is cancelled by the N!
///     equivalent labelings that the full configuration space contains but the
///     simulation never visits.
///
/// Both are O(ln N) — invisible per atom for a large cell, and the difference
/// between agreeing and disagreeing with a published reference free energy for
/// the 100-500 atom cells this module actually runs.
double einsteinFixedCenterOfMassCorrection(const TiSystem& system,
                                           double springConstantEvPerA2);

/// Second virial coefficient of the Lennard-Jones 12-6 fluid, in σ³ units
/// (i.e. B₂/σ³), as a function of the reduced temperature T* = k_BT/ε.
///
/// EXACT, not a fit. Integrating B₂ = −2π∫(e^{−βu} − 1)r²dr by parts and
/// expanding the attractive exponential gives the classical closed form
///
///     B₂*(T*) ≡ B₂ / (2πσ³/3) = −Σ_{k≥0} [2^{(2k+1)/2} / (4 k!)]
///                                        Γ((2k−1)/4) T*^{−(2k+1)/4}
///
/// which is the series in Hirschfelder, Curtiss & Bird, *Molecular Theory of
/// Gases and Liquids* (Wiley, 1954), §3.6. It converges for every T* > 0,
/// slowly at low T*; the sum is truncated when a term falls below the
/// tolerance and `converged` reports whether that happened.
///
/// Returns B₂/σ³ (so B₂* = 3/(2π) × the return value); `converged` is set to
/// false if the series was still moving when the term limit was reached.
double lennardJonesSecondVirialSigma3(double reducedTemperature,
                                      bool* converged = nullptr);

/// Free energy of the Lennard-Jones reference fluid.
///
/// THIS FUNCTION DELIBERATELY REFUSES MORE OFTEN THAN IT ANSWERS.
///
/// The LJ fluid has no exact closed-form free energy. The honest options are
/// (a) a published equation of state, or (b) getting the LJ free energy from a
/// thermodynamic integration of its own — the very machinery in this file, with
/// the ideal gas as the reference. This implementation offers (b), plus the one
/// piece of (a) that is exact:
///
///   * Below `kVirialDensityLimit` the excess free energy is evaluated from the
///     EXACT second virial coefficient, βA_ex/N = B₂ρ + O(ρ²). The truncation
///     error is the O(ρ²) term, so at ρ* = 0.05 it is percent-level on A_ex and
///     the function says so in `warnings`.
///
///   * Above it, the function requires `ljExcessFreeEnergyEvPerAtom` to have
///     been supplied (from a previous ideal-gas→LJ run of this module, or from
///     an equation of state the user chose). Without it the result is marked
///     invalid rather than being extrapolated.
///
/// No 33-parameter MBWR fit is transcribed here. An unverifiable table of
/// coefficients that silently produces plausible numbers is worse than a
/// function that refuses.
TiReferenceFreeEnergy lennardJonesFreeEnergy(
    const TiSystem& system, const TiReferenceParameters& parameters);

/// Reduced density ρ* = (N/V)σ³ above which the second-virial truncation stops
/// being quantitative. Conservative on purpose.
inline constexpr double kVirialDensityLimit = 0.05;

/// Dispatch to whichever reference `kind` names.
TiReferenceFreeEnergy referenceFreeEnergy(
    TiReference kind, const TiSystem& system,
    const TiReferenceParameters& parameters);

/// Reference name as it appears in the generated script and in the report.
std::string toString(TiReference reference);

// ---------------------------------------------------------------------------
// λ scheduling
// ---------------------------------------------------------------------------

/// How the λ windows are placed on [0, 1].
///
/// This is a PHYSICS choice, not a numerical one, because of the endpoint
/// singularity: with linear coupling to a target that has a hard repulsive
/// core, ⟨∂U/∂λ⟩ diverges as λ → 0 (the ideal-gas reference lets particles sit
/// on top of each other and the target then charges an unbounded energy for
/// it). A uniform grid integrates that divergence without ever noticing.
enum class TiLambdaSchedule {
    /// Uniform, INCLUDING both endpoints. The obvious choice, and the one to
    /// avoid whenever the reference is the ideal gas: λ = 0 samples the
    /// reference itself, where the target energy is unbounded.
    Uniform,
    /// The interior nodes of the n-point Gauss-Legendre rule mapped to [0, 1].
    /// Never evaluates λ = 0 or λ = 1, and clusters towards both ends, which is
    /// why it is the default: it dodges the singular endpoints and puts extra
    /// resolution exactly where the integrand bends.
    GaussLegendre,
    /// λ = u^p on a uniform u grid (p = `exponent`). Clusters towards λ = 0,
    /// which is the end that hurts when integrating away from an ideal gas.
    /// p = 1 degenerates to Uniform.
    PowerLaw,
    /// Symmetric clustering at BOTH ends (a smooth-step map of a uniform grid),
    /// for paths whose integrand is steep at either extreme — e.g. an Einstein
    /// reference with a stiff spring, where λ → 1 is the expensive end.
    ClusteredEnds,
};

/// The λ values for `windows` points under `schedule`.
///
/// `exponent` is read only by PowerLaw and ClusteredEnds. Returns an empty
/// vector for windows < 1. Always ascending.
std::vector<double> lambdaSchedule(TiLambdaSchedule schedule, int windows,
                                   double exponent = 2.0);

/// Nodes and weights of the n-point Gauss-Legendre rule on [a, b].
///
/// Computed by Newton iteration on the Legendre polynomial, which is what makes
/// the exactness testable: an n-point rule integrates every polynomial of
/// degree ≤ 2n − 1 to machine precision, and nothing weaker would.
struct GaussLegendreRule {
    std::vector<double> nodes;
    std::vector<double> weights;
};
GaussLegendreRule gaussLegendreRule(int points, double a = 0.0, double b = 1.0);

// ---------------------------------------------------------------------------
// Quadrature
// ---------------------------------------------------------------------------

enum class TiQuadrature {
    /// Composite trapezoid. Error O(h²); works on any ascending node set.
    Trapezoid,
    /// Composite Simpson. Error O(h⁴), but ONLY on a uniform grid with an odd
    /// number of points — the two conditions this rule silently violates
    /// otherwise. An even count is handled by Simpson's 3/8 rule on the last
    /// three intervals; a non-uniform grid falls back to the trapezoid and says
    /// so in the note.
    Simpson,
    /// Gauss-Legendre. Requires the nodes to BE the rule's nodes (use
    /// lambdaSchedule(GaussLegendre, n)); anything else is refused rather than
    /// silently reweighted, because Gauss weights on non-Gauss nodes are not an
    /// approximation of anything.
    GaussLegendre,
};

/// Quadrature expressed as WEIGHTS rather than as a number.
///
/// Deliberate: every rule here is linear, ∫f ≈ Σ wᵢ f(xᵢ), and having the
/// weights explicitly is what lets the statistical error propagate exactly
/// (σ_I² = Σ wᵢ²σᵢ²) instead of being guessed at.
struct TiQuadratureWeights {
    std::vector<double> weights;
    TiQuadrature ruleUsed = TiQuadrature::Trapezoid;
    bool valid = false;
    /// Non-empty when the requested rule could not be applied as asked and
    /// something else was used — the caller must surface this.
    std::string note;
};

TiQuadratureWeights quadratureWeights(TiQuadrature rule,
                                      const std::vector<double>& nodes);

/// Σ wᵢ yᵢ. Returns 0 when the sizes disagree.
double integrateWithWeights(const std::vector<double>& weights,
                            const std::vector<double>& values);

// ---------------------------------------------------------------------------
// Statistics of one window
// ---------------------------------------------------------------------------

/// Everything a single λ window's ∂U/∂λ time series says.
///
/// A free energy with no error bar cannot be compared with anything — not with
/// an experiment, not with another code, not with the same code at a different
/// cell size. So every window carries one, and it is an AUTOCORRELATION-AWARE
/// one: MD samples are not independent, and σ/√N over correlated samples
/// under-reports the error by √(2τ) — routinely a factor of three or more,
/// which is exactly the size of the discrepancies people then explain away.
struct TiSeriesStatistics {
    double mean = 0.0;
    double variance = 0.0;          ///< per-sample variance (biased-corrected)
    /// Standard error on the mean, corrected for autocorrelation:
    /// σ_mean = √(variance · τ_int / N).
    double standardError = 0.0;
    /// Integrated autocorrelation time in units of SAMPLES (≥ 1). 1 means
    /// uncorrelated.
    double correlationTime = 1.0;
    /// Independent cross-check: standard error from block averaging at the
    /// largest block count whose blocks are still ≥ 2·τ_int long.
    double blockStandardError = 0.0;
    int blocks = 0;
    long long samples = 0;
    bool valid = false;
};

/// Integrated autocorrelation time τ_int = 1 + 2Σ_{t≥1} ρ(t), truncated by
/// Sokal's automatic windowing (stop at the first W with W ≥ c·τ_int(W), c = 5).
///
/// Windowing rather than summing the whole correlogram: the tail of ρ(t) is
/// pure noise whose variance grows with the number of terms added, so the naive
/// full sum is not merely imprecise, it does not converge.
double integratedAutocorrelationTime(const std::vector<double>& series);

/// Block averaging: split into `blocks` contiguous blocks, take the standard
/// error of the block means. Returns 0 for fewer than 2 usable blocks.
double blockStandardError(const std::vector<double>& series, int blocks);

TiSeriesStatistics analyseSeries(const std::vector<double>& series);

// ---------------------------------------------------------------------------
// Windows, integration, assembly
// ---------------------------------------------------------------------------

/// One λ window as it comes back from a run.
struct TiWindowSample {
    int index = -1;
    double lambda = 0.0;
    /// ⟨U_target − U_ref⟩ over the PRODUCTION part of the window, eV, whole
    /// cell.
    double dudlEv = 0.0;
    /// 1σ on that mean (autocorrelation-corrected).
    double dudlErrorEv = 0.0;
    /// Per-sample variance of ∂U/∂λ. Not decoration: a variance that explodes
    /// at the end windows IS the endpoint singularity, and it is the only
    /// signature of it that survives averaging.
    double dudlVarianceEv2 = 0.0;
    double correlationTime = 1.0;
    long long samples = 0;
    /// False when the window did not produce a usable average — it crashed, it
    /// was never run, or its file is missing.
    bool ok = false;
    std::string failure;
};

/// What the endpoint check found.
struct TiEndpointDiagnostics {
    bool suspected = false;
    /// max(variance in the two end windows) / median(variance).
    double varianceRatio = 0.0;
    /// max(|⟨∂U/∂λ⟩| in the two end windows) / median(|⟨∂U/∂λ⟩|).
    double magnitudeRatio = 0.0;
    std::string message;
};

/// Flag a λ path whose integrand is blowing up at an endpoint.
///
/// The test is a ratio against the MEDIAN over all windows, not against a fixed
/// energy: the integrand's scale is whatever the system's cohesive energy
/// happens to be, and only its SHAPE says whether the quadrature is integrating
/// a smooth function or the shoulder of a divergence.
///
/// This does not fix anything. It exists so that a run which under-converged by
/// integrating a divergence says so, instead of reporting a number.
TiEndpointDiagnostics endpointDiagnostics(
    const std::vector<TiWindowSample>& windows, double ratioThreshold = 8.0);

/// The result of integrating one complete λ path.
struct TiIntegrationResult {
    /// TRUE ONLY IF EVERY EXPECTED WINDOW IS PRESENT AND OK.
    ///
    /// The single most important field here. A path with a dead window is not a
    /// slightly noisier path, it is a different integral — quadrature weights
    /// are defined by the node set, so dropping a node and reweighting the rest
    /// silently integrates a curve that was never sampled. When this is false
    /// `deltaFEv` is left at zero and must not be reported.
    bool complete = false;
    std::vector<int> missingWindows;
    double deltaFEv = 0.0;
    /// √(Σ wᵢ²σᵢ²) — the statistical error propagated exactly through the
    /// linear quadrature.
    double statisticalErrorEv = 0.0;
    /// |I_full − I_coarse|, where I_coarse uses every second node. A
    /// discretization-error estimate that costs nothing and catches both an
    /// under-resolved λ grid and an integrand with a singular end.
    double quadratureErrorEv = 0.0;
    /// The two added in quadrature. This is the number to quote.
    double totalErrorEv = 0.0;
    TiQuadrature ruleUsed = TiQuadrature::Trapezoid;
    TiEndpointDiagnostics endpoint;
    std::vector<std::string> warnings;
};

/// Integrate ⟨∂U/∂λ⟩ over the λ path.
///
/// `expectedWindows` is how many windows the run was SUPPOSED to have. Passing
/// the size of `windows` defeats the completeness check, which is the whole
/// point of the parameter: a job that died before writing its last two window
/// files hands back a shorter list, and only the expected count knows that.
TiIntegrationResult integrateThermodynamicPath(
    const std::vector<TiWindowSample>& windows, TiQuadrature rule,
    int expectedWindows);

/// Forward vs backward λ sweep — the standard reversibility diagnostic.
///
/// A path traversed in both directions must give the same ΔF. It will not if
/// the windows were not equilibrated, if the system changed phase somewhere
/// along the path, or if the coupling drove it through a barrier: all three
/// show up as HYSTERESIS, and none of them shows up as noise.
struct TiHysteresis {
    double forwardEv = 0.0;
    double backwardEv = 0.0;
    double differenceEv = 0.0;   ///< forward − backward
    double combinedErrorEv = 0.0;
    /// True when |difference| exceeds 2× the combined error: the path is not
    /// reversible and ΔF is not what either sweep says it is.
    bool significant = false;
    bool valid = false;
};
TiHysteresis compareHysteresis(const TiIntegrationResult& forward,
                               const TiIntegrationResult& backward);

/// The assembled absolute free energy.
struct TiAssembly {
    TiIntegrationResult integration;
    TiReferenceFreeEnergy reference;
    /// F = F_ref + ΔF, whole cell and per atom.
    double helmholtzEv = 0.0;
    double helmholtzEvPerAtom = 0.0;
    /// G = F + PV, whole cell and per atom. PV uses the cell volume and the
    /// external pressure of the NPT run; for an NVT run at P = 0 it is zero and
    /// G = F, which is correct rather than a placeholder.
    double gibbsEv = 0.0;
    double gibbsEvPerAtom = 0.0;
    double pvEv = 0.0;
    double errorEv = 0.0;           ///< whole cell
    double errorEvPerAtom = 0.0;
    /// False when either half is missing. A TiAssembly with valid == false
    /// carries zeros, not estimates.
    bool valid = false;
    std::vector<std::string> warnings;
};

/// F_ref + ∫⟨∂U/∂λ⟩dλ + PV, with the errors carried through.
TiAssembly assembleThermodynamicIntegration(
    const TiSystem& system, TiReference referenceKind,
    const TiReferenceParameters& parameters,
    const std::vector<TiWindowSample>& windows, TiQuadrature rule,
    int expectedWindows);

} // namespace calango::core
