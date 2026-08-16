#include "core/ElasticTensor.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

namespace calango::core {

double quadraticCurvature(const std::vector<double>& x, const std::vector<double>& y)
{
    if (x.size() != y.size() || x.size() < 3)
        return 0.0;
    // At least 3 DISTINCT x values are needed for a,b,c to be determined —
    // a repeated-x set (e.g. every point at eps=0) has no curvature
    // information at all.
    if (std::set<double>(x.begin(), x.end()).size() < 3)
        return 0.0;

    double sx = 0.0, sx2 = 0.0, sx3 = 0.0, sx4 = 0.0;
    double sy = 0.0, sxy = 0.0, sx2y = 0.0;
    const double n = static_cast<double>(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double xi = x[i], yi = y[i];
        const double xi2 = xi * xi;
        sx += xi;
        sx2 += xi2;
        sx3 += xi2 * xi;
        sx4 += xi2 * xi2;
        sy += yi;
        sxy += xi * yi;
        sx2y += xi2 * yi;
    }

    // Normal equations for y = a + b*x + c*x^2, solved by Cramer's rule:
    //   [ n   sx  sx2 ] [a]   [sy  ]
    //   [ sx  sx2 sx3 ] [b] = [sxy ]
    //   [ sx2 sx3 sx4 ] [c]   [sx2y]
    const double a11 = n, a12 = sx, a13 = sx2;
    const double a21 = sx, a22 = sx2, a23 = sx3;
    const double a31 = sx2, a32 = sx3, a33 = sx4;
    const double det = a11 * (a22 * a33 - a23 * a32) - a12 * (a21 * a33 - a23 * a31)
        + a13 * (a21 * a32 - a22 * a31);
    if (std::abs(det) < 1e-300)
        return 0.0;

    // Replace the third column with the right-hand side to solve for c
    // directly (Cramer's rule needs only this one determinant beyond det).
    const double detC = a11 * (a22 * sx2y - sxy * a32) - a12 * (a21 * sx2y - sxy * a31)
        + sy * (a21 * a32 - a22 * a31);
    const double c = detC / det;
    return 2.0 * c;
}

double crossCurvature(double ePlusPlus, double ePlusMinus, double eMinusPlus,
                      double eMinusMinus, double magnitude)
{
    if (magnitude == 0.0)
        return 0.0;
    return (ePlusPlus - ePlusMinus - eMinusPlus + eMinusMinus) / (4.0 * magnitude * magnitude);
}

std::array<double, 6> symmetricEigenvalues6x6(const Matrix6x6& c)
{
    double a[6][6];
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j)
            a[i][j] = c[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];

    // Classic cyclic Jacobi rotation: repeatedly zero the largest-magnitude
    // off-diagonal pair until the off-diagonal Frobenius norm is negligible.
    // A small in-repo solver (like invert6x6's Gauss-Jordan) rather than a
    // linear-algebra dependency, since this module only ever diagonalizes
    // this one fixed, tiny size.
    for (int sweep = 0; sweep < 100; ++sweep) {
        double offDiagSum = 0.0;
        for (int p = 0; p < 6; ++p)
            for (int q = p + 1; q < 6; ++q)
                offDiagSum += a[p][q] * a[p][q];
        if (offDiagSum < 1e-24)
            break;
        for (int p = 0; p < 5; ++p) {
            for (int q = p + 1; q < 6; ++q) {
                if (std::abs(a[p][q]) < 1e-300)
                    continue;
                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double sign = theta >= 0.0 ? 1.0 : -1.0;
                const double t = sign / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                const double cTheta = 1.0 / std::sqrt(t * t + 1.0);
                const double sTheta = t * cTheta;
                const double app = a[p][p], aqq = a[q][q], apq = a[p][q];
                a[p][p] = app - t * apq;
                a[q][q] = aqq + t * apq;
                a[p][q] = 0.0;
                a[q][p] = 0.0;
                for (int i = 0; i < 6; ++i) {
                    if (i == p || i == q)
                        continue;
                    const double aip = a[i][p], aiq = a[i][q];
                    a[i][p] = cTheta * aip - sTheta * aiq;
                    a[p][i] = a[i][p];
                    a[i][q] = sTheta * aip + cTheta * aiq;
                    a[q][i] = a[i][q];
                }
            }
        }
    }

    std::array<double, 6> eig{};
    for (int i = 0; i < 6; ++i)
        eig[static_cast<std::size_t>(i)] = a[i][i];
    std::sort(eig.begin(), eig.end());
    return eig;
}

CrystalClass classifyPointGroup(const std::string& pg)
{
    static const std::vector<std::string> kCubic
        = {"23", "m-3", "432", "-43m", "m-3m"};
    static const std::vector<std::string> kHexagonal
        = {"6", "-6", "6/m", "622", "6mm", "-6m2", "6/mmm"};
    static const std::vector<std::string> kTrigonal
        = {"3", "-3", "32", "3m", "-3m"};
    static const std::vector<std::string> kTetragonal
        = {"4", "-4", "4/m", "422", "4mm", "-42m", "4/mmm"};
    static const std::vector<std::string> kOrthorhombic = {"222", "mm2", "mmm"};

    auto has = [&](const std::vector<std::string>& set) {
        return std::find(set.begin(), set.end(), pg) != set.end();
    };
    if (has(kCubic))
        return CrystalClass::Cubic;
    if (has(kHexagonal))
        return CrystalClass::Hexagonal;
    if (has(kTrigonal))
        return CrystalClass::Trigonal;
    if (has(kTetragonal))
        return CrystalClass::Tetragonal;
    if (has(kOrthorhombic))
        return CrystalClass::Orthorhombic;
    return CrystalClass::Other;
}

namespace {

BornCriterion makeCriterion(std::string expr, double value)
{
    BornCriterion out;
    out.expression = std::move(expr);
    out.value = value;
    out.satisfied = value > 0.0;
    return out;
}

// Convenience accessors: c[i][j] is 0-based, so C11 == c[0][0], C12 ==
// c[0][1], etc. — the standard Voigt 1-based labels used in the doc
// comments map to these as Cxy(1-based) -> c[x-1][y-1].
double at(const Matrix6x6& c, int i, int j)
{
    return c[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
}

} // namespace

std::vector<BornCriterion> bornCriteriaForClass(const Matrix6x6& c, CrystalClass crystalClass)
{
    const double c11 = at(c, 0, 0), c12 = at(c, 0, 1), c13 = at(c, 0, 2);
    const double c14 = at(c, 0, 3);
    const double c22 = at(c, 1, 1), c23 = at(c, 1, 2);
    const double c33 = at(c, 2, 2);
    const double c44 = at(c, 3, 3), c55 = at(c, 4, 4), c66 = at(c, 5, 5);

    std::vector<BornCriterion> out;
    switch (crystalClass) {
    case CrystalClass::Cubic:
        out.push_back(makeCriterion("C11 - C12", c11 - c12));
        out.push_back(makeCriterion("C11 + 2*C12", c11 + 2.0 * c12));
        out.push_back(makeCriterion("C44", c44));
        break;
    case CrystalClass::Hexagonal:
        out.push_back(makeCriterion("C11 - C12", c11 - c12));
        out.push_back(makeCriterion("C33*(C11+C12) - 2*C13^2",
            c33 * (c11 + c12) - 2.0 * c13 * c13));
        out.push_back(makeCriterion("C44", c44));
        break;
    case CrystalClass::Tetragonal:
        out.push_back(makeCriterion("C11 - C12", c11 - c12));
        out.push_back(makeCriterion("C33*(C11+C12) - 2*C13^2",
            c33 * (c11 + c12) - 2.0 * c13 * c13));
        out.push_back(makeCriterion("C44", c44));
        out.push_back(makeCriterion("C66", c66));
        break;
    case CrystalClass::Trigonal:
        out.push_back(makeCriterion("C11 - C12", c11 - c12));
        out.push_back(makeCriterion("C44", c44));
        out.push_back(makeCriterion("C33*(C11+C12) - 2*C13^2",
            c33 * (c11 + c12) - 2.0 * c13 * c13));
        out.push_back(makeCriterion("C44*(C11-C12) - 2*C14^2",
            c44 * (c11 - c12) - 2.0 * c14 * c14));
        break;
    case CrystalClass::Orthorhombic:
        out.push_back(makeCriterion("C11", c11));
        out.push_back(makeCriterion("C11*C22 - C12^2", c11 * c22 - c12 * c12));
        out.push_back(makeCriterion(
            "C11*C22*C33 + 2*C12*C13*C23 - C11*C23^2 - C22*C13^2 - C33*C12^2",
            c11 * c22 * c33 + 2.0 * c12 * c13 * c23 - c11 * c23 * c23
                - c22 * c13 * c13 - c33 * c12 * c12));
        out.push_back(makeCriterion("C44", c44));
        out.push_back(makeCriterion("C55", c55));
        out.push_back(makeCriterion("C66", c66));
        break;
    case CrystalClass::Other:
        break;
    }
    return out;
}

ElasticModuli computeElasticModuli(const Matrix6x6& c)
{
    ElasticModuli m;
    const double c11 = at(c, 0, 0), c22 = at(c, 1, 1), c33 = at(c, 2, 2);
    const double c12 = at(c, 0, 1), c13 = at(c, 0, 2), c23 = at(c, 1, 2);
    const double c44 = at(c, 3, 3), c55 = at(c, 4, 4), c66 = at(c, 5, 5);

    m.bulkVoigtGPa = ((c11 + c22 + c33) + 2.0 * (c12 + c13 + c23)) / 9.0;
    m.shearVoigtGPa = ((c11 + c22 + c33) - (c12 + c13 + c23) + 3.0 * (c44 + c55 + c66)) / 15.0;

    const auto s = invert6x6(c);
    if (!s) {
        m.reussValid = false;
        return m;
    }
    const double s11 = at(*s, 0, 0), s22 = at(*s, 1, 1), s33 = at(*s, 2, 2);
    const double s12 = at(*s, 0, 1), s13 = at(*s, 0, 2), s23 = at(*s, 1, 2);
    const double s44 = at(*s, 3, 3), s55 = at(*s, 4, 4), s66 = at(*s, 5, 5);

    const double reussBulkDenominator = (s11 + s22 + s33) + 2.0 * (s12 + s13 + s23);
    if (std::abs(reussBulkDenominator) < 1e-300) {
        m.reussValid = false;
        return m;
    }
    m.bulkReussGPa = 1.0 / reussBulkDenominator;

    const double reussShearNumerator
        = 4.0 * (s11 + s22 + s33) - 4.0 * (s12 + s13 + s23) + 3.0 * (s44 + s55 + s66);
    if (std::abs(reussShearNumerator) < 1e-300) {
        m.reussValid = false;
        return m;
    }
    m.shearReussGPa = 15.0 / reussShearNumerator;

    m.bulkHillGPa = 0.5 * (m.bulkVoigtGPa + m.bulkReussGPa);
    m.shearHillGPa = 0.5 * (m.shearVoigtGPa + m.shearReussGPa);

    const double denom = 3.0 * m.bulkHillGPa + m.shearHillGPa;
    if (std::abs(denom) < 1e-300) {
        m.reussValid = false;
        return m;
    }
    m.youngHillGPa = 9.0 * m.bulkHillGPa * m.shearHillGPa / denom;
    m.poissonHill = (3.0 * m.bulkHillGPa - 2.0 * m.shearHillGPa) / (2.0 * denom);
    return m;
}

Matrix6x6 symmetrizeElasticTensor(const Matrix6x6& raw, const std::vector<Matrix3>& pointGroupOps)
{
    if (pointGroupOps.empty())
        return raw;

    // Expand Voigt -> the full, doubly-pair-symmetric rank-4 Cartesian
    // tensor C_ijkl (symmetric under i<->j, under k<->l, and under swapping
    // the two pairs (ij)<->(kl), since C itself is a symmetric 6x6 matrix).
    double cart[3][3][3][3] = {};
    for (int a = 0; a < 6; ++a) {
        const int i = kVoigtPairs[static_cast<std::size_t>(a)][0];
        const int j = kVoigtPairs[static_cast<std::size_t>(a)][1];
        for (int b = 0; b < 6; ++b) {
            const int k = kVoigtPairs[static_cast<std::size_t>(b)][0];
            const int l = kVoigtPairs[static_cast<std::size_t>(b)][1];
            const double v = raw[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)];
            cart[i][j][k][l] = v;
            cart[j][i][k][l] = v;
            cart[i][j][l][k] = v;
            cart[j][i][l][k] = v;
        }
    }

    double total[3][3][3][3] = {};
    for (const Matrix3& r : pointGroupOps) {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k)
                    for (int l = 0; l < 3; ++l) {
                        double sum = 0.0;
                        for (int p = 0; p < 3; ++p)
                            for (int q = 0; q < 3; ++q)
                                for (int s = 0; s < 3; ++s)
                                    for (int t = 0; t < 3; ++t)
                                        sum += r[i][p] * r[j][q] * r[k][s] * r[l][t]
                                            * cart[p][q][s][t];
                        total[i][j][k][l] += sum;
                    }
    }

    Matrix6x6 out{};
    const double n = static_cast<double>(pointGroupOps.size());
    for (int a = 0; a < 6; ++a) {
        const int i = kVoigtPairs[static_cast<std::size_t>(a)][0];
        const int j = kVoigtPairs[static_cast<std::size_t>(a)][1];
        for (int b = 0; b < 6; ++b) {
            const int k = kVoigtPairs[static_cast<std::size_t>(b)][0];
            const int l = kVoigtPairs[static_cast<std::size_t>(b)][1];
            out[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)] = total[i][j][k][l] / n;
        }
    }
    return out;
}

ElasticModuli2D computeElasticModuli2D(double c11, double c22, double c12)
{
    ElasticModuli2D m;
    const double det = c11 * c22 - c12 * c12;
    if (std::abs(c22) > 1e-300)
        m.youngXNPerM = det / c22;
    if (std::abs(c11) > 1e-300)
        m.youngYNPerM = det / c11;
    if (std::abs(c22) > 1e-300)
        m.poissonXY = c12 / c22;
    if (std::abs(c11) > 1e-300)
        m.poissonYX = c12 / c11;
    m.layerModulusNPerM = (c11 + c22 + 2.0 * c12) / 4.0;
    return m;
}

BornStability2D bornStability2D(double c11, double c22, double c12, double c66)
{
    BornStability2D result;
    result.positiveDefinite = makeCriterion("C11*C22 - C12^2", c11 * c22 - c12 * c12);
    result.shearPositive = makeCriterion("C66", c66);
    result.stable = result.positiveDefinite.satisfied && result.shearPositive.satisfied && c11 > 0.0;
    return result;
}

} // namespace calango::core
