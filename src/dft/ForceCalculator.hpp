#pragma once

#include "dft/DftTypes.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace calango::core {
class Structure;
}

namespace calango::dft {

class CalangoDFTEngine;

/// Forces on the nuclei, in eV/Å, with the decomposition that produced them.
struct AtomicForces {
    Outcome outcome;
    /// The force actually reported, and the only one a caller should use.
    std::vector<std::array<double, 3>> total;
    /// The electrostatic force on the bare nucleus. Analytic; finite systems
    /// only.
    std::vector<std::array<double, 3>> hellmannFeynman;
    /// The part arising because the BASIS moves with the nuclei. Analytic and
    /// PARTIAL — see ForceCalculator for exactly which terms are in it.
    std::vector<std::array<double, 3>> pulay;
    /// Largest single component of `total`, the number a relaxation stops on.
    double maxComponentEvPerA = 0.0;
    /// True when `total` came from differencing the energy rather than from
    /// the analytic expressions. Reported, never hidden: it changes the cost
    /// by a factor of 6N and it changes what the number means.
    bool fromFiniteDifference = false;
    /// Largest discrepancy between the analytic decomposition and the
    /// finite-difference force, when both were computed. This is the honest
    /// measure of what the analytic terms are still missing.
    double analyticResidualEvPerA = 0.0;
    /// For a finite-difference force: how much it MOVED when the displacement
    /// was doubled. An exact derivative barely changes; grid noise scales as
    /// 1/h and changes by half. This is the number that decides whether the
    /// force means anything.
    double noiseEstimateEvPerA = 0.0;
};

/// The stress tensor of a periodic cell.
struct StressTensor {
    Outcome outcome;
    /// σ_αβ in eV/Å³, row-major 3×3 and symmetric by construction.
    std::array<double, 9> tensor{{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}};
    /// −tr(σ)/3, the hydrostatic pressure, in GPa. Positive means the cell
    /// wants to shrink.
    double pressureGpa = 0.0;
    /// How much the tensor moved when the strain was doubled — the same
    /// guard the forces carry, for the same reason.
    double noiseEstimateEvPerA3 = 0.0;
};

/// One step of a relaxation.
struct RelaxationStep {
    double energyEv = 0.0;
    double maxForceEvPerA = 0.0;
};

/// The result of a geometry optimisation.
struct RelaxationResult {
    Outcome outcome;
    /// Positions in Å, in the order of the input structure.
    std::vector<std::array<double, 3>> positions;
    std::vector<RelaxationStep> history;
    int steps = 0;
    double finalMaxForceEvPerA = 0.0;
    double finalEnergyEv = 0.0;
};

/// Forces on the nuclei, and geometry optimisation driven by them.
///
/// WHY THERE ARE TWO PATHS, AND WHICH ONE IS THE PRODUCT.
///
/// With numerical atomic orbitals the force is not the Hellmann-Feynman term
/// alone. The basis functions are ATOM-CENTRED AND MOVE WITH THE NUCLEI, so
/// the variational space itself depends on the geometry and
/// ∂φ_i/∂R_A ≠ 0. That adds the Pulay force,
///
///     F_A^{Pulay} = −Σ_ij [ D_ij ∂H_ij/∂R_A − W_ij ∂S_ij/∂R_A ] ,
///
/// with D the density matrix and W the energy-weighted one, the derivatives
/// running over the explicit dependence of the basis on R_A. A third term
/// comes from the integration grid, which is also atom-centred and moves. A
/// Hellmann-Feynman-only force is smooth, plausible, and wrong by an amount
/// that GROWS as the basis gets smaller — which is the worst possible failure
/// mode, because it looks best exactly where it is least trustworthy.
///
/// STATUS, PLAINLY:
///
///   * The FINITE-DIFFERENCE force is the PRODUCT. It is exact for any
///     functional and either boundary condition, it drives `relax`, and it
///     costs 6N self-consistency cycles.
///   * The ANALYTIC decomposition implements the Hellmann-Feynman term and
///     the OVERLAP part of Pulay. Missing: the Hamiltonian part,
///     Σ_ij D_ij ∂H_ij/∂R_A, and the derivative of the Becke weights. On H₂
///     at 0.90 Å the implemented terms give 5.17 eV/Å where the true force is
///     0.79 — the missing terms DOMINATE. Both are reported so the gap stays
///     visible instead of being assumed away.
///
/// THE FINITE-DIFFERENCE FORCE WAS NOISE UNTIL THE ENERGY SURFACE WAS FIXED,
/// and the misdiagnosis is worth keeping. It carried 1–100 eV/Å of noise with
/// no plateau in the step size and no improvement on grid refinement, which
/// looked exactly like the atom-centred grid moving with the atoms — a real
/// effect, and the wrong culprit. It was the ENERGY: the neutral-atom pair
/// term ∫ρ_A v_B was being quadratured on atom A's spherical grid, and
/// v_B carries B's NUCLEAR POLE, sitting off-centre where no atomic partition
/// can tame it. Invisible for one atom (no pairs; it reproduced the radial
/// solver to 0.001 eV) and fatal for two. Doing that integral in closed form
/// instead — see HamiltonianAssembler::prepare — took the angular drift of
/// E(H₂) from 0.105 eV to 0.0007 eV, and the force from noise to a clean
/// plateau.
///
/// The lesson that generalises: a finite-difference derivative is only as
/// smooth as the energy underneath it, so a noisy force is evidence about the
/// ENERGY first and about the differencing second.
class ForceCalculator {
public:
    explicit ForceCalculator(Parameters parameters);

    /// −dE/dR by central differences of the converged total energy.
    ///
    /// Exact IN PRINCIPLE for every functional and both boundary conditions,
    /// and costs 12N self-consistency cycles — the force is evaluated at two
    /// displacements so the result can be checked.
    ///
    /// IT DOES NOT CURRENTLY WORK, and the reason is worth stating precisely
    /// because it decides what has to be built next. The multicentre grid is
    /// ATOM-CENTRED: displacing an atom moves the grid with it, so the
    /// quadrature error of E(R+h) and E(R−h) is not the same error and does
    /// not cancel in the difference. That residue is of order 10⁻²–10⁻¹ eV,
    /// and dividing it by 2h gives forces carrying between 1 and 100 eV/Å of
    /// pure noise. Measured on H₂: no plateau in the step size and no
    /// convergence as the grid is refined —
    ///
    ///     shells   h=0.005   h=0.01   h=0.02   h=0.05
    ///        60     +0.20     +0.08    −0.60    +2.60
    ///       100     −4.48     −7.22   −12.38    +3.08
    ///       160   −110.61     −8.55    +2.38    +0.70   (eV/Å)
    ///
    /// with tightening the self-consistency tolerances by four orders
    /// changing nothing, which rules out convergence depth and leaves the
    /// grid.
    ///
    /// So this returns a NUMERICAL FAILURE whenever the two step sizes
    /// disagree, rather than handing back a number that looks like a force.
    /// The consequence is architectural: for this engine the analytic force is
    /// not an optimisation over finite differences, it is the only viable
    /// route, because it never differences two different grids.
    /// `verify` runs the whole thing again at twice the step so the two can
    /// be compared. That doubles the cost, so a relaxation — which evaluates
    /// forces every step on a surface it has already checked once — turns it
    /// off after the first call.
    AtomicForces finiteDifference(const core::Structure& structure,
                                  double stepA = 0.01,
                                  bool verify = true) const;

    /// The analytic decomposition, plus the finite-difference force it is
    /// measured against when `compare` is set.
    AtomicForces analytic(const core::Structure& structure,
                          bool compare = true) const;

    /// The stress tensor, from the response of the total energy to a uniform
    /// strain: x → (1 + ε)x applied to both the lattice vectors and the
    /// atomic positions, with
    ///
    ///     σ_αβ = (1/Ω) ∂E/∂ε_αβ .
    ///
    /// Six independent components by central differences, so twelve
    /// self-consistency cycles. Every term of the energy contributes
    /// automatically — the kinetic and overlap matrices through the strained
    /// inter-atomic vectors, the electrostatics through both the lattice sum
    /// and the multipole expansion, the exchange-correlation through the
    /// volume element, and the basis through the same mechanism that gives
    /// rise to the Pulay force — because all of them are inside the energy
    /// being differenced.
    ///
    /// Periodic cells only: strain is not defined without one.
    /// `verify` repeats the whole thing at twice the strain so the two can be
    /// compared, doubling the cost.
    StressTensor stress(const core::Structure& structure,
                        double strain = 0.005, bool verify = true) const;

    /// Relax the geometry with FIRE, driven by finite-difference forces.
    ///
    /// FIRE rather than BFGS: it needs no line search and no Hessian estimate,
    /// which matters when a single force evaluation costs 6N self-consistency
    /// cycles and a rejected line-search trial would throw all of them away.
    RelaxationResult relax(const core::Structure& structure,
                           double forceToleranceEvPerA = 0.05,
                           int maxSteps = 30) const;

    const Parameters& parameters() const { return parameters_; }

private:
    Parameters parameters_;
};

} // namespace calango::dft
