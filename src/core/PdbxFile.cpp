#include "core/PdbxFile.hpp"

#include "core/Element.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <stdexcept>
#include <vector>

namespace calango::core {
namespace PdbxFile {

namespace {

/// Split a STAR/CIF data line into values, honouring the single- and
/// double-quoted forms the format uses for values containing spaces
/// (`'N-terminal ACE'`). Nothing fancier is needed: the `_atom_site` loop never
/// uses the multi-line semicolon form.
std::vector<std::string> splitCifLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
            ++i;
        if (i >= line.size())
            break;
        if (line[i] == '\'' || line[i] == '"') {
            const char quote = line[i++];
            const std::size_t start = i;
            while (i < line.size() && line[i] != quote)
                ++i;
            fields.emplace_back(line, start, i - start);
            if (i < line.size())
                ++i; // closing quote
        } else {
            const std::size_t start = i;
            while (i < line.size()
                   && !std::isspace(static_cast<unsigned char>(line[i])))
                ++i;
            fields.emplace_back(line, start, i - start);
        }
    }
    return fields;
}

std::string trimmed(const std::string& text)
{
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

/// CIF spells "no value" as `?` and "not applicable" as `.`; both mean the
/// field is absent, and neither should end up in a residue name.
bool isNullValue(const std::string& value)
{
    return value.empty() || value == "?" || value == ".";
}

double toDouble(const std::string& value, double fallback = 0.0)
{
    if (isNullValue(value))
        return fallback;
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        return fallback;
    }
}

int toInt(const std::string& value, int fallback = 0)
{
    if (isNullValue(value))
        return fallback;
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        return fallback;
    }
}

/// Atomic number from a PDBx `type_symbol` ("C", "N", "FE", "ZN"). The column
/// is upper-case by convention, which Elements::atomicNumber already handles
/// case-insensitively.
int elementFromSymbol(const std::string& symbol)
{
    return Elements::atomicNumber(trimmed(symbol));
}

} // namespace

bool looksLikePdbx(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
        return false;
    // Both flavours are large; the discriminating tag sits in the header half,
    // so a bounded scan is enough and keeps this cheap on a 20 MB entry.
    std::string line;
    int scanned = 0;
    while (std::getline(in, line) && scanned++ < 200000) {
        if (line.rfind("_atom_site.Cartn_x", 0) == 0)
            return true;
        // A small-molecule CIF: fractional coordinates under the underscore
        // (not dot) spelling. Decide against PDBx as soon as we see one.
        if (line.rfind("_atom_site_fract_x", 0) == 0)
            return false;
    }
    return false;
}

Structure read(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("Could not open '" + path + "'.");

    // -- Pass: walk the file, capture the cell keys and the _atom_site loop --
    double cellLengths[3] = {0.0, 0.0, 0.0};
    double cellAngles[3] = {90.0, 90.0, 90.0};

    std::map<std::string, int> columns; ///< _atom_site.<key> -> field index
    std::vector<Atom> atoms;
    std::vector<ResidueInfo> residues;

    std::string line;
    bool inAtomLoop = false;
    int columnCount = 0;
    while (std::getline(in, line)) {
        const std::string text = trimmed(line);
        if (text.empty() || text[0] == '#') {
            // A comment or blank line terminates the atom loop.
            if (inAtomLoop && !columns.empty() && !atoms.empty())
                inAtomLoop = false;
            continue;
        }

        if (!inAtomLoop) {
            if (text.rfind("_cell.length_a", 0) == 0)
                cellLengths[0] = toDouble(splitCifLine(text).back());
            else if (text.rfind("_cell.length_b", 0) == 0)
                cellLengths[1] = toDouble(splitCifLine(text).back());
            else if (text.rfind("_cell.length_c", 0) == 0)
                cellLengths[2] = toDouble(splitCifLine(text).back());
            else if (text.rfind("_cell.angle_alpha", 0) == 0)
                cellAngles[0] = toDouble(splitCifLine(text).back(), 90.0);
            else if (text.rfind("_cell.angle_beta", 0) == 0)
                cellAngles[1] = toDouble(splitCifLine(text).back(), 90.0);
            else if (text.rfind("_cell.angle_gamma", 0) == 0)
                cellAngles[2] = toDouble(splitCifLine(text).back(), 90.0);
        }

        // Header of the atom-site loop: one `_atom_site.<key>` per line, in
        // the order the data rows use.
        if (text.rfind("_atom_site.", 0) == 0) {
            columns[text.substr(std::string("_atom_site.").size())] = columnCount++;
            inAtomLoop = true;
            continue;
        }
        if (!inAtomLoop || columns.empty())
            continue;
        // A different loop or key started — the atom rows are over.
        if (text[0] == '_' || text.rfind("loop_", 0) == 0
            || text.rfind("data_", 0) == 0) {
            inAtomLoop = false;
            continue;
        }

        const std::vector<std::string> fields = splitCifLine(text);
        if (static_cast<int>(fields.size()) < columnCount)
            continue; // truncated row
        const auto field = [&fields, &columns](const char* key) -> std::string {
            const auto it = columns.find(key);
            return it == columns.end() ? std::string() : fields[it->second];
        };

        // Only the first model of a multi-model (NMR / ensemble) entry: the
        // later ones are alternative solutions of the SAME molecule, and
        // loading them as extra atoms would silently multiply the structure.
        if (const std::string model = field("pdbx_PDB_model_num");
            !isNullValue(model) && toInt(model, 1) != 1)
            continue;
        // Alternate conformations likewise: take blank or "A" only.
        if (const std::string alt = field("label_alt_id");
            !isNullValue(alt) && alt != "A")
            continue;

        const int z = elementFromSymbol(field("type_symbol"));
        if (z <= 0)
            continue; // unknown species (e.g. a placeholder row)

        Atom atom;
        atom.atomicNumber = z;
        atom.position = {toDouble(field("Cartn_x")), toDouble(field("Cartn_y")),
                         toDouble(field("Cartn_z"))};
        atoms.push_back(atom);

        // Prefer the AUTHOR numbering: it is the numbering the literature and
        // every figure caption use, whereas the label_* scheme is an internal
        // re-indexing that will not match anything the user has read.
        ResidueInfo info;
        std::string chain = field("auth_asym_id");
        if (isNullValue(chain))
            chain = field("label_asym_id");
        std::string comp = field("auth_comp_id");
        if (isNullValue(comp))
            comp = field("label_comp_id");
        std::string name = field("auth_atom_id");
        if (isNullValue(name))
            name = field("label_atom_id");
        std::string seq = field("auth_seq_id");
        if (isNullValue(seq))
            seq = field("label_seq_id");
        info.chain = isNullValue(chain) ? std::string() : chain;
        info.residue = isNullValue(comp) ? std::string() : comp;
        info.atomName = isNullValue(name) ? std::string() : name;
        info.residueSeq = toInt(seq);
        residues.push_back(std::move(info));
    }

    if (atoms.empty())
        throw std::runtime_error(
            "No atom sites found in '" + path
            + "'. The file has no usable _atom_site loop — it may be a header-"
              "only PDBx entry, or a small-molecule CIF.");

    Structure structure;
    for (const Atom& atom : atoms)
        structure.addAtom(atom);
    structure.setResidues(std::move(residues));

    // Many PDBx entries (cryo-EM, NMR, and this one) carry the placeholder
    // 1 Å cubic cell that means "no crystallographic cell". Treating that as a
    // real lattice would put every atom thousands of cells away from the
    // origin and make the periodic-image machinery nonsense, so it is dropped.
    const bool realCell = cellLengths[0] > 1.5 && cellLengths[1] > 1.5
        && cellLengths[2] > 1.5;
    if (realCell) {
        constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
        const double alpha = cellAngles[0] * kDegToRad;
        const double beta = cellAngles[1] * kDegToRad;
        const double gamma = cellAngles[2] * kDegToRad;
        // Standard crystallographic -> Cartesian construction: a along x, b in
        // the xy plane, c placed to satisfy both remaining angles.
        const Vec3 a{cellLengths[0], 0.0, 0.0};
        const Vec3 b{cellLengths[1] * std::cos(gamma),
                     cellLengths[1] * std::sin(gamma), 0.0};
        const double cx = cellLengths[2] * std::cos(beta);
        const double cy = cellLengths[2]
            * (std::cos(alpha) - std::cos(beta) * std::cos(gamma))
            / std::sin(gamma);
        const double cz2 = cellLengths[2] * cellLengths[2] - cx * cx - cy * cy;
        const Vec3 c{cx, cy, cz2 > 0.0 ? std::sqrt(cz2) : 0.0};
        structure.setCell(UnitCell(a, b, c, {true, true, true}));
    }
    return structure;
}

void write(const Structure& structure, const std::string& path)
{
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("Could not write '" + path + "'.");

    out << "data_calango\n"
           "#\n"
           "_entry.id   calango\n"
           "#\n";

    if (structure.cell().isDefined()) {
        const auto& v = structure.cell().vectors();
        const auto angle = [](const Vec3& p, const Vec3& q) {
            const double lengths = p.norm() * q.norm();
            if (lengths < 1e-12)
                return 90.0;
            return std::acos(std::clamp(p.dot(q) / lengths, -1.0, 1.0))
                * 180.0 / 3.14159265358979323846;
        };
        out << "_cell.length_a    " << v[0].norm() << "\n"
            << "_cell.length_b    " << v[1].norm() << "\n"
            << "_cell.length_c    " << v[2].norm() << "\n"
            << "_cell.angle_alpha " << angle(v[1], v[2]) << "\n"
            << "_cell.angle_beta  " << angle(v[0], v[2]) << "\n"
            << "_cell.angle_gamma " << angle(v[0], v[1]) << "\n"
               "#\n";
    }

    // The column set a reader needs to reconstruct what we loaded: element,
    // coordinates, and both the label_* and auth_* annotation (written
    // identically, since we only ever carry one numbering).
    out << "loop_\n"
           "_atom_site.group_PDB\n"
           "_atom_site.id\n"
           "_atom_site.type_symbol\n"
           "_atom_site.label_atom_id\n"
           "_atom_site.label_alt_id\n"
           "_atom_site.label_comp_id\n"
           "_atom_site.label_asym_id\n"
           "_atom_site.label_seq_id\n"
           "_atom_site.Cartn_x\n"
           "_atom_site.Cartn_y\n"
           "_atom_site.Cartn_z\n"
           "_atom_site.occupancy\n"
           "_atom_site.B_iso_or_equiv\n"
           "_atom_site.auth_seq_id\n"
           "_atom_site.auth_comp_id\n"
           "_atom_site.auth_asym_id\n"
           "_atom_site.auth_atom_id\n"
           "_atom_site.pdbx_PDB_model_num\n";

    out.setf(std::ios::fixed, std::ios::floatfield);
    const auto& atoms = structure.atoms();
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        const Atom& atom = atoms[i];
        const ResidueInfo& info = structure.residue(i);
        const std::string symbol = atom.symbol();
        // Fall back to a single synthetic chain of one-atom residues rather
        // than writing "?" everywhere: a PDBx file whose residues are all null
        // is loadable but useless, and the element name is at least true.
        const std::string chain = info.chain.empty() ? std::string("A") : info.chain;
        const std::string comp = info.residue.empty() ? symbol : info.residue;
        const std::string name = info.atomName.empty() ? symbol : info.atomName;
        const int seq = info.residueSeq != 0 ? info.residueSeq
                                             : static_cast<int>(i) + 1;
        // ATOM is reserved for polymer residues; anything without residue
        // annotation is a HETATM by definition.
        const char* group = info.residue.empty() ? "HETATM" : "ATOM";

        out.precision(3);
        out << group << ' ' << (i + 1) << ' ' << symbol << ' ' << name
            << " . " << comp << ' ' << chain << ' ' << seq << ' '
            << atom.position.x << ' ' << atom.position.y << ' '
            << atom.position.z << " 1.00 0.00 " << seq << ' ' << comp << ' '
            << chain << ' ' << name << " 1\n";
    }
    out << "#\n";
    if (!out)
        throw std::runtime_error("Failed while writing '" + path + "'.");
}

} // namespace PdbxFile
} // namespace calango::core
