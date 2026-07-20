#pragma once

#include <cmath>

namespace calango::core {

/// Minimal double-precision 3-vector.
///
/// The core model deliberately avoids Qt types (QVector3D is float-only and
/// would tie the Model layer to the GUI toolkit). Conversion to rendering
/// types happens at the render/ boundary.
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }

    constexpr double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    double norm() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const {
        const double n = norm();
        return n > 0.0 ? *this / n : Vec3{};
    }
};

constexpr Vec3 operator*(double s, const Vec3& v) { return v * s; }

} // namespace calango::core
