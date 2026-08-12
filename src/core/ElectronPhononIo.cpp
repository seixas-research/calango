#include "core/ElectronPhononIo.hpp"

#include "core/LocaleSafeNumber.hpp"
#include "core/NumpyArray.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace calango::core {

namespace {

bool fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
    return false;
}

std::string directoryOf(const std::string& path)
{
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

/// Load an array named in the manifest, resolved relative to the manifest.
bool loadArray(const std::string& base,
               const std::unordered_map<std::string, std::string>& files,
               const std::string& key, NumpyArray& out, std::string* error)
{
    const auto at = files.find(key);
    if (at == files.end())
        return fail(error, "The manifest names no '" + key + "' array.");
    if (!readNumpyArray(base + at->second, out, error))
        return false;
    return true;
}

void writeJsonString(std::ostream& out, const std::string& text)
{
    out << '"';
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out << '\\' << c;
        else if (c == '\n')
            out << "\\n";
        else
            out << c;
    }
    out << '"';
}

void writeArrayJson(std::ostream& out, const char* name,
                    const std::vector<double>& values, int precision)
{
    out << "  \"" << name << "\": [";
    char buffer[64];
    for (std::size_t i = 0; i < values.size(); ++i) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, values[i]);
        out << (i ? ", " : "") << buffer;
    }
    out << "]";
}

} // namespace

const char* electronPhononManifestName()
{
    return "elph_raw.txt";
}

bool loadElectronPhononInput(const std::string& manifestPath,
                             ElectronPhononInput& out, std::string* error)
{
    if (error)
        error->clear();
    out = ElectronPhononInput{};

    std::ifstream in(manifestPath);
    if (!in)
        return fail(error, "Cannot open the electron-phonon manifest '"
                        + manifestPath + "'.");

    std::unordered_map<std::string, std::vector<double>> numbers;
    std::unordered_map<std::string, std::string> files;
    bool sawMagic = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream fields(line);
        std::string key;
        fields >> key;
        if (key == "calango.elph.raw") {
            sawMagic = true;
            continue;
        }
        // A value that parses as a number is data; anything else is a
        // filename. Keeps the manifest free of type declarations.
        std::vector<double> values;
        std::string token;
        bool numeric = true;
        std::vector<std::string> tokens;
        while (fields >> token) {
            tokens.push_back(token);
            // localeSafeParse, NOT std::stod: stod follows LC_NUMERIC, which
            // QApplication sets from the environment. On a machine whose
            // locale uses a decimal comma (pt_BR, de_DE, fr_FR...) stod stops
            // at the '.' — so "1.5514" reads as 1 and the reciprocal lattice
            // silently becomes integers, taking N(E_F) and lambda with it.
            // The CLI escaped this only by being Qt-free; the GUI path did
            // not. Found by the CALPHAD work, which hit the same fault in its
            // own writer and in PdbxFile.
            double value = 0.0;
            if (localeSafeParse(token, &value))
                values.push_back(value);
            else
                numeric = false;
        }
        if (tokens.empty())
            continue;
        if (numeric)
            numbers[key] = values;
        else
            files[key] = tokens.front();
    }
    if (!sawMagic)
        return fail(error, "'" + manifestPath
                        + "' is not a Calango electron-phonon manifest.");

    const auto scalar = [&numbers](const std::string& key, double fallback) {
        const auto at = numbers.find(key);
        return (at == numbers.end() || at->second.empty()) ? fallback
                                                           : at->second.front();
    };

    const auto grid = numbers.find("kgrid");
    if (grid == numbers.end() || grid->second.size() != 3)
        return fail(error, "The manifest has no 3-component 'kgrid'.");
    for (int i = 0; i < 3; ++i) {
        out.kGrid[i] = static_cast<int>(std::lround(grid->second[i]));
        if (out.kGrid[i] <= 0)
            return fail(error, "The k-grid in the manifest is not positive.");
    }

    const auto reciprocal = numbers.find("reciprocal");
    if (reciprocal == numbers.end() || reciprocal->second.size() != 9)
        return fail(error, "The manifest has no 9-component 'reciprocal' "
                           "(b1, b2, b3 as rows, inverse Angstrom).");
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out.reciprocal[i][j] = reciprocal->second[i * 3 + j];

    out.spins = static_cast<int>(std::lround(scalar("spins", 1.0)));
    out.bands = static_cast<int>(std::lround(scalar("bands", 0.0)));
    out.qCount = static_cast<int>(std::lround(scalar("qcount", 0.0)));
    out.modes = static_cast<int>(std::lround(scalar("modes", 0.0)));
    out.fermiLevelEv = scalar("fermi", 0.0);
    out.temperatureK = scalar("temperature", 300.0);
    out.phononSmearingEv = scalar("smearing", out.phononSmearingEv);
    out.muStar = scalar("mu_star", out.muStar);
    out.plasmaFrequencyEv =
        scalar("plasma_frequency", out.plasmaFrequencyEv);
    const auto points = numbers.find("spectrum_points");
    if (points != numbers.end() && !points->second.empty())
        out.spectrumPoints = static_cast<int>(std::lround(points->second.front()));

    if (out.bands <= 0 || out.qCount <= 0 || out.modes <= 0)
        return fail(error, "The manifest is missing 'bands', 'qcount' or "
                           "'modes'.");

    const std::string base = directoryOf(manifestPath);
    const std::size_t nk = out.kPointCount();
    const std::size_t ns = static_cast<std::size_t>(out.spins);
    const std::size_t nb = static_cast<std::size_t>(out.bands);
    const std::size_t nq = static_cast<std::size_t>(out.qCount);
    const std::size_t nm = static_cast<std::size_t>(out.modes);

    NumpyArray array;
    if (!loadArray(base, files, "eigenvalues", array, error))
        return false;
    if (array.elementCount() != ns * nk * nb)
        return fail(error, "The eigenvalue array holds "
                        + std::to_string(array.elementCount())
                        + " values but the mesh declares "
                        + std::to_string(ns * nk * nb) + ".");
    out.eigenvalues = std::move(array.values);

    if (!loadArray(base, files, "frequencies", array, error))
        return false;
    if (array.elementCount() != nq * nm)
        return fail(error, "The phonon frequency array holds "
                        + std::to_string(array.elementCount())
                        + " values but the mesh declares "
                        + std::to_string(nq * nm) + ".");
    out.phononFrequenciesEv = std::move(array.values);

    if (!loadArray(base, files, "kplusq", array, error))
        return false;
    if (array.elementCount() != nq * nk)
        return fail(error, "The k+q map holds "
                        + std::to_string(array.elementCount())
                        + " entries but the mesh declares "
                        + std::to_string(nq * nk) + ".");
    out.kPlusQ.resize(array.elementCount());
    for (std::size_t i = 0; i < array.values.size(); ++i) {
        const long index = std::lround(array.values[i]);
        if (index < 0 || static_cast<std::size_t>(index) >= nk)
            return fail(error, "The k+q map points outside the k-mesh at "
                            "entry " + std::to_string(i) + ".");
        out.kPlusQ[i] = static_cast<int>(index);
    }

    // |g|^2 last: it is the array that costs something to read, so every
    // cheap check that could reject the run has already run.
    if (!loadArray(base, files, "gsquared", array, error))
        return false;
    if (array.elementCount() != ns * nq * nk * nm * nb * nb)
        return fail(error, "The matrix-element array holds "
                        + std::to_string(array.elementCount())
                        + " values but the mesh declares "
                        + std::to_string(ns * nq * nk * nm * nb * nb)
                        + " (spins x q x k x modes x bands x bands).");
    out.gSquaredEv2 = std::move(array.values);

    return true;
}

bool writeElectronPhononResult(const std::string& path,
                               const ElectronPhononResult& result,
                               std::string* error)
{
    if (error)
        error->clear();
    std::ofstream out(path);
    if (!out)
        return fail(error, "Cannot write '" + path + "'.");
    // 17 significant digits round-trips a double exactly. The default 6 does
    // not, and it silently breaks the EXACT relations this file is supposed
    // to preserve: hbar/tau == rate and rate == 2*pi*lambda*k_B*T both fail a
    // 1e-6 check when the operands have been rounded to six figures. The Al
    // benchmark caught exactly that.
    out << std::setprecision(17);

    out << "{\n";
    out << "  \"ok\": " << (result.ok ? "true" : "false") << ",\n";
    // The integration method is recorded because the number it produces is
    // not comparable with the smeared one this module used to write, and a
    // stored result with no method on it is unreadable a year later.
    out << "  \"integration\": \"tetrahedron\",\n";
    out << "  \"dos_at_fermi\": " << result.dosAtFermi << ",\n";
    out << "  \"lambda\": " << result.lambda << ",\n";
    out << "  \"omega_log_eV\": " << result.omegaLogEv << ",\n";
    out << "  \"temperature_K\": " << result.temperatureK << ",\n";
    out << "  \"scattering_rate_eV\": " << result.scatteringRateEv << ",\n";
    out << "  \"relaxation_time_fs\": " << result.relaxationTimeFs << ",\n";
    out << "  \"drude_rate_eV\": " << result.drudeRateEv << ",\n";
    out << "  \"debye_temperature_K\": " << result.debyeTemperatureK << ",\n";
    out << "  \"excluded_modes\": " << result.excludedModes << ",\n";
    out << "  \"omega_bar2_eV\": " << result.omegaBar2Ev << ",\n";
    out << "  \"mass_enhancement\": " << result.massEnhancement << ",\n";
    out << "  \"occupied_bandwidth_eV\": " << result.occupiedBandwidthEv
        << ",\n";
    out << "  \"retardation_log\": " << result.retardationLog << ",\n";

    // T_c and friends. Nested rather than flattened because the whole block
    // is conditional: `ok` false means the material is not a phonon-mediated
    // superconductor at this coupling, which is a result about the material
    // and not a failure of the calculation around it.
    const auto& sc = result.superconductivity;
    out << "  \"superconductivity\": {\n";
    out << "    \"ok\": " << (sc.ok ? "true" : "false") << ",\n";
    out << "    \"mu_star\": " << sc.muStar << ",\n";
    out << "    \"tc_allen_dynes_K\": " << sc.tcAllenDynesCorrectedK << ",\n";
    out << "    \"tc_allen_dynes_uncorrected_K\": " << sc.tcAllenDynesK
        << ",\n";
    out << "    \"tc_mcmillan_K\": " << sc.tcMcMillanK << ",\n";
    out << "    \"f1\": " << sc.f1 << ",\n";
    out << "    \"f2\": " << sc.f2 << ",\n";
    out << "    \"gap_meV\": " << sc.gapMeV << ",\n";
    out << "    \"gap_ratio\": " << sc.gapRatio << ",\n";
    out << "    \"tc_vs_mu_star\": [";
    for (std::size_t i = 0; i < sc.tcVsMuStar.size(); ++i)
        out << (i ? ", [" : "[") << sc.tcVsMuStar[i].first << ", "
            << sc.tcVsMuStar[i].second << "]";
    out << "],\n";
    out << "    \"warnings\": [";
    for (std::size_t i = 0; i < sc.warnings.size(); ++i) {
        out << (i ? ", " : "");
        writeJsonString(out, sc.warnings[i]);
    }
    out << "]\n  },\n";
    out << "  \"lambda_transport\": " << result.lambdaTransport << ",\n";
    out << "  \"scattering_rate_transport_eV\": "
        << result.scatteringRateTransportEv << ",\n";
    out << "  \"relaxation_time_transport_fs\": "
        << result.relaxationTimeTransportFs << ",\n";
    out << "  \"resistivity_micro_ohm_cm\": " << result.resistivityMicroOhmCm
        << ",\n";
    writeArrayJson(out, "alpha2F_transport", result.alpha2FTransport, 8);
    out << ",\n";
    writeArrayJson(out, "lambda_per_mode", result.lambdaPerMode, 8);
    out << ",\n";
    writeArrayJson(out, "linewidths_eV", result.linewidthsEv, 8);
    out << ",\n";
    writeArrayJson(out, "omega_eV", result.omegaEv, 8);
    out << ",\n";
    writeArrayJson(out, "alpha2F", result.alpha2F, 8);
    out << ",\n";
    writeArrayJson(out, "weight_per_mode", result.weightPerMode, 10);
    out << ",\n";
    out << "  \"warnings\": [";
    for (std::size_t i = 0; i < result.warnings.size(); ++i) {
        out << (i ? ", " : "");
        writeJsonString(out, result.warnings[i]);
    }
    out << "]\n}\n";
    return out.good() ? true : fail(error, "Failed while writing '" + path + "'.");
}

bool postProcessElectronPhonon(const std::string& directory,
                               ElectronPhononResult& result,
                               std::string* error)
{
    std::string base = directory;
    if (!base.empty() && base.back() != '/' && base.back() != '\\')
        base += '/';

    ElectronPhononInput input;
    if (!loadElectronPhononInput(base + electronPhononManifestName(), input,
                                 error))
        return false;

    result = analyzeElectronPhonon(input);
    // Written even when the analysis refused: the warnings are the useful
    // output in that case, and a missing epc.json reads like a crash.
    if (!writeElectronPhononResult(base + "epc.json", result, error))
        return false;
    if (!result.ok) {
        if (error && error->empty())
            *error = result.warnings.empty()
                ? std::string("The electron-phonon analysis did not converge.")
                : result.warnings.front();
        return false;
    }
    return true;
}

} // namespace calango::core
