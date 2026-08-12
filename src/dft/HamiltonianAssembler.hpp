#pragma once

#include "dft/DftTypes.hpp"
#include "dft/IntegrationGrid.hpp"
#include "dft/NAOBasisSet.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace calango::dft {

/// A symmetric matrix over basis functions, stored dense.
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

/// The electrostatic and exchange-correlation energies of one density.
struct PotentialEnergies {
    double electrostatic = 0.0;      ///< hartree, INCLUDING nuclear repulsion
    double exchangeCorrelation = 0.0;///< hartree
    /// ∫ρ v_eff dV with the potential the orbitals were found in — the term
    /// the band energy has to have removed from it to leave the kinetic
    /// energy.
    double potentialTrace = 0.0;
    /// The largest |∫δρ_A| over the atoms: how much net charge the difference
    /// density puts on a single centre. Reported rather than hidden because
    /// the electrostatics truncates the monopole tail of δρ, which is exact
    /// only while this is negligible. Anything above ~10⁻³ electrons means
    /// the answer needs an Ewald sum this engine does not yet do.
    double largestAtomicMonopole = 0.0;
};

/// Builds the overlap and Hamiltonian matrices in the numerical atomic
/// orbital basis, and the electrostatics that feeds them.
///
/// Nothing here is analytic. With numerically tabulated basis functions there
/// is no closed form for ⟨φ_i|φ_j⟩, so every matrix element is a quadrature
/// over the multicentre grid:
///
///     S_ij(k) = Σ_g w_g χ_i*(r_g) χ_j(r_g),
///     H_ij(k) = Σ_g w_g χ_i*(r_g) [ T̂ + v_eff(r_g) ] χ_j(r_g)
///
/// with χ_i^k(r) = Σ_T e^{ik·T} φ_i(r − τ_i − T) the Bloch sum of a confined
/// atomic orbital. The sum over T is FINITE, not truncated: φ is exactly zero
/// beyond its confinement radius.
///
/// The kinetic term is not differenced. Each basis function solves the radial
/// equation of the confined atom it came from, so
///
///     −½∇²φ = (ε − v_at(r)) φ
///
/// exactly, and T̂ acting on a basis function is a MULTIPLICATION by a
/// tabulated function. Written the obvious way — a numerical Laplacian, or
/// even the symmetric ∇φ·∇φ form — the largest single term in the total
/// energy would be carrying the noise of one or two numerical derivatives of
/// an interpolated table. The matrix is symmetrised over (i, j) because the
/// identity holds with each function's OWN ε and v_at.
///
/// Electrostatics uses the neutral-atom decomposition. The total charge of a
/// crystal is written as a sum of NEUTRAL free atoms plus a difference
/// density that integrates to zero:
///
///     n(r) = Σ_{A,T} [ρ_A^free(r−R_A−T) − Z_A δ(r−R_A−T)] + δρ(r)
///
/// Each bracket is neutral, so its potential decays exponentially instead of
/// as 1/r and the lattice sum converges absolutely — no Ewald, no conditional
/// convergence, no compensating background. What is left, δρ, is the small
/// bonding rearrangement, and its potential is solved centre by centre by
/// expanding it in real spherical harmonics on each atom's shells and
/// integrating the radial Poisson equation for each (l, m).
class HamiltonianAssembler {
public:
    struct Atom {
        int atomicNumber = 0;
        std::array<double, 3> position{{0.0, 0.0, 0.0}}; ///< bohr
    };

    HamiltonianAssembler(const NAOBasisSet& basis, Parameters parameters);

    /// Set up the geometry: enumerate basis functions, build the lattice
    /// image list, and tabulate every basis function at every grid point it
    /// reaches. Everything after this is a loop over stored values.
    ///
    /// `lattice` empty means a finite system.
    Outcome prepare(const std::vector<Atom>& atoms,
                    const std::vector<std::array<double, 3>>& lattice,
                    const IntegrationGrid& grid);

    std::size_t dimension() const { return functions_.size(); }
    const std::vector<BasisFunctionIndex>& functions() const
    {
        return functions_;
    }
    std::size_t gridSize() const { return gridSize_; }

    /// Overlap and kinetic matrices at one k-point (in units of the reciprocal
    /// lattice, i.e. fractional coordinates). Both are Hermitian.
    Outcome buildOverlapAndKinetic(
        const std::array<double, 3>& kFractional,
        std::vector<std::complex<double>>& overlap,
        std::vector<std::complex<double>>& kinetic) const;

    /// The matrix of an effective potential at one k-point.
    ///
    /// `potential` is the local part. `gradientField`, when non-empty, is the
    /// vector field 2(∂f/∂σ)∇ρ of a gradient functional, three components per
    /// grid point, and adds
    ///
    ///     ∫ V · ( φ_i* ∇φ_j + φ_j ∇φ_i* )
    ///
    /// — the integration-by-parts form, which needs only ∇φ and is manifestly
    /// Hermitian. Taking ∇·(∂f/∂∇ρ) directly instead would mean
    /// differentiating a vector field sampled on an unstructured multicentre
    /// grid, which is both awkward and noisy.
    Outcome buildPotentialMatrix(
        const std::vector<double>& potential,
        const std::vector<double>& gradientField,
        const std::array<double, 3>& kFractional,
        std::vector<std::complex<double>>& matrix) const;

    /// Superposition of free-atom densities — the initial guess, and the
    /// reference the electrostatics is a difference from.
    const std::vector<double>& superposedAtomicDensity() const
    {
        return atomicDensity_;
    }

    /// Its gradient, three components per point. Analytic — each free atom's
    /// density is radial, so ∇ρ_A = (dρ_A/dr)·r̂ and the superposition is a
    /// sum of those. Empty unless the functional asked for gradients.
    ///
    /// It exists so the FIRST self-consistency step of a gradient functional
    /// starts from a real gradient rather than from zero: the initial density
    /// is the superposition, and leaving σ = 0 there would make the opening
    /// potential silently local.
    const std::vector<double>& superposedAtomicDensityGradient() const
    {
        return atomicDensityGradient_;
    }

    /// Σ_{A,T} v_A^NA: the potential of all those neutral atoms, on the grid.
    const std::vector<double>& neutralAtomPotential() const
    {
        return neutralAtomPotential_;
    }

    /// The electrostatic energy of the free atoms alone, per cell: each atom's
    /// own electrostatic self-energy plus every neutral-atom pair interaction.
    /// Constant for a fixed geometry, computed once in `prepare`.
    double referenceElectrostaticEnergy() const { return referenceEnergy_; }

    /// Build the effective potential from a density, and the energies that go
    /// with it.
    ///
    /// `density` is ρ on the grid. `effective` comes back as
    /// v_es + v_xc, ready for `buildPotentialMatrix`.
    /// `densityGradient` (three components per point) is required for a
    /// gradient functional and ignored otherwise. `gradientField` comes back
    /// holding 2(∂f/∂σ)∇ρ, empty for a local functional.
    Outcome buildEffectivePotential(const std::vector<double>& density,
                                    const std::vector<double>& densityGradient,
                                    std::vector<double>& effective,
                                    std::vector<double>& gradientField,
                                    PotentialEnergies& energies) const;

    /// ρ(r) on the grid from a set of occupied orbitals.
    ///
    /// `coefficients[k]` holds the eigenvectors at k-point k (n × m, one
    /// vector per column) and `occupations[k]` their occupations including the
    /// k-point weight. The density comes out real: the imaginary parts cancel
    /// between k and −k, and any residue is a symmetry error worth reporting
    /// rather than discarding, so it is returned in `imaginaryResidue`.
    Outcome buildDensity(
        const std::vector<std::vector<std::complex<double>>>& coefficients,
        const std::vector<std::vector<double>>& occupations,
        const std::vector<std::array<double, 3>>& kPoints,
        std::vector<double>& density,
        std::vector<double>* densityGradient = nullptr) const;

    /// Whether `prepare` tabulated ∇φ. Set from the functional: a local
    /// functional pays neither the memory nor the time.
    bool hasGradients() const { return !contributionGradients_.empty(); }

    /// The electrostatic force on each bare nucleus, from the electron
    /// density on the grid plus the other nuclei. Hartree per bohr.
    ///
    /// FINITE SYSTEMS ONLY: the nuclear-nuclear part is a bare lattice sum
    /// that does not converge for a periodic cell without Ewald summation,
    /// and returning a truncated one would be worse than refusing.
    Outcome hellmannFeynmanForces(
        const std::vector<double>& density,
        std::vector<std::array<double, 3>>& forces) const;

    /// The overlap part of the Pulay force, −Σ_ij W_ij ∂S_ij/∂R_A, from the
    /// energy-weighted density matrix W. Hartree per bohr.
    Outcome pulayOverlapForces(
        const std::vector<std::vector<std::complex<double>>>& coefficients,
        const std::vector<std::vector<double>>& occupations,
        const std::vector<std::vector<double>>& eigenvalues,
        const std::vector<std::array<double, 3>>& kPoints,
        std::vector<std::array<double, 3>>& forces) const;

    const Parameters& parameters() const { return parameters_; }

private:
    /// One tabulated contribution: basis function `function` of lattice image
    /// `image` has value `value` at the grid point this entry belongs to, and
    /// the kinetic operator acting on it has value `kinetic` there.
    ///
    /// Both are tabulated once, in `prepare`. Carrying −½∇²φ alongside φ is
    /// what lets the basis contain functions that are not eigenstates of
    /// anything — a split-valence zeta is an orbital minus a polynomial — and
    /// it takes an interpolation of the atomic potential out of the innermost
    /// loop of every iteration as a side effect.
    struct Contribution {
        std::uint32_t function = 0;
        std::uint32_t image = 0;
        double value = 0.0;
        double kinetic = 0.0;
    };

    /// Bloch sums χ_i^k at every grid point, from the stored contributions.
    /// With `gradients` non-null, ∇χ_i^k as well — the same phases applied to
    /// the stored ∇φ.
    void blochSums(const std::array<double, 3>& kFractional,
                   std::vector<std::complex<double>>& values,
                   std::vector<std::complex<double>>* gradients = nullptr) const;

    const NAOBasisSet& basis_;
    Parameters parameters_;
    std::vector<Atom> atoms_;
    std::vector<std::array<double, 3>> lattice_;
    std::vector<BasisFunctionIndex> functions_;
    /// Lattice translations, in cartesian bohr and in integer multiples.
    std::vector<std::array<double, 3>> images_;
    std::vector<std::array<int, 3>> imageIndices_;
    /// Grid geometry, copied because the assembler outlives a caller's grid.
    std::vector<double> weights_;
    std::vector<std::array<double, 3>> positions_;
    std::vector<std::size_t> pointAtom_;
    std::vector<std::size_t> pointShell_;
    std::vector<std::size_t> pointDirection_;
    std::vector<double> shellRadii_;
    std::vector<double> shellWeights_;
    std::vector<AngularPoint> directions_;
    std::size_t gridSize_ = 0;
    /// Contributions, concatenated, with `offsets_` bracketing each point.
    std::vector<Contribution> contributions_;
    /// ∇φ for each contribution, three components, parallel to
    /// `contributions_`. A separate array rather than three more fields on
    /// Contribution so that a local functional allocates none of it.
    std::vector<double> contributionGradients_;
    std::vector<std::size_t> offsets_;
    /// Precomputed per-grid-point quantities.
    std::vector<double> atomicDensity_;
    std::vector<double> atomicDensityGradient_;
    std::vector<double> neutralAtomPotential_;
    double referenceEnergy_ = 0.0;
};

} // namespace calango::dft
