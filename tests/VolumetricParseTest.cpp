// Volumetric grid parsing.
//
// The value block of a real grid is millions of numbers, so it is read with a
// bulk read + std::from_chars rather than through `istream >> double`. That is
// a 5x speed-up and a hand-rolled tokenizer, which is exactly the combination
// worth pinning: a parser that is fast and subtly wrong produces a plausible
// isosurface of the wrong data, and nothing about that looks like a bug.
//
// So this checks what the fast path has to get right that the stream version
// got for free: whitespace and line-break handling, every float spelling a
// producer might emit, the Fortran-order transpose CHGCAR needs, and a
// truncated block still being reported as truncated rather than silently
// zero-filled.
//
// GUI-free, GL-free, Python-free.

#include "core/VolumetricData.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace calango;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

bool near(double a, double b, double tol = 1e-12)
{
    return std::abs(a - b) <= tol;
}

std::filesystem::path scratch()
{
    static const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "calango_volumetric_test";
    std::filesystem::create_directories(dir);
    return dir;
}

std::string write(const std::string& name, const std::string& body)
{
    const std::filesystem::path path = scratch() / name;
    std::ofstream(path) << body;
    return path.string();
}

/// A 2x2x2 cube file whose value block is spelled with `layout`.
std::string cubeWith(const std::string& values)
{
    return "comment one\ncomment two\n"
           "    1    0.000000    0.000000    0.000000\n"
           "    2    1.000000    0.000000    0.000000\n"
           "    2    0.000000    1.000000    0.000000\n"
           "    2    0.000000    0.000000    1.000000\n"
           "    1    1.000000    0.000000    0.000000    0.000000\n"
         + values;
}

} // namespace

int main()
{
    std::printf("Cube value block:\n");
    {
        // z fastest, one value per line — the layout a minimal writer emits.
        const std::string path = cubeWith(
            "1\n2\n3\n4\n5\n6\n7\n8\n");
        const auto data = core::VolumetricData::load(write("plain.cube", path));
        check(data.nx == 2 && data.ny == 2 && data.nz == 2,
              "the header's dimensions are read");
        check(data.values.size() == 8, "and the whole block");
        bool ordered = true;
        for (std::size_t i = 0; i < data.values.size(); ++i)
            ordered = ordered && near(data.values[i], static_cast<double>(i + 1));
        check(ordered, "in file order, which is the storage order");
    }
    {
        // Every spelling a producer might use, including the six-per-line
        // Gaussian layout, exponents in both cases, and no leading zero.
        const auto data = core::VolumetricData::load(write(
            "spellings.cube",
            cubeWith("  1.00000E+00 -2.5 3  4.0e0\n"
                     "\t5.000000000  -6.25E-01\n\n"
                     "   .75   8.125   \n")));
        const double expected[8] = {1.0, -2.5, 3.0, 4.0, 5.0, -0.625, 0.75, 8.125};
        bool all = data.values.size() == 8;
        for (std::size_t i = 0; all && i < 8; ++i)
            all = near(data.values[i], expected[i]);
        check(all, "exponents, tabs, blank lines and a bare .75 all parse");
    }
    {
        // Trailing junk after the block must not be reached at all: the count
        // in the header is what stops the read.
        const auto data = core::VolumetricData::load(write(
            "trailing.cube",
            cubeWith("1 2 3 4 5 6 7 8\nDSET_IDS not-a-number\n")));
        check(data.values.size() == 8 && near(data.values[7], 8.0),
              "the header's count stops the read before any trailing text");
    }
    {
        // A short block is an error, not a silently zero-filled grid — the one
        // failure mode that would produce a plausible-looking wrong picture.
        bool threw = false;
        try {
            core::VolumetricData::load(
                write("short.cube", cubeWith("1 2 3\n")));
        } catch (const std::exception&) {
            threw = true;
        }
        check(threw, "a truncated block raises rather than zero-filling");
    }
    {
        // Angstrom units are marked by a negative grid count, and must not
        // leave the axis count negative.
        const std::string body =
            "c1\nc2\n"
            "    1    0.000000    0.000000    0.000000\n"
            "   -2    1.000000    0.000000    0.000000\n"
            "   -2    0.000000    1.000000    0.000000\n"
            "   -2    0.000000    0.000000    1.000000\n"
            "    1    1.000000    0.000000    0.000000    0.000000\n"
            "1 2 3 4 5 6 7 8\n";
        const auto data =
            core::VolumetricData::load(write("angstrom.cube", body));
        check(data.nx == 2 && data.ny == 2 && data.nz == 2,
              "a negative grid count marks Angstrom without inverting the size");
        check(near(data.spanA.x, 2.0, 1e-9),
              "and the span is not scaled from Bohr");
    }

    std::printf("CHGCAR value block:\n");
    {
        // VASP writes x fastest; the loader transposes into z-fastest storage.
        // That transpose moved into the parse callback, so it is worth showing
        // that a value still lands where it did.
        const std::string body =
            "cell\n1.0\n"
            "  2.0 0.0 0.0\n  0.0 2.0 0.0\n  0.0 0.0 2.0\n"
            "  H\n  1\nDirect\n  0.0 0.0 0.0\n\n"
            "  2 2 2\n"
            "  1 2 3 4 5 6 7 8\n";
        const auto data = core::VolumetricData::load(write("CHGCAR", body));
        check(data.values.size() == 8, "the block is read whole");
        // Fortran index (ix, iy, iz) = i%2, (i/2)%2, i/4 -> our (ix*ny+iy)*nz+iz.
        bool placed = true;
        for (std::size_t i = 0; i < 8; ++i) {
            const std::size_t ix = i % 2, iy = (i / 2) % 2, iz = i / 4;
            placed = placed
                && near(data.values[(ix * 2 + iy) * 2 + iz],
                        static_cast<double>(i + 1));
        }
        check(placed, "and transposed from Fortran order into z-fastest");
    }

    std::printf(failures == 0 ? "\nAll volumetric parse checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
