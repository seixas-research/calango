#include "dft/NAOBasisSet.hpp"

#include <algorithm>

namespace calango::dft {

int SpeciesBasis::functionCount() const
{
    int count = 0;
    for (const RadialFunction& function : functions)
        count += function.functionCount();
    return count;
}

const SpeciesBasis* NAOBasisSet::forSpecies(int atomicNumber) const
{
    const auto it = byAtomicNumber_.find(atomicNumber);
    return it == byAtomicNumber_.end() ? nullptr : &it->second;
}

void NAOBasisSet::setSpeciesBasis(SpeciesBasis basis)
{
    const int z = basis.species.atomicNumber;
    byAtomicNumber_[z] = std::move(basis);
}

std::vector<int> NAOBasisSet::species() const
{
    std::vector<int> result;
    result.reserve(byAtomicNumber_.size());
    for (const auto& [z, basis] : byAtomicNumber_)
        result.push_back(z);
    return result; // std::map iterates in key order
}

std::size_t NAOBasisSet::totalFunctions(const std::map<int, int>& counts) const
{
    std::size_t total = 0;
    for (const auto& [z, atoms] : counts) {
        const SpeciesBasis* basis = forSpecies(z);
        if (!basis || atoms <= 0)
            continue;
        total += static_cast<std::size_t>(basis->functionCount())
            * static_cast<std::size_t>(atoms);
    }
    return total;
}

Outcome NAOBasisSet::generate(const std::vector<Species>& species,
                              const Parameters& parameters, int tiers)
{
    if (species.empty())
        return Outcome::invalid("no species were given to generate a basis for");
    if (tiers < 0)
        return Outcome::invalid("tier depth cannot be negative");
    if (!(parameters.confinementRadiusA > 0.0))
        return Outcome::invalid("the confinement radius must be positive");
    for (const Species& s : species) {
        if (s.atomicNumber < 1 || s.atomicNumber > 118)
            return Outcome::invalid("atomic number out of range");
    }
    if (grid_.empty())
        return Outcome::invalid("no radial grid has been set");

    // Validated, then refused. The checks above are not decoration: they are
    // the contract a future implementation will rely on, and having them run
    // now means the interface is exercised by callers and by tests before the
    // physics lands underneath it.
    return Outcome::notImplemented(
        "numerical atomic-orbital generation is not implemented yet. It needs "
        "a self-consistent all-electron solve of the free atom on the radial "
        "grid (one radial equation per angular-momentum channel, with the "
        "atom's own Hartree and exchange-correlation potentials iterated to "
        "consistency), followed by confinement of each solution to the cutoff "
        "radius. Supply a basis with setSpeciesBasis() in the meantime.");
}

} // namespace calango::dft
