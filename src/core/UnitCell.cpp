#include "core/UnitCell.hpp"

#include <cmath>

namespace calango::core {

UnitCell::UnitCell(const Vec3& a, const Vec3& b, const Vec3& c, std::array<bool, 3> pbc)
    : vectors_{a, b, c}
    , pbc_(pbc)
{
}

bool UnitCell::isDefined() const
{
    return volume() > 1e-9;
}

double UnitCell::volume() const
{
    return std::abs(vectors_[0].dot(vectors_[1].cross(vectors_[2])));
}

Vec3 UnitCell::fractionalToCartesian(const Vec3& frac) const
{
    return vectors_[0] * frac.x + vectors_[1] * frac.y + vectors_[2] * frac.z;
}

Vec3 UnitCell::cartesianToFractional(const Vec3& cart) const
{
    // Rows of the inverse are the reciprocal vectors (b×c)/V, (c×a)/V, (a×b)/V.
    const double signedVolume = vectors_[0].dot(vectors_[1].cross(vectors_[2]));
    if (std::abs(signedVolume) < 1e-12)
        return {};
    return {cart.dot(vectors_[1].cross(vectors_[2])) / signedVolume,
            cart.dot(vectors_[2].cross(vectors_[0])) / signedVolume,
            cart.dot(vectors_[0].cross(vectors_[1])) / signedVolume};
}

std::array<Vec3, 8> UnitCell::corners() const
{
    std::array<Vec3, 8> result;
    for (int i = 0; i < 8; ++i) {
        result[static_cast<std::size_t>(i)] = fractionalToCartesian(
            {static_cast<double>(i & 1), static_cast<double>((i >> 1) & 1),
             static_cast<double>((i >> 2) & 1)});
    }
    return result;
}

const std::array<std::pair<int, int>, 12>& UnitCell::edges()
{
    static const std::array<std::pair<int, int>, 12> kEdges = {{
        {0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3},
        {2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7},
    }};
    return kEdges;
}

} // namespace calango::core
