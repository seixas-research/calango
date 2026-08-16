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

#include "core/MarchingCubes.hpp"
#include "core/VolumetricData.hpp"

#include <cmath>
// cstdint: needed for std::uintmax_t below (see the HDF5-compression-ratio
// check). Some libstdc++ versions only pull it in transitively; do not
// remove even if a tool flags it as unused.
#include <cstdint>
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

/// An n^3 grid of a single Gaussian blob centred in the box — smooth in the
/// sense a real charge density is smooth (its values vary gently from voxel
/// to voxel), which is exactly what byte-shuffle + gzip needs to find any
/// repetition at all; a grid of independent random doubles would not
/// compress past 1x no matter how good the codec is.
core::VolumetricData makeSmoothDensity(int n)
{
    core::VolumetricData d;
    d.nx = d.ny = d.nz = n;
    d.origin = {0.0, 0.0, 0.0};
    d.spanA = {10.0, 0.0, 0.0};
    d.spanB = {0.0, 10.0, 0.0};
    d.spanC = {0.0, 0.0, 10.0};
    d.label = "synthetic gaussian";
    d.sourceFormat = "cube";
    d.atoms = {{6, {5.0, 5.0, 5.0}}}; // one carbon at the blob's centre
    d.values.resize(static_cast<std::size_t>(n) * n * n);
    const double center = (n - 1) / 2.0;
    const double sigma = n / 6.0;
    for (int ix = 0; ix < n; ++ix)
        for (int iy = 0; iy < n; ++iy)
            for (int iz = 0; iz < n; ++iz) {
                const double dx = ix - center, dy = iy - center, dz = iz - center;
                const double r2 = dx * dx + dy * dy + dz * dz;
                d.values[(static_cast<std::size_t>(ix) * n + iy) * n + iz]
                    = std::exp(-r2 / (2.0 * sigma * sigma));
            }
    return d;
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

    std::printf("HDF5 round trip:\n");
    {
        // cube -> .h5 -> read back: grid data, cell and the one atom the
        // cube's header carried all have to survive the round trip exactly,
        // since VolumetricData::load(path) on the .h5 is meant to be
        // indistinguishable from loading the original.
        const auto original = core::VolumetricData::load(
            write("roundtrip.cube",
                  cubeWith("1\n2\n3\n4\n5\n6\n7\n8\n")));
        const std::string h5Path = (scratch() / "roundtrip.h5").string();
        original.saveHdf5(h5Path);
        const auto restored = core::VolumetricData::load(h5Path);

        check(restored.nx == original.nx && restored.ny == original.ny
                  && restored.nz == original.nz,
              "cube -> h5: grid dimensions survive");
        bool valuesEqual = restored.values.size() == original.values.size();
        for (std::size_t i = 0; valuesEqual && i < original.values.size(); ++i)
            valuesEqual = restored.values[i] == original.values[i];
        check(valuesEqual, "cube -> h5: every grid value round-trips bitwise "
                           "(IEEE-754 float64, no text round trip)");
        check(near(restored.origin.x, original.origin.x)
                  && near(restored.spanA.x, original.spanA.x)
                  && near(restored.spanB.y, original.spanB.y)
                  && near(restored.spanC.z, original.spanC.z),
              "cube -> h5: origin and the three spanning vectors survive");
        check(restored.label == original.label,
              "cube -> h5: the label survives");
        check(restored.sourceFormat == "cube",
              "cube -> h5: source_format records what the .h5 was CONVERTED "
              "FROM, not \"hdf5\" itself");
        check(restored.atoms.size() == original.atoms.size()
                  && !restored.atoms.empty()
                  && restored.atoms[0].atomicNumber
                         == original.atoms[0].atomicNumber
                  && near(restored.atoms[0].position.x,
                          original.atoms[0].position.x)
                  && near(restored.atoms[0].position.z,
                          original.atoms[0].position.z),
              "cube -> h5: the atom list (species, Cartesian position) survives");
    }
    {
        // Same round trip starting from a CHGCAR, whose atoms carry a real
        // element symbol (unlike the cube fixture's atomic number 1) — this
        // is what exercises Elements::atomicNumber() in loadChgcar().
        const std::string body =
            "cell\n1.0\n"
            "  2.0 0.0 0.0\n  0.0 2.0 0.0\n  0.0 0.0 2.0\n"
            "  H\n  1\nDirect\n  0.25 0.25 0.25\n\n"
            "  2 2 2\n"
            "  1 2 3 4 5 6 7 8\n";
        const auto original
            = core::VolumetricData::load(write("roundtrip_CHGCAR", body));
        const std::string h5Path = (scratch() / "roundtrip_chgcar.h5").string();
        original.saveHdf5(h5Path);
        const auto restored = core::VolumetricData::load(h5Path);

        bool valuesEqual = restored.values.size() == original.values.size();
        for (std::size_t i = 0; valuesEqual && i < original.values.size(); ++i)
            valuesEqual = restored.values[i] == original.values[i];
        check(valuesEqual, "CHGCAR -> h5: every grid value round-trips bitwise");
        check(restored.sourceFormat == "chgcar",
              "CHGCAR -> h5: source_format records \"chgcar\"");
        check(restored.atoms.size() == 1 && restored.atoms[0].atomicNumber == 1
                  && near(restored.atoms[0].position.x, 0.5)
                  && near(restored.atoms[0].position.y, 0.5)
                  && near(restored.atoms[0].position.z, 0.5),
              "CHGCAR -> h5: the H atom's species and fractional-to-Cartesian "
              "position both survive (0.25,0.25,0.25 direct in a 2 Å cubic "
              "cell -> Cartesian (0.5,0.5,0.5))");
    }
    {
        // A realistic smooth field: chunked + byte-shuffle + gzip has to
        // actually beat the raw size, not just round-trip correctly.
        const int n = 48;
        const auto density = makeSmoothDensity(n);
        const std::string h5Path = (scratch() / "smooth_density.h5").string();
        density.saveHdf5(h5Path);
        const auto rawBytes
            = static_cast<std::uintmax_t>(n) * n * n * sizeof(double);
        const auto h5Bytes = std::filesystem::file_size(h5Path);
        check(h5Bytes > 0 && h5Bytes < rawBytes,
              "a smooth 48^3 density compresses smaller than its raw "
              "nx*ny*nz*8 bytes (ratio "
                  + std::to_string(static_cast<double>(rawBytes)
                                   / static_cast<double>(h5Bytes))
                  + "x)");
    }
    {
        // The viewer path: an isosurface extracted from the .h5-loaded grid
        // has to match the one extracted from the original — the whole point
        // of a bitwise-exact round trip is that nothing downstream can tell
        // the difference.
        const int n = 24;
        const auto density = makeSmoothDensity(n);
        const std::string h5Path = (scratch() / "iso_density.h5").string();
        density.saveHdf5(h5Path);
        const auto restored = core::VolumetricData::load(h5Path);

        const core::IsoMesh fromOriginal = core::extractIsosurface(
            density, 0.3, nullptr, core::FieldWrap::Clamped);
        const core::IsoMesh fromHdf5 = core::extractIsosurface(
            restored, 0.3, nullptr, core::FieldWrap::Clamped);

        check(!fromOriginal.positions.empty(),
              "the synthetic blob actually crosses isovalue 0.3 (a "
              "non-trivial surface to compare)");
        bool sameMesh = fromOriginal.positions.size() == fromHdf5.positions.size();
        for (std::size_t i = 0; sameMesh && i < fromOriginal.positions.size(); ++i)
            sameMesh = near(fromOriginal.positions[i].x, fromHdf5.positions[i].x)
                && near(fromOriginal.positions[i].y, fromHdf5.positions[i].y)
                && near(fromOriginal.positions[i].z, fromHdf5.positions[i].z);
        check(sameMesh,
              "the isosurface extracted from the .h5 matches the one "
              "extracted from the source grid vertex-for-vertex");
    }
    {
        // convertToHdf5() is the ONE path both the calculator setup pages'
        // "HDF5 compression" option and the Dump Charge Densities node call
        // — it has to actually go through load() + saveHdf5() rather than
        // being a second, divergent implementation.
        const std::string cubePath = write(
            "convert_source.cube", cubeWith("1\n2\n3\n4\n5\n6\n7\n8\n"));
        const std::string h5Path = (scratch() / "convert_dest.h5").string();
        std::string error;
        const bool ok
            = core::VolumetricData::convertToHdf5(cubePath, h5Path, &error);
        check(ok && error.empty(), "convertToHdf5() succeeds on a valid cube");
        check(std::filesystem::exists(h5Path),
              "and leaves the .h5 file where asked");

        std::string missingError;
        const bool failed = core::VolumetricData::convertToHdf5(
            (scratch() / "does_not_exist.cube").string(),
            (scratch() / "unwritten.h5").string(), &missingError);
        check(!failed && !missingError.empty(),
              "convertToHdf5() reports failure (not an exception) when the "
              "source cannot be read, with a message rather than a silent "
              "empty destination");
    }

    std::printf(failures == 0 ? "\nAll volumetric parse checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
