#include "core/BoltzmannTransport.hpp"
#include "core/PhysicalConstants.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace calango::core {

namespace {

/// −∂f/∂ε for the Fermi function, in eV⁻¹.
///
/// Written as sech² rather than as f(1−f)/kT: the latter is the difference of
/// two nearly-equal numbers far from μ and loses every significant digit
/// there, which is exactly where the tails of the transport integrals live.
double minusDfDe(double energy, double mu, double kT)
{
    if (kT <= 0.0)
        return 0.0;
    const double x = (energy - mu) / (2.0 * kT);
    if (std::abs(x) > 40.0)
        return 0.0;
    const double sech = 1.0 / std::cosh(x);
    return sech * sech / (4.0 * kT);
}

double fermi(double energy, double mu, double kT)
{
    if (kT <= 0.0)
        return energy <= mu ? 1.0 : 0.0;
    const double x = (energy - mu) / kT;
    if (x > 40.0)
        return 0.0;
    if (x < -40.0)
        return 1.0;
    return 1.0 / (1.0 + std::exp(x));
}

/// 3x3 inverse of a row-major matrix; falls back to a pseudo-inverse-ish
/// diagonal guard when the tensor is singular (a 1D or 2D system has zero
/// conductivity along the confined axis, and S is then undefined there rather
/// than infinite).
std::array<double, 9> invert3(const std::array<double, 9>& m, bool* ok)
{
    const double det = m[0] * (m[4] * m[8] - m[5] * m[7])
        - m[1] * (m[3] * m[8] - m[5] * m[6])
        + m[2] * (m[3] * m[7] - m[4] * m[6]);
    std::array<double, 9> out{};
    if (std::abs(det) < 1e-300) {
        if (ok)
            *ok = false;
        return out;
    }
    if (ok)
        *ok = true;
    const double inv = 1.0 / det;
    out[0] = (m[4] * m[8] - m[5] * m[7]) * inv;
    out[1] = (m[2] * m[7] - m[1] * m[8]) * inv;
    out[2] = (m[1] * m[5] - m[2] * m[4]) * inv;
    out[3] = (m[5] * m[6] - m[3] * m[8]) * inv;
    out[4] = (m[0] * m[8] - m[2] * m[6]) * inv;
    out[5] = (m[2] * m[3] - m[0] * m[5]) * inv;
    out[6] = (m[3] * m[7] - m[4] * m[6]) * inv;
    out[7] = (m[1] * m[6] - m[0] * m[7]) * inv;
    out[8] = (m[0] * m[4] - m[1] * m[3]) * inv;
    return out;
}

std::array<double, 9> matmul3(const std::array<double, 9>& a,
                              const std::array<double, 9>& b)
{
    std::array<double, 9> out{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k)
                sum += a[static_cast<std::size_t>(i * 3 + k)]
                    * b[static_cast<std::size_t>(k * 3 + j)];
            out[static_cast<std::size_t>(i * 3 + j)] = sum;
        }
    return out;
}

double traceAverage(const std::array<double, 9>& m)
{
    return (m[0] + m[4] + m[8]) / 3.0;
}

} // namespace

BoltzmannTransport::BoltzmannTransport(WannierHamiltonian hamiltonian,
                                       Options options)
    : hamiltonian_(std::move(hamiltonian)), options_(options)
{
    if (options_.energyBins < 8)
        throw std::invalid_argument("BoltzmannTransport: too few energy bins");
    if (!(options_.energyMax > options_.energyMin))
        throw std::invalid_argument("BoltzmannTransport: empty energy window");
    if (options_.smearing <= 0.0)
        throw std::invalid_argument("BoltzmannTransport: smearing must be > 0");
    if (options_.relaxationTime <= 0.0)
        throw std::invalid_argument(
            "BoltzmannTransport: relaxation time must be > 0");
}

BoltzmannTransport::BoltzmannTransport(WannierHamiltonian hamiltonian)
    : BoltzmannTransport(std::move(hamiltonian), Options{})
{
}

void BoltzmannTransport::build() const
{
    if (built_)
        return;

    const int bins = options_.energyBins;
    const double dE = (options_.energyMax - options_.energyMin) / (bins - 1);
    spectral_.energies.resize(static_cast<std::size_t>(bins));
    spectral_.sigma.assign(static_cast<std::size_t>(bins), {});
    spectral_.dos.assign(static_cast<std::size_t>(bins), 0.0);
    for (int i = 0; i < bins; ++i)
        spectral_.energies[static_cast<std::size_t>(i)] =
            options_.energyMin + i * dE;

    const auto points = WannierHamiltonian::monkhorstPack(options_.kmesh);
    const double norm = 1.0 / static_cast<double>(points.size());
    const double sigmaSmear = options_.smearing;
    const double gaussNorm = 1.0 / (sigmaSmear * std::sqrt(2.0 * kPi));
    // Beyond five sigma the Gaussian contributes below 1e-6 of its peak; the
    // cutoff turns the inner loop from O(bins) into O(1) per state and changes
    // nothing that survives to the reported digits.
    const int halfWidth =
        std::max(1, static_cast<int>(std::ceil(5.0 * sigmaSmear / dE)));

    // Level spacing near the window centre, for the mesh-density diagnostic.
    std::vector<double> centreEnergies;

    for (const auto& k : points) {
        const auto bands = hamiltonian_.bands(k, /*withVelocities=*/true);
        for (std::size_t n = 0; n < bands.energies.size(); ++n) {
            const double e = bands.energies[n];
            if (e < options_.energyMin - 6.0 * sigmaSmear
                || e > options_.energyMax + 6.0 * sigmaSmear)
                continue;
            if (std::abs(e - 0.5 * (options_.energyMin + options_.energyMax))
                < 0.1)
                centreEnergies.push_back(e);

            // v in m/s from ∂ε/∂k in eV·Å.
            std::array<double, 3> v{};
            for (int x = 0; x < 3; ++x)
                v[static_cast<std::size_t>(x)] =
                    bands.gradients[n][static_cast<std::size_t>(x)]
                    * WannierHamiltonian::kVelocitySI;

            const int centre =
                static_cast<int>(std::lround((e - options_.energyMin) / dE));
            const int lo = std::max(0, centre - halfWidth);
            const int hi = std::min(bins - 1, centre + halfWidth);
            for (int bin = lo; bin <= hi; ++bin) {
                const double delta = spectral_.energies[static_cast<std::size_t>(bin)] - e;
                const double weight =
                    gaussNorm * std::exp(-0.5 * delta * delta
                                         / (sigmaSmear * sigmaSmear))
                    * norm;
                spectral_.dos[static_cast<std::size_t>(bin)] +=
                    weight * options_.spinDegeneracy;
                auto& tensor = spectral_.sigma[static_cast<std::size_t>(bin)];
                for (int a = 0; a < 3; ++a)
                    for (int b = 0; b < 3; ++b)
                        tensor[static_cast<std::size_t>(a * 3 + b)] +=
                            weight * v[static_cast<std::size_t>(a)]
                            * v[static_cast<std::size_t>(b)];
            }
        }
    }

    // Σ(ε) so far is (1/N_k) Σ_nk v v δ(ε−ε_nk) with δ per unit cell. Divide by
    // the cell volume (Å³ → m³) and multiply by τ and the spin degeneracy to
    // reach SI. The elementary charges are applied in evaluate(), where the
    // Onsager combinations decide how many of them each quantity carries.
    const double volumeSI = hamiltonian_.volume() * 1e-30; // Å³ → m³
    const double prefactor = options_.relaxationTime
        * static_cast<double>(options_.spinDegeneracy) / volumeSI;
    for (auto& tensor : spectral_.sigma)
        for (double& value : tensor)
            value *= prefactor;

    std::sort(centreEnergies.begin(), centreEnergies.end());
    double spacing = 0.0;
    for (std::size_t i = 1; i < centreEnergies.size(); ++i)
        spacing += centreEnergies[i] - centreEnergies[i - 1];
    spectral_.meanLevelSpacing =
        centreEnergies.size() > 1
        ? spacing / static_cast<double>(centreEnergies.size() - 1)
        : 0.0;

    // Reference filling: electrons below the middle of the window at 300 K.
    // Carrier concentration is reported against this, so doping has a sign
    // that means something.
    referenceElectrons_ =
        electronCount(300.0, 0.5 * (options_.energyMin + options_.energyMax));
    built_ = true;
}

const BoltzmannTransport::SpectralConductivity&
BoltzmannTransport::spectralConductivity() const
{
    build();
    return spectral_;
}

double BoltzmannTransport::electronCount(double temperature,
                                         double chemicalPotential) const
{
    const double kT = kBoltzmann_eV * temperature;
    const auto points = WannierHamiltonian::monkhorstPack(options_.kmesh);
    double total = 0.0;
    for (const auto& k : points) {
        const auto bands = hamiltonian_.bands(k, /*withVelocities=*/false);
        for (double e : bands.energies)
            total += fermi(e, chemicalPotential, kT);
    }
    return total * options_.spinDegeneracy / static_cast<double>(points.size());
}

BoltzmannTransport::Point BoltzmannTransport::evaluate(
    double temperature, double chemicalPotential) const
{
    build();

    Point out;
    out.temperature = temperature;
    out.chemicalPotential = chemicalPotential;
    if (temperature <= 0.0)
        return out;

    const double kT = kBoltzmann_eV * temperature;
    const double dE = spectral_.energies[1] - spectral_.energies[0];

    // The three Onsager moments, in the units Σ(ε) carries times eV^m.
    std::array<double, 9> l0{};
    std::array<double, 9> l1{};
    std::array<double, 9> l2{};
    for (std::size_t bin = 0; bin < spectral_.energies.size(); ++bin) {
        const double e = spectral_.energies[bin];
        const double w = minusDfDe(e, chemicalPotential, kT) * dE;
        if (w <= 0.0)
            continue;
        const double d = e - chemicalPotential;
        const auto& s = spectral_.sigma[bin];
        for (int i = 0; i < 9; ++i) {
            l0[static_cast<std::size_t>(i)] += w * s[static_cast<std::size_t>(i)];
            l1[static_cast<std::size_t>(i)] +=
                w * d * s[static_cast<std::size_t>(i)];
            l2[static_cast<std::size_t>(i)] +=
                w * d * d * s[static_cast<std::size_t>(i)];
        }
    }

    // σ = e² L⁰. Σ(ε) already carries τ/V and the velocities in SI, and the
    // energy integral is in eV, so one factor of e converts eV → J and the
    // second is the charge itself.
    for (int i = 0; i < 9; ++i)
        out.sigma[static_cast<std::size_t>(i)] =
            kElementaryCharge * l0[static_cast<std::size_t>(i)];

    bool invertible = false;
    const auto l0inv = invert3(l0, &invertible);

    if (invertible) {
        // S = (1/(eT)) (L⁰)⁻¹ L¹ — with L in eV the explicit e cancels, so
        // the numerical factor is 1/T and the result is V/K.
        const auto sTensor = matmul3(l0inv, l1);
        for (int i = 0; i < 9; ++i)
            out.seebeck[static_cast<std::size_t>(i)] =
                sTensor[static_cast<std::size_t>(i)] / temperature;

        // κ_e = (1/(e²T))[L² − L¹(L⁰)⁻¹L¹], the ZERO-CURRENT thermal
        // conductivity. Subtracting the second term is what distinguishes it
        // from the zero-field value; leaving it out inflates κ_e and deflates
        // zT.
        const auto correction = matmul3(matmul3(l1, l0inv), l1);
        for (int i = 0; i < 9; ++i)
            out.kappaElectronic[static_cast<std::size_t>(i)] =
                kElementaryCharge
                * (l2[static_cast<std::size_t>(i)]
                   - correction[static_cast<std::size_t>(i)])
                / temperature;
    }

    for (int i = 0; i < 9; ++i) {
        const double s = out.seebeck[static_cast<std::size_t>(i)];
        out.powerFactor[static_cast<std::size_t>(i)] =
            s * s * out.sigma[static_cast<std::size_t>(i)];
        const double denominator =
            out.kappaElectronic[static_cast<std::size_t>(i)]
            + options_.latticeThermalConductivity;
        out.zT[static_cast<std::size_t>(i)] =
            denominator > 0.0
            ? out.powerFactor[static_cast<std::size_t>(i)] * temperature
                / denominator
            : 0.0;
    }

    out.sigmaAvg = traceAverage(out.sigma);
    out.seebeckAvg = traceAverage(out.seebeck);
    out.kappaAvg = traceAverage(out.kappaElectronic);
    out.powerFactorAvg = out.seebeckAvg * out.seebeckAvg * out.sigmaAvg;
    out.zTAvg = (out.kappaAvg + options_.latticeThermalConductivity) > 0.0
        ? out.powerFactorAvg * temperature
            / (out.kappaAvg + options_.latticeThermalConductivity)
        : 0.0;
    out.lorenzRatio = (out.sigmaAvg > 0.0)
        ? out.kappaAvg / (out.sigmaAvg * temperature)
        : 0.0;

    const double electrons = electronCount(temperature, chemicalPotential);
    const double volumeCm3 = hamiltonian_.volume() * 1e-24; // Å³ → cm³
    out.carrierConcentration =
        (electrons - referenceElectrons_) / std::max(volumeCm3, 1e-300);
    return out;
}

} // namespace calango::core
