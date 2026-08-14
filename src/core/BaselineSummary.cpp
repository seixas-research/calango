#include "core/BaselineSummary.hpp"

#include "core/CalculatorConfig.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace calango::core {

namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// First integer at or after `pos`, or -1. Deliberately hand-rolled rather
/// than std::stoi on a substring: the surrounding text carries brackets,
/// colons and commas, and this machine's locale is pt_BR (see
/// LocaleSafeNumber.hpp) — no locale-sensitive parse belongs anywhere near it.
int intAfter(const std::string& text, std::size_t pos)
{
    while (pos < text.size() && !std::isdigit(static_cast<unsigned char>(text[pos]))
           && text[pos] != '\n')
        ++pos;
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos])))
        return -1;
    int value = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        value = value * 10 + (text[pos] - '0');
        ++pos;
    }
    return value;
}

/// The integer following the first occurrence of `key`, or -1 if absent.
int intAfterKey(const std::string& text, const std::string& key)
{
    const std::size_t at = text.find(key);
    return at == std::string::npos ? -1 : intAfter(text, at + key.size());
}

/// The GPAW text log of a run, if the directory holds one.
///
/// Named files first (what Calango's own generators write), then any .txt/.out
/// carrying GPAW's banner — a directory produced by a hand-run job still counts,
/// and misidentifying some other .txt would be worse than finding nothing.
std::string gpawLog(const std::filesystem::path& dir, std::string& which)
{
    for (const char* name : {"gpaw.out", "gpaw.txt", "gpaw_wannier.txt",
                             "single_point.txt"}) {
        const std::string text = readFile(dir / name);
        if (!text.empty()) {
            which = name;
            return text;
        }
    }
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec)
            break;
        if (!entry.is_regular_file(ec))
            continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".txt" && ext != ".out")
            continue;
        const std::string text = readFile(entry.path());
        // GPAW's banner. Present in every log it writes, and in nothing else.
        if (text.find("|__ |  _|__|  || |") != std::string::npos
            || text.find("Number of atoms:") != std::string::npos) {
            which = entry.path().filename().string();
            return text;
        }
    }
    return {};
}

/// A JSON scalar, without pulling a parser into core for four fields.
/// `calculator.json` is written by Calango as a compact object of flat
/// key/value pairs, so this reads exactly the shape that is produced.
bool jsonHasKey(const std::string& json, const std::string& key)
{
    return json.find("\"" + key + "\"") != std::string::npos;
}

/// Value of a boolean key; only meaningful when jsonHasKey() is true.
bool jsonBool(const std::string& json, const std::string& key)
{
    const std::size_t at = json.find("\"" + key + "\"");
    if (at == std::string::npos)
        return false;
    std::size_t pos = json.find(':', at);
    if (pos == std::string::npos)
        return false;
    ++pos;
    while (pos < json.size()
           && std::isspace(static_cast<unsigned char>(json[pos])))
        ++pos;
    return json.compare(pos, 4, "true") == 0;
}

int jsonInt(const std::string& json, const std::string& key)
{
    const std::size_t at = json.find("\"" + key + "\"");
    if (at == std::string::npos)
        return -1;
    return intAfter(json, at + key.size() + 2);
}

std::string jsonString(const std::string& json, const std::string& key)
{
    const std::size_t at = json.find("\"" + key + "\"");
    if (at == std::string::npos)
        return {};
    const std::size_t colon = json.find(':', at);
    if (colon == std::string::npos)
        return {};
    const std::size_t open = json.find('"', colon);
    if (open == std::string::npos)
        return {};
    const std::size_t close = json.find('"', open + 1);
    if (close == std::string::npos)
        return {};
    return json.substr(open + 1, close - open - 1);
}

void appendEvidence(std::string& evidence, const std::string& line)
{
    if (!evidence.empty())
        evidence += "\n";
    evidence += line;
}

} // namespace

BaselineSummary readBaselineSummary(const std::string& jobDir)
{
    BaselineSummary summary;
    std::error_code ec;
    const std::filesystem::path dir(jobDir);
    if (jobDir.empty() || !std::filesystem::is_directory(dir, ec)) {
        summary.evidence = "no such run directory";
        return summary;
    }

    // ---- What Calango asked for -------------------------------------------
    const std::string calculator = readFile(dir / "calculator.json");
    if (!calculator.empty()) {
        summary.engine = jsonString(calculator, "engine");
        summary.engineKind = jsonInt(calculator, "engine_kind");
        const std::size_t kpts = calculator.find("\"kpts\"");
        if (kpts != std::string::npos) {
            std::size_t at = calculator.find('[', kpts);
            for (int i = 0; i < 3 && at != std::string::npos; ++i) {
                const int v = intAfter(calculator, at + 1);
                if (v < 0)
                    break;
                summary.kpts[i] = v;
                at = calculator.find(',', at + 1);
            }
        }
        if (const int nbands = jsonInt(calculator, "nbands"); nbands > 0)
            summary.bands = nbands;
    }

    const bool gpaw = summary.engineKind < 0
        || summary.engineKind == static_cast<int>(CalculatorKind::Gpaw);

    // ---- What the run actually did ----------------------------------------
    // Preferred over the request above, and by a wide margin: the BZ and IBZ
    // counts do not describe an intention, they describe the k-set that was
    // stored — which is the thing the wannierization will or will not find.
    if (gpaw) {
        std::string which;
        const std::string log = gpawLog(dir, which);
        if (!log.empty()) {
            const int bz = intAfterKey(log, "Number of BZ points:");
            const int ibz = intAfterKey(log, "Number of IBZ points:");
            const int ops = intAfterKey(log, "Number of symmetries:");
            const int bands = intAfterKey(log, "Bands:");
            if (bz > 0)
                summary.bzPoints = bz;
            if (ibz > 0)
                summary.ibzPoints = ibz;
            if (ops > 0)
                summary.symmetryOperations = ops;
            if (bands > 0)
                summary.bands = bands;
            // Monkhorst-Pack size: [16, 16, 16]
            if (const std::size_t mp = log.find("Monkhorst-Pack size:");
                mp != std::string::npos) {
                std::size_t at = log.find('[', mp);
                for (int i = 0; i < 3 && at != std::string::npos; ++i) {
                    const int v = intAfter(log, at + 1);
                    if (v < 0)
                        break;
                    summary.kpts[i] = v;
                    at = log.find(',', at + 1);
                }
            }
            if (summary.bzPoints > 0 && summary.ibzPoints > 0) {
                summary.symmetry = summary.ibzPoints == summary.bzPoints
                    ? SymmetryState::Off
                    : SymmetryState::On;
                appendEvidence(summary.evidence,
                               which + ": " + std::to_string(summary.ibzPoints)
                                   + " of " + std::to_string(summary.bzPoints)
                                   + " BZ points kept");
            }
            if (summary.bands > 0)
                appendEvidence(summary.evidence,
                               which + ": " + std::to_string(summary.bands)
                                   + " bands");
        }
    }

    // ---- Fall back to the request ------------------------------------------
    // Only when the key is PRESENT. An older baseline's calculator.json simply
    // lacks it, and reading that absence as `false` would report symmetry ON
    // for a run that may well have had it off.
    if (summary.symmetry == SymmetryState::Unknown && gpaw
        && jsonHasKey(calculator, "symmetry_off")) {
        const bool off = jsonBool(calculator, "symmetry_off");
        summary.symmetry = off ? SymmetryState::Off : SymmetryState::On;
        appendEvidence(summary.evidence,
                       std::string("calculator.json: symmetry_off = ")
                           + (off ? "true" : "false"));
    }

    // ---- Last resort: the script that was run ------------------------------
    if (summary.symmetry == SymmetryState::Unknown && gpaw) {
        const std::string script = readFile(dir / "run.py");
        if (!script.empty()) {
            const bool off = script.find("symmetry=\"off\"") != std::string::npos
                || script.find("symmetry='off'") != std::string::npos;
            if (off) {
                summary.symmetry = SymmetryState::Off;
                appendEvidence(summary.evidence, "run.py: symmetry=\"off\"");
            }
        }
    }

    if (summary.evidence.empty())
        appendEvidence(summary.evidence,
                       "no engine log, calculator.json symmetry flag, or "
                       "script found in this directory");
    return summary;
}

} // namespace calango::core
