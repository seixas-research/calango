#include "core/VolumetricData.hpp"

#include <algorithm>
#include <charconv>
#include <iterator>
#include <cctype>
#include <cmath>
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

    // Skip atom records.
    for (int i = 0; i < natoms; ++i) {
        int z;
        double q, x, y, zz;
        file >> z >> q >> x >> y >> zz;
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
    if (symbolic)
        std::getline(file, line); // the counts line follows the symbols
    std::istringstream countsLine(line);
    long long totalAtoms = 0;
    for (long long n; countsLine >> n;)
        totalAtoms += n;
    if (totalAtoms <= 0)
        throw parseError(path, "no atom counts in POSCAR header");

    std::getline(file, line); // "Direct"/"Cartesian" (or Selective dynamics)
    if (!line.empty()
        && (line[0] == 'S' || line[0] == 's')) // Selective dynamics
        std::getline(file, line);
    for (long long i = 0; i < totalAtoms; ++i)
        std::getline(file, line); // positions

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
    return data;
}

} // namespace calango::core
