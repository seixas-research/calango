#include "dft/HamiltonianAssembler.hpp"

#include "core/Structure.hpp"

#include <map>

namespace calango::dft {

void SymmetricMatrix::resize(std::size_t n)
{
    dimension = n;
    values.assign(n * n, 0.0);
}

double SymmetricMatrix::at(std::size_t i, std::size_t j) const
{
    if (i >= dimension || j >= dimension)
        return 0.0;
    return values[i * dimension + j];
}

void SymmetricMatrix::set(std::size_t i, std::size_t j, double value)
{
    if (i >= dimension || j >= dimension)
        return;
    values[i * dimension + j] = value;
    values[j * dimension + i] = value; // symmetric by construction, not by hope
}

HamiltonianAssembler::HamiltonianAssembler(const NAOBasisSet& basis,
                                           Parameters parameters)
    : basis_(basis)
    , parameters_(std::move(parameters))
{
}

std::size_t HamiltonianAssembler::dimension(
    const core::Structure& structure) const
{
    std::map<int, int> counts;
    for (const core::Atom& atom : structure.atoms())
        ++counts[atom.atomicNumber];
    // A species with no basis contributes nothing, which would silently give a
    // smaller matrix than the structure needs — so it is a hard zero instead.
    for (const auto& [z, atoms] : counts) {
        (void)atoms;
        if (!basis_.forSpecies(z))
            return 0;
    }
    return basis_.totalFunctions(counts);
}

Outcome HamiltonianAssembler::buildOverlap(const core::Structure& structure,
                                           SymmetricMatrix& overlap) const
{
    if (structure.empty())
        return Outcome::invalid("the structure has no atoms");
    const std::size_t n = dimension(structure);
    if (n == 0)
        return Outcome::invalid(
            "at least one species in the structure has no basis functions");
    // NOT resized. A caller that ignored the status would otherwise be handed
    // an n x n matrix of zeros — a singular overlap, which turns the
    // generalised eigenproblem into a failure three layers away from its
    // cause. Leaving it untouched makes the omission immediate.
    (void)overlap;
    return Outcome::notImplemented(
        "overlap assembly needs the multicentre integration grid: atom-centred "
        "radial x Lebedev spheres partitioned by a smooth nuclear weight so "
        "the overlapping grids sum to unity everywhere.");
}

Outcome HamiltonianAssembler::buildHamiltonian(
    const core::Structure& structure,
    const std::vector<double>& effectivePotential,
    SymmetricMatrix& hamiltonian) const
{
    if (structure.empty())
        return Outcome::invalid("the structure has no atoms");
    if (effectivePotential.empty())
        return Outcome::invalid(
            "no effective potential was supplied on the integration grid");
    (void)hamiltonian;
    return Outcome::notImplemented(
        "Hamiltonian assembly needs the multicentre integration grid and the "
        "basis-function gradients on it. The kinetic term is evaluated in its "
        "symmetric gradient form, not as a Laplacian: differentiating a "
        "tabulated function twice amplifies grid noise twice.");
}

Outcome HamiltonianAssembler::buildDensity(
    const core::Structure& structure,
    const std::vector<double>& orbitalCoefficients,
    const std::vector<double>& occupations, std::vector<double>& density) const
{
    if (structure.empty())
        return Outcome::invalid("the structure has no atoms");
    if (orbitalCoefficients.empty() || occupations.empty())
        return Outcome::invalid("no occupied orbitals were supplied");
    (void)density;
    return Outcome::notImplemented(
        "density evaluation needs the multicentre integration grid.");
}

} // namespace calango::dft
