#include "core/VolumetricData.hpp"

#include <hdf5.h>

#include <algorithm>
#include <charconv>
#include <iterator>
#include <cctype>
#include <cmath>
// cstdint: needed for int32_t below. clangd's unused-includes flags this on
// macOS because libstdc++ (GCC 13+, the Linux .deb build) does NOT pull it
// in transitively the way macOS's libc++ does — removing it builds here and
// breaks there. Do not remove.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace calango::core {

namespace {

constexpr double kBohrToAngstrom = 0.529177210903;

std::runtime_error parseError(const std::string& path, const std::string& what)
{
    return std::runtime_error("Could not read volumetric data from " + path
                              + ":\n" + what);
}

std::string fileStem(const std::string& path)
{
    const auto slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const auto dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

} // namespace

double VolumetricData::minValue() const
{
    return values.empty() ? 0.0 : *std::min_element(values.begin(), values.end());
}

double VolumetricData::maxValue() const
{
    return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end());
}

Vec3 VolumetricData::position(double ix, double iy, double iz) const
{
    // Grid nodes span the box with n points along each axis (period n —
    // crystal convention where node n would repeat node 0).
    const double fx = nx > 0 ? ix / nx : 0.0;
    const double fy = ny > 0 ? iy / ny : 0.0;
    const double fz = nz > 0 ? iz / nz : 0.0;
    return {origin.x + fx * spanA.x + fy * spanB.x + fz * spanC.x,
            origin.y + fx * spanA.y + fy * spanB.y + fz * spanC.y,
            origin.z + fx * spanA.z + fy * spanB.z + fz * spanC.z};
}

double VolumetricData::sample(double ix, double iy, double iz) const
{
    ix = std::clamp(ix, 0.0, static_cast<double>(nx - 1));
    iy = std::clamp(iy, 0.0, static_cast<double>(ny - 1));
    iz = std::clamp(iz, 0.0, static_cast<double>(nz - 1));
    const int x0 = static_cast<int>(ix), y0 = static_cast<int>(iy),
              z0 = static_cast<int>(iz);
    const int x1 = std::min(x0 + 1, nx - 1), y1 = std::min(y0 + 1, ny - 1),
              z1 = std::min(z0 + 1, nz - 1);
    const double tx = ix - x0, ty = iy - y0, tz = iz - z0;

    const auto lerp = [](double a, double b, double t) { return a + (b - a) * t; };
    const double c00 = lerp(at(x0, y0, z0), at(x1, y0, z0), tx);
    const double c10 = lerp(at(x0, y1, z0), at(x1, y1, z0), tx);
    const double c01 = lerp(at(x0, y0, z1), at(x1, y0, z1), tx);
    const double c11 = lerp(at(x0, y1, z1), at(x1, y1, z1), tx);
    return lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
}

double VolumetricData::samplePeriodic(double ix, double iy, double iz) const
{
    const auto wrap = [](double v, int n) {
        v = std::fmod(v, static_cast<double>(n));
        return v < 0.0 ? v + n : v;
    };
    ix = wrap(ix, nx);
    iy = wrap(iy, ny);
    iz = wrap(iz, nz);
    const int x0 = static_cast<int>(ix) % nx, y0 = static_cast<int>(iy) % ny,
              z0 = static_cast<int>(iz) % nz;
    const int x1 = (x0 + 1) % nx, y1 = (y0 + 1) % ny, z1 = (z0 + 1) % nz;
    const double tx = ix - std::floor(ix), ty = iy - std::floor(iy),
                 tz = iz - std::floor(iz);

    const auto lerp = [](double a, double b, double t) { return a + (b - a) * t; };
    const double c00 = lerp(at(x0, y0, z0), at(x1, y0, z0), tx);
    const double c10 = lerp(at(x0, y1, z0), at(x1, y1, z0), tx);
    const double c01 = lerp(at(x0, y0, z1), at(x1, y0, z1), tx);
    const double c11 = lerp(at(x0, y1, z1), at(x1, y1, z1), tx);
    return lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
}

/// Read `count` whitespace-separated doubles out of `file` into `out`.
///
/// The value block of a real grid is millions of numbers, and
/// `istream >> double` is the wrong tool for them: every extraction runs the
/// locale's num_get facet, which costs ~200 ns per value. Pulling the rest of
/// the file into one buffer and running std::from_chars over it is the same
/// parse without that machinery, and turns a multi-second load of a production
/// CHGCAR into a fraction of a second.
///
/// Behaviour matches the stream version exactly, including on malformed input:
/// parsing stops at the first token that is not a number, and the caller sees
/// a short read.
///
/// `visit(index, value)` places each value, so the CHGCAR loader's Fortran
/// transpose stays a one-liner instead of needing a second pass.
template <typename Visit>
bool readDoubleBlock(std::istream& file, std::size_t count, Visit&& visit)
{
    // Everything from the current position on. The header is already consumed
    // by the formatted reads above, so this is exactly the value block.
    //
    // One bulk read() rather than istreambuf_iterator: the iterator form goes
    // through the streambuf a character at a time and costs more than the
    // parsing it feeds.
    const std::streampos here = file.tellg();
    file.seekg(0, std::ios::end);
    const std::streamoff remaining = file.tellg() - here;
    file.seekg(here);
    std::string buffer;
    if (remaining > 0) {
        buffer.resize(static_cast<std::size_t>(remaining));
        file.read(buffer.data(), remaining);
        buffer.resize(static_cast<std::size_t>(file.gcount()));
    }

    const char* p = buffer.data();
    const char* const end = p + buffer.size();
    for (std::size_t i = 0; i < count; ++i) {
        while (p != end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            ++p;
        if (p == end)
            return false;
        double value = 0.0;
        const auto result = std::from_chars(p, end, value);
        if (result.ec != std::errc{})
            return false;
        p = result.ptr;
        visit(i, value);
    }
    return true;
}

VolumetricData VolumetricData::load(const std::string& path)
{
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".cube") == 0)
        return loadCube(path);
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".xsf") == 0)
        return loadXsf(path);
    if ((lower.size() >= 3 && lower.compare(lower.size() - 3, 3, ".h5") == 0)
        || (lower.size() >= 5
            && lower.compare(lower.size() - 5, 5, ".hdf5") == 0))
        return loadHdf5(path);
    if (lower.find("chgcar") != std::string::npos
        || lower.find("locpot") != std::string::npos
        || lower.find("parchg") != std::string::npos
        || lower.find("elfcar") != std::string::npos)
        return loadChgcar(path);
    // Fall back on content sniffing: CUBE files start with two comment
    // lines then "natoms origin"; try cube first, then VASP.
    try {
        return loadCube(path);
    } catch (const std::exception&) {
        return loadChgcar(path);
    }
}

// --- Gaussian cube ---------------------------------------------------------
// line 1-2: comments; line 3: natoms origin(3); lines 4-6: n vx vy vz per
// axis (n > 0: Bohr, n < 0: Angstrom); natoms atom lines; then values with
// z fastest, x slowest (already our layout).
VolumetricData VolumetricData::loadCube(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        throw parseError(path, "file not readable");

    std::string comment1, comment2;
    std::getline(file, comment1);
    std::getline(file, comment2);

    int natoms = 0;
    Vec3 origin;
    file >> natoms >> origin.x >> origin.y >> origin.z;
    if (!file)
        throw parseError(path, "invalid cube header");
    const bool dsetFlag = natoms < 0; // negative: file carries a DSET list
    natoms = std::abs(natoms);

    VolumetricData data;
    Vec3 axes[3];
    int dims[3] = {0, 0, 0};
    bool bohr = true;
    for (int i = 0; i < 3; ++i) {
        file >> dims[i] >> axes[i].x >> axes[i].y >> axes[i].z;
        if (!file || dims[i] == 0)
            throw parseError(path, "invalid cube axis line");
        if (dims[i] < 0) {
            bohr = false; // negative count marks Angstrom units
            dims[i] = -dims[i];
        }
    }
    const double unit = bohr ? kBohrToAngstrom : 1.0;
    data.nx = dims[0];
    data.ny = dims[1];
    data.nz = dims[2];
    data.origin = origin * unit;
    // Cube voxel vectors are per-step; the grid has n steps of extent in
    // the periodic-node convention used by position().
    data.spanA = axes[0] * (unit * dims[0]);
    data.spanB = axes[1] * (unit * dims[1]);
    data.spanC = axes[2] * (unit * dims[2]);

    // Atom records: element, nuclear charge (unused — always equal to z for
    // an all-electron calculation, redundant otherwise), Cartesian position.
    data.atoms.reserve(static_cast<std::size_t>(natoms));
    for (int i = 0; i < natoms; ++i) {
        int z;
        double q, x, y, zz;
        file >> z >> q >> x >> y >> zz;
        data.atoms.push_back({z, Vec3{x, y, zz} * unit});
    }
    if (dsetFlag) {
        int nsets = 0;
        file >> nsets;
        for (int i = 0; i < nsets; ++i) {
            int id;
            file >> id;
        }
    }

    const std::size_t count = static_cast<std::size_t>(data.nx) * data.ny * data.nz;
    data.values.resize(count);
    if (!readDoubleBlock(file, count,
                         [&](std::size_t i, double v) { data.values[i] = v; }))
        throw parseError(path, "truncated cube value block");
    data.label = fileStem(path);
    data.sourceFormat = "cube";
    return data;
}

// --- VASP CHGCAR / LOCPOT --------------------------------------------------
// POSCAR-style header (scale, 3 lattice vectors, species counts, positions),
// a blank line, "nx ny nz", then values with x fastest (Fortran order) —
// transposed into our z-fastest layout. CHGCAR stores rho*V_cell; we keep
// raw values (isovalues are relative anyway).
VolumetricData VolumetricData::loadChgcar(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        throw parseError(path, "file not readable");

    std::string line;
    std::getline(file, line); // title
    double scale = 1.0;
    file >> scale;
    Vec3 a, b, c;
    file >> a.x >> a.y >> a.z >> b.x >> b.y >> b.z >> c.x >> c.y >> c.z;
    if (!file)
        throw parseError(path, "invalid POSCAR lattice header");
    std::getline(file, line); // rest of the c-vector line

    // Species symbols (VASP5) or counts directly (VASP4).
    std::getline(file, line);
    std::istringstream speciesLine(line);
    std::vector<std::string> tokens;
    for (std::string token; speciesLine >> token;)
        tokens.push_back(token);
    bool symbolic = false;
    for (const char ch : tokens.empty() ? std::string() : tokens[0])
        if (std::isalpha(static_cast<unsigned char>(ch)))
            symbolic = true;
    // Empty for VASP4 (no symbol line) — those atoms are recorded with
    // atomicNumber 0 below, since nothing in the file names their species.
    const std::vector<std::string> speciesSymbols = symbolic ? tokens
                                                              : std::vector<std::string>{};
    if (symbolic)
        std::getline(file, line); // the counts line follows the symbols
    std::istringstream countsLine(line);
    std::vector<long long> speciesCounts;
    long long totalAtoms = 0;
    for (long long n; countsLine >> n;) {
        speciesCounts.push_back(n);
        totalAtoms += n;
    }
    if (totalAtoms <= 0)
        throw parseError(path, "no atom counts in POSCAR header");

    std::getline(file, line); // "Direct"/"Cartesian" (or Selective dynamics)
    if (!line.empty()
        && (line[0] == 'S' || line[0] == 's')) // Selective dynamics
        std::getline(file, line);
    // POSCAR only distinguishes the two modes by this line's first letter;
    // anything other than a leading 'C'/'c' (Cartesian) is Direct
    // (fractional), matching VASP's own reader.
    const bool cartesianPositions
        = !line.empty() && (line[0] == 'C' || line[0] == 'c');
    std::vector<Atom> atoms;
    atoms.reserve(static_cast<std::size_t>(totalAtoms));
    for (std::size_t species = 0; species < speciesCounts.size(); ++species) {
        const int z = species < speciesSymbols.size()
            ? Elements::atomicNumber(speciesSymbols[species])
            : 0;
        for (long long i = 0; i < speciesCounts[species]; ++i) {
            std::getline(file, line); // "fx fy fz [T/F T/F T/F]"
            std::istringstream position(line);
            double px = 0.0, py = 0.0, pz = 0.0;
            position >> px >> py >> pz;
            const Vec3 cart = cartesianPositions
                ? Vec3{px, py, pz} * scale
                : (a * px + b * py + c * pz) * scale;
            atoms.push_back({z, cart});
        }
    }

    int nx = 0, ny = 0, nz = 0;
    file >> nx >> ny >> nz;
    if (!file || nx <= 0 || ny <= 0 || nz <= 0)
        throw parseError(path, "invalid grid dimension line");

    VolumetricData data;
    data.nx = nx;
    data.ny = ny;
    data.nz = nz;
    data.origin = {0.0, 0.0, 0.0};
    data.spanA = a * scale;
    data.spanB = b * scale;
    data.spanC = c * scale;
    data.atoms = std::move(atoms);
    data.values.resize(static_cast<std::size_t>(nx) * ny * nz);

    // VASP order: x fastest, then y, then z. The transpose into our z-fastest
    // layout happens in the visitor, so the block is still read in one pass.
    const std::size_t total = static_cast<std::size_t>(nx) * ny * nz;
    if (!readDoubleBlock(file, total, [&](std::size_t i, double v) {
            const auto ix = static_cast<int>(i % static_cast<std::size_t>(nx));
            const auto iy = static_cast<int>((i / static_cast<std::size_t>(nx))
                                             % static_cast<std::size_t>(ny));
            const auto iz = static_cast<int>(i / (static_cast<std::size_t>(nx)
                                                  * static_cast<std::size_t>(ny)));
            data.values[(static_cast<std::size_t>(ix) * ny + iy) * nz + iz] = v;
        }))
        throw parseError(path, "truncated CHGCAR value block");
    data.label = fileStem(path);
    data.sourceFormat = "chgcar";
    return data;
}

// --- XCrySDen .xsf 3D data grid --------------------------------------------
// BEGIN_DATAGRID_3D: "nx ny nz", origin, three spanning vectors (Angstrom),
// then nx*ny*nz values with x fastest. XSF grids are "general" grids where
// the last point repeats the first — trimmed to the periodic convention.
VolumetricData VolumetricData::loadXsf(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        throw parseError(path, "file not readable");

    std::string line;
    while (std::getline(file, line)) {
        if (line.find("DATAGRID_3D") != std::string::npos
            && line.find("BEGIN") != std::string::npos)
            break;
    }
    if (!file)
        throw parseError(path, "no BEGIN_DATAGRID_3D block found");

    int gx = 0, gy = 0, gz = 0;
    Vec3 origin, a, b, c;
    file >> gx >> gy >> gz;
    file >> origin.x >> origin.y >> origin.z;
    file >> a.x >> a.y >> a.z >> b.x >> b.y >> b.z >> c.x >> c.y >> c.z;
    if (!file || gx < 2 || gy < 2 || gz < 2)
        throw parseError(path, "invalid DATAGRID_3D header");

    std::vector<double> raw(static_cast<std::size_t>(gx) * gy * gz);
    if (!readDoubleBlock(file, raw.size(),
                         [&](std::size_t i, double v) { raw[i] = v; }))
        throw parseError(path, "truncated DATAGRID_3D value block");

    VolumetricData data;
    data.nx = gx - 1; // drop the duplicated boundary plane
    data.ny = gy - 1;
    data.nz = gz - 1;
    data.origin = origin;
    data.spanA = a;
    data.spanB = b;
    data.spanC = c;
    data.values.resize(static_cast<std::size_t>(data.nx) * data.ny * data.nz);
    for (int ix = 0; ix < data.nx; ++ix)
        for (int iy = 0; iy < data.ny; ++iy)
            for (int iz = 0; iz < data.nz; ++iz)
                data.values[(static_cast<std::size_t>(ix) * data.ny + iy) * data.nz
                            + iz]
                    = raw[(static_cast<std::size_t>(iz) * gy + iy) * gx + ix];
    data.label = fileStem(path);
    data.sourceFormat = "xsf";
    return data;
}

// --- Calango's compressed HDF5 container ------------------------------------
// Layout (documented for readers/writers outside this file in
// docs/sphinx/source/reference/hdf5_density.md — keep the two in sync):
//
//   Root attributes: calango_hdf5_layout_version (int32, format-evolution
//   guard), source_format, label (fixed-length C strings), origin, span_a,
//   span_b, span_c (float64[3] each, Cartesian Angstrom, same convention as
//   the in-memory fields).
//
//   Dataset "/density": shape (nx, ny, nz), float64, chunked + byte-shuffle +
//   gzip. Row-major C order over (nx, ny, nz) already puts z fastest, so this
//   is `values` written and read with no permutation on either side.
//
//   Group "/atoms": datasets "atomic_numbers" (int32, shape (natoms,)) and
//   "positions" (float64, shape (natoms, 3), Cartesian Angstrom) — always
//   present, even for natoms == 0, so a reader never branches on the group
//   existing at all, only on its datasets being empty.
namespace {

constexpr std::int32_t kHdf5LayoutVersion = 1;

[[noreturn]] void throwHdf5Error(const std::string& path, const std::string& what)
{
    throw std::runtime_error("HDF5 volumetric container " + path + ": " + what);
}

/// Closes an HDF5 handle on scope exit via whichever H5*close() function it
/// needs — the alternative is a manual close() at every throw site above,
/// which is exactly the kind of bookkeeping an early error is guaranteed to
/// get wrong eventually.
class Hdf5Guard {
public:
    Hdf5Guard(hid_t id, herr_t (*closer)(hid_t)) : id_(id), closer_(closer) {}
    ~Hdf5Guard()
    {
        if (id_ >= 0)
            closer_(id_);
    }
    Hdf5Guard(const Hdf5Guard&) = delete;
    Hdf5Guard& operator=(const Hdf5Guard&) = delete;

private:
    hid_t id_;
    herr_t (*closer_)(hid_t);
};

void writeStringAttribute(hid_t loc, const char* name, const std::string& value)
{
    const hid_t type = H5Tcopy(H5T_C_S1);
    H5Tset_size(type, value.empty() ? 1 : value.size());
    H5Tset_strpad(type, H5T_STR_NULLTERM);
    const hid_t space = H5Screate(H5S_SCALAR);
    const hid_t attr
        = H5Acreate2(loc, name, type, space, H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(attr, type, value.empty() ? "" : value.data());
    H5Aclose(attr);
    H5Sclose(space);
    H5Tclose(type);
}

std::string readStringAttribute(hid_t loc, const char* name)
{
    if (H5Aexists(loc, name) <= 0)
        return {};
    const hid_t attr = H5Aopen(loc, name, H5P_DEFAULT);
    const hid_t type = H5Aget_type(attr);
    std::string value(H5Tget_size(type), '\0');
    H5Aread(attr, type, value.data());
    H5Tclose(type);
    H5Aclose(attr);
    // A fixed-length HDF5 string carries a trailing NUL by construction
    // (writeStringAttribute always sets H5T_STR_NULLTERM) — trim it back to
    // an ordinary std::string with no embedded terminator.
    const auto nul = value.find('\0');
    if (nul != std::string::npos)
        value.resize(nul);
    return value;
}

void writeVec3Attribute(hid_t loc, const char* name, const Vec3& v)
{
    const hsize_t dims[1] = {3};
    const hid_t space = H5Screate_simple(1, dims, nullptr);
    const hid_t attr = H5Acreate2(loc, name, H5T_NATIVE_DOUBLE, space,
                                  H5P_DEFAULT, H5P_DEFAULT);
    const double values[3] = {v.x, v.y, v.z};
    H5Awrite(attr, H5T_NATIVE_DOUBLE, values);
    H5Aclose(attr);
    H5Sclose(space);
}

Vec3 readVec3Attribute(hid_t loc, const char* name)
{
    if (H5Aexists(loc, name) <= 0)
        return {};
    const hid_t attr = H5Aopen(loc, name, H5P_DEFAULT);
    double values[3] = {0.0, 0.0, 0.0};
    H5Aread(attr, H5T_NATIVE_DOUBLE, values);
    H5Aclose(attr);
    return {values[0], values[1], values[2]};
}

} // namespace

void VolumetricData::saveHdf5(const std::string& path) const
{
    if (nx <= 0 || ny <= 0 || nz <= 0
        || values.size() != static_cast<std::size_t>(nx) * ny * nz)
        throwHdf5Error(path, "grid is empty or inconsistent, nothing to write");

    // We report our own errors (throwHdf5Error, checked return values);
    // HDF5's default handler otherwise prints its own diagnostics to stderr
    // on every failed call, including the ones this function recovers from.
    H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);

    // Written to a temporary sibling and renamed into place on success only:
    // an interrupted write (disk full, the process killed mid-compress) must
    // never leave a truncated file sitting at `path` for a later load() to
    // trip over.
    const std::string tmpPath = path + ".tmp";
    {
        const hid_t file
            = H5Fcreate(tmpPath.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
        if (file < 0)
            throwHdf5Error(path, "could not create the file");
        Hdf5Guard fileGuard(file, H5Fclose);

        {
            const hid_t space = H5Screate(H5S_SCALAR);
            const hid_t attr
                = H5Acreate2(file, "calango_hdf5_layout_version", H5T_NATIVE_INT32,
                            space, H5P_DEFAULT, H5P_DEFAULT);
            const std::int32_t version = kHdf5LayoutVersion;
            H5Awrite(attr, H5T_NATIVE_INT32, &version);
            H5Aclose(attr);
            H5Sclose(space);
        }
        writeStringAttribute(file, "source_format", sourceFormat);
        writeStringAttribute(file, "label", label);
        writeVec3Attribute(file, "origin", origin);
        writeVec3Attribute(file, "span_a", spanA);
        writeVec3Attribute(file, "span_b", spanB);
        writeVec3Attribute(file, "span_c", spanC);

        // Chunk size clamped to the grid itself so a grid smaller than 64
        // along some axis never asks HDF5 for a chunk bigger than the
        // dataset. Byte-shuffle before gzip: it regroups each float64's
        // bytes by significance across the chunk, which is what lets gzip
        // find repetition in a smoothly-varying density field instead of
        // the near-random mantissa bytes it would see un-shuffled.
        const hsize_t dims[3] = {static_cast<hsize_t>(nx), static_cast<hsize_t>(ny),
                                 static_cast<hsize_t>(nz)};
        const hsize_t chunk[3] = {std::min<hsize_t>(dims[0], 64),
                                  std::min<hsize_t>(dims[1], 64),
                                  std::min<hsize_t>(dims[2], 64)};
        const hid_t space = H5Screate_simple(3, dims, nullptr);
        Hdf5Guard spaceGuard(space, H5Sclose);
        const hid_t plist = H5Pcreate(H5P_DATASET_CREATE);
        Hdf5Guard plistGuard(plist, H5Pclose);
        H5Pset_chunk(plist, 3, chunk);
        H5Pset_shuffle(plist);
        H5Pset_deflate(plist, 6);
        const hid_t dataset = H5Dcreate2(file, "/density", H5T_NATIVE_DOUBLE, space,
                                         H5P_DEFAULT, plist, H5P_DEFAULT);
        if (dataset < 0)
            throwHdf5Error(path, "could not create the /density dataset");
        const herr_t wrote = H5Dwrite(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL,
                                      H5P_DEFAULT, values.data());
        H5Dclose(dataset);
        if (wrote < 0)
            throwHdf5Error(path, "could not write the /density dataset");

        const hid_t atomsGroup
            = H5Gcreate2(file, "/atoms", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        if (atomsGroup < 0)
            throwHdf5Error(path, "could not create the /atoms group");
        Hdf5Guard atomsGuard(atomsGroup, H5Gclose);

        const hsize_t natoms = atoms.size();
        std::vector<std::int32_t> atomicNumbers(natoms);
        std::vector<double> positions(natoms * 3);
        for (hsize_t i = 0; i < natoms; ++i) {
            atomicNumbers[i] = atoms[i].atomicNumber;
            positions[i * 3 + 0] = atoms[i].position.x;
            positions[i * 3 + 1] = atoms[i].position.y;
            positions[i * 3 + 2] = atoms[i].position.z;
        }

        const hsize_t zDims[1] = {natoms};
        const hid_t zSpace = H5Screate_simple(1, zDims, nullptr);
        const hid_t zDataset
            = H5Dcreate2(atomsGroup, "atomic_numbers", H5T_NATIVE_INT32, zSpace,
                        H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        if (zDataset >= 0) {
            if (natoms > 0)
                H5Dwrite(zDataset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                        atomicNumbers.data());
            H5Dclose(zDataset);
        }
        H5Sclose(zSpace);

        const hsize_t posDims[2] = {natoms, 3};
        const hid_t posSpace = H5Screate_simple(2, posDims, nullptr);
        const hid_t posDataset
            = H5Dcreate2(atomsGroup, "positions", H5T_NATIVE_DOUBLE, posSpace,
                        H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        if (posDataset >= 0) {
            if (natoms > 0)
                H5Dwrite(posDataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                        positions.data());
            H5Dclose(posDataset);
        }
        H5Sclose(posSpace);
    } // the file closes here (Hdf5Guard), before the rename below

    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        std::remove(tmpPath.c_str());
        throwHdf5Error(path, "could not move the finished file into place");
    }
}

VolumetricData VolumetricData::loadHdf5(const std::string& path)
{
    H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);

    const hid_t file = H5Fopen(path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file < 0)
        throwHdf5Error(path, "file not readable, or not a valid HDF5 container");
    Hdf5Guard fileGuard(file, H5Fclose);

    VolumetricData data;
    data.sourceFormat = readStringAttribute(file, "source_format");
    data.label = readStringAttribute(file, "label");
    data.origin = readVec3Attribute(file, "origin");
    data.spanA = readVec3Attribute(file, "span_a");
    data.spanB = readVec3Attribute(file, "span_b");
    data.spanC = readVec3Attribute(file, "span_c");

    const hid_t dataset = H5Dopen2(file, "/density", H5P_DEFAULT);
    if (dataset < 0)
        throwHdf5Error(path, "no /density dataset");
    Hdf5Guard datasetGuard(dataset, H5Dclose);
    const hid_t space = H5Dget_space(dataset);
    Hdf5Guard spaceGuard(space, H5Sclose);
    if (H5Sget_simple_extent_ndims(space) != 3)
        throwHdf5Error(path, "/density is not a 3D dataset");
    hsize_t dims[3] = {0, 0, 0};
    H5Sget_simple_extent_dims(space, dims, nullptr);
    data.nx = static_cast<int>(dims[0]);
    data.ny = static_cast<int>(dims[1]);
    data.nz = static_cast<int>(dims[2]);
    data.values.resize(static_cast<std::size_t>(data.nx) * data.ny * data.nz);
    if (!data.values.empty()
        && H5Dread(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                   data.values.data())
               < 0)
        throwHdf5Error(path, "could not read the /density dataset");

    if (H5Lexists(file, "/atoms", H5P_DEFAULT) > 0) {
        const hid_t atomsGroup = H5Gopen2(file, "/atoms", H5P_DEFAULT);
        if (atomsGroup >= 0) {
            Hdf5Guard atomsGuard(atomsGroup, H5Gclose);
            std::vector<std::int32_t> atomicNumbers;
            std::vector<double> positions;

            if (H5Lexists(atomsGroup, "atomic_numbers", H5P_DEFAULT) > 0) {
                const hid_t zDataset
                    = H5Dopen2(atomsGroup, "atomic_numbers", H5P_DEFAULT);
                if (zDataset >= 0) {
                    Hdf5Guard zGuard(zDataset, H5Dclose);
                    const hid_t zSpace = H5Dget_space(zDataset);
                    Hdf5Guard zSpaceGuard(zSpace, H5Sclose);
                    hsize_t n = 0;
                    H5Sget_simple_extent_dims(zSpace, &n, nullptr);
                    atomicNumbers.resize(n);
                    if (n > 0)
                        H5Dread(zDataset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL,
                               H5P_DEFAULT, atomicNumbers.data());
                }
            }
            if (H5Lexists(atomsGroup, "positions", H5P_DEFAULT) > 0) {
                const hid_t posDataset = H5Dopen2(atomsGroup, "positions", H5P_DEFAULT);
                if (posDataset >= 0) {
                    Hdf5Guard posGuard(posDataset, H5Dclose);
                    const hid_t posSpace = H5Dget_space(posDataset);
                    Hdf5Guard posSpaceGuard(posSpace, H5Sclose);
                    hsize_t posDims[2] = {0, 0};
                    H5Sget_simple_extent_dims(posSpace, posDims, nullptr);
                    positions.resize(static_cast<std::size_t>(posDims[0]) * 3);
                    if (posDims[0] > 0)
                        H5Dread(posDataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL,
                               H5P_DEFAULT, positions.data());
                }
            }

            const std::size_t natoms
                = std::min(atomicNumbers.size(), positions.size() / 3);
            data.atoms.reserve(natoms);
            for (std::size_t i = 0; i < natoms; ++i)
                data.atoms.push_back(
                    {atomicNumbers[i], Vec3{positions[i * 3 + 0], positions[i * 3 + 1],
                                            positions[i * 3 + 2]}});
        }
    }
    return data;
}

bool VolumetricData::convertToHdf5(const std::string& sourcePath,
                                   const std::string& destPath, std::string* error)
{
    try {
        const VolumetricData data = load(sourcePath);
        data.saveHdf5(destPath);
        return true;
    } catch (const std::exception& e) {
        if (error)
            *error = e.what();
        return false;
    }
}

} // namespace calango::core
