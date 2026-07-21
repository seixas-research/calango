#include "core/MarchingCubes.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace calango::core {

namespace {

/// 3x3 inverse for the grid-step matrix (gradient transformation).
std::array<double, 9> inverse3(const std::array<double, 9>& m)
{
    const double det = m[0] * (m[4] * m[8] - m[5] * m[7])
        - m[1] * (m[3] * m[8] - m[5] * m[6])
        + m[2] * (m[3] * m[7] - m[4] * m[6]);
    const double inv = std::abs(det) > 1e-30 ? 1.0 / det : 0.0;
    return {(m[4] * m[8] - m[5] * m[7]) * inv, (m[2] * m[7] - m[1] * m[8]) * inv,
            (m[1] * m[5] - m[2] * m[4]) * inv, (m[5] * m[6] - m[3] * m[8]) * inv,
            (m[0] * m[8] - m[2] * m[6]) * inv, (m[2] * m[3] - m[0] * m[5]) * inv,
            (m[3] * m[7] - m[4] * m[6]) * inv, (m[1] * m[6] - m[0] * m[7]) * inv,
            (m[0] * m[4] - m[1] * m[3]) * inv};
}

struct Corner {
    double gx, gy, gz; ///< grid coordinates (may exceed dims at the seam)
    double value;
};

} // namespace

IsoMesh extractIsosurface(const VolumetricData& field, double isovalue,
                          const VolumetricData* colorField)
{
    IsoMesh mesh;
    if (field.empty() || field.nx < 2 || field.ny < 2 || field.nz < 2)
        return mesh;

    // Step vectors of one voxel; M^-T maps grid-space gradients to
    // Cartesian ones.
    const Vec3 stepA = field.spanA * (1.0 / field.nx);
    const Vec3 stepB = field.spanB * (1.0 / field.ny);
    const Vec3 stepC = field.spanC * (1.0 / field.nz);
    const std::array<double, 9> stepMatrix{stepA.x, stepB.x, stepC.x,
                                           stepA.y, stepB.y, stepC.y,
                                           stepA.z, stepB.z, stepC.z};
    const std::array<double, 9> invT = [&] {
        auto inv = inverse3(stepMatrix); // rows of inv = M^-1
        return std::array<double, 9>{inv[0], inv[3], inv[6], inv[1], inv[4],
                                     inv[7], inv[2], inv[5], inv[8]};
    }();

    const auto gradient = [&](double gx, double gy, double gz) {
        constexpr double h = 0.5;
        const double dx = (field.samplePeriodic(gx + h, gy, gz)
                           - field.samplePeriodic(gx - h, gy, gz))
            / (2.0 * h);
        const double dy = (field.samplePeriodic(gx, gy + h, gz)
                           - field.samplePeriodic(gx, gy - h, gz))
            / (2.0 * h);
        const double dz = (field.samplePeriodic(gx, gy, gz + h)
                           - field.samplePeriodic(gx, gy, gz - h))
            / (2.0 * h);
        // Cartesian gradient = M^-T * grid gradient.
        Vec3 g{invT[0] * dx + invT[1] * dy + invT[2] * dz,
               invT[3] * dx + invT[4] * dy + invT[5] * dz,
               invT[6] * dx + invT[7] * dy + invT[8] * dz};
        const double norm = g.norm();
        return norm > 1e-12 ? g * (1.0 / norm) : Vec3{0.0, 0.0, 1.0};
    };

    const auto emitVertex = [&](const Corner& a, const Corner& b) {
        const double denom = b.value - a.value;
        const double t = std::abs(denom) > 1e-30
            ? std::clamp((isovalue - a.value) / denom, 0.0, 1.0)
            : 0.5;
        const double gx = a.gx + t * (b.gx - a.gx);
        const double gy = a.gy + t * (b.gy - a.gy);
        const double gz = a.gz + t * (b.gz - a.gz);
        mesh.positions.push_back(field.position(gx, gy, gz));
        mesh.normals.push_back(gradient(gx, gy, gz) * -1.0);
        if (colorField) {
            // Same box assumed: convert fractional coords to the color
            // field's own grid resolution.
            mesh.colorValues.push_back(colorField->samplePeriodic(
                gx / field.nx * colorField->nx, gy / field.ny * colorField->ny,
                gz / field.nz * colorField->nz));
        }
    };

    // The classic 6-tetrahedra decomposition sharing the 0-6 diagonal.
    static constexpr int kTets[6][4] = {{0, 5, 1, 6}, {0, 1, 2, 6},
                                        {0, 2, 3, 6}, {0, 3, 7, 6},
                                        {0, 7, 4, 6}, {0, 4, 5, 6}};
    // Cube corner offsets (VTK ordering: bit0 = x, bit1 = y, bit2 = z on
    // the bottom face, +4 on top).
    static constexpr int kOffsets[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0},
                                           {0, 1, 0}, {0, 0, 1}, {1, 0, 1},
                                           {1, 1, 1}, {0, 1, 1}};

    for (int ix = 0; ix < field.nx; ++ix) {
        for (int iy = 0; iy < field.ny; ++iy) {
            for (int iz = 0; iz < field.nz; ++iz) {
                Corner corner[8];
                for (int c = 0; c < 8; ++c) {
                    const int cx = ix + kOffsets[c][0];
                    const int cy = iy + kOffsets[c][1];
                    const int cz = iz + kOffsets[c][2];
                    corner[c] = {static_cast<double>(cx),
                                 static_cast<double>(cy),
                                 static_cast<double>(cz),
                                 field.at(cx % field.nx, cy % field.ny,
                                          cz % field.nz)};
                }
                for (const auto& tet : kTets) {
                    const Corner& p0 = corner[tet[0]];
                    const Corner& p1 = corner[tet[1]];
                    const Corner& p2 = corner[tet[2]];
                    const Corner& p3 = corner[tet[3]];
                    int mask = 0;
                    if (p0.value > isovalue) mask |= 1;
                    if (p1.value > isovalue) mask |= 2;
                    if (p2.value > isovalue) mask |= 4;
                    if (p3.value > isovalue) mask |= 8;
                    if (mask == 0 || mask == 15)
                        continue;

                    // One corner separated -> single triangle; two
                    // corners -> quad (two triangles). Enumerate the
                    // seven distinct cases (complements share geometry).
                    const auto tri = [&](const Corner& a1, const Corner& b1,
                                         const Corner& a2, const Corner& b2,
                                         const Corner& a3, const Corner& b3) {
                        emitVertex(a1, b1);
                        emitVertex(a2, b2);
                        emitVertex(a3, b3);
                    };
                    switch (mask) {
                    case 1: case 14:
                        tri(p0, p1, p0, p2, p0, p3);
                        break;
                    case 2: case 13:
                        tri(p1, p0, p1, p2, p1, p3);
                        break;
                    case 4: case 11:
                        tri(p2, p0, p2, p1, p2, p3);
                        break;
                    case 8: case 7:
                        tri(p3, p0, p3, p1, p3, p2);
                        break;
                    case 3: case 12: // {p0,p1} vs {p2,p3}
                        tri(p0, p2, p0, p3, p1, p3);
                        tri(p0, p2, p1, p3, p1, p2);
                        break;
                    case 5: case 10: // {p0,p2} vs {p1,p3}
                        tri(p0, p1, p0, p3, p2, p3);
                        tri(p0, p1, p2, p3, p2, p1);
                        break;
                    case 6: case 9: // {p1,p2} vs {p0,p3}
                        tri(p1, p0, p1, p3, p2, p3);
                        tri(p1, p0, p2, p3, p2, p0);
                        break;
                    }
                }
            }
        }
    }
    return mesh;
}

} // namespace calango::core
