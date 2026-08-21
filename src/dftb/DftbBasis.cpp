#include "dftb/DftbBasis.hpp"

#include "core/Element.hpp"

namespace calango::dftb {

dft::Outcome DftbBasis::build(const std::vector<int>& atomicNumbers,
                               const SlaterKosterTable& table, DftbBasis& out)
{
    out = DftbBasis{};
    out.atoms.reserve(atomicNumbers.size());
    int offset = 0;
    for (int z : atomicNumbers) {
        if (!table.onsite(z)) {
            return dft::Outcome::invalid(
                std::string("no on-site Slater-Koster data for element ")
                + core::Elements::data(z).symbol
                + " (its own homonuclear .skf did not load)");
        }
        AtomOrbitals ao;
        ao.atomicNumber = z;
        ao.hasP = table.hasPShell(z);
        ao.firstOrbital = offset;
        offset += ao.orbitalCount();
        out.atoms.push_back(ao);
    }
    out.totalOrbitals = offset;
    return dft::Outcome::success();
}

} // namespace calango::dftb
