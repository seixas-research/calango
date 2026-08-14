#include "core/CpaSolver.hpp"
#include "core/PhysicalConstants.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace calango::core {

CpaSolver::Lattice CpaSolver::Lattice::semicircular(double halfBandwidth,
                                                    int samples)
{
    // Sampled in θ with ε = W cos θ rather than uniformly in ε.
    //
    // The semi-elliptic band meets the real axis with a square-root edge, so a
    // uniform-in-ε rule converges as O(Δε^{3/2}) and leaves a visible error in
    // the band tails — exactly where the split-band tests look. Under the
    // substitution the weight becomes (2/π)sin²θ dθ, which is smooth and
    // periodic, so the midpoint rule is spectrally accurate instead.
    Lattice lattice;
    const int n = std::max(3, samples);
    lattice.energies.reserve(n);
    lattice.weights.reserve(n);
    const double dTheta = kPi / n;
    for (int j = 0; j < n; ++j) {
        const double theta = (j + 0.5) * dTheta;
        lattice.energies.push_back(halfBandwidth * std::cos(theta));
        lattice.weights.push_back(2.0 / kPi * std::sin(theta) * std::sin(theta)
                                  * dTheta);
    }
    return lattice;
}

CpaSolver::Lattice CpaSolver::Lattice::rectangular(double halfWidth,
                                                   int samples)
{
    Lattice lattice;
    const int n = std::max(3, samples);
    lattice.energies.reserve(n);
    lattice.weights.reserve(n);
    const double dE = 2.0 * halfWidth / n;
    for (int j = 0; j < n; ++j) {
        lattice.energies.push_back(-halfWidth + (j + 0.5) * dE);
        lattice.weights.push_back(1.0 / n);
    }
    return lattice;
}

CpaSolver::CpaSolver(std::vector<Component> components, Lattice lattice)
    : CpaSolver(std::move(components), std::move(lattice), Options{})
{
}

CpaSolver::CpaSolver(std::vector<Component> components, Lattice lattice,
                     Options options)
    : components_(std::move(components))
    , lattice_(std::move(lattice))
    , options_(options)
{
    if (components_.empty())
        throw std::invalid_argument("CpaSolver: no components");
    if (lattice_.energies.size() != lattice_.weights.size()
        || lattice_.energies.empty())
        throw std::invalid_argument("CpaSolver: malformed lattice");

    double total = 0.0;
    for (const auto& c : components_) {
        if (c.concentration < 0.0)
            throw std::invalid_argument("CpaSolver: negative concentration");
        total += c.concentration;
    }
    if (std::abs(total - 1.0) > 1e-6)
        throw std::invalid_argument("CpaSolver: concentrations must sum to 1");

    // Normalised here rather than trusted: every sum rule the tests check is
    // stated per site, and a quadrature that integrates to 0.999 would move
    // the band filling by that much with nothing reporting it.
    const double wsum =
        std::accumulate(lattice_.weights.begin(), lattice_.weights.end(), 0.0);
    if (wsum <= 0.0)
        throw std::invalid_argument("CpaSolver: lattice weights sum to zero");
    for (double& w : lattice_.weights)
        w /= wsum;
}

bool CpaSolver::magnetic() const
{
    return std::any_of(components_.begin(), components_.end(),
                       [](const Component& c) {
                           return std::abs(c.exchangeSplitting) > 0.0;
                       });
}

double CpaSolver::onsite(const Component& c, int spin) const
{
    // ε_{i↑} = ε_i − Δ/2, ε_{i↓} = ε_i + Δ/2.
    return c.onsiteEnergy - 0.5 * c.exchangeSplitting * (spin >= 0 ? 1.0 : -1.0);
}

std::complex<double> CpaSolver::latticeGreen(std::complex<double> zeta) const
{
    std::complex<double> g{0.0, 0.0};
    for (std::size_t j = 0; j < lattice_.energies.size(); ++j)
        g += lattice_.weights[j] / (zeta - lattice_.energies[j]);
    return g;
}

CpaSolver::Solution CpaSolver::solve(double energy, int spin) const
{
    const std::complex<double> z(energy, options_.broadening);

    // Started from the virtual-crystal average. It is the exact answer in the
    // two limits that bracket the problem (one component, or equal levels), so
    // the iteration begins converged wherever it can be.
    std::complex<double> sigma{0.0, 0.0};
    for (const auto& c : components_)
        sigma += c.concentration * onsite(c, spin);

    Solution out;
    out.componentGreen.assign(components_.size(), {0.0, 0.0});

    // One component is not an alloy: Σ = ε exactly, with no scattering to
    // iterate on. Short-circuited rather than left to converge, because it is
    // the reference case every test compares against and it should be exact,
    // not tolerance-limited — and because it is the inner loop of the
    // Hilbert-transform check.
    if (components_.size() == 1) {
        out.selfEnergy = sigma;
        out.greenFunction = latticeGreen(z - sigma);
        out.componentGreen[0] = out.greenFunction;
        out.iterations = 0;
        out.converged = true;
        out.residual = 0.0;
        return out;
    }

    for (int iter = 1; iter <= options_.maxIterations; ++iter) {
        const std::complex<double> g = latticeGreen(z - sigma);

        std::complex<double> tAverage{0.0, 0.0};
        std::complex<double> gAverage{0.0, 0.0};
        for (std::size_t i = 0; i < components_.size(); ++i) {
            const std::complex<double> delta = onsite(components_[i], spin) - sigma;
            const std::complex<double> denom = 1.0 - delta * g;
            // A vanishing denominator is a genuine pole of the single-site
            // problem, not a numerical accident; nudging it is the only way to
            // keep the iteration finite, and η is the scale it is nudged by.
            const std::complex<double> safe =
                (std::abs(denom) < 1e-14) ? std::complex<double>(1e-14, 0.0)
                                          : denom;
            const std::complex<double> t = delta / safe;
            const std::complex<double> gi = g / safe;
            out.componentGreen[i] = gi;
            tAverage += components_[i].concentration * t;
            gAverage += components_[i].concentration * gi;
        }

        // Σ_new = Σ + <T>/(1 + <T>G). Exact fixed point when <T> = 0, which is
        // the CPA condition itself; the denominator is what makes it Newton-
        // like rather than plain iteration, and is why weak scattering
        // converges in a handful of steps.
        const std::complex<double> update =
            tAverage / (1.0 + tAverage * g);
        const std::complex<double> sigmaNew =
            sigma + options_.mixing * update;

        out.residual = std::abs(gAverage - g);
        out.iterations = iter;
        out.selfEnergy = sigmaNew;
        out.greenFunction = g;

        if (std::abs(sigmaNew - sigma) < options_.tolerance) {
            sigma = sigmaNew;
            out.converged = true;
            break;
        }
        sigma = sigmaNew;
    }

    // Recomputed at the converged Σ so that G, G_i and Σ in the returned
    // solution are mutually consistent rather than one iteration apart.
    const std::complex<double> g = latticeGreen(z - sigma);
    std::complex<double> gAverage{0.0, 0.0};
    for (std::size_t i = 0; i < components_.size(); ++i) {
        const std::complex<double> delta = onsite(components_[i], spin) - sigma;
        const std::complex<double> denom = 1.0 - delta * g;
        const std::complex<double> safe =
            (std::abs(denom) < 1e-14) ? std::complex<double>(1e-14, 0.0) : denom;
        out.componentGreen[i] = g / safe;
        gAverage += components_[i].concentration * out.componentGreen[i];
    }
    out.selfEnergy = sigma;
    out.greenFunction = g;
    out.residual = std::abs(gAverage - g);
    return out;
}

std::vector<std::array<double, 2>>
CpaSolver::partialDosBySpin(double energy) const
{
    std::vector<std::array<double, 2>> out(components_.size(), {0.0, 0.0});

    const Solution up = solve(energy, +1);
    for (std::size_t i = 0; i < components_.size(); ++i)
        out[i][0] =
            -components_[i].concentration * up.componentGreen[i].imag() / kPi;

    if (!magnetic()) {
        // Solved once and copied. The two channels are identical by
        // construction here, so solving the second would cost the same again
        // and add a way for them to disagree.
        for (std::size_t i = 0; i < components_.size(); ++i)
            out[i][1] = out[i][0];
        return out;
    }

    const Solution down = solve(energy, -1);
    for (std::size_t i = 0; i < components_.size(); ++i)
        out[i][1] =
            -components_[i].concentration * down.componentGreen[i].imag() / kPi;
    return out;
}

std::vector<double> CpaSolver::partialDos(double energy) const
{
    const auto bySpin = partialDosBySpin(energy);
    std::vector<double> out(components_.size(), 0.0);
    for (std::size_t i = 0; i < components_.size(); ++i)
        out[i] = bySpin[i][0] + bySpin[i][1];
    return out;
}

double CpaSolver::totalDos(double energy) const
{
    const auto partial = partialDos(energy);
    return std::accumulate(partial.begin(), partial.end(), 0.0);
}

double CpaSolver::blochSpectralFunction(double bandEnergy, double energy,
                                        int spin) const
{
    const Solution sol = solve(energy, spin);
    const std::complex<double> z(energy, options_.broadening);
    const std::complex<double> denom = z - bandEnergy - sol.selfEnergy;
    // A = −Im[1/denom]/π, and Im[1/d] = −Im(d)/|d|², so the two signs cancel
    // and A = Im(denom)/(π|denom|²). Positive because Im(denom) = η − Im Σ and
    // Im Σ ≤ 0 for a retarded self-energy — which the tests assert rather than
    // assume, since a sign error here produces a negative spectral function.
    return denom.imag() / (kPi * std::norm(denom));
}

CpaSolver::DosGrid CpaSolver::computeDosGrid(double lowerBound,
                                             double upperBound,
                                             int samples) const
{
    DosGrid grid;
    const int n = std::max(3, (samples % 2 == 0) ? samples + 1 : samples);
    const double h = (upperBound - lowerBound) / (n - 1);
    grid.energies.reserve(n);
    grid.total.reserve(n);
    grid.partial.reserve(n);

    for (int j = 0; j < n; ++j) {
        const double e = lowerBound + j * h;
        auto bySpin = partialDosBySpin(e);
        double total = 0.0;
        for (const auto& perComponent : bySpin)
            total += perComponent[0] + perComponent[1];
        grid.energies.push_back(e);
        grid.total.push_back(total);
        grid.partial.push_back(std::move(bySpin));
    }

    // Trapezoidal cumulative rather than Simpson: it is the one rule whose
    // partial sums are themselves a valid integral up to an arbitrary upper
    // limit, which is exactly what inverting for E_F needs. Simpson's pairing
    // of intervals has no meaningful value at odd sample counts.
    grid.cumulative.assign(n, 0.0);
    for (int j = 1; j < n; ++j)
        grid.cumulative[j] = grid.cumulative[j - 1]
            + 0.5 * h * (grid.total[j - 1] + grid.total[j]);
    return grid;
}

double CpaSolver::integratedDos(const DosGrid& grid, double fermiEnergy)
{
    if (grid.energies.empty())
        return 0.0;
    if (fermiEnergy <= grid.energies.front())
        return 0.0;
    if (fermiEnergy >= grid.energies.back())
        return grid.cumulative.back();
    const double h = grid.energies[1] - grid.energies[0];
    const double x = (fermiEnergy - grid.energies.front()) / h;
    const auto lo = static_cast<std::size_t>(x);
    const double frac = x - static_cast<double>(lo);
    if (lo + 1 >= grid.cumulative.size())
        return grid.cumulative.back();
    return grid.cumulative[lo]
        + frac * (grid.cumulative[lo + 1] - grid.cumulative[lo]);
}

double CpaSolver::findFermiEnergy(const DosGrid& grid, double electrons)
{
    if (grid.energies.empty())
        return 0.0;
    if (electrons <= 0.0)
        return grid.energies.front();
    if (electrons >= grid.cumulative.back())
        return grid.energies.back();
    // The cumulative integral is non-decreasing, so a single scan locates the
    // bracket and one linear step lands inside it.
    for (std::size_t j = 1; j < grid.cumulative.size(); ++j) {
        if (grid.cumulative[j] < electrons)
            continue;
        const double span = grid.cumulative[j] - grid.cumulative[j - 1];
        const double frac =
            (span > 0.0) ? (electrons - grid.cumulative[j - 1]) / span : 0.0;
        return grid.energies[j - 1]
            + frac * (grid.energies[j] - grid.energies[j - 1]);
    }
    return grid.energies.back();
}

std::vector<double> CpaSolver::componentMoments(const DosGrid& grid,
                                                double fermiEnergy) const
{
    std::vector<double> moments(components_.size(), 0.0);
    if (grid.energies.size() < 2)
        return moments;
    const double h = grid.energies[1] - grid.energies[0];

    for (std::size_t j = 1; j < grid.energies.size(); ++j) {
        if (grid.energies[j] > fermiEnergy)
            break;
        for (std::size_t i = 0; i < components_.size(); ++i) {
            const double a = grid.partial[j - 1][i][0] - grid.partial[j - 1][i][1];
            const double b = grid.partial[j][i][0] - grid.partial[j][i][1];
            moments[i] += 0.5 * h * (a + b);
        }
    }

    // Reported per atom of that species: the spin-resolved DOS above carries a
    // factor c_i, and a moment quoted per alloy site rather than per atom is
    // the classic way to under-report a dilute magnetic component by 1/c.
    for (std::size_t i = 0; i < components_.size(); ++i) {
        const double c = components_[i].concentration;
        moments[i] = (c > 0.0) ? moments[i] / c : 0.0;
    }
    return moments;
}

} // namespace calango::core
