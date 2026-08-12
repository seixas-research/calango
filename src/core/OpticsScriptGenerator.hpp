#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Parameters for the linear-optics (dielectric response) workflow. The
/// calculator carries the GPAW ground-state knobs (plane-wave cutoff, xc,
/// k-grid); the remaining fields describe the frequency sampling of the
/// dielectric function and which Cartesian directions to evaluate.
struct OpticsConfig {
    /// The engine and its ground-state knobs. Two regimes:
    ///
    /// GPAW — retained for provenance only (which engine the baseline ran
    /// under, for the script header and the interpreter the job binds to).
    /// The generator does NOT emit any of these values: mode, cutoff, xc,
    /// k-grid and smearing all come back from the inherited .gpw on restart,
    /// and re-declaring them here would let the wizard silently disagree
    /// with the baseline.
    ///
    /// VASP — consulted in full: there is no .gpw to inherit, so the run is
    /// self-contained (SCF, then LOPTICS at fixed density) and ENCUT / KPTS
    /// / xc / the INCAR extras all come from here.
    CalculatorConfig calculator;
    /// ABSOLUTE path to the baseline GPAW restart file (`.gpw`, written with
    /// mode="all") from a completed Single-Point Calculation. Mandatory: the
    /// optics run loads that converged ground state and evaluates the response
    /// at FIXED DENSITY, never re-running the SCF cycle.
    ///
    /// This is not merely a saving. Re-converging the ground state inside the
    /// optics job would silently give a spectrum from a different SCF solution
    /// than the one the user validated — different smearing, a different
    /// k-grid, possibly a different magnetic state. Inheriting the baseline
    /// makes the spectrum attributable to a specific, inspected ground state.
    std::string baselineDensityPath;
    /// Out-of-plane vacuum axis for a 2D sheet (0=x, 1=y, 2=z), or -1 for a
    /// bulk 3D system. When set, the script also derives the 2D observables:
    /// the sheet polarizability α₂D, the 2D conductivity σ₂D and the
    /// absorbance A(ω) — quantities that are only meaningful once the
    /// arbitrary vacuum thickness is divided back out.
    int vacuumAxis = -1;
    /// Use linear tetrahedron integration over the Brillouin zone instead of
    /// the default point (sum-over-k) integration.
    ///
    /// Point integration replaces every interband transition with a Lorentzian
    /// of width η, so structure narrower than η — a van Hove singularity, a
    /// 2D absorption edge — is smeared into the broadening rather than
    /// resolved. Tetrahedron integration interpolates the bands linearly
    /// within each tetrahedron and integrates analytically, which resolves
    /// those features at a k-mesh where point integration still shows noise.
    ///
    /// It is NOT a free improvement: GPAW requires the ground-state k-grid to
    /// contain every vertex of the irreducible BZ (see
    /// `gpaw.bztools.find_high_symmetry_monkhorst_pack`). An ordinary
    /// Monkhorst-Pack grid usually does not, and the response code refuses to
    /// run rather than integrate over an incomplete zone. The generated script
    /// therefore reports that condition as an actionable error instead of
    /// silently falling back — a spectrum produced by a different integrator
    /// than the one requested is not the spectrum that was asked for.
    bool tetrahedronIntegration = false;

    /// Denser k-mesh for the fixed-density response step, overriding the
    /// baseline's own grid. {0,0,0} inherits it unchanged.
    ///
    /// The dielectric function is a Brillouin-zone integral over interband
    /// transitions and converges much more slowly with k-points than the total
    /// energy does, so the grid that converged the ground state is routinely
    /// too coarse for the spectrum. Re-sampling at fixed density is cheap
    /// compared with re-running the SCF, which is the point.
    int responseKpts[3] = {0, 0, 0};

    /// Let GPAW reduce the response k-mesh to the irreducible Brillouin zone
    /// and weight each point by its symmetry degeneracy, instead of sampling
    /// the full zone.
    ///
    /// Measured on bulk Si at 6x6x6: 28 irreducible points against 216 in the
    /// full zone -- a 7.7x reduction -- with eps_2 agreeing to 0.6 %. The two
    /// are not bit-identical because they sample the same integral differently;
    /// the IBZ result is the cheaper route to the same spectrum.
    ///
    /// The weights are GPAW's own, derived from the cell's symmetry. Computing
    /// them here and handing over a pre-weighted list would duplicate that
    /// analysis and risk contradicting it.
    ///
    /// INDEPENDENT of tetrahedron integration, which was checked rather than
    /// assumed (bulk Si, PW 250, PBE):
    ///
    ///   * It is not what governs the IBZ-VERTEX requirement. On a
    ///     non-compliant 6³ mesh the tetrahedron run fails identically with
    ///     symmetry on and off; only `qsymmetry=False` on the response lifts
    ///     that, and it is a different switch (see tetrahedronKpointsBlock).
    ///   * Under POINT integration the two agree bitwise — 29 irreducible
    ///     points against 512 in the full zone at 8³, same ε₁(0)=21.243 and
    ///     ε₂(3.5 eV)=40.269. Pure cost saving, ~17x here.
    ///   * Under TETRAHEDRON integration they differ, because the tessellation
    ///     is anchored on the wedge rather than the zone: ε₁(0) 12.879 vs
    ///     12.596 at 8³ (2.2 %), 12.336 vs 12.351 at 16³ (0.12 %). That is
    ///     interpolation error converging away, not one of them being wrong.
    ///
    /// So this is neither obsolete nor in conflict with the tetrahedron
    /// method, and disabling it there would cost the reduction while fixing
    /// nothing.
    bool includeIbzPoints = false;

    /// Include the intraband (Drude) free-carrier term in the optical limit.
    ///
    /// GPAW gates it on `self.gs.metallic and intraband` (chi0.py), so leaving
    /// it on is a verified no-op for a gapped system and supplies the
    /// free-carrier response for a metal. That is why the default is true and
    /// why the generator does not try to guess which case it has: guessing
    /// wrong in the "off" direction is what made metals silently wrong here,
    /// producing a spectrum with no Drude ε₁ below the interband onset.
    ///
    /// Switchable all the same, because comparing a metal's spectrum with and
    /// without the free-carrier part is a real thing to want — the interband
    /// structure is otherwise buried under the Drude tail.
    bool intrabandDrude = true;

    /// Tie the Drude relaxation rate to the broadening η instead of setting it
    /// from an explicit relaxation time (GPAW's own `rate="eta"` idiom).
    ///
    /// True keeps the free-carrier lifetime and the spectral broadening as one
    /// number, which is the conservative default: it cannot disagree with
    /// itself. False uses `drudeRelaxationTimeFs`, which is what a comparison
    /// against a measured Drude edge needs — the scattering time of a real
    /// metal (Au ≈ 9 fs, Al ≈ 8 fs) has nothing to do with the broadening
    /// chosen to make a plot readable.
    bool drudeRateFromBroadening = true;

    /// Free-carrier relaxation time τ, femtoseconds. Consulted only when
    /// `drudeRateFromBroadening` is false.
    ///
    /// Converted to GPAW's `rate` (eV) by the generator, and the factor of two
    /// in that conversion is not optional: GPAW implements the term as
    /// ω_p²/(ω + i·rate)², whereas the textbook Drude function is
    /// ω_p²/(ω(ω + iΓ)) with Γ = ħ/τ. Matching the two gives Γ = 2·rate, so
    /// rate = ħ/(2τ). GPAW's own docstring notes the discrepancy ("differs
    /// from some literature by a factor of 2"); a τ entered here and a rate
    /// read out of a paper are therefore NOT the same number.
    double drudeRelaxationTimeFs = 10.0;

    double broadeningEv = 0.1;  ///< Lorentzian broadening η (GPAW) / CSHIFT (VASP), eV
    double omegaMinEv = 0.0;    ///< lower photon energy of the spectrum, eV
    double omegaMaxEv = 20.0;   ///< upper photon energy of the spectrum, eV
    /// Frequency-grid samples: GPAW evaluates exactly this many points over
    /// the window; VASP's grid density is a tag (NEDOS), so there it sets
    /// NEDOS and the window is applied when the spectrum is read back.
    int npoints = 500;
    bool dirX = true;           ///< εxx (light polarized along x)
    bool dirY = true;           ///< εyy
    bool dirZ = true;           ///< εzz

    /// Additional empty bands for the response step, as a percentage of the
    /// occupied bands: 100 allocates as many empty bands as occupied ones,
    /// 200 twice as many. Engine-independent — GPAW's fixed-density NSCF and
    /// VASP's LOPTICS NBANDS both consume it. The dielectric function sums
    /// interband transitions INTO these states, so the percentage bounds the
    /// photon energy up to which the spectrum's tail is trustworthy. A small
    /// floor of empty states is always kept, or a tiny cell would truncate
    /// the sum to nothing.
    int emptyBandsPercent = 200;
};

/// Standalone run.py: loads the baseline ground state, runs a fixed-density
/// NSCF step with extra empty bands, then uses GPAW's response module
/// (gpaw.response.df.DielectricFunction) to evaluate the frequency-dependent
/// dielectric function for each requested direction. Derives ε₁/ε₂, the complex
/// refractive index (n, k), the absorption coefficient α(ω), reflectivity R(ω)
/// and the energy-loss function L(ω), and writes them to `optics.json` for the
/// OpticsResultsWindow to read back.
std::string generateOpticsScript(const OpticsConfig& cfg);

} // namespace calango::core
