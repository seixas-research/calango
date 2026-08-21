#include "dftb/DftbStructureIo.hpp"

#include "core/Element.hpp"
#include "core/LocaleSafeNumber.hpp"

#include <fstream>
#include <sstream>

namespace calango::dftb {

namespace {

/// The text between `key="` and the next `"` in `line`, or empty if `key=`
/// is not present.
std::string extractQuoted(const std::string& line, const std::string& key)
{
    const std::string needle = key + "=\"";
    const auto start = line.find(needle);
    if (start == std::string::npos)
        return {};
    const auto valueStart = start + needle.size();
    const auto end = line.find('"', valueStart);
    if (end == std::string::npos)
        return {};
    return line.substr(valueStart, end - valueStart);
}

} // namespace

dft::Outcome loadExtxyzStructure(const std::string& path, core::Structure& out)
{
    std::ifstream file(path);
    if (!file)
        return dft::Outcome::invalid("cannot open structure file: " + path);

    std::string countLine;
    if (!std::getline(file, countLine))
        return dft::Outcome::invalid("empty structure file: " + path);
    int natoms = 0;
    {
        std::istringstream stream(countLine);
        stream >> natoms;
    }
    if (natoms <= 0)
        return dft::Outcome::invalid("structure file declares no atoms: "
                                      + path);

    std::string commentLine;
    if (!std::getline(file, commentLine))
        return dft::Outcome::invalid("structure file is missing its "
                                      "Lattice/pbc comment line: " + path);

    out = core::Structure{};

    const std::string latticeText = extractQuoted(commentLine, "Lattice");
    const std::string pbcText = extractQuoted(commentLine, "pbc");
    std::array<bool, 3> pbc{false, false, false};
    if (!pbcText.empty()) {
        std::istringstream stream(pbcText);
        std::string token;
        for (int i = 0; i < 3 && stream >> token; ++i)
            pbc[static_cast<std::size_t>(i)] =
                (token == "T" || token == "True" || token == "true" || token == "1");
    }
    if (!latticeText.empty()) {
        std::istringstream stream(latticeText);
        double v[9] = {};
        for (double& value : v) {
            std::string token;
            if (!(stream >> token) || !core::localeSafeParse(token, &value))
                return dft::Outcome::invalid(
                    "malformed Lattice=\"...\" (need 9 numbers): " + path);
        }
        out.setCell(core::UnitCell({v[0], v[1], v[2]}, {v[3], v[4], v[5]},
                                    {v[6], v[7], v[8]}, pbc));
    }

    for (int i = 0; i < natoms; ++i) {
        std::string atomLine;
        if (!std::getline(file, atomLine))
            return dft::Outcome::invalid(
                "structure file truncated: expected " + std::to_string(natoms)
                + " atom lines, found " + std::to_string(i) + " (" + path + ")");
        std::istringstream stream(atomLine);
        std::string symbol;
        double x = 0.0, y = 0.0, z = 0.0;
        if (!(stream >> symbol >> x >> y >> z))
            return dft::Outcome::invalid("malformed atom line " + std::to_string(i)
                                          + " in " + path);
        const int z_number = core::Elements::atomicNumber(symbol);
        if (z_number <= 0)
            return dft::Outcome::invalid("unrecognized element symbol '"
                                          + symbol + "' in " + path);
        out.addAtom(core::Atom{z_number, {x, y, z}});
    }

    return dft::Outcome::success();
}

} // namespace calango::dftb
