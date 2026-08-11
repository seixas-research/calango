#pragma once

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
    /// function that is not an atomic solution (a polarisation or hydrogenic
    /// addition).
    double eigenvalue = 0.0;
    /// "2p", "3d", "H(2p,2.1)" — for logs and for the basis-set report.
    std::string label;

    /// (2l+1) real spherical harmonics per radial function.
    int functionCount() const { return 2 * l + 1; }
};

/// Every basis function of one chemical species.
///
/// The tiers are what a numerical atomic basis buys over a fixed analytic one:
/// tier 0 is the free atom's own occupied orbitals, which makes the isolated
/// atom EXACT at the smallest possible size, and each further tier is a set of
/// functions selected for the energy they recover in a bonded reference rather
/// than for their analytic form. That is why the convergence is monotone and
/// why "one tier up" is a meaningful instruction.
struct SpeciesBasis {
    Species species;
    /// Index into `functions` where each tier begins; tierOffsets[0] == 0.
    std::vector<std::size_t> tierOffsets;
    std::vector<RadialFunction> functions;

    /// Total (2l+1)-resolved basis functions on one atom of this species.
    int functionCount() const;
    /// How many tiers this basis carries.
    std::size_t tierCount() const { return tierOffsets.size(); }
};

/// The basis for a whole calculation: one SpeciesBasis per element present.
///
/// STATUS: the data model and its accessors are implemented; generation is
/// not. `generate()` reports NotImplemented rather than returning an empty
/// basis that would look like a valid minimal one and produce a plausible,
/// wrong energy.
///
/// What generation involves, so the interface is not mistaken for the work:
/// solving the radial all-electron problem for the free atom self-consistently
/// (that is a Schrödinger or scalar-relativistic Dirac solve per l channel,
/// with the nuclear Coulomb term and the atom's own Hartree and XC potentials
/// iterated to consistency), then imposing a confining potential so each
/// solution reaches zero at the cutoff radius instead of decaying forever.
class NAOBasisSet {
public:
    NAOBasisSet() = default;
    explicit NAOBasisSet(RadialGrid grid) : grid_(std::move(grid)) {}

    const RadialGrid& grid() const { return grid_; }
    void setGrid(RadialGrid grid) { grid_ = std::move(grid); }

    /// Basis for `atomicNumber`, or null when the species is absent.
    const SpeciesBasis* forSpecies(int atomicNumber) const;

    /// Install a basis for one species. Exposed so a basis can be supplied
    /// from a file or a test while generation is unimplemented — the assembler
    /// and the SCF loop can then be developed and checked against a basis
    /// whose provenance is known, instead of waiting on the generator.
    void setSpeciesBasis(SpeciesBasis basis);

    /// Species present, ascending by atomic number.
    std::vector<int> species() const;

    /// Basis functions on one atom of each species, summed over the structure
    /// the caller describes. `counts` maps atomic number to atom count.
    std::size_t totalFunctions(const std::map<int, int>& counts) const;

    /// Generate the basis for `species` at the requested tier depth.
    ///
    /// Not implemented — see the class comment for what it entails.
    Outcome generate(const std::vector<Species>& species,
                     const Parameters& parameters, int tiers);

private:
    RadialGrid grid_;
    std::map<int, SpeciesBasis> byAtomicNumber_;
};

} // namespace calango::dft
