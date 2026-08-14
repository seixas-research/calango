#include "core/WannierHamiltonian.hpp"
#include "core/PhysicalConstants.hpp"

#include "core/LocaleSafeNumber.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace calango::core {

namespace {
}

namespace linalg {

CMatrix identity(std::size_t n)
{
    CMatrix m(n, std::vector<Cplx>(n, Cplx{0.0, 0.0}));
    for (std::size_t i = 0; i < n; ++i)
        m[i][i] = Cplx{1.0, 0.0};
    return m;
}

CMatrix multiply(const CMatrix& a, const CMatrix& b)
{
    const std::size_t n = a.size();
    CMatrix out(n, std::vector<Cplx>(n, Cplx{0.0, 0.0}));
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = 0; k < n; ++k) {
            const Cplx aik = a[i][k];
            if (aik == Cplx{0.0, 0.0})
                continue;
            for (std::size_t j = 0; j < n; ++j)
                out[i][j] += aik * b[k][j];
        }
    return out;
}

CMatrix invert(CMatrix a)
{
    const std::size_t n = a.size();
    CMatrix inv = identity(n);
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        double best = std::abs(a[col][col]);
        for (std::size_t r = col + 1; r < n; ++r)
            if (std::abs(a[r][col]) > best) {
                best = std::abs(a[r][col]);
                pivot = r;
            }
        if (best < 1e-300)
            throw std::runtime_error("linalg::invert: singular matrix");
        std::swap(a[col], a[pivot]);
        std::swap(inv[col], inv[pivot]);

        const Cplx d = a[col][col];
        for (std::size_t j = 0; j < n; ++j) {
            a[col][j] /= d;
            inv[col][j] /= d;
        }
        for (std::size_t r = 0; r < n; ++r) {
            if (r == col)
                continue;
            const Cplx f = a[r][col];
            if (f == Cplx{0.0, 0.0})
                continue;
            for (std::size_t j = 0; j < n; ++j) {
                a[r][j] -= f * a[col][j];
                inv[r][j] -= f * inv[col][j];
            }
        }
    }
    return inv;
}

void hermitianEigen(CMatrix a, std::vector<double>& values, CMatrix& vectors)
{
    const std::size_t n = a.size();
    vectors = identity(n);
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0.0;
        for (std::size_t p = 0; p < n; ++p)
            for (std::size_t q = p + 1; q < n; ++q)
                off += std::norm(a[p][q]);
        if (off < 1e-26)
            break;

        for (std::size_t p = 0; p < n; ++p)
            for (std::size_t q = p + 1; q < n; ++q) {
                const double apq = std::abs(a[p][q]);
                if (apq < 1e-18)
                    continue;
                const Cplx phase = a[p][q] / apq;
                const double tau =
                    (a[q][q].real() - a[p][p].real()) / (2.0 * apq);
                const double t = (tau >= 0.0 ? 1.0 : -1.0)
                    / (std::abs(tau) + std::sqrt(1.0 + tau * tau));
                const double c = 1.0 / std::sqrt(1.0 + t * t);
                const double s = t * c;

                // U = D R, and BOTH factors are needed.
                //
                // D = diag(1, e^{-iφ}) is what makes the off-diagonal real:
                // (D†AD)_pq = a_pq e^{-iφ} = |a_pq|. Only then does the real
                // Jacobi rotation R = [[c, s], [−s, c]] — paired with the root
                // t of t² + 2τt − 1 = 0 computed above — annihilate it.
                //
                // The tempting symmetric-looking form
                //     [[c, s e^{-iφ}], [−s e^{iφ}, c]]
                // is NOT this product: working out (U†AU)_pp leaves an
                // uncancelled e^{2iφ}, so the off-diagonal survives and the
                // sweep converges to the wrong eigenvalues. On a QWZ
                // Hamiltonian that came out 5% low; with the rotation also
                // transposed, 34% low. Both are invisible whenever a_pp =
                // a_qq or a_pq is real, which is every 2x2 test case built
                // from a single Pauli matrix.
                CMatrix rot = identity(n);
                rot[p][p] = Cplx{c, 0.0};
                rot[p][q] = Cplx{s, 0.0};
                rot[q][p] = -s * std::conj(phase);
                rot[q][q] = c * std::conj(phase);

                CMatrix rotH(n, std::vector<Cplx>(n, Cplx{0.0, 0.0}));
                for (std::size_t i = 0; i < n; ++i)
                    for (std::size_t j = 0; j < n; ++j)
                        rotH[i][j] = std::conj(rot[j][i]);
                a = multiply(rotH, multiply(a, rot));
                vectors = multiply(vectors, rot);
            }
    }

    values.assign(n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        values[i] = a[i][i].real();

    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
              [&](std::size_t x, std::size_t y) { return values[x] < values[y]; });
    std::vector<double> sortedValues(n);
    CMatrix sortedVectors(n, std::vector<Cplx>(n, Cplx{0.0, 0.0}));
    for (std::size_t idx = 0; idx < n; ++idx) {
        sortedValues[idx] = values[order[idx]];
        for (std::size_t row = 0; row < n; ++row)
            sortedVectors[row][idx] = vectors[row][order[idx]];
    }
    values = std::move(sortedValues);
    vectors = std::move(sortedVectors);
}

} // namespace linalg

WannierHamiltonian::WannierHamiltonian(
    std::size_t orbitals, std::array<std::array<double, 3>, 3> cell,
    std::vector<HoppingBlock> hoppings)
    : orbitals_(orbitals), cell_(cell), hoppings_(std::move(hoppings))
{
    if (orbitals_ == 0)
        throw std::invalid_argument("WannierHamiltonian: no orbitals");
    for (const auto& block : hoppings_) {
        if (block.matrix.size() != orbitals_ * orbitals_)
            throw std::invalid_argument(
                "WannierHamiltonian: H(R) block is not n x n");
        if (!block.imaginary.empty()
            && block.imaginary.size() != orbitals_ * orbitals_)
            throw std::invalid_argument(
                "WannierHamiltonian: imaginary block size mismatch");
    }
}

double WannierHamiltonian::volume() const
{
    const auto& a = cell_;
    return std::abs(a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
                    - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
                    + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]));
}

std::array<std::array<double, 3>, 3> WannierHamiltonian::reciprocal() const
{
    const auto& a = cell_;
    const double v = volume();
    const auto cross = [](const std::array<double, 3>& x,
                          const std::array<double, 3>& y) {
        return std::array<double, 3>{x[1] * y[2] - x[2] * y[1],
                                     x[2] * y[0] - x[0] * y[2],
                                     x[0] * y[1] - x[1] * y[0]};
    };
    std::array<std::array<double, 3>, 3> b{};
    const std::array<std::array<double, 3>, 3> pairs[3] = {
        {a[1], a[2], {}}, {a[2], a[0], {}}, {a[0], a[1], {}}};
    for (int i = 0; i < 3; ++i) {
        const auto c = cross(pairs[i][0], pairs[i][1]);
        for (int x = 0; x < 3; ++x)
            b[static_cast<std::size_t>(i)][static_cast<std::size_t>(x)] =
                2.0 * kPi * c[static_cast<std::size_t>(x)] / v;
    }
    return b;
}

linalg::CMatrix WannierHamiltonian::hamiltonian(
    const std::array<double, 3>& k) const
{
    const std::size_t n = orbitals_;
    linalg::CMatrix h(n, std::vector<linalg::Cplx>(n, linalg::Cplx{0.0, 0.0}));
    for (const auto& block : hoppings_) {
        const double phase = 2.0 * kPi
            * (k[0] * block.lattice[0] + k[1] * block.lattice[1]
               + k[2] * block.lattice[2]);
        const linalg::Cplx factor{std::cos(phase), std::sin(phase)};
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j) {
                const std::size_t idx = i * n + j;
                const linalg::Cplx element{
                    block.matrix[idx],
                    block.imaginary.empty() ? 0.0 : block.imaginary[idx]};
                h[i][j] += factor * element;
            }
    }
    // Hermitised. An _hr.dat that lists only half the star of R vectors would
    // otherwise give complex eigenvalues, and the eigensolver would silently
    // return their real parts.
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i; j < n; ++j) {
            const linalg::Cplx avg = 0.5 * (h[i][j] + std::conj(h[j][i]));
            h[i][j] = avg;
            h[j][i] = std::conj(avg);
        }
    return h;
}

std::array<linalg::CMatrix, 3> WannierHamiltonian::gradient(
    const std::array<double, 3>& k) const
{
    const std::size_t n = orbitals_;
    std::array<linalg::CMatrix, 3> grad;
    for (auto& g : grad)
        g.assign(n, std::vector<linalg::Cplx>(n, linalg::Cplx{0.0, 0.0}));

    for (const auto& block : hoppings_) {
        const double phase = 2.0 * kPi
            * (k[0] * block.lattice[0] + k[1] * block.lattice[1]
               + k[2] * block.lattice[2]);
        const linalg::Cplx factor{std::cos(phase), std::sin(phase)};
        // R in CARTESIAN Å: the derivative is with respect to Cartesian k, so
        // the prefactor is a real length, not the integer triple.
        std::array<double, 3> r{0.0, 0.0, 0.0};
        for (int axis = 0; axis < 3; ++axis)
            for (int x = 0; x < 3; ++x)
                r[static_cast<std::size_t>(x)] +=
                    block.lattice[static_cast<std::size_t>(axis)]
                    * cell_[static_cast<std::size_t>(axis)]
                           [static_cast<std::size_t>(x)];

        for (int x = 0; x < 3; ++x) {
            const linalg::Cplx pre =
                linalg::Cplx{0.0, r[static_cast<std::size_t>(x)]} * factor;
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t j = 0; j < n; ++j) {
                    const std::size_t idx = i * n + j;
                    const linalg::Cplx element{
                        block.matrix[idx],
                        block.imaginary.empty() ? 0.0 : block.imaginary[idx]};
                    grad[static_cast<std::size_t>(x)][i][j] += pre * element;
                }
        }
    }

    // ∂H/∂k must be Hermitian too — H(k) is Hermitian at every k, so its
    // derivative is. Enforced for the same reason as above.
    for (auto& g : grad)
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = i; j < n; ++j) {
                const linalg::Cplx avg = 0.5 * (g[i][j] + std::conj(g[j][i]));
                g[i][j] = avg;
                g[j][i] = std::conj(avg);
            }
    return grad;
}

WannierHamiltonian::Bands WannierHamiltonian::bands(
    const std::array<double, 3>& k, bool withVelocities) const
{
    Bands out;
    linalg::hermitianEigen(hamiltonian(k), out.energies, out.vectors);
    if (!withVelocities)
        return out;

    const auto grad = gradient(k);
    const std::size_t n = orbitals_;
    out.gradients.assign(n, {0.0, 0.0, 0.0});
    for (std::size_t band = 0; band < n; ++band)
        for (int x = 0; x < 3; ++x) {
            // ⟨n| ∂H/∂k_α |n⟩. Imaginary part is zero by hermiticity; taking
            // the real part discards only round-off.
            linalg::Cplx sum{0.0, 0.0};
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t j = 0; j < n; ++j)
                    sum += std::conj(out.vectors[i][band])
                        * grad[static_cast<std::size_t>(x)][i][j]
                        * out.vectors[j][band];
            out.gradients[band][static_cast<std::size_t>(x)] = sum.real();
        }
    return out;
}

std::vector<std::array<double, 3>> WannierHamiltonian::monkhorstPack(
    const std::array<int, 3>& mesh)
{
    std::vector<std::array<double, 3>> points;
    const int n0 = std::max(1, mesh[0]);
    const int n1 = std::max(1, mesh[1]);
    const int n2 = std::max(1, mesh[2]);
    points.reserve(static_cast<std::size_t>(n0) * n1 * n2);
    for (int a = 0; a < n0; ++a)
        for (int b = 0; b < n1; ++b)
            for (int c = 0; c < n2; ++c)
                points.push_back({static_cast<double>(a) / n0,
                                  static_cast<double>(b) / n1,
                                  static_cast<double>(c) / n2});
    return points;
}

WannierHamiltonian WannierHamiltonian::fromHrDat(
    const std::string& path, std::array<std::array<double, 3>, 3> cell,
    std::string* error)
{
    std::ifstream in(path);
    if (!in) {
        if (error)
            *error = "could not open " + path;
        return {};
    }
    std::string line;
    std::getline(in, line); // comment / date

    int orbitals = 0;
    int points = 0;
    if (!(std::getline(in, line) && (std::istringstream(line) >> orbitals))
        || orbitals <= 0) {
        if (error)
            *error = path + " does not start like a wannier90 _hr.dat";
        return {};
    }
    if (!(std::getline(in, line) && (std::istringstream(line) >> points))
        || points <= 0) {
        if (error)
            *error = "could not read the Wigner-Seitz point count";
        return {};
    }

    // The degeneracy table: `points` integers, wrapped over as many lines as
    // it takes. Each H(R) is DIVIDED by its degeneracy — that weighting is
    // part of the format, and skipping it silently rescales every hopping.
    std::vector<int> degeneracy;
    degeneracy.reserve(static_cast<std::size_t>(points));
    while (static_cast<int>(degeneracy.size()) < points && std::getline(in, line)) {
        std::istringstream stream(line);
        int value = 0;
        while (stream >> value && static_cast<int>(degeneracy.size()) < points)
            degeneracy.push_back(value);
    }
    if (static_cast<int>(degeneracy.size()) != points) {
        if (error)
            *error = "the degeneracy table is short";
        return {};
    }

    struct Block {
        std::vector<double> re;
        std::vector<double> im;
    };
    std::map<std::array<int, 3>, Block> blocks;
    std::vector<std::array<int, 3>> order;
    int seen = 0;
    while (std::getline(in, line)) {
        std::istringstream stream(line);
        int r0 = 0, r1 = 0, r2 = 0, i = 0, j = 0;
        double re = 0.0, im = 0.0;
        if (!(stream >> r0 >> r1 >> r2 >> i >> j >> re))
            continue;
        stream >> im;
        if (i < 1 || j < 1 || i > orbitals || j > orbitals)
            continue;
        const std::array<int, 3> key{r0, r1, r2};
        auto it = blocks.find(key);
        if (it == blocks.end()) {
            Block fresh;
            fresh.re.assign(static_cast<std::size_t>(orbitals) * orbitals, 0.0);
            fresh.im.assign(static_cast<std::size_t>(orbitals) * orbitals, 0.0);
            it = blocks.emplace(key, std::move(fresh)).first;
            order.push_back(key);
        }
        const int weight =
            (seen / (orbitals * orbitals) < points)
            ? std::max(1, degeneracy[static_cast<std::size_t>(
                  seen / (orbitals * orbitals))])
            : 1;
        const std::size_t idx =
            static_cast<std::size_t>(i - 1) * orbitals + (j - 1);
        it->second.re[idx] = re / weight;
        it->second.im[idx] = im / weight;
        ++seen;
    }

    std::vector<HoppingBlock> hoppings;
    hoppings.reserve(order.size());
    for (const auto& key : order) {
        HoppingBlock block;
        block.lattice = key;
        block.matrix = blocks[key].re;
        block.imaginary = blocks[key].im;
        hoppings.push_back(std::move(block));
    }
    if (hoppings.empty()) {
        if (error)
            *error = "no hopping blocks were parsed from " + path;
        return {};
    }
    return WannierHamiltonian(static_cast<std::size_t>(orbitals), cell,
                              std::move(hoppings));
}

} // namespace calango::core
