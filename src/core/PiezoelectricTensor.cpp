#include "core/PiezoelectricTensor.hpp"

#include <cmath>

namespace calango::core {

std::vector<double> unwrapPhaseBranch(std::vector<double> phases, double period)
{
    if (phases.size() < 2)
        return phases;

    std::vector<double> out = phases;
    double correction = 0.0;
    for (std::size_t i = 1; i < phases.size(); ++i) {
        const double delta = phases[i] - phases[i - 1];
        // Wrap `delta` into (-period/2, period/2].
        double wrapped = std::fmod(delta + period / 2.0, period);
        if (wrapped < 0.0)
            wrapped += period;
        wrapped -= period / 2.0;
        correction += wrapped - delta;
        out[i] = phases[i] + correction;
    }
    out[0] = phases[0];
    return out;
}

double linearFitSlope(const std::vector<double>& x, const std::vector<double>& y)
{
    const auto n = static_cast<double>(x.size());
    if (n < 2.0)
        return 0.0;

    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumXX = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        sumX += x[i];
        sumY += y[i];
        sumXY += x[i] * y[i];
        sumXX += x[i] * x[i];
    }
    const double denom = n * sumXX - sumX * sumX;
    if (std::abs(denom) < 1e-300) // every x identical: no slope information
        return 0.0;
    return (n * sumXY - sumX * sumY) / denom;
}

Matrix3x6 properPiezoelectricCorrection(const std::array<double, 3>& p0)
{
    Matrix3x6 correction{};
    for (int alpha = 0; alpha < 6; ++alpha) {
        const int j = kVoigtPairs[static_cast<std::size_t>(alpha)][0];
        const int k = kVoigtPairs[static_cast<std::size_t>(alpha)][1];
        for (int i = 0; i < 3; ++i) {
            const double deltaJk = (j == k) ? 1.0 : 0.0;
            const double deltaIj = (i == j) ? 1.0 : 0.0;
            const double deltaIk = (i == k) ? 1.0 : 0.0;
            correction[static_cast<std::size_t>(i)][static_cast<std::size_t>(alpha)] =
                deltaJk * p0[static_cast<std::size_t>(i)]
                - 0.5 * (deltaIj * p0[static_cast<std::size_t>(k)]
                         + deltaIk * p0[static_cast<std::size_t>(j)]);
        }
    }
    return correction;
}

namespace {

using Tensor3 = std::array<std::array<std::array<double, 3>, 3>, 3>;

/// Expand a Voigt e_i,alpha into the full (symmetric-in-jk) Cartesian
/// rank-3 tensor e_ijk. No scale factors: the piezoelectric tensor's Voigt
/// reduction, unlike the elastic stiffness tensor's, needs none — it
/// couples to exactly one (already Voigt-doubled) strain index, so the
/// factor of two that turns tensor strain into engineering strain is
/// already accounted for on the strain side, not the tensor side.
Tensor3 expandVoigtToCartesian(const Matrix3x6& v)
{
    Tensor3 t{};
    for (int alpha = 0; alpha < 6; ++alpha) {
        const int j = kVoigtPairs[static_cast<std::size_t>(alpha)][0];
        const int k = kVoigtPairs[static_cast<std::size_t>(alpha)][1];
        for (int i = 0; i < 3; ++i) {
            const double value = v[static_cast<std::size_t>(i)][static_cast<std::size_t>(alpha)];
            t[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)][static_cast<std::size_t>(k)] = value;
            t[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)][static_cast<std::size_t>(j)] = value;
        }
    }
    return t;
}

Matrix3x6 contractCartesianToVoigt(const Tensor3& t)
{
    Matrix3x6 v{};
    for (int alpha = 0; alpha < 6; ++alpha) {
        const int j = kVoigtPairs[static_cast<std::size_t>(alpha)][0];
        const int k = kVoigtPairs[static_cast<std::size_t>(alpha)][1];
        for (int i = 0; i < 3; ++i)
            v[static_cast<std::size_t>(i)][static_cast<std::size_t>(alpha)] =
                t[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)][static_cast<std::size_t>(k)];
    }
    return v;
}

/// e'_ijk = R_ii' R_jj' R_kk' e_i'j'k' — the definition of how a rank-3
/// Cartesian tensor transforms under an orthogonal map R (rotation OR
/// rotoinversion; the formula does not care which).
Tensor3 rotateTensor(const Tensor3& e, const Matrix3& r)
{
    Tensor3 out{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) {
                double sum = 0.0;
                for (int ip = 0; ip < 3; ++ip)
                    for (int jp = 0; jp < 3; ++jp)
                        for (int kp = 0; kp < 3; ++kp)
                            sum += r[static_cast<std::size_t>(i)][static_cast<std::size_t>(ip)]
                                * r[static_cast<std::size_t>(j)][static_cast<std::size_t>(jp)]
                                * r[static_cast<std::size_t>(k)][static_cast<std::size_t>(kp)]
                                * e[static_cast<std::size_t>(ip)][static_cast<std::size_t>(jp)][static_cast<std::size_t>(kp)];
                out[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)][static_cast<std::size_t>(k)] = sum;
            }
    return out;
}

} // namespace

Matrix3x6 symmetrizePiezoelectricTensor(const Matrix3x6& raw,
                                        const std::vector<Matrix3>& pointGroupOps)
{
    if (pointGroupOps.empty())
        return raw;

    const Tensor3 cart = expandVoigtToCartesian(raw);
    Tensor3 sum{};
    for (const auto& r : pointGroupOps) {
        const Tensor3 rotated = rotateTensor(cart, r);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k)
                    sum[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)][static_cast<std::size_t>(k)] +=
                        rotated[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)][static_cast<std::size_t>(k)];
    }
    const double count = static_cast<double>(pointGroupOps.size());
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                sum[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)][static_cast<std::size_t>(k)] /= count;
    return contractCartesianToVoigt(sum);
}

bool containsInversion(const std::vector<Matrix3>& ops, double tolerance)
{
    for (const auto& r : ops) {
        bool isMinusIdentity = true;
        for (int i = 0; i < 3 && isMinusIdentity; ++i)
            for (int j = 0; j < 3 && isMinusIdentity; ++j) {
                const double expected = (i == j) ? -1.0 : 0.0;
                if (std::abs(r[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] - expected) > tolerance)
                    isMinusIdentity = false;
            }
        if (isMinusIdentity)
            return true;
    }
    return false;
}

std::optional<Matrix6x6> invert6x6(const Matrix6x6& c)
{
    // Augmented [C | I], Gauss-Jordan with partial pivoting.
    std::array<std::array<double, 12>, 6> aug{};
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j)
            aug[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                c[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
        aug[static_cast<std::size_t>(i)][static_cast<std::size_t>(6 + i)] = 1.0;
    }

    for (int col = 0; col < 6; ++col) {
        int pivotRow = col;
        double pivotMag = std::abs(aug[static_cast<std::size_t>(col)][static_cast<std::size_t>(col)]);
        for (int row = col + 1; row < 6; ++row) {
            const double mag = std::abs(aug[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)]);
            if (mag > pivotMag) {
                pivotMag = mag;
                pivotRow = row;
            }
        }
        if (pivotMag < 1e-12)
            return std::nullopt; // singular to working precision
        if (pivotRow != col)
            std::swap(aug[static_cast<std::size_t>(col)], aug[static_cast<std::size_t>(pivotRow)]);

        const double pivot = aug[static_cast<std::size_t>(col)][static_cast<std::size_t>(col)];
        for (int j = 0; j < 12; ++j)
            aug[static_cast<std::size_t>(col)][static_cast<std::size_t>(j)] /= pivot;

        for (int row = 0; row < 6; ++row) {
            if (row == col)
                continue;
            const double factor = aug[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
            if (factor == 0.0)
                continue;
            for (int j = 0; j < 12; ++j)
                aug[static_cast<std::size_t>(row)][static_cast<std::size_t>(j)] -=
                    factor * aug[static_cast<std::size_t>(col)][static_cast<std::size_t>(j)];
        }
    }

    Matrix6x6 inv{};
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j)
            inv[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                aug[static_cast<std::size_t>(i)][static_cast<std::size_t>(6 + j)];
    return inv;
}

Matrix3x6 stressToStrainPiezoelectricTensor(const Matrix3x6& e, const Matrix6x6& compliance)
{
    Matrix3x6 d{};
    for (int i = 0; i < 3; ++i)
        for (int alpha = 0; alpha < 6; ++alpha) {
            double sum = 0.0;
            for (int beta = 0; beta < 6; ++beta)
                sum += e[static_cast<std::size_t>(i)][static_cast<std::size_t>(beta)]
                    * compliance[static_cast<std::size_t>(beta)][static_cast<std::size_t>(alpha)];
            d[static_cast<std::size_t>(i)][static_cast<std::size_t>(alpha)] = sum;
        }
    return d;
}

} // namespace calango::core
