#include "core/DislocationBuilder.hpp"
#include "core/PhysicalConstants.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace calango::core {

namespace {

using Complex = std::complex<double>;


// ---------------------------------------------------------------------------
// Voigt <-> full fourth-rank tensor
// ---------------------------------------------------------------------------

/// Voigt index of the pair (i, j): 11->0, 22->1, 33->2, 23->3, 13->4, 12->5.
constexpr int voigtIndex(int i, int j)
{
    if (i == j)
        return i;
    if ((i == 1 && j == 2) || (i == 2 && j == 1))
        return 3;
    if ((i == 0 && j == 2) || (i == 2 && j == 0))
        return 4;
    return 5;
}

using Voigt = std::array<std::array<double, 6>, 6>;

double component(const Voigt& c, int i, int j, int k, int l)
{
    return c[static_cast<std::size_t>(voigtIndex(i, j))]
            [static_cast<std::size_t>(voigtIndex(k, l))];
}

double axisComponent(const Vec3& v, int index)
{
    return index == 0 ? v.x : (index == 1 ? v.y : v.z);
}

// ---------------------------------------------------------------------------
// Small dense linear algebra, only as much as the Stroh solution needs
// ---------------------------------------------------------------------------

/// Bilinear (NOT Hermitian) cross product. The null vector of a rank-2 complex
/// 3x3 matrix is bilinear-orthogonal to two independent rows — conjugating
/// here would find the null vector of a different matrix.
std::array<Complex, 3> bilinearCross(const std::array<Complex, 3>& a,
                                     const std::array<Complex, 3>& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
}

double norm(const std::array<Complex, 3>& v)
{
    return std::sqrt(std::norm(v[0]) + std::norm(v[1]) + std::norm(v[2]));
}

/// Gaussian elimination with partial pivoting on a real N x N system.
/// Returns false when the matrix is numerically singular.
bool solveLinear(std::vector<std::vector<double>>& a, std::vector<double>& b)
{
    const std::size_t n = b.size();
    for (std::size_t column = 0; column < n; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < n; ++row)
            if (std::abs(a[row][column]) > std::abs(a[pivot][column]))
                pivot = row;
        if (std::abs(a[pivot][column]) < 1e-12)
            return false;
        std::swap(a[pivot], a[column]);
        std::swap(b[pivot], b[column]);
        for (std::size_t row = 0; row < n; ++row) {
            if (row == column)
                continue;
            const double factor = a[row][column] / a[column][column];
            if (factor == 0.0)
                continue;
            for (std::size_t k = column; k < n; ++k)
                a[row][k] -= factor * a[column][k];
            b[row] -= factor * b[column];
        }
    }
    for (std::size_t i = 0; i < n; ++i)
        b[i] /= a[i][i];
    return true;
}

// -- Polynomials over the complex plane, coefficients ascending -------------

using Poly = std::vector<double>;

Poly polyMultiply(const Poly& a, const Poly& b)
{
    Poly product(a.size() + b.size() - 1, 0.0);
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < b.size(); ++j)
            product[i + j] += a[i] * b[j];
    return product;
}

Poly polyAdd(const Poly& a, const Poly& b, double scale)
{
    Poly sum(std::max(a.size(), b.size()), 0.0);
    for (std::size_t i = 0; i < a.size(); ++i)
        sum[i] += a[i];
    for (std::size_t i = 0; i < b.size(); ++i)
        sum[i] += scale * b[i];
    return sum;
}

Complex evaluate(const Poly& p, Complex z)
{
    Complex value{0.0, 0.0};
    for (std::size_t i = p.size(); i-- > 0;)
        value = value * z + p[i];
    return value;
}

/// Durand-Kerner (Weierstrass) simultaneous root finder. Degree 6 with well
/// conditioned elastic constants converges in a few dozen iterations; the
/// iteration cap is a backstop, not the expected exit.
std::vector<Complex> roots(const Poly& polynomial)
{
    Poly p = polynomial;
    while (p.size() > 1 && std::abs(p.back()) < 1e-14)
        p.pop_back();
    const std::size_t degree = p.size() - 1;
    if (degree == 0)
        return {};
    for (double& coefficient : p)
        coefficient /= p.back();

    std::vector<Complex> z(degree);
    const Complex seed{0.4, 0.9};
    Complex power{1.0, 0.0};
    for (std::size_t i = 0; i < degree; ++i) {
        power *= seed;
        z[i] = power;
    }

    for (int iteration = 0; iteration < 2000; ++iteration) {
        double shift = 0.0;
        for (std::size_t i = 0; i < degree; ++i) {
            Complex denominator{1.0, 0.0};
            for (std::size_t j = 0; j < degree; ++j)
                if (j != i)
                    denominator *= (z[i] - z[j]);
            if (std::abs(denominator) < 1e-30)
                continue;
            const Complex step = evaluate(p, z[i]) / denominator;
            z[i] -= step;
            shift = std::max(shift, std::abs(step));
        }
        if (shift < 1e-14)
            break;
    }
    return z;
}

// ---------------------------------------------------------------------------
// Stroh sextic solution
// ---------------------------------------------------------------------------

struct StrohSolution {
    std::array<Complex, 3> p{};                     ///< roots with Im p > 0
    std::array<std::array<Complex, 3>, 3> a{};      ///< a[alpha][k]
    std::array<Complex, 3> d{};                     ///< the D coefficients
    bool valid = false;
};

/// Solve the sextic and the boundary conditions for Burgers vector `b`.
/// Returns an invalid solution when the roots are degenerate (which is what
/// EXACT elastic isotropy produces: a triple root at p = i) or the boundary
/// system is singular — the caller retries with a lifted degeneracy.
StrohSolution solveStroh(const Voigt& c, const Vec3& b)
{
    StrohSolution solution;

    // Q_ik = C_i1k1, R_ik = C_i1k2, T_ik = C_i2k2.
    double q[3][3], r[3][3], t[3][3];
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) {
            q[i][k] = component(c, i, 0, k, 0);
            r[i][k] = component(c, i, 0, k, 1);
            t[i][k] = component(c, i, 1, k, 1);
        }

    // N(p) = Q + p (R + R^T) + p^2 T, entry-wise a quadratic in p.
    Poly n[3][3];
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k)
            n[i][k] = Poly{q[i][k], r[i][k] + r[k][i], t[i][k]};

    // det N(p), by cofactor expansion in polynomial arithmetic. Exact — no
    // sampling and re-fitting, which is where a determinant of quadratics
    // usually goes wrong.
    const Poly determinant = polyAdd(
        polyAdd(polyMultiply(n[0][0],
                             polyAdd(polyMultiply(n[1][1], n[2][2]),
                                     polyMultiply(n[1][2], n[2][1]), -1.0)),
                polyMultiply(n[0][1],
                             polyAdd(polyMultiply(n[1][0], n[2][2]),
                                     polyMultiply(n[1][2], n[2][0]), -1.0)),
                -1.0),
        polyMultiply(n[0][2], polyAdd(polyMultiply(n[1][0], n[2][1]),
                                      polyMultiply(n[1][1], n[2][0]), -1.0)),
        1.0);

    std::vector<Complex> all = roots(determinant);
    std::vector<Complex> upper;
    for (const Complex& root : all)
        if (root.imag() > 1e-9)
            upper.push_back(root);
    if (upper.size() != 3)
        return solution;
    // Degenerate roots make the three A vectors linearly dependent; the
    // formalism has no solution there and the 6x6 system below would return
    // numerical noise that looks like a displacement field.
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j)
            if (std::abs(upper[static_cast<std::size_t>(i)]
                         - upper[static_cast<std::size_t>(j)])
                < 1e-5)
                return solution;

    std::array<std::array<Complex, 3>, 3> bVectors{};
    for (int alpha = 0; alpha < 3; ++alpha) {
        const Complex p = upper[static_cast<std::size_t>(alpha)];
        std::array<std::array<Complex, 3>, 3> matrix{};
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < 3; ++k)
                matrix[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] =
                    q[i][k] + p * (r[i][k] + r[k][i]) + p * p * t[i][k];

        // Null vector: the cross product of the two most independent rows.
        std::array<Complex, 3> best{};
        double bestNorm = 0.0;
        for (int i = 0; i < 3; ++i)
            for (int j = i + 1; j < 3; ++j) {
                const auto candidate = bilinearCross(
                    matrix[static_cast<std::size_t>(i)],
                    matrix[static_cast<std::size_t>(j)]);
                if (const double n2 = norm(candidate); n2 > bestNorm) {
                    bestNorm = n2;
                    best = candidate;
                }
            }
        if (bestNorm < 1e-12)
            return solution;
        for (Complex& value : best)
            value /= bestNorm;

        solution.p[static_cast<std::size_t>(alpha)] = p;
        solution.a[static_cast<std::size_t>(alpha)] = best;
        // B = (R^T + p T) A: the traction the mode carries on the e2 plane.
        for (int i = 0; i < 3; ++i) {
            Complex sum{0.0, 0.0};
            for (int k = 0; k < 3; ++k)
                sum += (r[k][i] + p * t[i][k]) * best[static_cast<std::size_t>(k)];
            bVectors[static_cast<std::size_t>(alpha)][static_cast<std::size_t>(i)] =
                sum;
        }
    }

    // Two boundary conditions, six real equations:
    //   2 Re( sum_alpha A_k,alpha D_alpha ) = b_k     (the Burgers circuit)
    //   2 Re( sum_alpha B_k,alpha D_alpha ) = 0       (no net force on the cut)
    // Unknowns are (Re D, Im D), and Re(A D) = Re A Re D - Im A Im D.
    std::vector<std::vector<double>> system(6, std::vector<double>(6, 0.0));
    std::vector<double> rhs(6, 0.0);
    for (int k = 0; k < 3; ++k) {
        for (int alpha = 0; alpha < 3; ++alpha) {
            const Complex av =
                solution.a[static_cast<std::size_t>(alpha)]
                          [static_cast<std::size_t>(k)];
            const Complex bv =
                bVectors[static_cast<std::size_t>(alpha)]
                        [static_cast<std::size_t>(k)];
            system[static_cast<std::size_t>(k)][static_cast<std::size_t>(alpha)] =
                2.0 * av.real();
            system[static_cast<std::size_t>(k)]
                  [static_cast<std::size_t>(alpha + 3)] = -2.0 * av.imag();
            system[static_cast<std::size_t>(k + 3)]
                  [static_cast<std::size_t>(alpha)] = 2.0 * bv.real();
            system[static_cast<std::size_t>(k + 3)]
                  [static_cast<std::size_t>(alpha + 3)] = -2.0 * bv.imag();
        }
        rhs[static_cast<std::size_t>(k)] = axisComponent(b, k);
    }
    if (!solveLinear(system, rhs))
        return solution;
    for (int alpha = 0; alpha < 3; ++alpha)
        solution.d[static_cast<std::size_t>(alpha)] =
            Complex{rhs[static_cast<std::size_t>(alpha)],
                    rhs[static_cast<std::size_t>(alpha + 3)]};
    solution.valid = true;
    return solution;
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

struct Extent {
    double min = 0.0;
    double max = 0.0;
    double span() const { return max - min; }
};

Extent extentAlong(const Structure& s, const Vec3& direction)
{
    Extent extent{std::numeric_limits<double>::max(),
                  std::numeric_limits<double>::lowest()};
    for (const Atom& atom : s.atoms()) {
        const double projection = atom.position.dot(direction);
        extent.min = std::min(extent.min, projection);
        extent.max = std::max(extent.max, projection);
    }
    return extent;
}

/// Closest approach between any two atoms under the minimum-image convention.
/// O(N^2); capped, because this is a diagnostic and a builder that spends a
/// minute on it for a 50 000-atom cell has stopped being useful.
double minimumSeparation(const Structure& s, std::size_t cap)
{
    const auto& atoms = s.atoms();
    if (atoms.size() < 2 || atoms.size() > cap)
        return 0.0;
    const auto& cell = s.cell().vectors();
    const bool periodic = s.cell().isDefined();
    double best = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i + 1 < atoms.size(); ++i)
        for (std::size_t j = i + 1; j < atoms.size(); ++j) {
            const Vec3 raw = atoms[j].position - atoms[i].position;
            if (!periodic) {
                best = std::min(best, raw.norm());
                continue;
            }
            for (int u = -1; u <= 1; ++u)
                for (int v = -1; v <= 1; ++v)
                    for (int w = -1; w <= 1; ++w) {
                        const Vec3 delta =
                            raw - (cell[0] * u + cell[1] * v + cell[2] * w);
                        best = std::min(best, delta.norm());
                    }
        }
    return best == std::numeric_limits<double>::max() ? 0.0 : best;
}

} // namespace

// ---------------------------------------------------------------------------
// Public displacement fields
// ---------------------------------------------------------------------------

Vec3 DislocationBuilder::screwDisplacement(double x1, double x2, double burgers)
{
    // Anti-plane strain: the only non-zero component is along the line, and it
    // winds once by b around it.
    if (x1 == 0.0 && x2 == 0.0)
        return {};
    return {0.0, 0.0, burgers / (2.0 * kPi) * std::atan2(x2, x1)};
}

Vec3 DislocationBuilder::edgeDisplacement(double x1, double x2, double burgers,
                                          double poisson)
{
    const double r2 = x1 * x1 + x2 * x2;
    if (r2 < 1e-20)
        return {};
    const double factor = burgers / (2.0 * kPi);
    const double oneMinusNu = 1.0 - poisson;
    // atan2, not atan(x2/x1): the branch cut of atan(x2/x1) has period pi, so
    // the displacement would wind by b/2 per turn — half a dislocation, which
    // is not a defect that exists.
    const double u1 =
        factor * (std::atan2(x2, x1) + x1 * x2 / (2.0 * oneMinusNu * r2));
    const double u2 = -factor
        * ((1.0 - 2.0 * poisson) / (4.0 * oneMinusNu) * std::log(r2)
           + (x1 * x1 - x2 * x2) / (4.0 * oneMinusNu * r2));
    return {u1, u2, 0.0};
}

Vec3 DislocationBuilder::anisotropicDisplacement(double x1, double x2,
                                                 const Vec3& b,
                                                 const Voigt& voigt)
{
    if (x1 == 0.0 && x2 == 0.0)
        return {};

    // Exact elastic isotropy collapses the sextic to a triple root at p = i,
    // where Stroh's formalism is degenerate and has no solution of this form.
    // The standard remedy is to lift the degeneracy by a perturbation far
    // below any physically meaningful difference in the constants and far
    // above the numerical noise floor. Three attempts, growing: a tensor that
    // is only NEARLY degenerate needs a smaller nudge than an exactly
    // isotropic one, and using the largest one unconditionally would bias
    // every anisotropic answer by the amount needed for the worst case.
    StrohSolution solution = solveStroh(voigt, b);
    for (int attempt = 0; !solution.valid && attempt < 3; ++attempt) {
        Voigt lifted = voigt;
        const double epsilon = 1.0e-5 * std::pow(10.0, attempt);
        for (int k = 3; k < 6; ++k)
            lifted[static_cast<std::size_t>(k)][static_cast<std::size_t>(k)] *=
                1.0 + epsilon * (k - 2);
        solution = solveStroh(lifted, b);
    }
    if (!solution.valid)
        throw std::invalid_argument(
            "the elastic tensor does not admit a Stroh solution (is it "
            "positive definite?)");

    // u_k = (1/pi) Im[ sum_alpha A_k,alpha D_alpha ln(x1 + p_alpha x2) ]
    std::array<double, 3> u{};
    for (int alpha = 0; alpha < 3; ++alpha) {
        const Complex z =
            Complex{x1, 0.0} + solution.p[static_cast<std::size_t>(alpha)] * x2;
        const Complex logarithm = std::log(z);
        for (int k = 0; k < 3; ++k)
            u[static_cast<std::size_t>(k)] +=
                (solution.a[static_cast<std::size_t>(alpha)]
                           [static_cast<std::size_t>(k)]
                 * solution.d[static_cast<std::size_t>(alpha)] * logarithm)
                    .imag();
    }
    return {u[0] / kPi, u[1] / kPi, u[2] / kPi};
}

Voigt DislocationBuilder::elasticTensor(ElasticSymmetry symmetry, double c11,
                                        double c12, double c44, double c13,
                                        double c33)
{
    Voigt c{};
    switch (symmetry) {
    case ElasticSymmetry::Isotropic:
        // One independent shear: an isotropic solid has C44 fixed by the other
        // two, and accepting a third number would let a caller specify a
        // tensor that is not isotropic while asking for isotropy.
        c44 = 0.5 * (c11 - c12);
        [[fallthrough]];
    case ElasticSymmetry::Cubic:
        for (int i = 0; i < 3; ++i) {
            c[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)] = c11;
            c[static_cast<std::size_t>(i + 3)][static_cast<std::size_t>(i + 3)] =
                c44;
            for (int j = 0; j < 3; ++j)
                if (i != j)
                    c[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                        c12;
        }
        break;
    case ElasticSymmetry::Hexagonal:
        // Unique axis along z, matching the crystal frame the caller supplies.
        c[0][0] = c[1][1] = c11;
        c[2][2] = c33;
        c[0][1] = c[1][0] = c12;
        c[0][2] = c[2][0] = c[1][2] = c[2][1] = c13;
        c[3][3] = c[4][4] = c44;
        c[5][5] = 0.5 * (c11 - c12);
        break;
    }
    return c;
}

Voigt DislocationBuilder::rotateVoigt(const Voigt& voigt,
                                      const std::array<Vec3, 3>& frame)
{
    // C'_ijkl = Q_ia Q_jb Q_kc Q_ld C_abcd, with the rows of Q the new basis
    // vectors expressed in the old one.
    Voigt rotated{};
    for (int i = 0; i < 3; ++i)
        for (int j = i; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                for (int l = k; l < 3; ++l) {
                    double sum = 0.0;
                    for (int a = 0; a < 3; ++a)
                        for (int bIndex = 0; bIndex < 3; ++bIndex)
                            for (int cIndex = 0; cIndex < 3; ++cIndex)
                                for (int d = 0; d < 3; ++d)
                                    sum += axisComponent(
                                               frame[static_cast<std::size_t>(i)], a)
                                        * axisComponent(
                                               frame[static_cast<std::size_t>(j)],
                                               bIndex)
                                        * axisComponent(
                                               frame[static_cast<std::size_t>(k)],
                                               cIndex)
                                        * axisComponent(
                                               frame[static_cast<std::size_t>(l)], d)
                                        * component(voigt, a, bIndex, cIndex, d);
                    const auto row = static_cast<std::size_t>(voigtIndex(i, j));
                    const auto column = static_cast<std::size_t>(voigtIndex(k, l));
                    rotated[row][column] = sum;
                    rotated[column][row] = sum;
                }
    return rotated;
}

std::array<Vec3, 3> DislocationBuilder::frameFor(Axis axis)
{
    switch (axis) {
    case Axis::X: return {Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec3{1, 0, 0}};
    case Axis::Y: return {Vec3{0, 0, 1}, Vec3{1, 0, 0}, Vec3{0, 1, 0}};
    case Axis::Z: break;
    }
    return {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
}

std::string DislocationBuilder::toString(Type type)
{
    switch (type) {
    case Type::Screw:       return "screw";
    case Type::Glide:       return "glide dipole";
    case Type::Climb:       return "climb dipole";
    case Type::Anisotropic: return "anisotropic";
    case Type::Edge:        break;
    }
    return "edge";
}

std::string DislocationBuilder::toString(Axis axis)
{
    switch (axis) {
    case Axis::X: return "x";
    case Axis::Y: return "y";
    case Axis::Z: break;
    }
    return "z";
}

// ---------------------------------------------------------------------------
// generate()
// ---------------------------------------------------------------------------

DislocationBuilder::Result DislocationBuilder::generate(const Structure& source,
                                                        const Params& params)
{
    if (source.empty())
        throw std::invalid_argument("no atoms to displace");
    if (!source.cell().isDefined())
        throw std::invalid_argument(
            "a dislocation needs a periodic cell: the line runs along a "
            "lattice direction and the field is periodic along it");
    if (params.burgers <= 0.0)
        throw std::invalid_argument("the Burgers vector must have a length");
    if (params.poisson < 0.0 || params.poisson >= 0.5)
        throw std::invalid_argument(
            "the Poisson ratio must lie in [0, 0.5); 0.5 is incompressible and "
            "the edge field is singular there");

    const std::array<Vec3, 3> frame = frameFor(params.lineAxis);
    const Vec3& e1 = frame[0];
    const Vec3& e2 = frame[1];
    const Vec3& e3 = frame[2];

    const Extent span1 = extentAlong(source, e1);
    const Extent span2 = extentAlong(source, e2);
    const double c1 = span1.min + params.center[0] * span1.span();
    const double c2 = span2.min + params.center[1] * span2.span();

    const bool dipole =
        params.type == Type::Glide || params.type == Type::Climb;
    const bool alongE1 = params.type == Type::Glide;
    double separation = params.dipoleSeparation;
    if (dipole && separation <= 0.0)
        separation = (alongE1 ? span1.span() : span2.span()) / 3.0;
    if (dipole) {
        const double available = alongE1 ? span1.span() : span2.span();
        if (separation >= available)
            throw std::invalid_argument(
                "the two cores of the dipole are further apart than the cell "
                "is wide in that direction");
    }

    Result result;
    result.structure = source;
    const double sign = params.burgersSign >= 0 ? 1.0 : -1.0;
    const double b = params.burgers * sign;

    // -- Core positions -----------------------------------------------------
    struct Core {
        double u1 = 0.0; ///< along e1
        double u2 = 0.0; ///< along e2
        double sign = 1.0;
    };
    std::vector<Core> cores;
    if (!dipole) {
        cores.push_back({c1, c2, 1.0});
    } else if (alongE1) {
        cores.push_back({c1 - 0.5 * separation, c2, +1.0});
        cores.push_back({c1 + 0.5 * separation, c2, -1.0});
    } else {
        cores.push_back({c1, c2 - 0.5 * separation, +1.0});
        cores.push_back({c1, c2 + 0.5 * separation, -1.0});
    }

    // -- Climb removes material; that is what makes it climb ----------------
    //
    // The platelet is one Burgers vector thick, normal to e1, and spans the
    // gap between the two cores. Deleting it and letting the two opposite
    // fields close the hole is the collapsed vacancy disc that a climbing
    // edge dislocation leaves behind.
    if (params.type == Type::Climb) {
        const double low = cores[0].u2;
        const double high = cores[1].u2;
        const double thickness = std::abs(b);
        std::vector<std::size_t> doomed;
        const auto& atoms = result.structure.atoms();
        for (std::size_t i = 0; i < atoms.size(); ++i) {
            const double p1 = atoms[i].position.dot(e1);
            const double p2 = atoms[i].position.dot(e2);
            if (p1 >= c1 - thickness && p1 < c1 && p2 > low && p2 < high)
                doomed.push_back(i);
        }
        // Descending, so the indices behind each removal stay valid.
        for (std::size_t k = doomed.size(); k-- > 0;)
            result.structure.removeAtom(doomed[k]);
        result.atomsRemoved = static_cast<int>(doomed.size());
        if (doomed.empty())
            result.warnings.emplace_back(
                "the vacancy platelet caught no atoms — the Burgers vector is "
                "smaller than the spacing of the planes it was meant to "
                "remove, so this is an elastic dipole with no mass transport");
    }

    // -- Elastic field ------------------------------------------------------
    Voigt rotated{};
    Vec3 burgersFrame{};
    if (params.type == Type::Anisotropic) {
        const Voigt crystal =
            elasticTensor(params.symmetry, params.c11, params.c12, params.c44,
                          params.c13, params.c33);
        rotated = rotateVoigt(crystal, frame);
        Vec3 direction{params.burgersDirection[0], params.burgersDirection[1],
                       params.burgersDirection[2]};
        if (direction.norm() < 1e-12)
            throw std::invalid_argument(
                "the Burgers direction must not be the zero vector");
        burgersFrame = direction.normalized() * b;
    }

    const auto fieldAt = [&](double x1, double x2) -> Vec3 {
        switch (params.type) {
        case Type::Screw:
            return screwDisplacement(x1, x2, b);
        case Type::Anisotropic:
            return anisotropicDisplacement(x1, x2, burgersFrame, rotated);
        case Type::Edge:
        case Type::Glide:
        case Type::Climb:
            break;
        }
        return edgeDisplacement(x1, x2, b, params.poisson);
    };

    // The uniform distortion that makes a DIPOLE periodic again.
    //
    // Superposing two opposite fields cancels the net Burgers vector but
    // leaves the average plastic distortion the pair carries: the ribbon
    // between the cores has slipped, and the cell has to be sheared (glide) or
    // stretched (climb) by minus that average or the two faces of the cell no
    // longer match. Without this the construction looks right in a viewer and
    // has a b-sized discontinuity at the periodic boundary.
    //
    // beta^p = |b| A_cut / V, with A_cut = separation x (the cell's repeat
    // length along the line). Measured against the CELL, not against the
    // atoms' bounding box: the box is one lattice spacing short of the cell in
    // a periodic block, and compensating by that wrong fraction leaves a
    // residual strain of a few percent that nothing downstream would flag.
    const double lineLength = std::abs(
        source.cell().vectors()[static_cast<std::size_t>(params.lineAxis)].dot(e3));
    const double volume = source.cell().volume();
    const double plastic = dipole && volume > 1e-12
        ? std::abs(b) * separation * lineLength / volume
        : 0.0;

    double maxDisplacement = 0.0;
    for (Atom& atom : result.structure.atoms()) {
        const double p1 = atom.position.dot(e1);
        const double p2 = atom.position.dot(e2);
        Vec3 local{};
        for (const Core& core : cores) {
            const Vec3 u = fieldAt(p1 - core.u1, p2 - core.u2);
            local += u * core.sign;
        }
        if (dipole) {
            // Glide slips along e1 across planes normal to e2 (a simple
            // shear); climb opens material along e1 across planes normal to
            // e1 (a uniaxial strain). Both are applied about the cell centre
            // so the compensation does not also translate the crystal.
            if (alongE1)
                local += e1 * (-plastic * (p2 - (span2.min + 0.5 * span2.span())));
            else
                local += e1 * (-plastic * (p1 - (span1.min + 0.5 * span1.span())));
        }
        const Vec3 displacement =
            e1 * local.x + e2 * local.y + e3 * local.z;
        maxDisplacement = std::max(maxDisplacement, displacement.norm());
        atom.position += displacement;
    }
    result.maxDisplacement = maxDisplacement;

    // The cell has to take the same distortion the atoms did, or the atoms sit
    // in a box that no longer describes them.
    if (dipole) {
        auto vectors = result.structure.cell().vectors();
        for (Vec3& vector : vectors) {
            const double v1 = vector.dot(e1);
            const double v2 = vector.dot(e2);
            vector += e1 * (-plastic * (alongE1 ? v2 : v1));
        }
        UnitCell cell = result.structure.cell();
        cell.setVectors(vectors);
        result.structure.setCell(cell);
    }

    if (params.wrapIntoCell) {
        const UnitCell& cell = result.structure.cell();
        for (Atom& atom : result.structure.atoms()) {
            Vec3 fractional = cell.cartesianToFractional(atom.position);
            fractional.x -= std::floor(fractional.x);
            fractional.y -= std::floor(fractional.y);
            fractional.z -= std::floor(fractional.z);
            atom.position = cell.fractionalToCartesian(fractional);
        }
    }

    // -- Periodicity --------------------------------------------------------
    //
    // A single dislocation cannot be made periodic normal to its line: its
    // field winds by b around the line, so the two faces of the cell differ by
    // b however the cell is chosen. Saying so in the pbc flags is more useful
    // than leaving a cell that claims a periodicity it does not have.
    if (!dipole) {
        std::array<bool, 3> pbc{false, false, false};
        pbc[static_cast<std::size_t>(params.lineAxis)] = true;
        UnitCell cell = result.structure.cell();
        cell.setPbc(pbc);
        result.structure.setCell(cell);
        result.warnings.emplace_back(
            "a single dislocation is periodic only along its line; the two "
            "lateral directions are now free surfaces. For a fully periodic "
            "cell use a Glide or Climb dipole, whose Burgers vectors cancel");
    }

    for (const Core& core : cores) {
        const Vec3 position = e1 * core.u1 + e2 * core.u2;
        result.cores.emplace_back(position, core.sign > 0 ? +1 : -1);
        result.netBurgers += (params.type == Type::Anisotropic
                                  ? (e1 * burgersFrame.x + e2 * burgersFrame.y
                                     + e3 * burgersFrame.z)
                                  : (params.type == Type::Screw ? e3 * b
                                                                : e1 * b))
            * core.sign;
    }

    result.minSeparation = minimumSeparation(result.structure, 6000);
    if (result.minSeparation > 0.0 && result.minSeparation < 0.5)
        result.warnings.emplace_back(
            "two atoms ended up closer than 0.5 Å. Linear elasticity is "
            "singular at the line, so the innermost atoms are placed by a "
            "formula that does not apply to them — relax the core, or move "
            "the line off the atomic positions");

    std::ostringstream description;
    description << toString(params.type) << " dislocation, line along "
                << toString(params.lineAxis) << ", |b| = " << params.burgers
                << " Å";
    if (dipole)
        description << ", cores " << separation << " Å apart";
    result.description = description.str();
    return result;
}

} // namespace calango::core
