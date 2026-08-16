#include "core/BseSolver.hpp"

#include "core/PhysicalConstants.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace calango::core {

using Cplx = std::complex<double>;

namespace bse_detail {

double staticScreenedPotential3D(double qAngstromInverse, double epsilonInfinity,
                                 double qMinAngstromInverse)
{
    const double q = qAngstromInverse > qMinAngstromInverse ? qAngstromInverse
                                                              : qMinAngstromInverse;
    return 4.0 * kPi * kCoulombEvAngstrom / (epsilonInfinity * q * q);
}

double rytovaKeldyshPotential2D(double qAngstromInverse, double r0Angstrom,
                                double environmentEpsilon, double cellAreaAngstrom2,
                                double qMinAngstromInverse)
{
    const double q = qAngstromInverse > qMinAngstromInverse ? qAngstromInverse
                                                              : qMinAngstromInverse;
    return 2.0 * kPi * kCoulombEvAngstrom
        / (cellAreaAngstrom2 * environmentEpsilon * q * (1.0 + r0Angstrom * q));
}

} // namespace bse_detail

namespace {

/// Cartesian |k - k'| for two fractional k-points, via the Hamiltonian's own
/// reciprocal lattice vectors (2*pi convention, per WannierHamiltonian).
double cartesianQ(const std::array<double, 3>& kFrac, const std::array<double, 3>& kpFrac,
                  const std::array<std::array<double, 3>, 3>& reciprocal)
{
    std::array<double, 3> dFrac{kFrac[0] - kpFrac[0], kFrac[1] - kpFrac[1],
                                kFrac[2] - kpFrac[2]};
    std::array<double, 3> cart{0.0, 0.0, 0.0};
    for (int alpha = 0; alpha < 3; ++alpha)
        for (int i = 0; i < 3; ++i)
            cart[alpha] += dFrac[i] * reciprocal[i][alpha];
    return std::sqrt(cart[0] * cart[0] + cart[1] * cart[1] + cart[2] * cart[2]);
}

double inPlaneCartesianQ(const std::array<double, 3>& kFrac, const std::array<double, 3>& kpFrac,
                         const std::array<std::array<double, 3>, 3>& reciprocal,
                         int vacuumAxis)
{
    std::array<double, 3> dFrac{kFrac[0] - kpFrac[0], kFrac[1] - kpFrac[1],
                                kFrac[2] - kpFrac[2]};
    std::array<double, 3> cart{0.0, 0.0, 0.0};
    for (int alpha = 0; alpha < 3; ++alpha)
        for (int i = 0; i < 3; ++i)
            cart[alpha] += dFrac[i] * reciprocal[i][alpha];
    double sum = 0.0;
    for (int alpha = 0; alpha < 3; ++alpha)
        if (alpha != vacuumAxis)
            sum += cart[alpha] * cart[alpha];
    return std::sqrt(sum);
}

/// Smallest nonzero |k-k'| over the mesh, Cartesian A^-1 -- the q -> 0
/// divergence regularizer both potentials use.
double meshMinimumSpacing(const std::vector<std::array<double, 3>>& kpoints,
                          const std::array<std::array<double, 3>, 3>& reciprocal,
                          bool inPlaneOnly, int vacuumAxis)
{
    if (kpoints.size() < 2)
        return 1e-3;
    double minQ = std::numeric_limits<double>::max();
    // Only ever needs the spacing along one axis from a representative point
    // (a regular Monkhorst-Pack mesh is uniform), so comparing point 0
    // against every other point is enough -- O(Nk), not O(Nk^2).
    for (std::size_t i = 1; i < kpoints.size(); ++i) {
        const double q = inPlaneOnly
            ? inPlaneCartesianQ(kpoints[0], kpoints[i], reciprocal, vacuumAxis)
            : cartesianQ(kpoints[0], kpoints[i], reciprocal);
        if (q > 1e-8 && q < minQ)
            minQ = q;
    }
    return minQ == std::numeric_limits<double>::max() ? 1e-3 : minQ;
}

/// Complex-Hermitian mat-vec, dense: y = H x.
std::vector<Cplx> matVec(const std::vector<std::vector<Cplx>>& h, const std::vector<Cplx>& x)
{
    const std::size_t n = x.size();
    std::vector<Cplx> y(n, Cplx(0.0, 0.0));
    for (std::size_t i = 0; i < n; ++i) {
        Cplx sum(0.0, 0.0);
        const auto& row = h[i];
        for (std::size_t j = 0; j < n; ++j)
            sum += row[j] * x[j];
        y[i] = sum;
    }
    return y;
}

Cplx innerProduct(const std::vector<Cplx>& a, const std::vector<Cplx>& b)
{
    Cplx sum(0.0, 0.0);
    for (std::size_t i = 0; i < a.size(); ++i)
        sum += std::conj(a[i]) * b[i];
    return sum;
}

double norm(const std::vector<Cplx>& a)
{
    return std::sqrt(innerProduct(a, a).real());
}

/// Lowest `count` eigenpairs of a dense complex-Hermitian matrix by Lanczos
/// tridiagonalization with full reorthogonalization (robust at the modest
/// iteration counts this module uses; the classical Lanczos failure mode --
/// spurious duplicate Ritz values from lost orthogonality -- is exactly what
/// reorthogonalizing against every previous vector every step prevents, at
/// an O(m^2 N) cost that is negligible next to the O(m N^2) mat-vecs for the
/// basis sizes here).
///
/// Falls back to full dense diagonalization internally (via
/// linalg::hermitianEigen on the small m x m tridiagonal matrix -- itself
/// trivial cost) for the Ritz problem; only the O(N) mat-vec is ever done
/// against the full basis dimension.
void lanczosLowest(const std::vector<std::vector<Cplx>>& h, int iterations, int count,
                   std::vector<double>& values, std::vector<std::vector<Cplx>>& vectors)
{
    const std::size_t n = h.size();
    const int m = std::min<int>(iterations, static_cast<int>(n));

    std::mt19937 rng(20260816); // fixed seed: solve() must be reproducible
    std::uniform_real_distribution<double> dist(-1.0, 1.0);

    std::vector<std::vector<Cplx>> basis;
    std::vector<Cplx> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = Cplx(dist(rng), dist(rng));
    double vn = norm(v);
    for (auto& c : v)
        c /= vn;
    basis.push_back(v);

    std::vector<double> alpha, beta;
    for (int j = 0; j < m; ++j) {
        std::vector<Cplx> w = matVec(h, basis[static_cast<std::size_t>(j)]);
        const double a = innerProduct(basis[static_cast<std::size_t>(j)], w).real();
        alpha.push_back(a);
        for (std::size_t i = 0; i < n; ++i)
            w[i] -= a * basis[static_cast<std::size_t>(j)][i];
        if (j > 0) {
            for (std::size_t i = 0; i < n; ++i)
                w[i] -= beta[static_cast<std::size_t>(j) - 1]
                    * basis[static_cast<std::size_t>(j) - 1][i];
        }
        // Full reorthogonalization against every previous Lanczos vector.
        for (int k = 0; k <= j; ++k) {
            const Cplx proj = innerProduct(basis[static_cast<std::size_t>(k)], w);
            for (std::size_t i = 0; i < n; ++i)
                w[i] -= proj * basis[static_cast<std::size_t>(k)][i];
        }
        const double b = norm(w);
        if (b < 1e-12 || j == m - 1) {
            if (j < m - 1)
                beta.push_back(0.0);
            break;
        }
        beta.push_back(b);
        for (auto& c : w)
            c /= b;
        basis.push_back(w);
    }

    const std::size_t mActual = alpha.size();
    // Embed the real symmetric tridiagonal matrix T as a complex Hermitian
    // one and reuse WannierHamiltonian's own small-matrix eigensolver --
    // T is only mActual x mActual (a handful of tens), trivial cost either
    // way, and this avoids a second, dedicated tridiagonal eigensolver.
    linalg::CMatrix t(mActual, std::vector<Cplx>(mActual, Cplx(0.0, 0.0)));
    for (std::size_t i = 0; i < mActual; ++i) {
        t[i][i] = Cplx(alpha[i], 0.0);
        if (i + 1 < mActual) {
            t[i][i + 1] = Cplx(beta[i], 0.0);
            t[i + 1][i] = Cplx(beta[i], 0.0);
        }
    }
    std::vector<double> ritzValues;
    linalg::CMatrix ritzVectors;
    linalg::hermitianEigen(t, ritzValues, ritzVectors);

    const int keep = std::min<int>(count, static_cast<int>(mActual));
    values.assign(ritzValues.begin(), ritzValues.begin() + keep);
    vectors.assign(static_cast<std::size_t>(keep), std::vector<Cplx>(n, Cplx(0.0, 0.0)));
    for (int s = 0; s < keep; ++s) {
        for (std::size_t j = 0; j < mActual; ++j) {
            const Cplx coeff = ritzVectors[j][static_cast<std::size_t>(s)];
            for (std::size_t i = 0; i < n; ++i)
                vectors[static_cast<std::size_t>(s)][i] += coeff * basis[j][i];
        }
    }
}

} // namespace

BseSolver::BseSolver(WannierHamiltonian hamiltonian, Options options)
    : hamiltonian_(std::move(hamiltonian))
    , options_(options)
{
}

std::vector<BseSolver::BasisState> BseSolver::basisStates() const
{
    std::vector<BasisState> out;
    const auto kpoints = WannierHamiltonian::monkhorstPack(options_.kmesh);
    out.reserve(kpoints.size() * static_cast<std::size_t>(options_.nValence)
        * static_cast<std::size_t>(options_.nConduction));
    for (const auto& k : kpoints) {
        const auto bands = hamiltonian_.bands(k, /*withVelocities=*/false);
        for (int dv = 0; dv < options_.nValence; ++dv) {
            const int vBand = options_.valenceBandTop - dv;
            if (vBand < 0 || static_cast<std::size_t>(vBand) >= bands.energies.size())
                continue;
            for (int dc = 0; dc < options_.nConduction; ++dc) {
                const int cBand = options_.valenceBandTop + 1 + dc;
                if (cBand < 0 || static_cast<std::size_t>(cBand) >= bands.energies.size())
                    continue;
                BasisState state;
                state.valenceBand = vBand;
                state.conductionBand = cBand;
                state.kFractional = k;
                state.energy = bands.energies[static_cast<std::size_t>(cBand)]
                    - bands.energies[static_cast<std::size_t>(vBand)];
                out.push_back(state);
            }
        }
    }
    return out;
}

double BseSolver::estimatedDenseMemoryMiB() const
{
    const double n = static_cast<double>(basisStates().size());
    return n * n * 16.0 / (1024.0 * 1024.0); // complex<double> = 16 bytes
}

double BseSolver::transitionDipoleSquared(const BasisState& state) const
{
    const auto bands = hamiltonian_.bands(state.kFractional, /*withVelocities=*/false);
    const auto gradient = hamiltonian_.gradient(state.kFractional);
    const auto& vVec = bands.vectors[static_cast<std::size_t>(state.valenceBand)];
    const auto& cVec = bands.vectors[static_cast<std::size_t>(state.conductionBand)];
    const std::size_t n = hamiltonian_.orbitals();
    double sum = 0.0;
    for (int alpha = 0; alpha < 3; ++alpha) {
        Cplx p(0.0, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            Cplx rowSum(0.0, 0.0);
            for (std::size_t j = 0; j < n; ++j)
                rowSum += gradient[static_cast<std::size_t>(alpha)][i][j] * cVec[j];
            p += std::conj(vVec[i]) * rowSum;
        }
        sum += std::norm(p);
    }
    return sum;
}

std::vector<std::array<Cplx, 3>> BseSolver::computeDipoles(
    const std::vector<BasisState>& states) const
{
    // d_vc^alpha(k) = p_vc^alpha(k) / (E_c(k)-E_v(k)), the standard
    // velocity-to-position dipole conversion, computed once and reused by
    // both the exchange kernel (buildHamiltonian) and every exciton's
    // oscillator strength (solve()).
    std::vector<std::array<Cplx, 3>> dipoles(states.size());
    for (std::size_t i = 0; i < states.size(); ++i) {
        const auto bands = hamiltonian_.bands(states[i].kFractional, /*withVelocities=*/false);
        const auto gradient = hamiltonian_.gradient(states[i].kFractional);
        const auto& vVec = bands.vectors[static_cast<std::size_t>(states[i].valenceBand)];
        const auto& cVec = bands.vectors[static_cast<std::size_t>(states[i].conductionBand)];
        const std::size_t norb = hamiltonian_.orbitals();
        for (int alpha = 0; alpha < 3; ++alpha) {
            Cplx p(0.0, 0.0);
            for (std::size_t a = 0; a < norb; ++a) {
                Cplx rowSum(0.0, 0.0);
                for (std::size_t b = 0; b < norb; ++b)
                    rowSum += gradient[static_cast<std::size_t>(alpha)][a][b] * cVec[b];
                p += std::conj(vVec[a]) * rowSum;
            }
            dipoles[i][static_cast<std::size_t>(alpha)] =
                std::abs(states[i].energy) > 1e-9 ? p / states[i].energy : Cplx(0.0, 0.0);
        }
    }
    return dipoles;
}

std::vector<std::vector<Cplx>> BseSolver::buildHamiltonian(
    const std::vector<BasisState>& states, const std::vector<std::array<Cplx, 3>>& dipoles) const
{
    const std::size_t n = states.size();
    const auto kpoints = WannierHamiltonian::monkhorstPack(options_.kmesh);
    const double nk = static_cast<double>(kpoints.size());
    const auto reciprocal = hamiltonian_.reciprocal();
    const double cellVolume = hamiltonian_.volume();
    // In-plane cell area for the 2D potential: |a1 x a2| of the Hamiltonian's
    // own cell (the vacuum axis is whichever Cartesian direction the cell's
    // third vector is (approximately) aligned with -- the same convention
    // the piezoelectric/elastic modules use, but here inferred from the
    // cell geometry itself rather than a separate config field, since
    // WannierHamiltonian carries no vacuum-axis flag of its own).
    int vacuumAxis = 2;
    {
        const auto& cell = hamiltonian_.cell();
        double best = 0.0;
        for (int axis = 0; axis < 3; ++axis) {
            const double comp = std::abs(cell[2][static_cast<std::size_t>(axis)]);
            if (comp > best) {
                best = comp;
                vacuumAxis = axis;
            }
        }
    }
    const bool is2D = options_.dimensionality == Dimensionality::Slab2D;
    double cellAreaAngstrom2 = 1.0;
    if (is2D) {
        const auto& cell = hamiltonian_.cell();
        const int a1 = (vacuumAxis + 1) % 3, a2 = (vacuumAxis + 2) % 3;
        cellAreaAngstrom2 = std::abs(cell[static_cast<std::size_t>(a1)][static_cast<std::size_t>(a1)]
                * cell[static_cast<std::size_t>(a2)][static_cast<std::size_t>(a2)]
            - cell[static_cast<std::size_t>(a1)][static_cast<std::size_t>(a2)]
                * cell[static_cast<std::size_t>(a2)][static_cast<std::size_t>(a1)]);
        if (cellAreaAngstrom2 < 1e-9)
            cellAreaAngstrom2 = 1.0;
    }
    const double qMin = meshMinimumSpacing(kpoints, reciprocal, is2D, vacuumAxis);

    // Direct term uses the full screening (epsilon_inf / r0) at the ACTUAL
    // momentum transfer q = k - k'.
    const auto directPotential = [&](std::size_t i, std::size_t j) {
        return is2D ? bse_detail::rytovaKeldyshPotential2D(
                   inPlaneCartesianQ(states[i].kFractional, states[j].kFractional, reciprocal,
                       vacuumAxis),
                   options_.keldyshR0Angstrom, options_.environmentEpsilon, cellAreaAngstrom2,
                   qMin)
                     : bse_detail::staticScreenedPotential3D(
                         cartesianQ(states[i].kFractional, states[j].kFractional, reciprocal),
                         options_.epsilonInfinity, qMin);
    };
    // Exchange is the BARE (epsilon -> 1, r0 -> 0) potential at the
    // LONG-WAVELENGTH limit q -> 0 (the same qMin regularization the k=k'
    // direct term uses), multiplied by the transition-dipole OUTER PRODUCT
    // d_i . conj(d_j) — the standard structure of the BSE exchange kernel
    // (e.g. Rohlfing & Louie; Onida-Reining-Rubio, Rev. Mod. Phys. 74, 601
    // (2002), Eq. 112): K^x_{i,j} = (2/Omega) v(q->0) M_i conj(M_j), with
    // M the same long-wavelength optical/dipole matrix element the
    // oscillator strength is built from — NOT a uniform scalar constant
    // added to every off-diagonal entry the way an earlier version of this
    // file (and, before that, a version evaluating exchange at the ACTUAL
    // k-k' with an unscreened potential) both got wrong. Both of those are
    // more than an accuracy shortfall: a matrix M M^H scaled by a positive
    // constant is POSITIVE SEMI-DEFINITE, so by Weyl's monotonicity
    // theorem adding it to H_triplet can only RAISE every eigenvalue,
    // never lower one — the exact, sign-robust guarantee "exchange is
    // repulsive" needs. Neither of the two earlier, wrong constructions
    // (evaluate-at-k-k'-unscreened; uniform-scalar-at-q-0) is PSD in
    // general, which is exactly why both let the singlet come out MORE
    // bound than the triplet on a real test run — see BseSolverTest.cpp's
    // singlet/triplet check and its own comment on what each wrong version
    // did.
    const double exchangeV0 = is2D
        ? bse_detail::rytovaKeldyshPotential2D(0.0, 0.0, 1.0, cellAreaAngstrom2, qMin)
        : bse_detail::staticScreenedPotential3D(0.0, 1.0, qMin);

    std::vector<std::vector<Cplx>> h(n, std::vector<Cplx>(n, Cplx(0.0, 0.0)));
    const double norm3D = 1.0 / (nk * cellVolume);
    // 2D: the potential above is already area-normalized (divided by
    // A_cell), so only the k-mesh count remains -- the areal analogue of
    // norm3D's 1/(Nk * V_cell).
    const double norm2D = 1.0 / nk;
    const double basisNorm = is2D ? norm2D : norm3D;
    const bool singlet = options_.spin == Spin::Singlet;
    for (std::size_t i = 0; i < n; ++i) {
        h[i][i] = Cplx(states[i].energy, 0.0);
        if (singlet)
            for (int alpha = 0; alpha < 3; ++alpha)
                h[i][i] += 2.0 * basisNorm * exchangeV0
                    * std::norm(dipoles[i][static_cast<std::size_t>(alpha)]);
        for (std::size_t j = i + 1; j < n; ++j) {
            // Diagonal-in-band-character kernel (see the class doc's
            // "point charge at the Wannier centre" note): only couples
            // pairs sharing the SAME valence and conduction band index,
            // through the k-dependent model potential.
            if (states[i].valenceBand != states[j].valenceBand
                || states[i].conductionBand != states[j].conductionBand)
                continue;
            Cplx k(-basisNorm * directPotential(i, j), 0.0);
            if (singlet) {
                Cplx outer(0.0, 0.0);
                for (int alpha = 0; alpha < 3; ++alpha)
                    outer += dipoles[i][static_cast<std::size_t>(alpha)]
                        * std::conj(dipoles[j][static_cast<std::size_t>(alpha)]);
                k += 2.0 * basisNorm * exchangeV0 * outer;
            }
            h[i][j] = k;
            h[j][i] = std::conj(k);
        }
    }
    return h;
}

std::vector<std::vector<Cplx>> BseSolver::hamiltonianForTesting() const
{
    const auto states = basisStates();
    return buildHamiltonian(states, computeDipoles(states));
}

BseSolver::Result BseSolver::solve() const
{
    Result result;
    const auto states = basisStates();
    const std::size_t n = states.size();
    result.basisDimension = n;
    result.estimatedDenseMemoryMiB = estimatedDenseMemoryMiB();
    if (n == 0)
        return result;

    result.minimumDirectGapEv = states[0].energy;
    for (const auto& s : states)
        result.minimumDirectGapEv = std::min(result.minimumDirectGapEv, s.energy);

    // Transition dipoles, once: reused both by the exchange kernel below
    // (its rank-1 M M^H structure — see buildHamiltonian's comment) and by
    // every exciton's oscillator strength further down.
    const std::vector<std::array<Cplx, 3>> dipoles = computeDipoles(states);
    const std::vector<std::vector<Cplx>> h = buildHamiltonian(states, dipoles);

    std::vector<double> eigenvalues;
    std::vector<std::vector<Cplx>> eigenvectors; // eigenvectors[state][basisIndex]
    result.usedIterativeSolver = n > options_.denseSizeLimit;
    if (!result.usedIterativeSolver) {
        std::vector<double> allValues;
        linalg::CMatrix allVectors;
        linalg::hermitianEigen(h, allValues, allVectors);
        const int keep = std::min<int>(options_.lowestExcitons, static_cast<int>(n));
        eigenvalues.assign(allValues.begin(), allValues.begin() + keep);
        eigenvectors.assign(static_cast<std::size_t>(keep), std::vector<Cplx>(n));
        for (int s = 0; s < keep; ++s)
            for (std::size_t i = 0; i < n; ++i)
                eigenvectors[static_cast<std::size_t>(s)][i] = allVectors[i][static_cast<std::size_t>(s)];
    } else {
        lanczosLowest(h, options_.lanczosIterations, options_.lowestExcitons, eigenvalues,
                      eigenvectors);
    }

    for (std::size_t s = 0; s < eigenvalues.size(); ++s) {
        Exciton exciton;
        exciton.energy = eigenvalues[s];
        exciton.bindingEnergy = eigenvalues[s] - result.minimumDirectGapEv;
        exciton.amplitude = eigenvectors[s];
        double f = 0.0;
        for (int alpha = 0; alpha < 3; ++alpha) {
            Cplx sum(0.0, 0.0);
            for (std::size_t i = 0; i < n; ++i)
                sum += std::conj(eigenvectors[s][i]) * dipoles[i][static_cast<std::size_t>(alpha)];
            f += std::norm(sum);
        }
        // Relative oscillator strength: (2/3) * E_S * sum_alpha |sum_i A*_i d_i^alpha|^2.
        // Internally consistent (comparable peak-to-peak within one run and
        // against the independent-particle spectrum below, both built from
        // the SAME dipole/normalization convention) but NOT independently
        // calibrated to the Thomas-Reiche-Kuhn f-sum rule -- see the class
        // doc comment.
        exciton.oscillatorStrength = (2.0 / 3.0) * eigenvalues[s] * f;
        result.excitons.push_back(exciton);
    }

    // Absorption spectra: Gaussian-broadened stick spectra. Independent-
    // particle: every basis TRANSITION on its own (no BSE mixing) at its
    // bare energy, oscillator strength from the same dipole convention as
    // above (amplitude = 1 on itself). Excitonic: the diagonalized states
    // actually computed -- the full window when solved densely, only the
    // lowest `lowestExcitons` when the iterative path was used (documented
    // in the UI, not silently partial).
    const double window = options_.spectrumWindowEv;
    const double sigma = std::max(options_.broadeningEv, 1e-4);
    const int points = std::max(options_.spectrumPoints, 2);
    result.spectrum.energiesEv.resize(static_cast<std::size_t>(points));
    result.spectrum.excitonic.assign(static_cast<std::size_t>(points), 0.0);
    result.spectrum.independentParticle.assign(static_cast<std::size_t>(points), 0.0);
    const double eMin = result.minimumDirectGapEv - 0.5 * window;
    const double eMax = result.minimumDirectGapEv + window;
    for (int p = 0; p < points; ++p)
        result.spectrum.energiesEv[static_cast<std::size_t>(p)] =
            eMin + (eMax - eMin) * p / (points - 1);

    const auto addGaussian = [&](std::vector<double>& target, double center, double weight) {
        for (int p = 0; p < points; ++p) {
            const double e = result.spectrum.energiesEv[static_cast<std::size_t>(p)];
            const double x = (e - center) / sigma;
            target[static_cast<std::size_t>(p)] += weight * std::exp(-0.5 * x * x);
        }
    };
    for (std::size_t i = 0; i < n; ++i) {
        double f = 0.0;
        for (int alpha = 0; alpha < 3; ++alpha)
            f += std::norm(dipoles[i][static_cast<std::size_t>(alpha)]);
        addGaussian(result.spectrum.independentParticle, states[i].energy,
                    (2.0 / 3.0) * states[i].energy * f);
    }
    for (const auto& exciton : result.excitons)
        addGaussian(result.spectrum.excitonic, exciton.energy, exciton.oscillatorStrength);

    return result;
}

} // namespace calango::core
