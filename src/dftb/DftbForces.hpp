#pragma once

#include "core/Structure.hpp"
#include "dft/DftTypes.hpp"
#include "dftb/DftbScf.hpp"
#include "dftb/SlaterKosterTable.hpp"

#include <vector>

/// Atomic forces by central finite difference of the total SCC-DFTB energy.
///
/// NOT analytic. `calango::dft::ForceCalculator` (src/dft, the native
/// all-electron engine) already established the precedent this follows: its
/// own doc says outright that the finite-difference force is "the number a
/// caller should act on... which is exact", with an analytic path existing
/// only to be MEASURED against it — hand-differentiating the Slater-Koster
/// angular transform (SlaterKosterTransform.hpp) with respect to every bond
/// vector component is a real, error-prone undertaking of its own, and nine
/// re-diagonalizations of an already fast, validated pipeline is a smaller
/// risk than a fresh derivative implementation for a first release. See
/// FUTURE.md for an analytic Hellmann-Feynman force as a follow-up.
///
/// Cost: 6 extra SCF solves per atom (+/- x, y, z), reusing
/// DftbHamiltonianBuilder::build() and DftbScf::run() unchanged — the right
/// trade for a SINGLE-POINT calculator (this engine's only current task;
/// geometry optimisation/MD are explicitly out of scope, see FUTURE.md), not
/// for a hot MD loop.
namespace calango::dftb {

struct DftbForces {
    /// eV/Angstrom, index-aligned with the structure's atoms — the
    /// convention `core::CalculatorConfig`'s generated scripts already use,
    /// so this integrates with the same downstream reporting (max-force
    /// convergence, single_point.json's forces_eV_per_A) unchanged.
    std::vector<core::Vec3> forcesEvPerAngstrom;
    /// Cartesian displacement actually used, Angstrom (diagnostic).
    double displacementAngstrom = 0.0;
};

/// Compute forces on every atom of `structure` at its CURRENT geometry.
/// `kpoints` is the SAME k-mesh a single-point SCF run at this geometry
/// would use (Gamma only, weight 1, for a molecule) — rebuilt at each
/// perturbed geometry internally alongside the basis and Hamiltonian (the
/// parameter table itself does not change, only atomic positions do).
dft::Outcome computeDftbForces(const core::Structure& structure,
                                const SlaterKosterTable& table,
                                const std::vector<DftbKPoint>& kpoints,
                                const DftbScfSettings& settings,
                                DftbForces& out,
                                double displacementAngstrom = 0.01);

} // namespace calango::dftb
