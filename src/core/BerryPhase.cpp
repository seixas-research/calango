#include "core/BerryPhase.hpp"
#include "core/PhysicalConstants.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace calango::core {

namespace {

using linalg::CMatrix;
using linalg::Cplx;

/// Wrap to (-pi, pi].
double wrapPhase(double phase)
{
    while (phase > kPi)
        phase -= 2.0 * kPi;
    while (phase <= -kPi)
        phase += 2.0 * kPi;
    return phase;
}

/// Overlap matrix M_mn = <u_m(k)|u_n(k')> restricted to the listed bands.
///
/// In a Wannier/tight-binding basis the cell-periodic parts are the eigenvector
/// columns themselves, so the overlap is just the conjugated column product.
/// This is the object every Wilson loop is built from, and it is where gauge
/// covariance comes from: an arbitrary phase on a column enters once
/// conjugated and once not, and cancels around the loop.
CMatrix overlap(const CMatrix& a, const CMatrix& b,
                const std::vector<int>& bands)
{
    const std::size_t n = bands.size();
    const std::size_t dim = a.size();
    CMatrix m(n, std::vector<Cplx>(n, Cplx{0.0, 0.0}));
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            Cplx sum{0.0, 0.0};
            for (std::size_t orb = 0; orb < dim; ++orb)
                sum += std::conj(a[orb][static_cast<std::size_t>(bands[i])])
                    * b[orb][static_cast<std::size_t>(bands[j])];
            m[i][j] = sum;
        }
    return m;
}

/// det of a small complex matrix by Gaussian elimination with partial
/// pivoting. Returns the determinant, tracking the sign of the row swaps.
Cplx determinant(CMatrix a)
{
    const std::size_t n = a.size();
    if (n == 0)
        return Cplx{1.0, 0.0};
    Cplx det{1.0, 0.0};
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        double best = std::abs(a[col][col]);
        for (std::size_t r = col + 1; r < n; ++r)
            if (std::abs(a[r][col]) > best) {
                best = std::abs(a[r][col]);
                pivot = r;
            }
        if (best < 1e-300)
            return Cplx{0.0, 0.0};
        if (pivot != col) {
            std::swap(a[col], a[pivot]);
            det = -det;
        }
        det *= a[col][col];
        for (std::size_t r = col + 1; r < n; ++r) {
            const Cplx f = a[r][col] / a[col][col];
            for (std::size_t j = col; j < n; ++j)
                a[r][j] -= f * a[col][j];
        }
    }
    return det;
}

/// Eigen-phases of a unitary matrix: the Wilson-loop spectrum.
///
/// Obtained from the Hermitian pair (U + U†)/2 and (U − U†)/2i, which share
/// the eigenvectors of a unitary U, so the phases come out as atan2 of the two
/// eigenvalue sets. Avoids a general complex eigensolver for what is a small
/// and very structured problem.
std::vector<double> unitaryPhases(const CMatrix& u)
{
    const std::size_t n = u.size();
    if (n == 0)
        return {};
    // Hermitian part H = (U + U^dagger)/2. Its eigenvectors are U's, and the
    // corresponding eigenvalue is cos(phi).
    CMatrix h(n, std::vector<Cplx>(n, Cplx{0.0, 0.0}));
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j)
            h[i][j] = 0.5 * (u[i][j] + std::conj(u[j][i]));

    std::vector<double> cosines;
    CMatrix vectors;
    linalg::hermitianEigen(h, cosines, vectors);

    std::vector<double> phases(n, 0.0);
    for (std::size_t band = 0; band < n; ++band) {
        // sin(phi) from the anti-Hermitian part on the same eigenvector.
        Cplx sinPart{0.0, 0.0};
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j)
                sinPart += std::conj(vectors[i][band])
                    * (0.5 * (u[i][j] - std::conj(u[j][i]))) * vectors[j][band];
        phases[band] = std::atan2(sinPart.imag(), std::clamp(cosines[band], -1.0, 1.0));
    }
    std::sort(phases.begin(), phases.end());
    return phases;
}

} // namespace

BerryPhase::BerryPhase(WannierHamiltonian hamiltonian, Options options)
    : hamiltonian_(std::move(hamiltonian)), options_(std::move(options))
{
    if (options_.loopPoints < 4)
        throw std::invalid_argument("BerryPhase: a loop needs >= 4 points");
}

BerryPhase::BerryPhase(WannierHamiltonian hamiltonian)
    : BerryPhase(std::move(hamiltonian), Options{})
{
}

std::vector<int> BerryPhase::occupiedAt(const std::vector<double>& energies) const
{
    if (!options_.occupiedBands.empty())
        return options_.occupiedBands;
    std::vector<int> bands;
    for (std::size_t i = 0; i < energies.size(); ++i)
        if (energies[i] <= options_.fermiLevel)
            bands.push_back(static_cast<int>(i));
    return bands;
}

BerryPhase::LoopResult BerryPhase::wilsonLoop(
    const std::vector<std::array<double, 3>>& path) const
{
    LoopResult out;
    if (path.size() < 2)
        return out;

    const auto first = hamiltonian_.bands(path.front(), /*withVelocities=*/false);
    const auto bands = occupiedAt(first.energies);
    if (bands.empty())
        return out;

    // Product of overlaps around the loop. The last factor closes back onto
    // the first point: in convention I the Hamiltonian is exactly periodic in
    // k, so |u(k_N)> = |u(k_0)> and no correction phase is needed.
    CMatrix product = linalg::identity(bands.size());
    CMatrix previous = first.vectors;
    for (std::size_t step = 1; step <= path.size(); ++step) {
        const CMatrix next = (step == path.size())
            ? first.vectors
            : hamiltonian_.bands(path[step], false).vectors;
        product = linalg::multiply(product, overlap(previous, next, bands));
        previous = next;
    }

    const Cplx det = determinant(product);
    out.berryPhase = wrapPhase(-std::atan2(det.imag(), det.real()));

    // The individual eigenphases. The product is only unitary in the limit of
    // a dense loop, so it is not projected back onto U(N) first; for a loop
    // dense enough for the total phase to be meaningful, the deviation is the
    // same discretisation error already present in the determinant.
    out.wannierCentres = unitaryPhases(product);
    for (double& centre : out.wannierCentres)
        centre = -centre / (2.0 * kPi); // in units of the loop period
    return out;
}

BerryPhase::LoopResult BerryPhase::wilsonLoopAlong(
    int axis, const std::array<double, 3>& base) const
{
    std::vector<std::array<double, 3>> path;
    path.reserve(static_cast<std::size_t>(options_.loopPoints));
    for (int i = 0; i < options_.loopPoints; ++i) {
        auto k = base;
        k[static_cast<std::size_t>(axis)] =
            static_cast<double>(i) / options_.loopPoints;
        path.push_back(k);
    }
    return wilsonLoop(path);
}

double BerryPhase::bandCurvature(const std::array<double, 3>& k, int band,
                                 int alpha, int beta,
                                 double degeneracyCutoff) const
{
    const auto bands = hamiltonian_.bands(k, /*withVelocities=*/false);
    const auto grad = hamiltonian_.gradient(k);
    const std::size_t n = bands.energies.size();
    const auto nIdx = static_cast<std::size_t>(band);
    if (nIdx >= n)
        return 0.0;

    // v^alpha_nm = <n| dH/dk_alpha |m>, in eV.A.
    const auto element = [&](int direction, std::size_t bra, std::size_t ket) {
        Cplx sum{0.0, 0.0};
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j)
                sum += std::conj(bands.vectors[i][bra])
                    * grad[static_cast<std::size_t>(direction)][i][j]
                    * bands.vectors[j][ket];
        return sum;
    };

    double omega = 0.0;
    for (std::size_t m = 0; m < n; ++m) {
        if (m == nIdx)
            continue;
        const double gap = bands.energies[nIdx] - bands.energies[m];
        if (std::abs(gap) < degeneracyCutoff)
            continue; // a genuine divergence, not a number worth reporting
        const Cplx va = element(alpha, nIdx, m);
        const Cplx vb = element(beta, m, nIdx);
        omega += -2.0 * (va * vb).imag() / (gap * gap);
    }
    return omega;
}

double BerryPhase::totalCurvature(const std::array<double, 3>& k, int alpha,
                                  int beta) const
{
    const auto bands = hamiltonian_.bands(k, /*withVelocities=*/false);
    const auto occupied = occupiedAt(bands.energies);
    double total = 0.0;
    for (int band : occupied)
        total += bandCurvature(k, band, alpha, beta);
    return total;
}

BerryPhase::CurvatureMap BerryPhase::curvaturePlane(int dir1, int dir2,
                                                    int samples1, int samples2,
                                                    double fixedCoordinate,
                                                    int alpha, int beta) const
{
    CurvatureMap map;
    const int n1 = std::max(2, samples1);
    const int n2 = std::max(2, samples2);
    const int fixedDir = 3 - dir1 - dir2;

    map.axis1.resize(static_cast<std::size_t>(n1));
    map.axis2.resize(static_cast<std::size_t>(n2));
    map.values.assign(static_cast<std::size_t>(n1),
                      std::vector<double>(static_cast<std::size_t>(n2), 0.0));

    double lo = 0.0;
    double hi = 0.0;
    bool first = true;
    for (int i = 0; i < n1; ++i) {
        const double f1 = static_cast<double>(i) / n1;
        map.axis1[static_cast<std::size_t>(i)] = f1;
        for (int j = 0; j < n2; ++j) {
            const double f2 = static_cast<double>(j) / n2;
            map.axis2[static_cast<std::size_t>(j)] = f2;
            std::array<double, 3> k{};
            k[static_cast<std::size_t>(dir1)] = f1;
            k[static_cast<std::size_t>(dir2)] = f2;
            k[static_cast<std::size_t>(fixedDir)] = fixedCoordinate;
            const double value = totalCurvature(k, alpha, beta);
            map.values[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                value;
            if (first) {
                lo = hi = value;
                first = false;
            } else {
                lo = std::min(lo, value);
                hi = std::max(hi, value);
            }
        }
    }
    map.minimum = lo;
    map.maximum = hi;
    return map;
}

BerryPhase::HallResult BerryPhase::anomalousHall(int alpha, int beta) const
{
    HallResult out;
    const auto points = WannierHamiltonian::monkhorstPack(options_.kmesh);
    double sum = 0.0;
    for (const auto& k : points)
        sum += totalCurvature(k, alpha, beta);
    const double mean = sum / static_cast<double>(points.size());

    // Chern number for the plane spanned by (alpha, beta):
    //   C = (1/2pi) INT Omega d^2k  = (1/2pi) * <Omega> * A_BZ
    // and the 2D BZ area is (2pi)^2/A_cell, so C = <Omega> * 2pi / A_cell.
    // A_cell is the real-space area of the same plane.
    const auto& cell = hamiltonian_.cell();
    const auto& a = cell[static_cast<std::size_t>(alpha)];
    const auto& b = cell[static_cast<std::size_t>(beta)];
    const std::array<double, 3> cross{a[1] * b[2] - a[2] * b[1],
                                      a[2] * b[0] - a[0] * b[2],
                                      a[0] * b[1] - a[1] * b[0]};
    const double area = std::sqrt(cross[0] * cross[0] + cross[1] * cross[1]
                                  + cross[2] * cross[2]);
    out.chernNumber = (area > 0.0) ? mean * 2.0 * kPi / area : 0.0;

    // sigma_ab = -(e^2/hbar) (1/V) SUM_k Omega. With <Omega> in A^2 and the
    // volume in A^3 the ratio is 1/A, converted to 1/m.
    const double volume = hamiltonian_.volume();
    out.sigmaSI =
        (volume > 0.0) ? -kEsquaredOverHbar * mean / (volume * 1e-10) : 0.0;
    out.sigmaInConductanceQuanta = out.chernNumber;
    return out;
}

BerryPhase::Polarization BerryPhase::polarization(int axis,
                                                  int transverseSamples) const
{
    Polarization out;
    const int d1 = (axis + 1) % 3;
    const int d2 = (axis + 2) % 3;
    const int n = std::max(1, transverseSamples);

    // Averaged over the transverse plane: the polarization is the mean of the
    // hybrid Wannier centres, and a single loop only samples one line of them.
    double sum = 0.0;
    int counted = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            std::array<double, 3> base{};
            base[static_cast<std::size_t>(d1)] = static_cast<double>(i) / n;
            base[static_cast<std::size_t>(d2)] = static_cast<double>(j) / n;
            sum += wilsonLoopAlong(axis, base).berryPhase;
            ++counted;
        }
    out.phaseRadians = (counted > 0) ? sum / counted : 0.0;

    // P = (e/2pi) * phase * a_axis / V, so the dipole per cell is
    // phase/(2pi) * |a| in e.A.
    const auto& a = hamiltonian_.cell()[static_cast<std::size_t>(axis)];
    const double length =
        std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    out.dipolePerCell = out.phaseRadians / (2.0 * kPi) * length;

    const double volume = hamiltonian_.volume();
    // e.A per A^3 -> C/m^2: 1.602e-19 C * 1e-10 m / 1e-30 m^3 = 1.602e1.
    constexpr double kToSI = 1.602176634e-19 * 1e-10 / 1e-30;
    if (volume > 0.0) {
        out.siValue = out.dipolePerCell / volume * kToSI;
        // The quantum is one full lattice translation of the centre.
        out.quantumSI = length / volume * kToSI;
    }
    return out;
}

BerryPhase::CentreFlow BerryPhase::wannierCentreFlow(int loopAxis,
                                                     int transverseAxis,
                                                     int steps) const
{
    CentreFlow flow;
    const int n = std::max(2, steps);
    flow.transverse.reserve(static_cast<std::size_t>(n));
    flow.centres.reserve(static_cast<std::size_t>(n));

    double previousSum = 0.0;
    double accumulated = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / n;
        std::array<double, 3> base{};
        base[static_cast<std::size_t>(transverseAxis)] = t;
        const auto loop = wilsonLoopAlong(loopAxis, base);
        flow.transverse.push_back(t);
        flow.centres.push_back(loop.wannierCentres);

        // Winding of the SUMMED centres, unwrapped step by step. The sum is
        // gauge invariant where the individual centres are only defined up to
        // a permutation, which is why the Chern number is read off it.
        double sum = 0.0;
        for (double centre : loop.wannierCentres)
            sum += centre;
        if (i > 0) {
            double delta = sum - previousSum;
            while (delta > 0.5)
                delta -= 1.0;
            while (delta < -0.5)
                delta += 1.0;
            accumulated += delta;
        }
        previousSum = sum;
    }
    flow.winding = accumulated;
    return flow;
}

} // namespace calango::core
