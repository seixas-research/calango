#include "dftb/DftbPdos.hpp"

#include "core/Element.hpp"
#include "dft/Constants.hpp"
#include "dftb/DftbMulliken.hpp"

#include <algorithm>
#include <cmath>

namespace calango::dftb {

namespace {

double gaussian(double x, double sigma)
{
    if (sigma <= 0.0)
        return 0.0;
    const double a = x / sigma;
    return std::exp(-0.5 * a * a) / (sigma * std::sqrt(2.0 * dft::kPi));
}

} // namespace

dft::Outcome computeDftbPdos(const DftbScfResult& scf,
                              const DftbHamiltonianBuilder& hamiltonian,
                              const DftbBasis& basis,
                              const SlaterKosterTable& table,
                              double broadeningHartree,
                              double binWidthHartree, DftbPdosResult& out)
{
    if (scf.kpoints.empty())
        return dft::Outcome::invalid(
            "no SCF k-point results to project a PDOS from");
    if (broadeningHartree <= 0.0 || binWidthHartree <= 0.0)
        return dft::Outcome::invalid(
            "PDOS broadening and bin width must both be positive");

    out = DftbPdosResult{};
    out.fermiEv = scf.fermiEnergyHartree * dft::kHartreeToEv;
    out.binWidthEv = binWidthHartree * dft::kHartreeToEv;

    double minE = 1.0e300, maxE = -1.0e300;
    for (const auto& kr : scf.kpoints) {
        if (kr.eigenvaluesHartree.empty())
            continue;
        minE = std::min(minE, kr.eigenvaluesHartree.front());
        maxE = std::max(maxE, kr.eigenvaluesHartree.back());
    }
    if (minE > maxE)
        return dft::Outcome::invalid("no eigenvalues found in the SCF result");
    // Pad by a few broadenings so a Gaussian centred at the extreme bands
    // does not get truncated right at its own peak.
    const double pad = 5.0 * broadeningHartree;
    minE -= pad;
    maxE += pad;
    const int nBins = std::max(
        2, static_cast<int>((maxE - minE) / binWidthHartree) + 1);
    out.energiesEv.resize(static_cast<std::size_t>(nBins));
    for (int b = 0; b < nBins; ++b)
        out.energiesEv[static_cast<std::size_t>(b)] =
            (minE + b * binWidthHartree) * dft::kHartreeToEv;

    // Group key per orbital: "<Symbol> s" or "<Symbol> p".
    const auto dimension = static_cast<std::size_t>(hamiltonian.dimension());
    std::vector<std::string> groupKeyPerOrbital(dimension);
    for (const auto& ao : basis.atoms) {
        const std::string symbol = core::Elements::data(ao.atomicNumber).symbol;
        groupKeyPerOrbital[static_cast<std::size_t>(ao.firstOrbital)] =
            symbol + " s";
        for (int p = 0; p < 3 && ao.hasP; ++p)
            groupKeyPerOrbital[static_cast<std::size_t>(ao.firstOrbital + 1 + p)] =
                symbol + " p";
    }
    // Pre-create every group's (zero) array so a shell present on one atom
    // but with zero weight at a given energy still has an entry, not a
    // missing key downstream.
    std::vector<std::string> groupNames;
    for (const auto& key : groupKeyPerOrbital) {
        if (std::find(groupNames.begin(), groupNames.end(), key)
            == groupNames.end())
            groupNames.push_back(key);
    }
    for (const auto& name : groupNames)
        out.projections[name].assign(static_cast<std::size_t>(nBins), 0.0);

    for (const auto& kr : scf.kpoints) {
        std::vector<std::complex<double>> h, s;
        hamiltonian.blochMatrices(kr.fractional, h, s); // shift doesn't affect S
        for (std::size_t i = 0; i < kr.eigenvaluesHartree.size(); ++i) {
            // Per-orbital Mulliken weight of THIS single state (occupation
            // 1 there, 0 elsewhere) — reuses the same population math the
            // SCC charges are built from, per the module's own convention.
            std::vector<double> oneHot(kr.eigenvaluesHartree.size(), 0.0);
            oneHot[i] = 1.0;
            std::vector<double> orbitalWeight(dimension, 0.0);
            accumulateMullikenPopulation(kr.eigenvectors, s,
                                          hamiltonian.dimension(), oneHot,
                                          orbitalWeight);

            std::map<std::string, double> groupWeight;
            for (std::size_t mu = 0; mu < dimension; ++mu)
                groupWeight[groupKeyPerOrbital[mu]] += orbitalWeight[mu];

            const double e = kr.eigenvaluesHartree[i];
            const double w = kr.weight;
            for (int b = 0; b < nBins; ++b) {
                const double eBin = minE + b * binWidthHartree;
                const double g = gaussian(eBin - e, broadeningHartree);
                if (g < 1.0e-12)
                    continue;
                for (const auto& [name, weight] : groupWeight) {
                    if (weight == 0.0)
                        continue;
                    // A DENSITY (states per Hartree) — NOT multiplied by
                    // binWidthHartree here: that factor belongs to whoever
                    // integrates this (Sum_bins value * binWidth = states),
                    // not to the density itself. Multiplying it in here
                    // AND expecting a caller to multiply by bin width again
                    // when integrating double-counts it — exactly the bug
                    // DftbTest.cpp's sum-rule check caught (an integral
                    // short by one extra factor of binWidth).
                    out.projections[name][static_cast<std::size_t>(b)] +=
                        w * weight * g;
                }
            }
        }
    }

    // Values are a DENSITY (states per Hartree); convert to states per eV to
    // match every other engine's pdos.json convention (energies_eV, so the
    // density itself is expected in the same unit's reciprocal).
    for (auto& [name, values] : out.projections)
        for (double& v : values)
            v /= dft::kHartreeToEv;

    (void)table; // reserved: a future per-shell l-resolved (not just s/p
                 // pooled) breakdown would need the on-site data this
                 // carries; kept in the signature so that extension does
                 // not need to touch every call site again.
    return dft::Outcome::success();
}

} // namespace calango::dftb
