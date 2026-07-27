#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Parameters for a Born effective charge calculation, filled in by the Born
/// Charges wizard and consumed by generateBornChargesScript(). Deliberately
/// UI-free so the script can also be generated headlessly.
///
/// The Born effective charge tensor
///
///     Z*_{k,αβ} = (Ω / e) · ∂P_α / ∂u_{kβ}
///
/// is the dynamical charge that couples an atomic displacement to the
/// macroscopic polarization. It is not the formal ionic charge and is not a
/// scalar: it is a 3×3 tensor per atom, it can exceed the nominal valence by a
/// factor of two or more in ferroelectrics, and it is what determines LO-TO
/// splitting and infrared intensities. A phonon spectrum of a polar insulator
/// computed without it is wrong at Γ by construction.
struct BornChargesConfig {
    /// Engine + backend knobs. Only read when `baselinePath` is empty; with a
    /// baseline the whole calculator is restored from it instead. Only GPAW
    /// can evaluate the Berry-phase polarization this method differentiates;
    /// the generated script says so plainly when another engine is selected.
    CalculatorConfig calculator;

    /// Absolute path to a completed Single-Point Calculation's `.gpw`.
    ///
    /// MANDATORY in the GUI, and it supplies two things: the converged
    /// GEOMETRY the displacements are taken about, and the calculator
    /// parameters every displaced run is rebuilt from — so all 6N of them use
    /// exactly the settings the baseline was validated with.
    ///
    /// Note what it does NOT supply, unlike the Optics or Electronic Structure
    /// baselines: a density to evaluate at. Z* is the response of the charge
    /// distribution TO a displacement, so each displaced geometry has to
    /// re-converge its own SCF — there is no fixed-density shortcut, and the
    /// cost is 6N self-consistent runs however the baseline is chosen.
    std::string baselinePath;

    /// Displacement amplitude in Å for the central finite difference. Small
    /// enough to stay linear, large enough that the polarization difference is
    /// not swamped by SCF noise; 0.01 Å is the usual compromise.
    double displacement = 0.01;

    /// Restrict the calculation to these 0-based atom indices. Empty means all
    /// atoms. Each atom costs 6 SCF runs, so a symmetry-inequivalent subset is
    /// often all that is wanted.
    std::vector<int> atomIndices;

    /// Impose the acoustic sum rule Σ_k Z*_k = 0 by subtracting the mean
    /// residual from every tensor.
    ///
    /// The sum rule is exact — translating the whole crystal cannot polarize it
    /// — so a nonzero sum measures the calculation's own convergence error.
    /// Enforcing it is standard practice; the raw tensors are reported
    /// alongside the corrected ones so the size of that correction stays
    /// visible instead of being quietly absorbed.
    bool acousticSumRule = true;
};

/// Turns a BornChargesConfig into a standalone ASE/GPAW script that writes
/// `born_charges.json` into the job directory and emits the
/// `CALANGO_RESULT born_charges=born_charges.json` marker the controller
/// watches for.
std::string generateBornChargesScript(const BornChargesConfig& cfg);

} // namespace calango::core
