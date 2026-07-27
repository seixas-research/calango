#include "core/BandUnfolding.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace calango::core {

int SupercellMatrix::determinant() const
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
        - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

namespace {

/// Inverse of a 3x3 given as row vectors; returns false when singular.
bool invert3x3(const std::array<Vec3, 3>& rows, std::array<Vec3, 3>& inverse)
{
    const double a = rows[0].x, b = rows[0].y, c = rows[0].z;
    const double d = rows[1].x, e = rows[1].y, f = rows[1].z;
    const double g = rows[2].x, h = rows[2].y, i = rows[2].z;
    const double det = a * (e * i - f * h) - b * (d * i - f * g)
        + c * (d * h - e * g);
    if (std::abs(det) < 1e-12)
        return false;
    const double inv = 1.0 / det;
    inverse[0] = {(e * i - f * h) * inv, (c * h - b * i) * inv,
                  (b * f - c * e) * inv};
    inverse[1] = {(f * g - d * i) * inv, (a * i - c * g) * inv,
                  (c * d - a * f) * inv};
    inverse[2] = {(d * h - e * g) * inv, (b * g - a * h) * inv,
                  (a * e - b * d) * inv};
    return true;
}

/// Reduce a fractional coordinate into [-0.5, 0.5), the first Brillouin zone
/// in fractional terms.
double reduceToFirstZone(double x)
{
    double reduced = x - std::floor(x); // -> [0, 1)
    if (reduced >= 0.5)
        reduced -= 1.0;
    return reduced;
}

} // namespace

SupercellMatrix deduceSupercellMatrix(const UnitCell& primitive,
                                      const UnitCell& supercell,
                                      double* residual)
{
    SupercellMatrix result;
    if (residual)
        *residual = 0.0;

    std::array<Vec3, 3> primitiveInverse{};
    if (!invert3x3(primitive.vectors(), primitiveInverse)) {
        if (residual)
            *residual = std::numeric_limits<double>::infinity();
        return result; // identity; the caller rejects it on the residual
    }

    // M = S · P⁻¹, with both cells stored as row vectors (ASE convention).
    // Row i of M is row i of S expressed in the primitive basis.
    const auto& s = supercell.vectors();
    const auto component = [](const Vec3& v, int index) {
        return index == 0 ? v.x : index == 1 ? v.y : v.z;
    };
    double worst = 0.0;
    for (int i = 0; i < 3; ++i) {
        const Vec3& row = s[static_cast<std::size_t>(i)];
        for (int j = 0; j < 3; ++j) {
            const double exact = row.x * component(primitiveInverse[0], j)
                + row.y * component(primitiveInverse[1], j)
                + row.z * component(primitiveInverse[2], j);
            const double rounded = std::round(exact);
            // The residual is the caller's commensurability check: a supercell
            // that is not an integer multiple of the primitive cell cannot be
            // unfolded, and silently rounding would produce a plausible-looking
            // but meaningless band structure.
            worst = std::max(worst, std::abs(exact - rounded));
            result.m[i][j] = static_cast<int>(rounded);
        }
    }
    if (residual)
        *residual = worst;
    return result;
}

bool forceCommensuratePrimitive(const UnitCell& supercell,
                                const SupercellMatrix& matrix,
                                const UnitCell& originalPrimitive,
                                UnitCell& forcedPrimitive, double* strain)
{
    if (!matrix.valid())
        return false;

    // M as doubles, then inverted: P = M⁻¹ · S. Inverting the integer matrix
    // rather than solving per row keeps this symmetric with the deduction
    // above, which computed M = S · P⁻¹ the same way.
    std::array<Vec3, 3> m{};
    for (int i = 0; i < 3; ++i) {
        m[static_cast<std::size_t>(i)] = {
            static_cast<double>(matrix.m[i][0]),
            static_cast<double>(matrix.m[i][1]),
            static_cast<double>(matrix.m[i][2])};
    }
    std::array<Vec3, 3> inverse{};
    if (!invert3x3(m, inverse))
        return false;

    const auto& s = supercell.vectors();
    std::array<Vec3, 3> forced{};
    for (int i = 0; i < 3; ++i) {
        const Vec3& row = inverse[static_cast<std::size_t>(i)];
        forced[static_cast<std::size_t>(i)] =
            s[0] * row.x + s[1] * row.y + s[2] * row.z;
    }
    forcedPrimitive = UnitCell(forced[0], forced[1], forced[2]);

    if (strain) {
        // Relative length change per vector, worst case. Lengths rather than
        // components, because a rotation between the two settings is not a
        // deformation and should not be reported as one.
        double worst = 0.0;
        const auto& original = originalPrimitive.vectors();
        for (int i = 0; i < 3; ++i) {
            const double before = original[static_cast<std::size_t>(i)].norm();
            const double after = forced[static_cast<std::size_t>(i)].norm();
            if (before > 1e-12)
                worst = std::max(worst, std::abs(after - before) / before);
        }
        *strain = worst;
    }
    return true;
}

Vec3 foldToSupercell(const Vec3& kPrimitive, const SupercellMatrix& matrix)
{
    // Reciprocal-space transformation is the TRANSPOSE of the real-space one:
    // with S = M·P in real space, the supercell reciprocal vectors satisfy
    // b_super = (M⁻¹)ᵀ · b_prim, so fractional coordinates transform as
    // K_frac = Mᵀ · k_frac. Using M instead of Mᵀ is the classic error here
    // and only shows up for non-diagonal supercells.
    const double kx = kPrimitive.x, ky = kPrimitive.y, kz = kPrimitive.z;
    const Vec3 folded{
        matrix.m[0][0] * kx + matrix.m[1][0] * ky + matrix.m[2][0] * kz,
        matrix.m[0][1] * kx + matrix.m[1][1] * ky + matrix.m[2][1] * kz,
        matrix.m[0][2] * kx + matrix.m[1][2] * ky + matrix.m[2][2] * kz};
    return {reduceToFirstZone(folded.x), reduceToFirstZone(folded.y),
            reduceToFirstZone(folded.z)};
}

SpectralFunction computeSpectralFunction(
    const std::vector<UnfoldedColumn>& columns,
    const SpectralFunctionOptions& options)
{
    SpectralFunction result;
    const int bins = std::max(2, options.energyBins);
    const double span = options.energyMax - options.energyMin;
    if (columns.empty() || span <= 0.0)
        return result;

    const double sigma = std::max(options.sigma, 1e-6);
    const double norm = 1.0 / (sigma * std::sqrt(2.0 * M_PI));
    const double twoSigmaSquared = 2.0 * sigma * sigma;
    // A Gaussian is numerically dead past ~4 sigma; limiting each state to
    // that window turns an O(states x bins) sum into a local one, which
    // matters when a supercell contributes thousands of bands per k-point.
    const double cutoff = 4.0 * sigma;

    result.energies.resize(static_cast<std::size_t>(bins));
    const double step = span / (bins - 1);
    for (int b = 0; b < bins; ++b)
        result.energies[static_cast<std::size_t>(b)] = options.energyMin + b * step;

    result.pathCoordinates.reserve(columns.size());
    result.intensity.reserve(columns.size());
    for (const UnfoldedColumn& column : columns) {
        result.pathCoordinates.push_back(column.pathCoordinate);
        std::vector<double> profile(static_cast<std::size_t>(bins), 0.0);
        for (const UnfoldedState& state : column.states) {
            if (state.weight < options.weightThreshold)
                continue; // negligible primitive character: not drawn
            const double lo = state.energy - cutoff;
            const double hi = state.energy + cutoff;
            if (hi < options.energyMin || lo > options.energyMax)
                continue;
            const int first = std::max(
                0, static_cast<int>(std::floor((lo - options.energyMin) / step)));
            const int last = std::min(
                bins - 1,
                static_cast<int>(std::ceil((hi - options.energyMin) / step)));
            for (int b = first; b <= last; ++b) {
                const double delta =
                    result.energies[static_cast<std::size_t>(b)] - state.energy;
                profile[static_cast<std::size_t>(b)] +=
                    state.weight * norm * std::exp(-delta * delta / twoSigmaSquared);
            }
        }
        for (const double value : profile)
            result.maxIntensity = std::max(result.maxIntensity, value);
        result.intensity.push_back(std::move(profile));
    }
    return result;
}

} // namespace calango::core
