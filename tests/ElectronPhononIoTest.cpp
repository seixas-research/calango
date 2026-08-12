// Reading raw gpaw.elph output back into the analysis.
//
// Two things are checked, and the second is the one that matters.
//
// The .npy reader is checked against BYTES WRITTEN HERE from the format spec,
// not against a writer of my own — otherwise a misread header and a
// mis-written one would agree and the test would pass on both being wrong.
// The headers below are literally what numpy.lib.format emits.
//
// Then the whole path is round-tripped: the analytic free-electron case that
// ElectronPhononAnalysisTest pins against a closed form is written out as a
// manifest plus arrays, read back, and analysed. If lambda survives that
// unchanged, then the manifest, the index order, the |g|^2 magnitude and the
// k+q map all came back the way they went in. Any single index transposed
// shows up as a different lambda.

#include "core/ElectronPhononAnalysis.hpp"
#include "core/ElectronPhononIo.hpp"
#include "core/NumpyArray.hpp"
#include "core/TetrahedronBz.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

constexpr double kPi = 3.14159265358979323846;
constexpr double kHbar2Over2m = 3.80998212;

/// Where this test's throwaway .npy files go.
///
/// NEVER the current directory. This test writes a dozen files with generic
/// names (real.npy, eig.npy, epc.json...), and defaulting to "./" meant that
/// running the binary by hand from the repository root scattered them through
/// the source tree — which is exactly what happened while this module was
/// being debugged. ctest sets CALANGO_TEST_TMPDIR to the build directory; a
/// bare run now gets a subdirectory of the system temp directory instead.
std::string scratchDir()
{
    if (const char* dir = std::getenv("CALANGO_TEST_TMPDIR"))
        return std::string(dir) + "/";
    const std::filesystem::path fallback =
        std::filesystem::temp_directory_path() / "calango_elph_io_test";
    std::error_code ignored;
    std::filesystem::create_directories(fallback, ignored);
    return fallback.string() + "/";
}

/// Write a .npy exactly the way numpy.lib.format does: magic, version 1.0,
/// a 2-byte little-endian header length, then the dict padded with spaces so
/// the whole preamble is a multiple of 64 bytes and ends in a newline.
void writeNpy(const std::string& path, const std::string& descr,
              const std::vector<std::size_t>& shape,
              const std::vector<double>& raw, bool fortran = false)
{
    std::string dict = "{'descr': '" + descr + "', 'fortran_order': "
        + (fortran ? "True" : "False") + ", 'shape': (";
    for (std::size_t i = 0; i < shape.size(); ++i)
        dict += std::to_string(shape[i])
            + (shape.size() == 1 || i + 1 < shape.size() ? "," : "");
    dict += "), }";
    std::size_t total = 10 + dict.size() + 1;
    while (total % 64 != 0) {
        dict += ' ';
        ++total;
    }
    dict += '\n';

    std::ofstream out(path, std::ios::binary);
    out.write("\x93NUMPY", 6);
    const char version[2] = {1, 0};
    out.write(version, 2);
    const unsigned char length[2] = {
        static_cast<unsigned char>(dict.size() & 0xFF),
        static_cast<unsigned char>((dict.size() >> 8) & 0xFF)};
    out.write(reinterpret_cast<const char*>(length), 2);
    out.write(dict.data(), static_cast<std::streamsize>(dict.size()));
    out.write(reinterpret_cast<const char*>(raw.data()),
              static_cast<std::streamsize>(raw.size() * sizeof(double)));
}

} // namespace

int main()
{
    using calango::core::ElectronPhononInput;
    using calango::core::NumpyArray;
    using calango::core::TetrahedronBz;

    const std::string dir = scratchDir();

    // -- The reader, against bytes laid out from the spec -------------------
    std::printf("The .npy reader:\n");
    {
        const std::string path = dir + "real.npy";
        writeNpy(path, "<f8", {3, 2}, {1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
        NumpyArray array;
        std::string error;
        check(calango::core::readNumpyArray(path, array, &error),
              "a float64 array reads");
        check(array.shape.size() == 2 && array.shape[0] == 3
                  && array.shape[1] == 2,
              "with its shape recovered from the header");
        check(array.values.size() == 6 && array.values[0] == 1.0
                  && array.values[5] == 6.0,
              "and its values in C order");
    }
    {
        // 3 + 4i has |z|^2 = 25 — a value no arithmetic slip lands on by luck.
        const std::string path = dir + "complex.npy";
        writeNpy(path, "<c16", {2}, {3.0, 4.0, 0.0, -1.0});
        NumpyArray array;
        std::string error;
        check(calango::core::readNumpyArray(path, array, &error),
              "a complex128 array reads");
        check(array.type == NumpyArray::Type::Complex128
                  && array.values.size() == 2,
              "as complex, one value per element rather than per component");
        check(std::abs(array.values[0] - 25.0) < 1e-12
                  && std::abs(array.values[1] - 1.0) < 1e-12,
              "returning |z|^2, which is all the coupling sums use");
    }
    {
        NumpyArray array;
        std::string error;
        const std::string path = dir + "fortran.npy";
        writeNpy(path, "<f8", {2, 2}, {1.0, 2.0, 3.0, 4.0}, true);
        check(!calango::core::readNumpyArray(path, array, &error)
                  && error.find("Fortran") != std::string::npos,
              "Fortran order is refused, not silently transposed — a "
              "transposed g array still yields a believable lambda");

        const std::string shortPath = dir + "short.npy";
        writeNpy(shortPath, "<f8", {100}, {1.0, 2.0});
        check(!calango::core::readNumpyArray(shortPath, array, &error)
                  && error.find("early") != std::string::npos,
              "a file shorter than its declared shape is refused");

        const std::string intPath = dir + "int.npy";
        writeNpy(intPath, "<i8", {2}, {1.0, 2.0});
        check(!calango::core::readNumpyArray(intPath, array, &error),
              "an unsupported dtype is named rather than reinterpreted");

        check(!calango::core::readNumpyArray(dir + "absent.npy", array, &error),
              "and a missing file is an error, not an empty array");
    }

    // -- The whole path, round-tripped --------------------------------------
    std::printf("Round trip through the manifest:\n");
    const double a = 4.0;
    const double b = 2.0 * kPi / a;
    const int m = 24;
    const int shift = 3;
    const double fermi = 1.2;
    const double omega0 = 0.025;
    const double g0Squared = 1.0e-4;

    ElectronPhononInput input;
    input.kGrid = {m, m, m};
    input.reciprocal = {{{b, 0.0, 0.0}, {0.0, b, 0.0}, {0.0, 0.0, b}}};
    input.spins = 1;
    input.bands = 1;
    input.qCount = 1;
    input.modes = 1;
    input.fermiLevelEv = fermi;
    input.temperatureK = 300.0;
    input.phononFrequenciesEv = {omega0};

    const std::size_t nk = input.kPointCount();
    TetrahedronBz bz(input.kGrid, input.reciprocal);
    const int half = m / 2;
    const auto fold = [m, half](int i) {
        return static_cast<double>((i + half) % m - half) / static_cast<double>(m);
    };
    input.eigenvalues.assign(nk, 0.0);
    input.kPlusQ.assign(nk, 0);
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < m; ++j)
            for (int k = 0; k < m; ++k) {
                const double kx = fold(i) * b;
                const double ky = fold(j) * b;
                const double kz = fold(k) * b;
                input.eigenvalues[bz.index(i, j, k)] =
                    kHbar2Over2m * (kx * kx + ky * ky + kz * kz);
                input.kPlusQ[bz.index(i, j, k)] =
                    static_cast<int>(bz.index(i + shift, j, k));
            }
    input.gSquaredEv2.assign(nk, g0Squared);

    const auto direct = calango::core::analyzeElectronPhonon(input);
    check(direct.ok, "the in-memory analysis runs, as the closed-form test pins");

    // Write it the way the generated script does. |g|^2 goes out as COMPLEX,
    // because that is what GPAW's own gsqklnn.npy holds and the manifest
    // points straight at that file rather than recopying tens of gigabytes.
    {
        std::vector<double> complexG(nk * 2, 0.0);
        for (std::size_t i = 0; i < nk; ++i) {
            complexG[2 * i] = std::sqrt(g0Squared); // real part
            complexG[2 * i + 1] = 0.0;
        }
        writeNpy(dir + "eig.npy", "<f8", {nk, 1}, input.eigenvalues);
        writeNpy(dir + "freq.npy", "<f8", {1, 1}, input.phononFrequenciesEv);
        std::vector<double> kq(nk);
        for (std::size_t i = 0; i < nk; ++i)
            kq[i] = input.kPlusQ[i];
        writeNpy(dir + "kq.npy", "<f8", {1, nk}, kq);
        writeNpy(dir + "gsq.npy", "<c16",
                 {1, 1, nk, 1, 1, 1}, complexG);

        std::ofstream manifest(dir + calango::core::electronPhononManifestName());
        // Full precision, as the generator does (it writes Python repr()).
        // At the stream default of six digits the reciprocal vectors come
        // back good to only 1e-6, which would hide a real precision loss
        // behind a loose tolerance.
        manifest << std::setprecision(17);
        manifest << "calango.elph.raw 1\n";
        manifest << "kgrid " << m << " " << m << " " << m << "\n";
        manifest << "reciprocal " << b << " 0 0 0 " << b << " 0 0 0 " << b
                 << "\n";
        manifest << "spins 1\nbands 1\nqcount 1\nmodes 1\n";
        manifest << "fermi " << fermi << "\n";
        manifest << "temperature 300\n";
        manifest << "eigenvalues eig.npy\n";
        manifest << "frequencies freq.npy\n";
        manifest << "kplusq kq.npy\n";
        manifest << "gsquared gsq.npy\n";
    }

    ElectronPhononInput loaded;
    std::string error;
    check(calango::core::loadElectronPhononInput(
              dir + calango::core::electronPhononManifestName(), loaded, &error),
          "the manifest and its arrays load (" + error + ")");
    check(loaded.kGrid == input.kGrid && loaded.bands == input.bands
              && loaded.qCount == input.qCount && loaded.modes == input.modes,
          "with the mesh dimensions recovered");
    check(loaded.kPlusQ == input.kPlusQ,
          "and the k+q map identical — this is the index that has no local "
          "symptom when wrong");

    const auto reloaded = calango::core::analyzeElectronPhonon(loaded);
    std::printf("    lambda direct %.8f, via files %.8f\n", direct.lambda,
                reloaded.lambda);
    check(reloaded.ok, "the reloaded input analyses");
    check(std::abs(reloaded.lambda - direct.lambda) < 1e-9 * direct.lambda,
          "and gives the same lambda as the in-memory case — the file path "
          "adds nothing and loses nothing");
    check(std::abs(reloaded.dosAtFermi - direct.dosAtFermi) < 1e-12,
          "with the same N(E_F)");

    // -- Shape disagreement is fatal ----------------------------------------
    {
        ElectronPhononInput bad;
        std::ofstream manifest(dir + "wrong.txt");
        manifest << "calango.elph.raw 1\n";
        manifest << "kgrid " << m << " " << m << " " << m << "\n";
        manifest << "reciprocal " << b << " 0 0 0 " << b << " 0 0 0 " << b
                 << "\n";
        // Four bands declared, one band's worth of data on disk.
        manifest << "spins 1\nbands 4\nqcount 1\nmodes 1\nfermi " << fermi
                 << "\n";
        manifest << "eigenvalues eig.npy\nfrequencies freq.npy\n";
        manifest << "kplusq kq.npy\ngsquared gsq.npy\n";
        manifest.close();
        std::string message;
        check(!calango::core::loadElectronPhononInput(dir + "wrong.txt", bad,
                                                      &message),
              "an array whose size contradicts the declared mesh is refused");
        check(message.find("declares") != std::string::npos,
              "and the message says what was expected against what was found");
    }

    // -- Decimal-comma locales ----------------------------------------------
    //
    // std::stod and printf follow LC_NUMERIC, and QApplication sets it from
    // the environment. On a pt_BR / de_DE / fr_FR machine the manifest's
    // "reciprocal 1.5514 ..." parsed as 1, so the reciprocal lattice became
    // integers and N(E_F), lambda and tau all followed it down — in the GUI
    // only, which is why no Qt-free test saw it.
    std::printf("Decimal-comma locales:\n");
    {
        const char* candidates[] = {"pt_BR.UTF-8", "de_DE.UTF-8",
                                    "fr_FR.UTF-8"};
        const char* engaged = nullptr;
        for (const char* name : candidates)
            if (std::setlocale(LC_NUMERIC, name)) {
                engaged = name;
                break;
            }
        if (!engaged) {
            std::printf("    (no comma locale available; skipped)\n");
        } else {
            std::printf("    (under %s)\n", engaged);
            ElectronPhononInput commaLoaded;
            std::string message;
            check(calango::core::loadElectronPhononInput(
                      dir + calango::core::electronPhononManifestName(),
                      commaLoaded, &message),
                  "the manifest still loads under a decimal-comma locale");
            check(std::abs(commaLoaded.fermiLevelEv - fermi) < 1e-12,
                  "with the Fermi level intact, not truncated at the point");
            check(std::abs(commaLoaded.reciprocal[0][0] - b) < 1e-12,
                  "and the reciprocal lattice still a lattice rather than "
                  "integers");
            const auto commaResult =
                calango::core::analyzeElectronPhonon(commaLoaded);
            check(std::abs(commaResult.lambda - direct.lambda)
                      < 1e-9 * direct.lambda,
                  "so lambda is unchanged by the machine's locale");
            std::setlocale(LC_NUMERIC, "C");
        }
    }

    // -- epc.json -----------------------------------------------------------
    {
        const std::string path = dir + "epc.json";
        std::string message;
        check(calango::core::writeElectronPhononResult(path, direct, &message),
              "the result writes as epc.json");
        std::ifstream json(path);
        const std::string text((std::istreambuf_iterator<char>(json)),
                               std::istreambuf_iterator<char>());
        check(text.find("\"integration\": \"tetrahedron\"") != std::string::npos,
              "recording the integration method, so a stored result is not "
              "confused with the smeared numbers this module used to write");
        check(text.find("\"lambda\"") != std::string::npos
                  && text.find("\"alpha2F\"") != std::string::npos
                  && text.find("\"relaxation_time_fs\"") != std::string::npos,
              "with lambda, the spectrum and tau all present");
    }

    if (failures == 0) {
        std::printf("\nAll electron-phonon IO checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d electron-phonon IO check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
