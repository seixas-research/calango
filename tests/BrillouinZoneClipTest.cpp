// Wigner-Seitz-cell clipping of an isosurface mesh (core::clipToWignerSeitzCell).
//
// THE BUG THIS EXISTS FOR. A Fermi surface is extracted on the grid's own
// PARALLELEPIPED — the shape a regular grid can be laid on, spanned by the
// primitive reciprocal vectors — then clipped to the Wigner-Seitz cell for
// display, because that is the region a Fermi surface is conventionally
// drawn in. For a cubic direct lattice the two cells coincide and clipping a
// single un-replicated copy of the box mesh is correct. They do not coincide
// in general: Cu is FCC, so its reciprocal lattice is BCC, whose
// Wigner-Seitz cell is a truncated octahedron — and half that octahedron's
// vertices (its W points) sit OUTSIDE the parallelepiped, up to 3/4 of a
// cell edge out where the box only reaches 1/2. A single un-replicated clip
// does not sample that region at all, so it just deletes it instead of
// filling it in: literal holes in the rendered sheet, concentrated exactly
// where Cu's Fermi-surface necks touch the zone boundary — confirmed against
// a real Wannier-interpolated run, where 12 of the 24 true Wigner-Seitz
// vertices land outside the sampled box.
//
// clipToWignerSeitzCell() fixes this by replicating the periodic images the
// corners actually need — read off the Wigner-Seitz cell's own vertices,
// not a fixed assumption — before clipping. The tests below reproduce the
// BCC-shaped cell (the general skewed case) and check that content sampled
// honestly inside the box reaches its true corner, that a cubic cell (where
// box and zone already coincide) is left untouched, and that nothing ever
// clips to outside the zone or loses the flat-per-triangle-normal contract.

#include "core/BrillouinZone.hpp"
#include "core/MarchingCubes.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace core = calango::core;
using core::Vec3;

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

void checkClose(double got, double want, double tol, const std::string& what)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  %-4s %s (got %.6g, want %.6g, tol %g)\n", ok ? "ok" : "FAIL",
               what.c_str(), got, want, tol);
    if (!ok)
        ++failures;
}

/// basis * f = v, by Cramer's rule — an independent re-derivation (not a
/// call into BrillouinZone.cpp's own, unexported fracOf) of which periodic
/// image of the box a point belongs to.
Vec3 fracOf(const Vec3& v, const std::array<Vec3, 3>& basis)
{
    const double det = basis[0].dot(basis[1].cross(basis[2]));
    return Vec3{basis[1].cross(basis[2]).dot(v) / det,
               basis[2].cross(basis[0]).dot(v) / det,
               basis[0].cross(basis[1]).dot(v) / det};
}

/// k is in the Wigner-Seitz cell of `basis` iff it is no farther from the
/// origin than from any other lattice point: |k| <= |k - L|, equivalently
/// k.L <= |L|^2/2. Searched over the same +/-2 shells bisectorPlanes() uses
/// internally, so this is just as authoritative for a skewed cell — and it
/// is derived from the textbook Voronoi-cell definition directly, not from
/// reading clipToWignerSeitzCell()'s own half-space list.
bool insideWignerSeitz(const Vec3& v, const std::array<Vec3, 3>& basis, double tol)
{
    for (int i = -2; i <= 2; ++i) {
        for (int j = -2; j <= 2; ++j) {
            for (int k = -2; k <= 2; ++k) {
                if (i == 0 && j == 0 && k == 0)
                    continue;
                const Vec3 l = basis[0] * i + basis[1] * j + basis[2] * k;
                if (v.dot(l) > 0.5 * l.dot(l) + tol)
                    return false;
            }
        }
    }
    return true;
}

core::IsoMesh oneTriangle(const Vec3& a, const Vec3& b, const Vec3& c,
                          const Vec3& na, const Vec3& nb, const Vec3& nc)
{
    core::IsoMesh mesh;
    mesh.positions = {a, b, c};
    mesh.normals = {na, nb, nc};
    return mesh;
}

void testCubicBoxIsAlreadyTheZone()
{
    // Closed form: for an orthogonal (here cubic) basis the parallelepiped
    // IS the Wigner-Seitz cell, so no periodic image but the identity one
    // can ever contribute — a triangle safely inside is neither cut nor
    // duplicated across images, and comes back with its positions unchanged.
    const std::array<Vec3, 3> basis{Vec3{2.0, 0.0, 0.0}, Vec3{0.0, 2.0, 0.0},
                                    Vec3{0.0, 0.0, 2.0}};
    const core::IsoMesh input =
        oneTriangle(Vec3{0.3, 0.1, -0.2}, Vec3{0.5, 0.2, -0.1}, Vec3{0.2, 0.6, 0.1},
                   Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1});

    const core::IsoMesh out = core::clipToWignerSeitzCell(input, basis);
    check(out.positions.size() == 3,
         "a triangle safely inside a cubic cell is neither cut nor duplicated");
    for (std::size_t i = 0; i < out.positions.size() && i < input.positions.size(); ++i)
        checkClose((out.positions[i] - input.positions[i]).norm(), 0.0, 1e-9,
                   "position " + std::to_string(i) + " passes through unchanged");
}

void testSkewedCellCornersAreRecovered()
{
    // The FCC-real / BCC-reciprocal shape — the one Cu (and every other FCC
    // metal) actually has. Absolute scale does not matter: fractional
    // coordinates, and therefore which vertices land outside the box, are
    // scale-invariant, so this is the same geometry as the real run.
    const Vec3 b1{0.0, 1.0, 1.0};
    const Vec3 b2{1.0, 0.0, 1.0};
    const Vec3 b3{1.0, 1.0, 0.0};
    const std::array<Vec3, 3> basis{b1, b2, b3};

    const core::PolyhedronMesh ws = core::wignerSeitzCell(basis);

    int cornersOutsideBox = 0;
    int cornersRecovered = 0;
    for (const Vec3& v : ws.vertices) {
        const Vec3 f = fracOf(v, basis);
        const double maxAbs = std::max({std::abs(f.x), std::abs(f.y), std::abs(f.z)});
        if (maxAbs <= 0.5 + 1e-9)
            continue; // this vertex is already inside the box; not the case under test
        ++cornersOutsideBox;

        // Just inside the true corner (not exactly on it, to stay off the
        // clip planes themselves), then folded back to where marching cubes
        // would actually have sampled it: the periodic image whose
        // fractional coordinates all fall in [-1/2, 1/2].
        const Vec3 target = v * 0.999;
        const Vec3 tf = fracOf(target, basis);
        const auto shiftOf = [](double x) { return x > 0.5 ? -1 : (x < -0.5 ? 1 : 0); };
        const Vec3 home = target + b1 * shiftOf(tf.x) + b2 * shiftOf(tf.y)
            + b3 * shiftOf(tf.z);

        // A tiny triangle at `home`, exactly as extractIsosurface would have
        // emitted it: three close-together points. Normals are arbitrary —
        // clipToWignerSeitzCell flattens them away regardless.
        const core::IsoMesh input =
            oneTriangle(home, home + Vec3{0.01, 0.0, 0.0}, home + Vec3{0.0, 0.01, 0.0},
                       Vec3{0, 0, 1}, Vec3{0, 0, 1}, Vec3{0, 0, 1});

        const core::IsoMesh clipped = core::clipToWignerSeitzCell(input, basis);
        for (const Vec3& p : clipped.positions) {
            if ((p - target).norm() < 0.05) {
                ++cornersRecovered;
                break;
            }
        }
    }

    check(cornersOutsideBox > 0,
         "the BCC-shaped test cell has Wigner-Seitz vertices outside its own "
         "parallelepiped (the case Cu hits)");
    check(cornersRecovered == cornersOutsideBox,
         "every one of those corners is recovered by clipToWignerSeitzCell, "
         "not silently dropped");
}

void testEveryOutputVertexIsInsideTheZone()
{
    // Correctness in the other direction: nothing clipToWignerSeitzCell
    // emits may fall outside the zone, whichever periodic image it came
    // from. A scattering of small triangles spread through the box,
    // including right at its faces and corners, all fed through in one call.
    const std::array<Vec3, 3> basis{Vec3{0.0, 1.0, 1.0}, Vec3{1.0, 0.0, 1.0},
                                    Vec3{1.0, 1.0, 0.0}};
    core::IsoMesh input;
    for (int i = -2; i <= 2; ++i) {
        for (int j = -2; j <= 2; ++j) {
            for (int k = -2; k <= 2; ++k) {
                const Vec3 centre = basis[0] * (0.2 * i) + basis[1] * (0.2 * j)
                    + basis[2] * (0.2 * k);
                input.positions.push_back(centre);
                input.positions.push_back(centre + Vec3{0.02, 0.0, 0.0});
                input.positions.push_back(centre + Vec3{0.0, 0.02, 0.0});
                for (int v = 0; v < 3; ++v)
                    input.normals.push_back(Vec3{0.0, 0.0, 1.0});
            }
        }
    }

    const core::IsoMesh out = core::clipToWignerSeitzCell(input, basis);
    check(!out.positions.empty(), "the scattered input produced some clipped output");
    bool allInside = true;
    for (const Vec3& p : out.positions) {
        if (!insideWignerSeitz(p, basis, 1e-6)) {
            allInside = false;
            break;
        }
    }
    check(allInside, "every output vertex, from every periodic image, lies "
                     "inside the Wigner-Seitz cell");
}

void testNormalsAreFlattenedPerSourceTriangle()
{
    // A source triangle's own vertex normals are averaged into one flat
    // normal, and every fragment a clip cuts it into inherits that same
    // normal — a cut fragment has no gradient sample of its own to draw a
    // smooth normal from.
    const std::array<Vec3, 3> basis{Vec3{2.0, 0.0, 0.0}, Vec3{0.0, 2.0, 0.0},
                                    Vec3{0.0, 0.0, 2.0}};
    // x = 1.3 sits outside the box (the nearest plane along x is x <= 1), so
    // this triangle is genuinely cut into a fan of more than one fragment.
    const core::IsoMesh input = oneTriangle(
        Vec3{-0.9, -0.1, 0.0}, Vec3{1.3, -0.1, 0.0}, Vec3{0.0, 0.9, 0.0},
        Vec3{0.1, 0.0, 1.0}, Vec3{-0.1, 0.0, 1.0}, Vec3{0.0, 0.1, 1.0});

    const core::IsoMesh out = core::clipToWignerSeitzCell(input, basis);
    check(!out.positions.empty(), "the straddling triangle produced clipped fragments");
    check(out.positions.size() > 3, "the cut actually split it into more than one triangle");
    bool allSame = true;
    for (std::size_t i = 1; i < out.normals.size(); ++i) {
        if ((out.normals[i] - out.normals[0]).norm() > 1e-12) {
            allSame = false;
            break;
        }
    }
    check(allSame, "every fragment of the cut triangle shares one flat normal");
}

/// Barycentric weights of `p` in the plane of triangle (v0, v1, v2) — the
/// least-squares solution of p = v0 + w1*(v1-v0) + w2*(v2-v0), valid for any
/// p actually in that plane (weights outside [0,1] just mean p is outside
/// the triangle, in-plane).
std::array<double, 3> barycentric(const Vec3& p, const Vec3& v0, const Vec3& v1,
                                  const Vec3& v2)
{
    const Vec3 e1 = v1 - v0, e2 = v2 - v0, e0 = p - v0;
    const double d11 = e1.dot(e1), d12 = e1.dot(e2), d22 = e2.dot(e2);
    const double d1 = e0.dot(e1), d2 = e0.dot(e2);
    const double denom = d11 * d22 - d12 * d12;
    const double w1 = (d22 * d1 - d12 * d2) / denom;
    const double w2 = (d11 * d2 - d12 * d1) / denom;
    return {1.0 - w1 - w2, w1, w2};
}

void testGradientMagnitudeSurvivesTheClip()
{
    // Closed-form check, re-derived independently of clipToWignerSeitzCell's
    // own machinery: every output vertex — an original corner, translated to
    // whichever periodic image it belongs to, or a new point interpolated
    // onto a cut — is some point of the ORIGINAL triangle's plane, shifted by
    // SOME integer combination of the lattice vectors. Its gradientMagnitude
    // must equal the ORIGINAL triangle's own barycentric interpolation at
    // that (de-shifted) point: a plain copy is barycentric weights (1,0,0)
    // etc., a cut point is whatever weights the cutting plane put it at, and
    // in neither case is a periodic field's value re-evaluated at the new
    // position — a translated copy keeps the value it had.
    const std::array<Vec3, 3> basis{Vec3{0.0, 1.0, 1.0}, Vec3{1.0, 0.0, 1.0},
                                    Vec3{1.0, 1.0, 0.0}};
    // Straddles several WS-cell faces at once, so both plain copies (to the
    // images that already contain a corner) and genuine cuts are exercised.
    // Deliberately NOT symmetric under the basis's own x<->y permutation
    // symmetry (b0=(0,1,1) and b1=(1,0,1) swap under it): a triangle that
    // shared that symmetry let two DIFFERENT shifts both look like valid,
    // in-triangle reconstructions of the same output point, which is a
    // genuine ambiguity in how this test verifies the answer, not a defect
    // in clipToWignerSeitzCell itself — this asymmetric one has only one.
    const Vec3 v0{-0.15, -0.11, -0.13}, v1{0.91, -0.17, -0.09},
        v2{-0.13, 0.87, -0.19};
    const double s0 = 2.3, s1 = -1.7, s2 = 0.6; // arbitrary, no relation to position
    core::IsoMesh input;
    input.positions = {v0, v1, v2};
    input.normals = {Vec3{0, 0, 1}, Vec3{0, 0, 1}, Vec3{0, 0, 1}};
    input.gradientMagnitude = {s0, s1, s2};

    const core::IsoMesh out = core::clipToWignerSeitzCell(input, basis);
    check(!out.positions.empty(), "the straddling triangle produced clipped fragments");
    check(out.gradientMagnitude.size() == out.positions.size(),
         "gradientMagnitude has one entry per output vertex, same as positions");

    // Per OUTPUT TRIANGLE, not per vertex: Sutherland-Hodgman clips one
    // translated copy of the source triangle per shift, so a triangle's own
    // three vertices always share the shift that produced them. Matching
    // per vertex instead is ambiguous exactly where two images meet — a
    // point ON a shared Wigner-Seitz face can look like a valid, in-triangle
    // point under more than one shift, and picking the wrong one of two
    // otherwise-valid matches would compare against the wrong barycentric
    // weights. Requiring all three vertices to agree on ONE shift resolves
    // that ambiguity in every case this test constructs.
    int matchedTriangles = 0;
    int totalTriangles = 0;
    bool allConsistent = true;
    for (std::size_t t = 0; t + 2 < out.positions.size(); t += 3) {
        ++totalTriangles;
        bool foundShift = false;
        for (int a = -1; a <= 1 && !foundShift; ++a) {
            for (int b = -1; b <= 1 && !foundShift; ++b) {
                for (int c = -1; c <= 1 && !foundShift; ++c) {
                    const Vec3 shift = basis[0] * a + basis[1] * b + basis[2] * c;
                    std::array<double, 3> expected{};
                    bool allThreeMatch = true;
                    for (std::size_t k = 0; k < 3 && allThreeMatch; ++k) {
                        const Vec3 q = out.positions[t + k] - shift;
                        const auto w = barycentric(q, v0, v1, v2);
                        const Vec3 reconstructed =
                            v0 * w[0] + v1 * w[1] + v2 * w[2];
                        if ((q - reconstructed).norm() > 1e-9
                            || w[0] < -1e-6 || w[0] > 1 + 1e-6 || w[1] < -1e-6
                            || w[1] > 1 + 1e-6 || w[2] < -1e-6 || w[2] > 1 + 1e-6)
                            allThreeMatch = false;
                        else
                            expected[k] = s0 * w[0] + s1 * w[1] + s2 * w[2];
                    }
                    if (!allThreeMatch)
                        continue;
                    foundShift = true;
                    ++matchedTriangles;
                    for (std::size_t k = 0; k < 3; ++k)
                        if (std::abs(out.gradientMagnitude[t + k] - expected[k])
                            > 1e-6)
                            allConsistent = false;
                }
            }
        }
    }
    check(matchedTriangles == totalTriangles,
         "every output triangle is traceable back to the original triangle "
         "under a single consistent lattice shift");
    check(allConsistent,
         "and each vertex's gradientMagnitude matches the original "
         "triangle's own barycentric interpolation there");
}

} // namespace

int main()
{
    std::printf("Wigner-Seitz-cell clipping\n\n");
    testCubicBoxIsAlreadyTheZone();
    testSkewedCellCornersAreRecovered();
    testEveryOutputVertexIsInsideTheZone();
    testNormalsAreFlattenedPerSourceTriangle();
    testGradientMagnitudeSurvivesTheClip();

    std::printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
