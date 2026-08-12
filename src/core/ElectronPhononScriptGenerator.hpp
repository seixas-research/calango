#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Electron-phonon coupling by supercell finite differences (`gpaw.elph`).
///
/// The workflow GPAW documents, in three stages, each of which is a separate
/// expense and the reason this module has the shape it does:
///
///   1. `DisplacementRunner` — displaces every atom of the primitive cell by
///      ±δ along x/y/z inside the supercell and stores the change in the
///      effective potential, ∂V/∂u, together with the forces. This is 6N+1
///      supercell SCF runs and is almost the whole cost.
///   2. `Supercell.calculate_supercell_matrix` — projects those potential
///      gradients onto the LCAO basis, giving g in the local-orbital basis.
///   3. `ElectronPhononMatrix.bloch_matrix` — rotates into the Bloch basis and
///      contracts with the phonon eigenvectors, giving
///      g_mn^ν(k,q) = sqrt(ħ/2Mω_qν) ⟨m,k+q| ε_qν·∇V |n,k⟩ in eV.
///
/// LCAO is not a preference here: `Supercell` projects onto basis functions,
/// so the ground state must have them. Plane-wave mode has no LCAO basis to
/// project onto and the stage-2 call has nothing to work with.
struct ElectronPhononConfig {
    /// Engine and its ground-state knobs. GPAW only — `gpaw.elph` is the
    /// implementation, and there is no equivalent path through the other
    /// engines' ASE calculators.
    CalculatorConfig calculator;

    /// Finite-displacement supercell, in repetitions of the primitive cell.
    ///
    /// This is the range of the electron-phonon interaction the calculation
    /// can represent: ∂V/∂u is only known out to the supercell boundary, so a
    /// 1×1×1 "supercell" would confine every coupling to the home cell and a
    /// too-small one truncates it silently rather than failing. It also sets
    /// which q-points are commensurate — see `qGrid`.
    int supercell[3] = {2, 2, 2};

    /// ±displacement per atom and Cartesian direction, Å.
    double deltaAngstrom = 0.01;

    /// Electronic k-mesh for the ground state and for the Bloch-basis
    /// rotation. The Fermi-surface double delta in α²F is an integral over
    /// THIS mesh, so it converges as slowly here as the plasma frequency does
    /// in the optics module, and for the same reason: only states at E_F
    /// contribute.
    int kGrid[3] = {8, 8, 8};

    /// Phonon q-mesh the coupling is evaluated on.
    ///
    /// Must be commensurate with the supercell — a q that is not a reciprocal
    /// vector of the supercell has no phonon in the finite-difference cache
    /// and GPAW refuses it. The generated script therefore checks this against
    /// `supercell` before the expensive stage and says so, rather than letting
    /// stage 3 fail after stages 1 and 2 have been paid for.
    int qGrid[3] = {2, 2, 2};

    /// LCAO basis for the ground state and the projection.
    ///
    /// "dzp" is the production choice; "sz(dzp)" is what GPAW's own elph tests
    /// use because it is the cheapest basis that still has p functions.
    std::string basis = "dzp";

    // No Fermi-surface smearing: the two δ(ε − E_F) factors are integrated
    // with the linear tetrahedron method (see TetrahedronBz), which has no
    // width. The field that used to be here decided the answer — λ on fcc Al
    // spanned 0.009 to 31 across a 16× change in it with no plateau — so it
    // was removed rather than defaulted.

    /// Morel-Anderson Coulomb pseudopotential μ*, for the T_c estimate.
    ///
    /// Empirical and not computed anywhere here: 0.10–0.15 for sp metals,
    /// higher for transition metals. T_c depends on it exponentially, which
    /// is why it travels into the manifest rather than being defaulted at
    /// analysis time.
    double muStar = 0.10;

    /// Drude plasma frequency ħω_p in eV, for the resistivity ρ(T).
    ///
    /// Comes from the Optics or K-point Convergence module, not from here.
    /// Zero means "not known", and ρ is then skipped rather than estimated —
    /// ρ goes as 1/ω_p², so a guess would produce a plausible-looking number.
    double plasmaFrequencyEv = 0.0;

    /// Gaussian width for the phonon delta δ(ω − ω_qν) that bins α²F, eV.
    double phononSmearingEv = 0.005;

    /// Frequency bins for α²F(ω).
    int alpha2fPoints = 400;

    /// Temperature at which the electron-phonon relaxation time is reported, K.
    ///
    /// τ is temperature-dependent — that is the physics, not a setting — and
    /// the Drude model needs it at the temperature the optical measurement was
    /// made. 300 K is the default because that is what a room-temperature
    /// reflectivity is compared against.
    double temperatureK = 300.0;
};

/// The `run.py` for the full three-stage workflow, ending in `epc.json`.
///
/// Beyond the matrix elements themselves the script derives the quantities a
/// user actually asks this module for: the Eliashberg spectral function
/// α²F(ω), the mass-enhancement coupling constant λ = 2∫α²F(ω)/ω dω, and from
/// those the electron-phonon relaxation time τ(T) — which is the number the
/// Drude term in the optics module takes as its input.
std::string generateElectronPhononScript(const ElectronPhononConfig& cfg);

} // namespace calango::core
