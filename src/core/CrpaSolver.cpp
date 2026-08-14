#include "core/CrpaSolver.hpp"
#include "core/PhysicalConstants.hpp"

#include "core/WannierHamiltonian.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace calango::core {

namespace {

/// e²/(4πε₀) in eV·Å — the one constant that fixes the absolute scale of U.
constexpr double kCoulombEvAngstrom = 14.399645;

using linalg::CMatrix;
using linalg::Cplx;

// identity / multiply / invert / hermitianEigen used to be duplicated here.
// They now live in core::linalg (WannierHamiltonian.hpp), which is the only
// copy and the one the Boltzmann-transport and Berry-phase modules share.
//
// That de-duplication was not cosmetic: the copy here carried a wrong complex
// Jacobi rotation, which returns correct eigenvalues whenever the diagonal
// elements are equal or the off-diagonal is real — true of every matrix these
// tests built — and wrong ones otherwise. Fixing it in one place fixes it
// everywhere, which is the point of having one place.
using linalg::identity;
using linalg::invert;
using linalg::multiply;

double fermiOccupation(double energy, double mu, double smearing)
{
    if (smearing <= 0.0)
        return energy <= mu ? 1.0 : 0.0;
    const double x = (energy - mu) / smearing;
    if (x > 40.0)
        return 0.0;
    if (x < -40.0)
        return 1.0;
    return 1.0 / (1.0 + std::exp(x));
}

} // namespace

CrpaSolver::CrpaSolver(Model model, Options options)
    : model_(std::move(model)), options_(options)
{
    if (model_.orbitals.empty())
        throw std::invalid_argument("CrpaSolver: no Wannier orbitals");
    const std::size_t n = model_.orbitals.size();
    for (const auto& block : model_.hoppings)
        if (block.matrix.size() != n * n)
            throw std::invalid_argument("CrpaSolver: H(R) block is not n x n");
    for (int m : options_.kmesh)
        if (m < 1)
            throw std::invalid_argument("CrpaSolver: k-mesh must be positive");
}

CrpaSolver::CrpaSolver(Model model) : CrpaSolver(std::move(model), Options{}) {}

std::vector<std::size_t> CrpaSolver::correlatedIndices() const
{
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < model_.orbitals.size(); ++i)
        if (model_.orbitals[i].correlated)
            out.push_back(i);
    return out;
}

std::vector<std::vector<double>> CrpaSolver::bareCoulomb() const
{
    const std::size_t n = model_.orbitals.size();
    std::vector<std::vector<double>> v(n, std::vector<double>(n, 0.0));

    for (std::size_t i = 0; i < n; ++i) {
        // wannier90 reports Ω = ⟨r²⟩ − ⟨r⟩² summed over the three Cartesian
        // directions, so the per-dimension Gaussian width is sqrt(Ω/3).
        const double si2 = std::max(model_.orbitals[i].spread, 1e-6) / 3.0;
        for (std::size_t j = 0; j < n; ++j) {
            const double sj2 = std::max(model_.orbitals[j].spread, 1e-6) / 3.0;
            const double width = std::sqrt(2.0 * (si2 + sj2));

            // Minimum image. The long-range lattice sum (and its neutralising
            // background) is NOT performed: it shifts every element of V by a
            // common Madelung-like constant, which largely cancels in U − U'
            // and hence in J, but does move the absolute U. Stated here rather
            // than buried, because it is the second approximation after the
            // Gaussian shape.
            double best = std::numeric_limits<double>::infinity();
            for (int a = -1; a <= 1; ++a)
                for (int b = -1; b <= 1; ++b)
                    for (int c = -1; c <= 1; ++c) {
                        double d2 = 0.0;
                        for (int x = 0; x < 3; ++x) {
                            const double delta =
                                model_.orbitals[i].centre[x]
                                - model_.orbitals[j].centre[x]
                                + a * model_.cell[0][x] + b * model_.cell[1][x]
                                + c * model_.cell[2][x];
                            d2 += delta * delta;
                        }
                        best = std::min(best, d2);
                    }
            const double r = std::sqrt(best);

            // erf(r/width)/r in Hartree·Bohr, rescaled to eV·Å. The r → 0
            // limit is taken analytically rather than by a guard on r, so an
            // on-site element is exact rather than nearly-singular.
            v[i][j] = (r < 1e-8)
                ? kCoulombEvAngstrom * std::sqrt(2.0 / (kPi * (si2 + sj2)))
                : kCoulombEvAngstrom * std::erf(r / width) / r;
        }
    }
    return v;
}

CMatrix CrpaSolver::hamiltonianAt(const std::array<double, 3>& k) const
{
    const std::size_t n = model_.orbitals.size();
    CMatrix h(n, std::vector<Cplx>(n, Cplx{0.0, 0.0}));
    for (const auto& block : model_.hoppings) {
        const double phase = 2.0 * kPi
            * (k[0] * block.lattice[0] + k[1] * block.lattice[1]
               + k[2] * block.lattice[2]);
        const Cplx factor{std::cos(phase), std::sin(phase)};
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j)
                h[i][j] += factor * block.matrix[i * n + j];
    }
    // Hermitised. A wannier90 _hr.dat that lists only half the star of R
    // vectors would otherwise give complex eigenvalues, and the eigensolver
    // would silently return their real parts.
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i; j < n; ++j) {
            const Cplx avg = 0.5 * (h[i][j] + std::conj(h[j][i]));
            h[i][j] = avg;
            h[j][i] = std::conj(avg);
        }
    return h;
}

CrpaSolver::Bands CrpaSolver::diagonalize(const std::array<double, 3>& k) const
{
    Bands bands;
    linalg::hermitianEigen(hamiltonianAt(k), bands.energies, bands.vectors);
    return bands;
}

std::vector<std::array<double, 3>> CrpaSolver::kPoints() const
{
    std::vector<std::array<double, 3>> points;
    points.reserve(static_cast<std::size_t>(options_.kmesh[0])
                   * options_.kmesh[1] * options_.kmesh[2]);
    for (int a = 0; a < options_.kmesh[0]; ++a)
        for (int b = 0; b < options_.kmesh[1]; ++b)
            for (int c = 0; c < options_.kmesh[2]; ++c)
                points.push_back({static_cast<double>(a) / options_.kmesh[0],
                                  static_cast<double>(b) / options_.kmesh[1],
                                  static_cast<double>(c) / options_.kmesh[2]});
    return points;
}

double CrpaSolver::fermiLevel() const
{
    const auto points = kPoints();
    std::vector<double> all;
    for (const auto& k : points) {
        const auto bands = diagonalize(k);
        all.insert(all.end(), bands.energies.begin(), bands.energies.end());
    }
    if (all.empty())
        return 0.0;
    double lo = *std::min_element(all.begin(), all.end()) - 10.0;
    double hi = *std::max_element(all.begin(), all.end()) + 10.0;
    const double target = model_.electrons;
    for (int iter = 0; iter < 200; ++iter) {
        const double mid = 0.5 * (lo + hi);
        double count = 0.0;
        for (double e : all)
            count += 2.0 * fermiOccupation(e, mid, options_.smearing);
        count /= static_cast<double>(points.size());
        if (count < target)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

CMatrix CrpaSolver::polarizability(double omega,
                                   bool includeCorrelatedTransitions) const
{
    const std::size_t n = model_.orbitals.size();
    CMatrix p(n, std::vector<Cplx>(n, Cplx{0.0, 0.0}));
    const auto points = kPoints();
    const double mu = fermiLevel();
    const auto correlated = correlatedIndices();

    for (const auto& k : points) {
        const auto bands = diagonalize(k);
        const std::size_t nb = bands.energies.size();

        // Weight of each band on the correlated subspace, w_n = Σ_{i∈d}|c_in|².
        // This is what makes the constraint well defined for ENTANGLED bands:
        // a transition is removed in proportion to how much of it lives inside
        // the subspace, which is the Şaşıoğlu-Friedrich-Blügel prescription.
        // For cleanly separated bands w is 0 or 1 and it reduces to deleting
        // the d→d block outright.
        std::vector<double> dWeight(nb, 0.0);
        for (std::size_t band = 0; band < nb; ++band)
            for (std::size_t i : correlated)
                dWeight[band] += std::norm(bands.vectors[i][band]);

        for (std::size_t occ = 0; occ < nb; ++occ) {
            const double fOcc =
                fermiOccupation(bands.energies[occ], mu, options_.smearing);
            for (std::size_t emp = 0; emp < nb; ++emp) {
                if (occ == emp)
                    continue;
                const double fEmp =
                    fermiOccupation(bands.energies[emp], mu, options_.smearing);
                const double df = fOcc - fEmp;
                if (std::abs(df) < 1e-12)
                    continue;
                const double de = bands.energies[emp] - bands.energies[occ];
                if (std::abs(de) > options_.screeningCutoff)
                    continue;

                double keep = 1.0;
                if (!includeCorrelatedTransitions) {
                    keep = 1.0 - dWeight[occ] * dWeight[emp];
                    if (keep <= 1e-12)
                        continue;
                }

                const Cplx denom{omega - de, options_.broadening};
                for (std::size_t i = 0; i < n; ++i) {
                    const Cplx mi = std::conj(bands.vectors[i][occ])
                        * bands.vectors[i][emp];
                    if (mi == Cplx{0.0, 0.0})
                        continue;
                    for (std::size_t j = 0; j < n; ++j) {
                        const Cplx mj = std::conj(bands.vectors[j][occ])
                            * bands.vectors[j][emp];
                        p[i][j] += keep * df * mi * std::conj(mj) / denom;
                    }
                }
            }
        }
    }

    const double norm = 2.0 / static_cast<double>(points.size()); // spin factor
    for (auto& row : p)
        for (auto& value : row)
            value *= norm;
    return p;
}

CMatrix CrpaSolver::screenedCoulomb(double omega,
                                    bool includeCorrelatedTransitions) const
{
    const auto bare = bareCoulomb();
    const std::size_t n = bare.size();
    const auto p = polarizability(omega, includeCorrelatedTransitions);

    // ε = 1 − V P, then W = ε⁻¹ V.
    CMatrix epsilon = identity(n);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            Cplx vp{0.0, 0.0};
            for (std::size_t m = 0; m < n; ++m)
                vp += bare[i][m] * p[m][j];
            epsilon[i][j] -= vp;
        }

    const CMatrix inverse = invert(epsilon);
    CMatrix w(n, std::vector<Cplx>(n, Cplx{0.0, 0.0}));
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            Cplx sum{0.0, 0.0};
            for (std::size_t m = 0; m < n; ++m)
                sum += inverse[i][m] * bare[m][j];
            w[i][j] = sum;
        }
    return w;
}

std::array<double, 4> CrpaSolver::slaterIntegrals(double spread, int samples)
{
    // Radial density of the isotropic Gaussian whose second moment is the
    // reported spread: P(r) = sqrt(2/pi) r²/s³ exp(−r²/2s²), normalised to 1.
    const double s = std::sqrt(std::max(spread, 1e-9) / 3.0);
    const int n = std::max(101, (samples % 2 == 0) ? samples + 1 : samples);
    // Six sigma captures the density to ~1e-8; the integrand is a product of
    // two of them, so the tail contributes below that again.
    const double rMax = 8.0 * s;
    const double h = rMax / (n - 1);

    std::vector<double> r(n), p(n);
    for (int i = 0; i < n; ++i) {
        r[i] = (i == 0) ? 1e-12 : i * h;
        p[i] = std::sqrt(2.0 / kPi) * r[i] * r[i] / (s * s * s)
            * std::exp(-r[i] * r[i] / (2.0 * s * s));
    }
    // Renormalised against its own quadrature rather than trusted: F⁰ has to
    // reproduce the closed-form on-site Coulomb energy exactly, and a rule
    // that integrates P to 0.9999 would miss it by that much.
    double norm = 0.0;
    for (int i = 1; i < n; ++i)
        norm += 0.5 * h * (p[i - 1] + p[i]);
    for (double& value : p)
        value /= norm;

    std::array<double, 4> f{0.0, 0.0, 0.0, 0.0};
    const int ks[4] = {0, 2, 4, 6};
    for (int idx = 0; idx < 4; ++idx) {
        const int k = ks[idx];
        // Inner integrals accumulated once each rather than recomputed per
        // outer point: the double integral is O(N) this way instead of O(N²).
        std::vector<double> lower(n, 0.0); // ∫₀^r P(u) u^k du
        std::vector<double> upper(n, 0.0); // ∫_r^∞ P(u)/u^{k+1} du
        for (int i = 1; i < n; ++i)
            lower[i] = lower[i - 1]
                + 0.5 * h
                    * (p[i - 1] * std::pow(r[i - 1], k)
                       + p[i] * std::pow(r[i], k));
        for (int i = n - 2; i >= 0; --i)
            upper[i] = upper[i + 1]
                + 0.5 * h
                    * (p[i] / std::pow(r[i], k + 1)
                       + p[i + 1] / std::pow(r[i + 1], k + 1));

        double total = 0.0;
        std::vector<double> integrand(n, 0.0);
        for (int i = 0; i < n; ++i)
            integrand[i] = p[i]
                * (lower[i] / std::pow(r[i], k + 1) + std::pow(r[i], k) * upper[i]);
        for (int i = 1; i < n; ++i)
            total += 0.5 * h * (integrand[i - 1] + integrand[i]);
        f[idx] = kCoulombEvAngstrom * total;
    }
    return f;
}

CrpaSolver::Interaction CrpaSolver::staticInteraction() const
{
    const auto correlated = correlatedIndices();
    if (correlated.empty())
        throw std::runtime_error(
            "CrpaSolver: no orbital was flagged as correlated");

    const auto w = screenedCoulomb(0.0);
    const auto bare = bareCoulomb();

    Interaction out;
    out.screenedMatrix.assign(correlated.size(),
                              std::vector<double>(correlated.size(), 0.0));

    double diag = 0.0;
    double offDiag = 0.0;
    double bareDiag = 0.0;
    std::size_t offCount = 0;
    for (std::size_t a = 0; a < correlated.size(); ++a) {
        for (std::size_t b = 0; b < correlated.size(); ++b) {
            const double value = w[correlated[a]][correlated[b]].real();
            out.screenedMatrix[a][b] = value;
            if (a == b) {
                diag += value;
                bareDiag += bare[correlated[a]][correlated[b]];
            } else {
                offDiag += value;
                ++offCount;
            }
        }
    }

    out.u = diag / static_cast<double>(correlated.size());
    out.uBare = bareDiag / static_cast<double>(correlated.size());
    out.uPrime = (offCount > 0) ? offDiag / static_cast<double>(offCount) : 0.0;
    // Kanamori: U' = U − 2J. With only the density-density channel available
    // from a two-index W, this is the DEFINITION of J rather than an
    // independent measurement of it, and it returns zero for a degenerate
    // shell because every element of the block is then the same number.
    out.jKanamori = (offCount > 0) ? 0.5 * (out.u - out.uPrime) : 0.0;

    // Slater-Condon route, when the shell declared an angular momentum. The
    // radial integrals come from the Gaussian; the coefficients are the exact
    // atomic angular algebra, which is the part the density-density channel
    // cannot supply.
    int shellL = 0;
    double shellSpread = 0.0;
    bool consistentL = true;
    for (std::size_t index : correlated) {
        const auto& orbital = model_.orbitals[index];
        if (shellSpread == 0.0)
            shellL = orbital.angularL;
        else if (orbital.angularL != shellL)
            consistentL = false;
        shellSpread += orbital.spread;
    }
    shellSpread /= static_cast<double>(correlated.size());

    if (shellL > 0 && consistentL) {
        out.slaterF = slaterIntegrals(shellSpread);
        const double f2 = out.slaterF[1];
        const double f4 = out.slaterF[2];
        const double f6 = out.slaterF[3];
        switch (shellL) {
        case 1:
            out.jSlater = f2 / 5.0;
            break;
        case 2:
            out.jSlater = (f2 + f4) / 14.0;
            break;
        case 3:
            out.jSlater = (286.0 * f2 + 195.0 * f4 + 250.0 * f6) / 6435.0;
            break;
        default:
            out.jSlater = 0.0;
            break;
        }
        out.j = out.jSlater;
        out.jFromSlater = true;
    } else {
        // No angular information: fall back to what the density-density block
        // can say, which for a degenerate shell is honestly zero.
        out.slaterF = {out.uBare, 0.0, 0.0, 0.0};
        out.j = out.jKanamori;
        out.jFromSlater = false;
    }
    return out;
}

} // namespace calango::core
