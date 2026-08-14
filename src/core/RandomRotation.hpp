#pragma once

#include "core/PhysicalConstants.hpp"
#include "core/Vec3.hpp"

#include <array>
#include <cmath>
#include <random>

namespace calango::core {

/// A uniformly distributed random rotation (Shoemake's quaternion method),
/// as three row vectors. Sampling three Euler angles uniformly does NOT give
/// a uniform orientation — it clusters orientations near the poles, which
/// shows up as spurious texture in anything assembled from "random"
/// rotations (grain orientations, solvent molecules).
inline std::array<Vec3, 3> randomRotationMatrix(std::mt19937& rng)
{
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    const double u1 = uniform(rng);
    const double u2 = uniform(rng);
    const double u3 = uniform(rng);
    const double r1 = std::sqrt(1.0 - u1);
    const double r2 = std::sqrt(u1);
    const double x = r1 * std::sin(2.0 * kPi * u2);
    const double y = r1 * std::cos(2.0 * kPi * u2);
    const double z = r2 * std::sin(2.0 * kPi * u3);
    const double w = r2 * std::cos(2.0 * kPi * u3);
    return {Vec3{1 - 2 * (y * y + z * z), 2 * (x * y - z * w),
                 2 * (x * z + y * w)},
            Vec3{2 * (x * y + z * w), 1 - 2 * (x * x + z * z),
                 2 * (y * z - x * w)},
            Vec3{2 * (x * z - y * w), 2 * (y * z + x * w),
                 1 - 2 * (x * x + y * y)}};
}

} // namespace calango::core
