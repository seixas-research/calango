#include "core/GridInterpolation.hpp"

#include <cmath>

namespace calango::core {

namespace {

int wrap(int i, int n)
{
    i %= n;
    if (i < 0)
        i += n;
    return i;
}

/// Catmull-Rom cubic through p1,p2 at parameter t ∈ [0,1] (p0,p3 are the
/// flanking samples that set the tangents).
double catmull(double p0, double p1, double p2, double p3, double t)
{
    const double a = -0.5 * p0 + 1.5 * p1 - 1.5 * p2 + 0.5 * p3;
    const double b = p0 - 2.5 * p1 + 2.0 * p2 - 0.5 * p3;
    const double c = -0.5 * p0 + 0.5 * p2;
    return ((a * t + b) * t + c) * t + p1;
}

/// Tricubic sample at fractional grid-index coordinate (gx,gy,gz), periodic.
double tricubic(const VolumetricData& f, double gx, double gy, double gz)
{
    const int ix = static_cast<int>(std::floor(gx));
    const int iy = static_cast<int>(std::floor(gy));
    const int iz = static_cast<int>(std::floor(gz));
    const double tx = gx - ix, ty = gy - iy, tz = gz - iz;

    double vz[4];
    for (int dz = -1; dz <= 2; ++dz) {
        const int kz = wrap(iz + dz, f.nz);
        double vy[4];
        for (int dy = -1; dy <= 2; ++dy) {
            const int ky = wrap(iy + dy, f.ny);
            double vx[4];
            for (int dx = -1; dx <= 2; ++dx)
                vx[dx + 1] = f.at(wrap(ix + dx, f.nx), ky, kz);
            vy[dy + 1] = catmull(vx[0], vx[1], vx[2], vx[3], tx);
        }
        vz[dz + 1] = catmull(vy[0], vy[1], vy[2], vy[3], ty);
    }
    return catmull(vz[0], vz[1], vz[2], vz[3], tz);
}

} // namespace

VolumetricData refineGrid(const VolumetricData& field, int factor,
                          GridInterpolation scheme)
{
    if (scheme == GridInterpolation::None || factor <= 1 || field.empty())
        return field;

    VolumetricData out;
    out.nx = field.nx * factor;
    out.ny = field.ny * factor;
    out.nz = field.nz * factor;
    out.origin = field.origin;
    out.spanA = field.spanA;
    out.spanB = field.spanB;
    out.spanC = field.spanC;
    out.label = field.label;
    out.values.resize(static_cast<std::size_t>(out.nx) * out.ny * out.nz);

    const double inv = 1.0 / factor;
    for (int ix = 0; ix < out.nx; ++ix) {
        const double gx = ix * inv; // old grid-index coordinate
        for (int iy = 0; iy < out.ny; ++iy) {
            const double gy = iy * inv;
            for (int iz = 0; iz < out.nz; ++iz) {
                const double gz = iz * inv;
                const double v = scheme == GridInterpolation::Tricubic
                    ? tricubic(field, gx, gy, gz)
                    : field.samplePeriodic(gx, gy, gz);
                out.values[(static_cast<std::size_t>(ix) * out.ny + iy) * out.nz
                           + iz] = v;
            }
        }
    }
    return out;
}

} // namespace calango::core
