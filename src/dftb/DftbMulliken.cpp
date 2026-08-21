#include "dftb/DftbMulliken.hpp"

namespace calango::dftb {

void accumulateMullikenPopulation(
    const std::vector<std::complex<double>>& eigenvectors,
    const std::vector<std::complex<double>>& overlapMatrix, int dimension,
    const std::vector<double>& occupation, std::vector<double>& population)
{
    const auto n = static_cast<std::size_t>(dimension);
    // n_mu = sum_i f_i * Re[ conj(C_mu,i) * (S C)_mu,i ] — the standard
    // Mulliken definition (see the header's derivation note): for each
    // state, v = S * (that eigenvector column), then each orbital's share
    // is Re[conj(c_mu) * v_mu].
    std::vector<std::complex<double>> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (occupation[i] == 0.0)
            continue;
        for (std::size_t mu = 0; mu < n; ++mu) {
            std::complex<double> sum(0.0, 0.0);
            for (std::size_t nu = 0; nu < n; ++nu)
                sum += overlapMatrix[mu * n + nu] * eigenvectors[nu * n + i];
            v[mu] = sum;
        }
        for (std::size_t mu = 0; mu < n; ++mu)
            population[mu] +=
                occupation[i] * (std::conj(eigenvectors[mu * n + i]) * v[mu]).real();
    }
}

std::vector<double> mullikenChargeFluctuation(
    const std::vector<double>& orbitalPopulation, const DftbBasis& basis,
    const SlaterKosterTable& table)
{
    std::vector<double> deltaQ(basis.atoms.size(), 0.0);
    for (std::size_t a = 0; a < basis.atoms.size(); ++a) {
        const AtomOrbitals& ao = basis.atoms[a];
        double q = 0.0;
        for (int o = 0; o < ao.orbitalCount(); ++o)
            q += orbitalPopulation[static_cast<std::size_t>(ao.firstOrbital + o)];
        const auto* shells = table.onsite(ao.atomicNumber);
        const double reference =
            shells ? (*shells)[0].occupation + (*shells)[1].occupation : 0.0;
        deltaQ[a] = q - reference;
    }
    return deltaQ;
}

double totalValenceElectrons(const DftbBasis& basis,
                              const SlaterKosterTable& table)
{
    double total = 0.0;
    for (const auto& ao : basis.atoms) {
        const auto* shells = table.onsite(ao.atomicNumber);
        if (shells)
            total += (*shells)[0].occupation + (*shells)[1].occupation;
    }
    return total;
}

} // namespace calango::dftb
