// Regression test for core::imageRange.
//
// Seven analysis modules (RDF, coordination, distributions, local entropy,
// chemical order, cluster expansion, Monte Carlo) each kept a private copy of
// this function. Two of them had been given a degenerate-cell guard; the other
// five had not, so a structure with a zero-volume cell took the
// `rMax / 0 == +inf` path and narrowed +inf to int — undefined behavior, in
// practice a garbage image count that hangs or crashes the neighbor loops.
//
// The copies are now one shared definition carrying the guard. These cases
// pin both the guard and the geometry it must not disturb.

#include "core/PeriodicImages.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using calango::core::imageRange;
using calango::core::UnitCell;
using calango::core::Vec3;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

void checkRange(const std::array<int, 3>& got, int a, int b, int c,
                const std::string& what)
{
    const bool ok = got[0] == a && got[1] == b && got[2] == c;
    std::printf("  %s %s  (got %d %d %d, want %d %d %d)\n", ok ? "ok  " : "FAIL",
                what.c_str(), got[0], got[1], got[2], a, b, c);
    if (!ok)
        ++failures;
}

UnitCell cubic(double a, std::array<bool, 3> pbc = {true, true, true})
{
    return UnitCell({a, 0.0, 0.0}, {0.0, a, 0.0}, {0.0, 0.0, a}, pbc);
}

} // namespace

int main()
{
    std::printf("Degenerate cells (the bug):\n");
    {
        // Zero-volume cell, still flagged periodic — exactly what a molecule
        // loaded without a cell but with pbc set looks like. The unguarded
        // copies computed ceil(rMax / 0) here.
        const auto range = imageRange(cubic(0.0), 6.0);
        checkRange(range, 0, 0, 0, "zero-volume cell yields no images");
        for (const int n : range) {
            check(n >= 0 && n < 1000,
                  "image count is finite and sane (not a narrowed inf)");
        }
    }
    {
        // Flat cell: two vectors span a plane, the third is null.
        const UnitCell flat({5.0, 0.0, 0.0}, {0.0, 5.0, 0.0}, {0.0, 0.0, 0.0},
                            {true, true, true});
        checkRange(imageRange(flat, 6.0), 0, 0, 0, "flat (2D-degenerate) cell");
    }
    {
        // Parallel lattice vectors: volume is zero and the cross-product area
        // of one pair is zero too.
        const UnitCell parallel({5.0, 0.0, 0.0}, {5.0, 0.0, 0.0},
                                {0.0, 0.0, 5.0}, {true, true, true});
        checkRange(imageRange(parallel, 6.0), 0, 0, 0, "parallel lattice vectors");
    }

    std::printf("Ordinary cells (geometry preserved):\n");
    {
        // 5 A cube, 6 A cutoff -> ceil(6/5) = 2 images each way.
        checkRange(imageRange(cubic(5.0), 6.0), 2, 2, 2, "cubic 5 A, rMax 6 A");
        // Cutoff below the width still needs one image.
        checkRange(imageRange(cubic(5.0), 3.0), 1, 1, 1, "cubic 5 A, rMax 3 A");
        checkRange(imageRange(cubic(5.0), 0.0), 0, 0, 0, "zero cutoff");
    }
    {
        // Mixed periodicity: a slab periodic in x/y only.
        checkRange(imageRange(cubic(5.0, {true, true, false}), 6.0), 2, 2, 0,
                   "slab: non-periodic z gets no images");
        checkRange(imageRange(cubic(5.0, {false, false, false}), 6.0), 0, 0, 0,
                   "fully aperiodic cell");
    }
    {
        // Sheared cell: the bound must use the *perpendicular width*
        // (volume / |a_j x a_k|), not the vector length. Here c is tilted, so
        // |c| = sqrt(20^2 + 5^2) ~= 20.6 but the width along c is only 5.
        // Using the length would under-count and miss real neighbors.
        const UnitCell sheared({5.0, 0.0, 0.0}, {0.0, 5.0, 0.0},
                               {20.0, 0.0, 5.0}, {true, true, true});
        const auto range = imageRange(sheared, 6.0);
        std::printf("  ..  sheared cell -> %d %d %d\n", range[0], range[1], range[2]);
        check(range[2] == 2,
              "sheared cell uses perpendicular width (2), not |c| (would be 1)");
        check(range[0] >= 2, "sheared cell widens the in-plane range too");
    }

    std::printf(failures == 0 ? "\nAll periodic-image checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
