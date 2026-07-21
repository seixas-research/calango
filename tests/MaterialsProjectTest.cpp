// Integration test for the Materials Project client: resolve MP_API_KEY
// the same way the app does ($MP_API_KEY, then ~/.env), fetch mp-1434
// live, and verify the returned unit cell and atomic sites parse into
// core::Structure. Guards the 2026 endpoint migration (the path-suffix
// REST form now returns an "upgrade to mp-api" error blob).
//
// Note: the Materials Project database maps mp-1434 to MoS2 (1 Mo + 2 S
// sites) — the assertions below pin what the live API serves.
//
// Exit codes: 0 = pass, 1 = fail, 77 = skipped (no API key configured).

#include "python_bridge/MaterialsProject.hpp"
#include "python_bridge/PythonEngine.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>

namespace {

/// Minimal ~/.env reader mirroring gui::parseEnvFile: KEY=VALUE lines,
/// '#' comments, optional "export " prefix, optional quotes.
std::string keyFromEnvFile()
{
    const char* home = std::getenv("HOME");
    if (!home)
        return {};
    std::ifstream file(std::string(home) + "/.env");
    std::string line;
    while (std::getline(file, line)) {
        auto trim = [](std::string s) {
            const auto begin = s.find_first_not_of(" \t\r");
            const auto end = s.find_last_not_of(" \t\r");
            return begin == std::string::npos ? std::string()
                                              : s.substr(begin, end - begin + 1);
        };
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        if (line.rfind("export ", 0) == 0)
            line = trim(line.substr(7));
        const auto equals = line.find('=');
        if (equals == std::string::npos || trim(line.substr(0, equals)) != "MP_API_KEY")
            continue;
        std::string value = trim(line.substr(equals + 1));
        if (value.size() >= 2
            && ((value.front() == '"' && value.back() == '"')
                || (value.front() == '\'' && value.back() == '\'')))
            value = value.substr(1, value.size() - 2);
        return value;
    }
    return {};
}

} // namespace

int main()
{
    using namespace calango;

    std::string apiKey;
    if (const char* env = std::getenv("MP_API_KEY"); env && *env)
        apiKey = env;
    else
        apiKey = keyFromEnvFile();
    if (apiKey.empty()) {
        std::printf("SKIP: no MP_API_KEY in the environment or ~/.env\n");
        return 77;
    }

    pybridge::PythonEngine python;
    if (!python.aseAvailable()) {
        std::fprintf(stderr, "FAIL: ASE not importable\n");
        return 1;
    }

    core::Structure structure;
    try {
        structure = pybridge::MaterialsProject::fetchStructure("mp-1434", apiKey);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: fetch threw:\n%s\n", e.what());
        return 1;
    }

    // Sites: mp-1434 is MoS2 — 1 Mo + 2 S per cell.
    std::map<int, int> counts;
    for (const auto& atom : structure.atoms())
        ++counts[atom.atomicNumber];
    if (structure.size() != 3 || counts[42] != 1 || counts[16] != 2) {
        std::fprintf(stderr, "FAIL: expected 1 Mo + 2 S sites, got %zu atoms "
                             "(Mo=%d, S=%d)\n",
                     structure.size(), counts[42], counts[16]);
        return 1;
    }

    // Unit cell: defined, periodic, physically sensible volume.
    if (!structure.cell().isDefined()) {
        std::fprintf(stderr, "FAIL: no unit cell parsed\n");
        return 1;
    }
    const auto& v = structure.cell().vectors();
    const double a = v[0].norm(), b = v[1].norm(), c = v[2].norm();
    const double volume = structure.cell().volume();
    if (a < 1.0 || b < 1.0 || c < 1.0 || volume < 10.0 || volume > 1000.0) {
        std::fprintf(stderr,
                     "FAIL: implausible cell a=%.3f b=%.3f c=%.3f V=%.2f\n",
                     a, b, c, volume);
        return 1;
    }

    std::printf("PASS: mp-1434 (MoS2) — 3 sites (1 Mo + 2 S), "
                "cell a=%.3f b=%.3f c=%.3f Å, V=%.2f Å³\n",
                a, b, c, volume);
    return 0;
}
