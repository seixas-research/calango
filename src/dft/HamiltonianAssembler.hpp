#pragma once

#include "dft/DftTypes.hpp"
#include "dft/NAOBasisSet.hpp"

#include <cstddef>
#include <vector>

namespace calango::core {
class Structure;
}

namespace calango::dft {

/// A symmetric matrix over basis functions, stored dense for now.
///
/// Dense is a scaffolding decision, not the design. A strictly confined basis
/// makes H and S sparse — that is the entire reason for confining it — and the
/// production form is a per-atom-pair block list with an index that skips
/// pairs further apart than the sum of their cutoff radii. The dense form is
/// here because it is impossible to get wrong, which is what a reference
/// implementation to check the sparse one against has to be.
struct SymmetricMatrix {
    std::size_t dimension = 0;
    std::vector<double> values; ///< row-major, full square

    void resize(std::size_t n);
    double at(std::size_t i, std::size_t j) const;
    void set(std::size_t i, std::size_t j, double value);
    bool empty() const { return dimension == 0; }
};

/// Builds the overlap and Hamiltonian matrices in the numerical atomic
/// orbital basis.
///
/// Nothing here is analytic, and that is the defining constraint. With
/// numerically tabulated basis functions there is no closed form for
/// ⟨φ_i|φ_j⟩ or ⟨φ_i|V|φ_j⟩, so every element is a quadrature over a
/// real-space grid:
///
///     S_ij = Σ_g w_g φ_i(r_g) φ_j(r_g)
///     H_ij = Σ_g w_g [ ∇φ_i·∇φ_j / 2 + φ_i φ_j V_eff(r_g) ]
///
/// The kinetic term is written in its symmetric gradient form rather than as
/// φ_i ∇²φ_j: the Laplacian of a numerically tabulated function amplifies
/// grid noise twice, while the gradient form needs one derivative of each
/// factor and is variationally better behaved.
///
/// The grid `g` is a superposition of atom-centred spherical grids (radial
/// shells × Lebedev angular points), partitioned between atoms by a smooth
/// nuclear weight function so the overlapping spheres sum to exactly one
/// everywhere — Becke's multicentre scheme. That partition is what makes a
/// molecular integral as accurate as an atomic one without a global mesh.
///
/// STATUS: interfaces and the matrix container are implemented; the
/// quadrature is not. Every build call reports NotImplemented rather than
/// returning a zero matrix, because a zero overlap matrix is a singular
/// generalised eigenproblem and the failure would surface three layers away.
class HamiltonianAssembler {
public:
    HamiltonianAssembler(const NAOBasisSet& basis, Parameters parameters);

    /// Total basis functions for `structure` under the current basis, or 0
    /// when a species in the structure has no basis.
    std::size_t dimension(const core::Structure& structure) const;

    /// Overlap matrix S_ij = ⟨φ_i|φ_j⟩.
    ///
    /// Not implemented. Needs the multicentre integration grid.
    Outcome buildOverlap(const core::Structure& structure,
                         SymmetricMatrix& overlap) const;

    /// Hamiltonian H_ij for the given effective potential sampled on the
    /// integration grid.
    ///
    /// Not implemented. Needs the multicentre integration grid and the
    /// gradients of the basis functions on it.
    Outcome buildHamiltonian(const core::Structure& structure,
                             const std::vector<double>& effectivePotential,
                             SymmetricMatrix& hamiltonian) const;

    /// Electron density on the integration grid from the occupied orbitals.
    ///
    /// Not implemented.
    Outcome buildDensity(const core::Structure& structure,
                         const std::vector<double>& orbitalCoefficients,
                         const std::vector<double>& occupations,
                         std::vector<double>& density) const;

    const Parameters& parameters() const { return parameters_; }

private:
    const NAOBasisSet& basis_;
    Parameters parameters_;
};

} // namespace calango::dft
