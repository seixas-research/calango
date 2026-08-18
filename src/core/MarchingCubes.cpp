#include "core/MarchingCubes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

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
                          const VolumetricData* colorField, FieldWrap wrap)
{
    IsoMesh mesh;
    if (field.empty() || field.nx < 2 || field.ny < 2 || field.nz < 2)
        return mesh;

    const bool periodic = wrap == FieldWrap::Periodic;
    // Sampling the field for the gradient has to agree with the wrap mode, or
    // the outermost shell of a Clamped window gets normals computed from the
    // opposite side of the box.
    const auto sampleField = [&](double gx, double gy, double gz) {
        return periodic ? field.samplePeriodic(gx, gy, gz)
                        : field.sample(gx, gy, gz);
    };

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

    // Returns the unit outward normal AND the raw gradient magnitude — the
    // latter is already computed here (it's what `norm` normalizes away),
    // so keeping it costs nothing extra. For a scalar field that is itself
    // an energy (e.g. a Fermi surface's E_n(k)), this magnitude is the
    // group-velocity proxy |∇E(k)| a caller may want to color the surface
    // by; anything not asking for it just ignores the second element.
    const auto gradient = [&](double gx, double gy, double gz) {
        constexpr double h = 0.5;
        const double dx =
            (sampleField(gx + h, gy, gz) - sampleField(gx - h, gy, gz))
            / (2.0 * h);
        const double dy =
            (sampleField(gx, gy + h, gz) - sampleField(gx, gy - h, gz))
            / (2.0 * h);
        const double dz =
            (sampleField(gx, gy, gz + h) - sampleField(gx, gy, gz - h))
            / (2.0 * h);
        // Cartesian gradient = M^-T * grid gradient.
        Vec3 g{invT[0] * dx + invT[1] * dy + invT[2] * dz,
               invT[3] * dx + invT[4] * dy + invT[5] * dz,
               invT[6] * dx + invT[7] * dy + invT[8] * dz};
        const double norm = g.norm();
        const Vec3 unit = norm > 1e-12 ? g * (1.0 / norm) : Vec3{0.0, 0.0, 1.0};
        return std::pair<Vec3, double>{unit, norm};
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
        const auto [unitGradient, gradientMagnitude] = gradient(gx, gy, gz);
        mesh.normals.push_back(unitGradient * -1.0);
        mesh.gradientMagnitude.push_back(gradientMagnitude);
        if (colorField) {
            // Same box assumed: convert fractional coords to the color
            // field's own grid resolution.
            const double cx = gx / field.nx * colorField->nx;
            const double cy = gy / field.ny * colorField->ny;
            const double cz = gz / field.nz * colorField->nz;
            mesh.colorValues.push_back(periodic
                                           ? colorField->samplePeriodic(cx, cy, cz)
                                           : colorField->sample(cx, cy, cz));
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

    // Periodic runs one cell PAST the last node, closing the surface through
    // the seam onto node 0. Clamped stops one short, so no cell straddles the
    // outer face and `% n` below is a no-op.
    const int lastX = periodic ? field.nx : field.nx - 1;
    const int lastY = periodic ? field.ny : field.ny - 1;
    const int lastZ = periodic ? field.nz : field.nz - 1;

    // ---- Empty-space skipping ---------------------------------------------
    //
    // An isosurface touches a vanishing fraction of the grid it lives in, and
    // the periodic continuation made that ratio far worse: a localized Wannier
    // function in a two-cell window occupies about 0.2% of it, so 99.8% of the
    // marching was spent proving that eight corners agree.
    //
    // The remedy is one cheap linear pass. A block of kBlock³ cells can be
    // skipped outright when every NODE it touches lies on the same side of the
    // isovalue, which is exactly the condition under which each of its cells
    // would produce mask 0 or 15 and emit nothing. So this changes no output —
    // it only declines to rediscover the same answer cell by cell.
    //
    // The predicate mirrors the mask test below (`value > isovalue`) rather
    // than approximating it: a cell emits iff its corners straddle, i.e. iff
    // min <= isovalue < max. Deriving it from the same comparison is what makes
    // "skipped" and "would have emitted nothing" the same set and not merely
    // overlapping ones.
    constexpr int kBlock = 8;
    const auto blocks = [](int extent) { return (extent + kBlock - 1) / kBlock; };
    const int bx = blocks(lastX), by = blocks(lastY), bz = blocks(lastZ);
    std::vector<unsigned char> active(
        static_cast<std::size_t>(bx) * by * bz, 0u);
    for (int i0 = 0; i0 < bx; ++i0)
        for (int j0 = 0; j0 < by; ++j0)
            for (int k0 = 0; k0 < bz; ++k0) {
                // Nodes, not cells: a block's last cell reaches one node past
                // its own extent, and forgetting that would skip a block whose
                // surface crosses its far face.
                const int xEnd = std::min(i0 * kBlock + kBlock, lastX);
                const int yEnd = std::min(j0 * kBlock + kBlock, lastY);
                const int zEnd = std::min(k0 * kBlock + kBlock, lastZ);
                double lo = std::numeric_limits<double>::infinity();
                double hi = -std::numeric_limits<double>::infinity();
                for (int x = i0 * kBlock; x <= xEnd; ++x)
                    for (int y = j0 * kBlock; y <= yEnd; ++y)
                        for (int z = k0 * kBlock; z <= zEnd; ++z) {
                            // The same wrapping accessor the march uses, so the
                            // periodic seam is covered by the scan too.
                            const double v = field.at(x % field.nx,
                                                      y % field.ny,
                                                      z % field.nz);
                            lo = std::min(lo, v);
                            hi = std::max(hi, v);
                        }
                active[(static_cast<std::size_t>(i0) * by + j0) * bz + k0] =
                    (lo <= isovalue && isovalue < hi) ? 1u : 0u;
            }

    for (int ix = 0; ix < lastX; ++ix) {
        for (int iy = 0; iy < lastY; ++iy) {
            for (int iz = 0; iz < lastZ; ++iz) {
                // Skip a whole inactive block in one step rather than testing
                // every cell in it: z is the contiguous axis, so this turns the
                // dominant inner loop into one lookup per kBlock cells. The
                // iteration ORDER is untouched, so the emitted triangle
                // sequence is identical to the exhaustive march's.
                if (!active[(static_cast<std::size_t>(ix / kBlock) * by
                             + iy / kBlock)
                                * bz
                            + iz / kBlock]) {
                    iz = (iz / kBlock + 1) * kBlock - 1; // ++iz lands on the next
                    continue;
                }
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
                    // corners -> quad (two triangles).
                    //
                    // ORIENTATION COMES FROM THE FIELD, NOT FROM THE TABLE.
                    //
                    // Two independent parities decide a triangle's winding
                    // here: each case is shared with its COMPLEMENT (mask 1
                    // has p0 above the isovalue, mask 14 has it below — same
                    // surface, opposite outside), and the six tetrahedra of
                    // the decomposition do not all have the same HANDEDNESS.
                    // The table as written accounted for neither, leaving
                    // about 46% of triangles reversed; flipping the complement
                    // set merely exchanged which 46%.
                    //
                    // That mattered because a renderer trusts the winding: the
                    // lit isosurface shader flips its normal on gl_FrontFacing,
                    // which turned the (correct, gradient-derived) normals of
                    // the reversed facets inward, zeroed the diffuse term and
                    // the specular gated on it, and dropped them to the ambient
                    // floor — a dense speckle of dark pits that read as holes
                    // under the glossy material.
                    //
                    // The gradient already knows which way is out. Emitting the
                    // three vertices, comparing the geometric normal against
                    // them and swapping when they disagree is O(1) per
                    // triangle, needs no table, and is correct by construction
                    // for every case and every tetrahedron handedness.
                    const auto tri = [&](const Corner& a1, const Corner& b1,
                                         const Corner& a2, const Corner& b2,
                                         const Corner& a3, const Corner& b3) {
                        const std::size_t base = mesh.positions.size();
                        emitVertex(a1, b1);
                        emitVertex(a2, b2);
                        emitVertex(a3, b3);
                        const Vec3 geometric =
                            (mesh.positions[base + 1] - mesh.positions[base])
                                .cross(mesh.positions[base + 2]
                                       - mesh.positions[base]);
                        const Vec3 outward = mesh.normals[base]
                            + mesh.normals[base + 1] + mesh.normals[base + 2];
                        if (geometric.dot(outward) < 0.0) {
                            std::swap(mesh.positions[base + 1],
                                      mesh.positions[base + 2]);
                            std::swap(mesh.normals[base + 1],
                                      mesh.normals[base + 2]);
                            if (mesh.colorValues.size() == mesh.positions.size())
                                std::swap(mesh.colorValues[base + 1],
                                          mesh.colorValues[base + 2]);
                        }
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

WeldedMesh weldVertices(const IsoMesh& mesh, double tolerance)
{
    WeldedMesh welded;
    const std::size_t count = mesh.positions.size();
    if (count == 0)
        return welded;

    const double grid = tolerance > 0.0 ? tolerance : 1e-5;
    const auto key = [grid](const Vec3& p) {
        return std::array<long long, 3>{
            static_cast<long long>(std::llround(p.x / grid)),
            static_cast<long long>(std::llround(p.y / grid)),
            static_cast<long long>(std::llround(p.z / grid))};
    };
    struct KeyHash {
        std::size_t operator()(const std::array<long long, 3>& k) const
        {
            std::size_t h = 1469598103934665603ULL;
            for (const long long v : k)
                h = (h ^ static_cast<std::size_t>(v)) * 1099511628211ULL;
            return h;
        }
    };

    std::unordered_map<std::array<long long, 3>, int, KeyHash> unique;
    unique.reserve(count);
    welded.index.resize(count);
    welded.points.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto [it, inserted] = unique.try_emplace(
            key(mesh.positions[i]), static_cast<int>(welded.points.size()));
        if (inserted)
            welded.points.push_back(mesh.positions[i]);
        welded.index[i] = it->second;
    }
    return welded;
}

IsoMesh flattenTriangleNormals(const IsoMesh& mesh)
{
    IsoMesh out;
    out.positions = mesh.positions;
    out.colorValues = mesh.colorValues;
    out.gradientMagnitude = mesh.gradientMagnitude;
    out.normals.resize(mesh.normals.size());
    for (std::size_t t = 0; t + 2 < mesh.positions.size(); t += 3) {
        Vec3 normal = mesh.normals[t] + mesh.normals[t + 1] + mesh.normals[t + 2];
        normal = normal.dot(normal) > 1e-24
            ? normal.normalized()
            : (mesh.positions[t + 1] - mesh.positions[t])
                  .cross(mesh.positions[t + 2] - mesh.positions[t])
                  .normalized();
        out.normals[t] = normal;
        out.normals[t + 1] = normal;
        out.normals[t + 2] = normal;
    }
    return out;
}

void smoothMesh(IsoMesh& mesh, int passes)
{
    if (passes <= 0 || mesh.positions.size() < 3)
        return;
    const WeldedMesh welded = weldVertices(mesh);
    const std::vector<int>& weld = welded.index;
    // A mutable copy: the smoothing passes below swap through it.
    std::vector<Vec3> points = welded.points;
    const std::size_t count = mesh.positions.size();

    // Adjacency over the welded points, from the triangle edges.
    std::vector<std::vector<int>> neighbors(points.size());
    const std::size_t triangles = count / 3;
    for (std::size_t t = 0; t < triangles; ++t) {
        const int a = weld[3 * t], b = weld[3 * t + 1], c = weld[3 * t + 2];
        const int edges[3][2] = {{a, b}, {b, c}, {c, a}};
        for (const auto& e : edges) {
            if (e[0] == e[1])
                continue;
            neighbors[static_cast<std::size_t>(e[0])].push_back(e[1]);
            neighbors[static_cast<std::size_t>(e[1])].push_back(e[0]);
        }
    }

    // Under-relaxed (lambda < 1) so the surface creeps toward its neighbours
    // instead of collapsing: at lambda = 1 a few passes visibly shrink a lobe.
    constexpr double kLambda = 0.5;
    std::vector<Vec3> next(points.size());
    for (int pass = 0; pass < passes; ++pass) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto& adjacent = neighbors[i];
            if (adjacent.empty()) {
                next[i] = points[i];
                continue;
            }
            Vec3 sum{0.0, 0.0, 0.0};
            for (const int j : adjacent)
                sum = sum + points[static_cast<std::size_t>(j)];
            const Vec3 average = sum * (1.0 / static_cast<double>(adjacent.size()));
            next[i] = points[i] + (average - points[i]) * kLambda;
        }
        points.swap(next);
    }

    for (std::size_t i = 0; i < count; ++i)
        mesh.positions[i] = points[static_cast<std::size_t>(weld[i])];

    // Re-derive the normals from the smoothed geometry, area-weighted over the
    // triangles meeting each welded point.
    if (!mesh.normals.empty()) {
        std::vector<Vec3> accumulated(points.size(), Vec3{0.0, 0.0, 0.0});
        for (std::size_t t = 0; t < triangles; ++t) {
            const Vec3& p0 = mesh.positions[3 * t];
            const Vec3& p1 = mesh.positions[3 * t + 1];
            const Vec3& p2 = mesh.positions[3 * t + 2];
            const Vec3 face = (p1 - p0).cross(p2 - p0);
            for (int k = 0; k < 3; ++k) {
                const auto index = static_cast<std::size_t>(weld[3 * t + k]);
                accumulated[index] = accumulated[index] + face;
            }
        }
        for (std::size_t i = 0; i < count; ++i) {
            const Vec3& n = accumulated[static_cast<std::size_t>(weld[i])];
            // A cancelled-out sum leaves the extraction's own normal, which is
            // still the field gradient and better than a zero vector.
            if (n.norm() > 1e-12)
                mesh.normals[i] = n.normalized();
        }
    }
}

} // namespace calango::core
