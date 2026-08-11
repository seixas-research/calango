#pragma once

#include "dft/AtomicSolver.hpp"
#include "dft/DftTypes.hpp"
#include "dft/RadialGrid.hpp"

#include <map>
#include <string>
#include <vector>

namespace calango::dft {

/// One numerically tabulated radial function u(r) with an angular momentum.
///
/// The basis function it belongs to is
///
///     φ(r) = [u(r) / r] · Y_lm(θ, φ)
///
/// stored as u(r) = r·R(r) rather than R(r) directly. That is not a
/// convention detail: u(r) is what the radial Schrödinger equation is written
/// in (u'' = [l(l+1)/r² + 2(V − ε)] u, with no first-derivative term), it goes
/// to zero at the origin for every l so the cusp is not a numerical problem,
/// and ∫|u|²dr = 1 is the normalisation with no r² weight to lose precision
/// on.
struct RadialFunction {
    int l = 0;              ///< angular momentum
    int principal = 1;      ///< n, for labelling ("2p"); not used in the maths
    /// u(r) = r·R(r), sampled on the owning basis set's grid.
    std::vector<double> u;
    /// Radius beyond which u is exactly zero (bohr). Every function is
    /// strictly confined — this is what makes the overlap sparse.
    double cutoffBohr = 0.0;
    /// Free-atom eigenvalue this function came from (hartree), or 0 for a
    /// function that is not an atomic solution. Kept for the basis report.
    double eigenvalue = 0.0;
    /// r·(−½∇²φ) with the angular part divided out: the RADIAL PART OF THE
    /// KINETIC OPERATOR ACTING ON THIS FUNCTION, tabulated on the same mesh.
    ///
    /// Stored rather than derived, because not every basis function is an
    /// eigenfunction. For one that is, −½∇²φ = (ε − v_at)φ and this array is
    /// just (ε − v_at)·u — which is what the assembler used to recompute on
    /// the fly. A split-valence function is NOT an eigenfunction of anything:
    /// it is one orbital minus a polynomial, and the only honest way to give
    /// the assembler its kinetic energy is to compute it when the function is
    /// built, where both pieces are known in closed form, and carry it along.
    ///
    /// Doing it this way also removes an interpolation of the atomic potential
    /// from the innermost loop of every iteration, so the general form is the
    /// faster one too.
    ///
    /// Smooth through the origin: for small r it goes as Z·r^l, because the
    /// 1/r of the nuclear potential is cancelled by the r^{l+1} of u.
    std::vector<double> kineticU;
    /// du/dr on the same mesh. Needed only for ∇φ — which a gradient
    /// functional needs, and which the Pulay forces will need — so it is
    /// tabulated once here rather than differenced inside the assembler's
    /// innermost loop.
    std::vector<double> du;
    /// "2p", "3d", "H(2p,2.1)" — for logs and for the basis-set report.
    std::string label;

    /// (2l+1) real spherical harmonics per radial function.
    int functionCount() const { return 2 * l + 1; }
};

/// Every basis function of one chemical species.
///
/// TIERS. A numerical atomic basis is built in nested levels, each strictly
/// containing the last, so "one tier up" is a meaningful instruction and the
/// energy falls monotonically:
///
///   * TIER 1, minimal (single-zeta): the confined atom's own occupied
///     orbitals, core included. Exact for the free atom in the limit of no
///     confinement, and nothing else.
///   * TIER 2, double-zeta plus polarisation: a second radial function for
///     each VALENCE angular momentum, plus one shell of the first angular
///     momentum the free atom does not occupy (d for silicon).
///   * TIER 3: a third zeta for the valence channels and a second zeta for
///     the polarisation channel.
///
/// The second zeta is the SPLIT-VALENCE construction, which needs no bonded
/// reference calculation and no fitting. Inside a radius r_s chosen so that a
/// fixed fraction of the orbital's norm lies beyond it, the orbital is
/// replaced by the smooth polynomial r^l(a − br²) matched in value and slope
/// at r_s; the new basis function is the DIFFERENCE between the orbital and
/// that polynomial, which is exactly zero for r > r_s. So the extra function
/// costs nothing in range — it is strictly more localised than the one it
/// refines — and it gives the variational freedom to change the orbital's
/// shape near the nucleus independently of its tail. That freedom is exactly
/// what a bond needs, and its absence is most of why a minimal basis
/// overbinds.
///
/// The polarisation shell is a genuine solution of the confined atom's radial
/// equation at an angular momentum the atom does not occupy. Unbound in the
/// free atom, bound in the box: the confinement is what makes it exist.
struct SpeciesBasis {
    Species species;
    /// Index into `functions` where each tier begins; tierOffsets[0] == 0.
    std::vector<std::size_t> tierOffsets;
    std::vector<RadialFunction> functions;

    /// The EFFECTIVE POTENTIAL of the confined atom these functions solve,
    /// v_at(r) = −Z/r + v_H + v_xc, in hartree, on the shared radial grid —
    /// minus the nuclear part, which is analytic and kept out of the array.
    ///
    /// Stored because it makes the kinetic energy exact rather than
    /// differenced. Each φ satisfies the radial equation it came from, so
    ///
    ///     −½∇²φ = (ε − v_at(r)) φ
    ///
    /// identically: the kinetic operator acting on a basis function is a
    /// MULTIPLICATION, and every kinetic matrix element is an ordinary
    /// quadrature of a product of tabulated functions. The alternative — a
    /// numerical Laplacian, or the ∇φ·∇φ form — differentiates a table twice
    /// or once, and the noise it amplifies lands squarely on the largest term
    /// in the total energy.
    std::vector<double> atomicPotential;
    /// Free-atom density ρ(r) in electrons/bohr³, on the same grid. This is
    /// the initial guess for the crystal and the reference the electrostatics
    /// is written as a difference from.
    std::vector<double> freeAtomDensity;
    /// The free atom's own electrostatic potential, −Z/r + v_H[ρ_free], with
    /// the nuclear part INCLUDED. For a neutral atom this is short-ranged: it
    /// decays exponentially rather than as 1/r, because the electrons screen
    /// the nucleus completely. That is the single fact that makes the
    /// electrostatics of a periodic solid summable in real space.
    std::vector<double> neutralAtomPotential;

    /// Total (2l+1)-resolved basis functions on one atom of this species.
    int functionCount() const;
    /// How many tiers this basis carries.
    std::size_t tierCount() const { return tierOffsets.size(); }
    /// Radius beyond which the free-atom density and the neutral-atom
    /// potential are below the engine's own noise floor and are treated as
    /// exactly zero.
    ///
    /// Unlike the basis cutoff this is not a physical confinement — the free
    /// atom's tail really does go on forever — it is where continuing to
    /// evaluate it stops buying anything. It matters because it sizes the
    /// LATTICE SUM: the radial mesh runs to 50 bohr, and taking that as the
    /// range would put several thousand periodic images inside every grid
    /// point's loop to add up numbers of order 10⁻³⁰.
    double densityCutoffBohr = 0.0;

    /// The largest cutoff of any function — the interaction range of this atom.
    double maxCutoffBohr() const;
};

/// One basis function of the whole calculation: an atom, a radial function on
/// it, and one of that function's 2l+1 angular components.
struct BasisFunctionIndex {
    std::size_t atom = 0;
    int atomicNumber = 0;
    std::size_t radial = 0; ///< index into the species' `functions`
    int l = 0;
    int m = 0; ///< −l … l
};

/// The basis for a whole calculation: one SpeciesBasis per element present.
class NAOBasisSet {
public:
    NAOBasisSet() = default;
    explicit NAOBasisSet(RadialGrid grid) : grid_(std::move(grid)) {}

    const RadialGrid& grid() const { return grid_; }
    void setGrid(RadialGrid grid) { grid_ = std::move(grid); }

    /// Basis for `atomicNumber`, or null when the species is absent.
    const SpeciesBasis* forSpecies(int atomicNumber) const;

    /// Install a basis for one species. Exposed so a basis can be supplied
    /// from a file or a test — the assembler can then be checked against a
    /// basis whose provenance is known.
    void setSpeciesBasis(SpeciesBasis basis);

    /// Species present, ascending by atomic number.
    std::vector<int> species() const;

    /// Basis functions on one atom of each species, summed over the structure
    /// the caller describes. `counts` maps atomic number to atom count.
    std::size_t totalFunctions(const std::map<int, int>& counts) const;

    /// Generate the basis for each species by solving its free atom in a
    /// sphere of radius `parameters.confinementRadiusA`.
    ///
    /// The minimal basis is the confined atom's own occupied orbitals — every
    /// one of them, core included, because this is an all-electron method and
    /// there is no pseudopotential standing in for the core. `tiers` above 1
    /// is not implemented and is reported as such rather than silently
    /// producing a minimal basis under a name that promises more.
    Outcome generate(const std::vector<Species>& speciesList,
                     const Parameters& parameters, int tiers);

    /// The flat list of basis functions for a set of atoms, in the order the
    /// matrices are indexed: atom-major, then radial function, then m.
    std::vector<BasisFunctionIndex> enumerate(
        const std::vector<int>& atomicNumbers) const;

    /// φ(r) and (−½∇²φ)(r) for every basis function at a displacement from
    /// ITS OWN atom.
    ///
    /// `displacements[i]` is r − R_{atom(i)} in bohr. Functions beyond their
    /// cutoff are exactly zero, which is what the assembler's sparsity rests
    /// on. Both quantities share the spherical harmonic, so they are produced
    /// together — computing them in separate passes would evaluate the
    /// harmonics twice for the same direction.
    void evaluate(const std::vector<BasisFunctionIndex>& functions,
                  const std::vector<std::array<double, 3>>& displacements,
                  std::vector<double>& values,
                  std::vector<double>& kineticValues) const;

    /// The same, plus ∇φ (three components per function, contiguous).
    ///
    /// With φ = R(r)Y_lm = g(r)·S_lm and S the solid harmonic,
    ///
    ///     ∇φ = (R′ − ℓR/r)·r̂·Y_lm + (R/r^ℓ)·∇S_lm ,
    ///
    /// which has no coordinate singularity anywhere: both pieces are finite at
    /// the poles, where the angular gradient of Y_lm alone is not.
    void evaluateWithGradients(
        const std::vector<BasisFunctionIndex>& functions,
        const std::vector<std::array<double, 3>>& displacements,
        std::vector<double>& values, std::vector<double>& kineticValues,
        std::vector<double>& gradients) const;

private:
    RadialGrid grid_;
    std::map<int, SpeciesBasis> byAtomicNumber_;
};

} // namespace calango::dft
