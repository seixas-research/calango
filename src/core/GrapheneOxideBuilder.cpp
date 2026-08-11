#include "core/GrapheneOxideBuilder.hpp"

#include "core/UnitCell.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace calango::core {

namespace {

using Base = GrapheneOxideBuilder::Base;
using Config = GrapheneOxideBuilder::Config;
using Group = GrapheneOxideBuilder::Group;
using Lattice = GrapheneOxideBuilder::Lattice;
using Region = GrapheneOxideBuilder::Region;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

// Graphene lattice constant (Å) and the derived C-C bond length.
constexpr double kLatticeA = 2.46;
constexpr double kCC = kLatticeA / 1.7320508075688772; // a / sqrt(3) = 1.42 Å

// Vacuum along z. Enough that periodic images of the functional groups do not
// interact — the groups themselves stand ~1.5 Å off the plane, so a thinner
// slab than this would have them talking to their own image.
constexpr double kVacuum = 20.0;
// A finite flake is a molecule in a box: the same 10 Å clearance the sheet
// gets above and below, applied on all six faces.
constexpr double kFlakePad = kVacuum * 0.5;

// Basal (sp3, out-of-plane) chemistry.
constexpr double kCO_epoxide = 1.44;
constexpr double kCO_hydroxyl = 1.48;
constexpr double kOH = 0.98;

// Edge (sp2, in-plane) chemistry. The C-O lengths differ from their basal
// counterparts because the carbon stays sp2: a phenolic C-O is 1.36 Å, not the
// 1.48 Å of an sp3 alcohol, and an aryl-COOH C-C is 1.48 Å, not 1.52 Å.
constexpr double kCH_edge = 1.09;
constexpr double kCO_carbonyl = 1.23;
constexpr double kCC_carboxyl = 1.48;
constexpr double kCO_double = 1.21;
constexpr double kCO_single = 1.34;

constexpr int kZ_H = 1;
constexpr int kZ_C = 6;
constexpr int kZ_O = 8;

/// Closest approach allowed between atoms of two DIFFERENT groups.
///
/// The builder produces unrelaxed starting geometries, so some strain is
/// expected and fine — an epoxide and a hydroxyl on neighbouring carbons come
/// out at an O···O of 1.9 Å and relax apart as the carbons pucker. What is not
/// fine is a contact shorter than a covalent bond: two groups placed on
/// adjacent sites with their substituents pointing at each other can land
/// oxygens 0.1 Å apart, which is not a strained structure but a fused one, and
/// no optimizer recovers from it. Placements that would do that are refused and
/// the site is tried in another orientation, or skipped.
constexpr double kMinContact = 1.55;

/// Framework carbons this far apart cannot host groups that reach each other:
/// no group extends more than ~2.4 Å from its carbon.
constexpr double kStericNeighbourhood = 5.2;

/// How many free sites a single placement will try before giving up on the
/// group. Sites are shuffled, so this samples the pool rather than scanning a
/// region of it; a group that cannot find room in 32 random tries is at
/// saturation, and the shortfall is reported either way.
constexpr int kStericTries = 32;

/// Rotate an in-plane vector about z.
Vec3 rotateInPlane(const Vec3& v, double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return Vec3{v.x * c - v.y * s, v.x * s + v.y * c, 0.0};
}

/// The carbon skeleton plus everything the chemistry needs to know about it:
/// who is bonded to whom, which carbons are edge carbons, and which way "out"
/// is at each edge site.
struct Framework {
    Structure carbons;
    std::vector<std::vector<int>> neighbors;
    std::vector<char> isEdge;
    /// In-plane unit vector pointing away from the flake. Meaningful for edge
    /// carbons only; zero elsewhere.
    std::vector<Vec3> outward;
    /// Bonded pairs with BOTH carbons basal — the only pairs an epoxide may
    /// bridge.
    std::vector<std::pair<int, int>> basalBonds;
    /// Carbons within `kStericNeighbourhood` of each carbon: the only ones
    /// whose groups a new group could possibly run into.
    std::vector<std::vector<int>> nearby;
    bool periodic = true;
    int basalCount = 0;
    int edgeCount = 0;
};

/// Carbon positions of the nanoflake of index m, C(6m²)H(6m).
///
/// Built from hexagonal RINGS rather than from a carved lattice, which is what
/// makes the family exact: the rings form a hex-shaped patch of the triangular
/// ring lattice of radius m-1 (3m(m-1)+1 rings), and the union of their
/// vertices is a D6h all-armchair flake with exactly 6m² carbons. Carving a
/// hexagon out of a sheet by a distance cutoff instead gives ragged edges whose
/// atom count depends on where the cut lands.
std::vector<Vec3> nanoflakeCarbons(int index)
{
    const int m = std::clamp(index, 1,
                             GrapheneOxideBuilder::kMaxFlakeIndex);
    const int r = m - 1;

    // Ring-centre lattice: neighbouring rings sit a = 2.46 Å apart, at 0° and
    // 60°, while the six vertices of a ring sit 1.42 Å out at 30° + 60°k. That
    // offset by 30° is what makes adjacent rings share an edge rather than a
    // vertex.
    const Vec3 h1{kLatticeA, 0.0, 0.0};
    const Vec3 h2{kLatticeA * 0.5, kLatticeA * std::sqrt(3.0) / 2.0, 0.0};

    std::vector<Vec3> points;
    for (int i = -r; i <= r; ++i) {
        for (int j = -r; j <= r; ++j) {
            if (std::abs(i + j) > r)
                continue; // outside the hexagonal patch of rings
            const Vec3 centre = h1 * static_cast<double>(i)
                + h2 * static_cast<double>(j);
            for (int k = 0; k < 6; ++k) {
                const double angle = (30.0 + 60.0 * k) * kDeg;
                const Vec3 v{centre.x + kCC * std::cos(angle),
                             centre.y + kCC * std::sin(angle), 0.0};
                const bool duplicate =
                    std::any_of(points.begin(), points.end(),
                                [&](const Vec3& p) {
                                    return std::abs(p.x - v.x) < 0.05
                                        && std::abs(p.y - v.y) < 0.05;
                                });
                if (!duplicate)
                    points.push_back(v);
            }
        }
    }

    // Row-major order, so the atom indices do not depend on the ring loop.
    std::sort(points.begin(), points.end(), [](const Vec3& a, const Vec3& b) {
        const long ay = std::lround(a.y * 1000.0);
        const long by = std::lround(b.y * 1000.0);
        if (ay != by)
            return ay < by;
        return std::lround(a.x * 1000.0) < std::lround(b.x * 1000.0);
    });
    return points;
}

Framework buildFramework(const Config& config)
{
    Framework fw;
    fw.periodic = config.base == Base::PeriodicSheet;

    if (fw.periodic) {
        const int nx = std::max(1, config.supercell[0]);
        const int ny = std::max(1, config.supercell[1]);
        std::vector<Vec3> basis;
        Vec3 a1;
        Vec3 a2;

        if (config.lattice == Lattice::Primitive) {
            // Rhombohedral 2-atom cell: |a1| = |a2| = 2.46 Å at 60°.
            a1 = Vec3{kLatticeA, 0.0, 0.0};
            a2 = Vec3{kLatticeA * 0.5, kLatticeA * std::sqrt(3.0) / 2.0, 0.0};
            basis = {Vec3{0.0, 0.0, 0.0},
                     Vec3{(a1.x + a2.x) / 3.0, (a1.y + a2.y) / 3.0, 0.0}};
        } else {
            // Orthogonal 4-atom cell: the conventional rectangular tiling,
            // easier to build supercells and slabs from because the axes are
            // orthogonal.
            const double bx = kLatticeA;
            const double by = kCC * 3.0; // 4.26 Å
            a1 = Vec3{bx, 0.0, 0.0};
            a2 = Vec3{0.0, by, 0.0};
            basis = {Vec3{0.0, 0.0, 0.0},
                     Vec3{bx * 0.5, kCC * 0.5, 0.0},
                     Vec3{bx * 0.5, kCC * 1.5, 0.0},
                     Vec3{0.0, kCC * 2.0, 0.0}};
        }

        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                const Vec3 shift{a1.x * i + a2.x * j, a1.y * i + a2.y * j, 0.0};
                for (const Vec3& site : basis) {
                    Atom atom;
                    atom.atomicNumber = kZ_C;
                    atom.position = Vec3{site.x + shift.x, site.y + shift.y,
                                         kVacuum * 0.5};
                    fw.carbons.addAtom(atom);
                }
            }
        }
        fw.carbons.setCell(UnitCell(Vec3{a1.x * nx, a1.y * nx, 0.0},
                                    Vec3{a2.x * ny, a2.y * ny, 0.0},
                                    Vec3{0.0, 0.0, kVacuum}));
    } else {
        for (const Vec3& p : nanoflakeCarbons(config.flakeIndex)) {
            Atom atom;
            atom.atomicNumber = kZ_C;
            atom.position = Vec3{p.x, p.y, 0.0};
            fw.carbons.addAtom(atom);
        }
        // The cell is fitted around the finished structure, once the groups
        // that stick out of the flake are known.
    }

    const int n = static_cast<int>(fw.carbons.size());
    fw.neighbors.assign(static_cast<std::size_t>(n), {});
    fw.isEdge.assign(static_cast<std::size_t>(n), 0);
    fw.outward.assign(static_cast<std::size_t>(n), Vec3{});

    // Minimum image in x/y so pairs across the periodic boundary are found too;
    // the sheet is flat, so z plays no part. The flake is finite and uses plain
    // distances.
    const auto& v = fw.carbons.cell().vectors();
    const double lx = v[0].x;
    const double ly = v[1].y;
    const double shear = v[1].x; // non-zero for the primitive cell
    const auto separation = [&](const Vec3& a, const Vec3& b) {
        Vec3 d{b.x - a.x, b.y - a.y, 0.0};
        if (fw.periodic) {
            // Reduce along a2 first (it carries the shear), then along a1.
            const double nj = std::round(d.y / ly);
            d.y -= nj * ly;
            d.x -= nj * shear;
            d.x -= std::round(d.x / lx) * lx;
        }
        return d;
    };

    const double bondCut = kCC * 1.25;
    std::vector<std::pair<int, int>> bonds;
    fw.nearby.assign(static_cast<std::size_t>(n), {});
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            const Vec3 d = separation(fw.carbons.atoms()[i].position,
                                      fw.carbons.atoms()[j].position);
            const double r = std::sqrt(d.x * d.x + d.y * d.y);
            if (r < bondCut) {
                bonds.push_back({i, j});
                fw.neighbors[static_cast<std::size_t>(i)].push_back(j);
                fw.neighbors[static_cast<std::size_t>(j)].push_back(i);
            }
            if (r < kStericNeighbourhood) {
                fw.nearby[static_cast<std::size_t>(i)].push_back(j);
                fw.nearby[static_cast<std::size_t>(j)].push_back(i);
            }
        }
    }

    // --- Basal / edge classification ---------------------------------------
    // A carbon with three carbon neighbours is basal: sp2, and it rehybridizes
    // to sp3 under an epoxide or an out-of-plane hydroxyl. A carbon with fewer
    // is an edge carbon: it carries a substitutable hydrogen and does the
    // in-plane sp2 chemistry of carbonyls, carboxyls and phenols.
    //
    // A periodic sheet is edgeless by construction and is short-circuited
    // rather than counted, because minimum-image folding collapses the three
    // bonds of a small cell onto a single neighbour entry and would classify a
    // perfectly good sheet as all edge.
    if (!fw.periodic) {
        for (int i = 0; i < n; ++i) {
            const auto& nb = fw.neighbors[static_cast<std::size_t>(i)];
            if (nb.size() >= 3)
                continue;
            fw.isEdge[static_cast<std::size_t>(i)] = 1;

            // "Out" is the direction that bisects the missing bonds: the sum of
            // the unit vectors pointing back from each neighbour.
            Vec3 out{};
            const Vec3 here = fw.carbons.atoms()[i].position;
            for (int j : nb) {
                const Vec3 back{here.x - fw.carbons.atoms()[j].position.x,
                                here.y - fw.carbons.atoms()[j].position.y, 0.0};
                out += back.normalized();
            }
            if (out.norm() < 1e-6) {
                // Collinear neighbours: fall back to pointing away from the
                // flake centre. Cannot happen on a honeycomb, but a zero
                // direction would silently stack atoms on top of each other.
                out = Vec3{here.x, here.y, 0.0};
            }
            fw.outward[static_cast<std::size_t>(i)] = out.normalized();
        }
    }

    for (int i = 0; i < n; ++i)
        (fw.isEdge[static_cast<std::size_t>(i)] ? fw.edgeCount : fw.basalCount)++;

    for (const auto& [i, j] : bonds) {
        if (!fw.isEdge[static_cast<std::size_t>(i)]
            && !fw.isEdge[static_cast<std::size_t>(j)])
            fw.basalBonds.push_back({i, j});
    }
    return fw;
}

/// Fit an orthorhombic box with `kFlakePad` of vacuum on every side around a
/// finite structure, translating it to sit inside. A cluster still needs a cell
/// because every plane-wave code demands one; what it must not have is a
/// neighbour.
void fitFlakeCell(Structure& structure)
{
    if (structure.empty())
        return;
    Vec3 lo = structure.atoms().front().position;
    Vec3 hi = lo;
    for (const Atom& atom : structure.atoms()) {
        lo.x = std::min(lo.x, atom.position.x);
        lo.y = std::min(lo.y, atom.position.y);
        lo.z = std::min(lo.z, atom.position.z);
        hi.x = std::max(hi.x, atom.position.x);
        hi.y = std::max(hi.y, atom.position.y);
        hi.z = std::max(hi.z, atom.position.z);
    }
    for (Atom& atom : structure.atoms()) {
        atom.position.x += kFlakePad - lo.x;
        atom.position.y += kFlakePad - lo.y;
        atom.position.z += kFlakePad - lo.z;
    }
    structure.setCell(UnitCell(Vec3{hi.x - lo.x + 2 * kFlakePad, 0.0, 0.0},
                               Vec3{0.0, hi.y - lo.y + 2 * kFlakePad, 0.0},
                               Vec3{0.0, 0.0, hi.z - lo.z + 2 * kFlakePad},
                               {false, false, false}));
}

Atom makeAtom(int z, const Vec3& position)
{
    Atom atom;
    atom.atomicNumber = z;
    atom.position = position;
    return atom;
}

} // namespace

std::vector<GrapheneOxideBuilder::GroupCluster>
GrapheneOxideBuilder::findFunctionalGroups(const Structure& structure)
{
    const int n = static_cast<int>(structure.size());
    std::vector<GroupCluster> clusters;
    if (n == 0)
        return clusters;

    // Minimum-image aware, so a group bridging the cell boundary of a periodic
    // sheet is found like any other.
    std::vector<std::vector<int>> neighbours(static_cast<std::size_t>(n));
    for (const Bond& bond : structure.detectBonds()) {
        neighbours[static_cast<std::size_t>(bond.i)].push_back(bond.j);
        neighbours[static_cast<std::size_t>(bond.j)].push_back(bond.i);
    }

    const auto z = [&structure](int atom) {
        return structure.atoms()[static_cast<std::size_t>(atom)].atomicNumber;
    };
    const auto countOf = [&](int atom, int element) {
        int count = 0;
        for (const int other : neighbours[static_cast<std::size_t>(atom)])
            if (z(other) == element)
                ++count;
        return count;
    };

    std::vector<char> claimed(static_cast<std::size_t>(n), 0);
    const auto free_ = [&claimed](int atom) {
        return !claimed[static_cast<std::size_t>(atom)];
    };
    const auto emit = [&](Group kind, std::vector<int> atoms) {
        for (const int atom : atoms)
            claimed[static_cast<std::size_t>(atom)] = 1;
        clusters.push_back({kind, std::move(atoms)});
    };

    // --- Carboxyl: a carbon carrying two oxygens and one carbon -------------
    // First, because it CONTAINS a carbonyl and a hydroxyl. Looking for those
    // first would report every -COOH as two unrelated half-groups sharing a
    // carbon, which is both wrong and unselectable.
    for (int atom = 0; atom < n; ++atom) {
        if (z(atom) != kZ_C || !free_(atom))
            continue;
        if (countOf(atom, kZ_O) != 2 || countOf(atom, kZ_C) != 1)
            continue;
        std::vector<int> cluster{atom};
        bool usable = true;
        for (const int other : neighbours[static_cast<std::size_t>(atom)]) {
            if (!free_(other)) {
                usable = false;
                break;
            }
            cluster.push_back(other);
            if (z(other) != kZ_O)
                continue;
            // The acidic hydrogen hangs off the single-bonded oxygen, one bond
            // further out than the loop above reaches.
            for (const int tail : neighbours[static_cast<std::size_t>(other)])
                if (z(tail) == kZ_H && free_(tail))
                    cluster.push_back(tail);
        }
        if (usable)
            emit(Group::Carboxyl, std::move(cluster));
    }

    // --- Epoxide: one oxygen bridging two carbons ---------------------------
    for (int atom = 0; atom < n; ++atom) {
        if (z(atom) != kZ_O || !free_(atom))
            continue;
        if (countOf(atom, kZ_C) != 2 || countOf(atom, kZ_H) != 0)
            continue;
        std::vector<int> cluster{atom};
        for (const int carbon : neighbours[static_cast<std::size_t>(atom)])
            if (z(carbon) == kZ_C && free_(carbon))
                cluster.push_back(carbon);
        emit(Group::Epoxide, std::move(cluster));
    }

    // --- Hydroxyl: one oxygen with one carbon and one hydrogen --------------
    for (int atom = 0; atom < n; ++atom) {
        if (z(atom) != kZ_O || !free_(atom))
            continue;
        if (countOf(atom, kZ_C) != 1 || countOf(atom, kZ_H) != 1)
            continue;
        int host = -1;
        std::vector<int> cluster{atom};
        for (const int other : neighbours[static_cast<std::size_t>(atom)]) {
            if (!free_(other))
                continue;
            cluster.push_back(other);
            if (z(other) == kZ_C)
                host = other;
        }
        // Reported as a hydroxyl wherever it sits. The generator no longer
        // places phenolic edge -OH, but a structure from a file may carry it
        // and it is still a hydroxyl group; which carbon hosts it — and
        // therefore whether it is the 1.48 Å sp3 basal kind or the 1.36 Å sp2
        // phenol — is readable from that carbon's coordination and from the
        // "edge" scalar field, without a second Group value that nothing can
        // build.
        (void)host;
        emit(Group::Hydroxyl, std::move(cluster));
    }

    // --- Carbonyl: one oxygen with a single carbon and nothing else ---------
    for (int atom = 0; atom < n; ++atom) {
        if (z(atom) != kZ_O || !free_(atom))
            continue;
        if (countOf(atom, kZ_C) != 1
            || neighbours[static_cast<std::size_t>(atom)].size() != 1)
            continue;
        std::vector<int> cluster{atom};
        for (const int carbon : neighbours[static_cast<std::size_t>(atom)])
            if (free_(carbon))
                cluster.push_back(carbon);
        emit(Group::Carbonyl, std::move(cluster));
    }

    return clusters;
}

std::vector<int>
GrapheneOxideBuilder::functionalGroupLabels(const Structure& structure)
{
    std::vector<int> labels(structure.size(), -1);
    for (const GroupCluster& cluster : findFunctionalGroups(structure)) {
        for (const int atom : cluster.atoms) {
            if (atom >= 0 && atom < static_cast<int>(labels.size()))
                labels[static_cast<std::size_t>(atom)] =
                    static_cast<int>(cluster.kind);
        }
    }
    return labels;
}

const char* GrapheneOxideBuilder::name(Group group)
{
    switch (group) {
    case Group::Epoxide:      return "epoxide";
    case Group::Hydroxyl:     return "hydroxyl";
    case Group::Carboxyl:     return "carboxyl";
    case Group::Carbonyl:     return "carbonyl";
    }
    return "?";
}

GrapheneOxideBuilder::Region GrapheneOxideBuilder::region(Group group)
{
    switch (group) {
    case Group::Epoxide:
    case Group::Hydroxyl:
        return Region::Basal;
    case Group::Carboxyl:
    case Group::Carbonyl:
        return Region::Edge;
    }
    return Region::Basal;
}

int GrapheneOxideBuilder::carbonCost(Group group)
{
    // An epoxide bridges a C-C bond and rehybridizes BOTH carbons. This is what
    // makes the coverages additive.
    return group == Group::Epoxide ? 2 : 1;
}

int GrapheneOxideBuilder::oxygensPerGroup(Group group)
{
    return group == Group::Carboxyl ? 2 : 1;
}

int GrapheneOxideBuilder::flakeCarbonCount(int generation)
{
    const int m = std::clamp(generation, 1, kMaxFlakeIndex);
    return 6 * m * m;
}

int GrapheneOxideBuilder::flakeEdgeCarbonCount(int generation)
{
    const int m = std::clamp(generation, 1, kMaxFlakeIndex);
    return 6 * m;
}

const char* GrapheneOxideBuilder::flakeName(int generation)
{
    switch (std::clamp(generation, 1, kMaxFlakeIndex)) {
    case 1:  return "benzene";
    case 2:  return "coronene";
    case 3:  return "circumcoronene";
    case 4:  return "dicircumcoronene";
    case 5:  return "tricircumcoronene";
    case 6:  return "tetracircumcoronene";
    case 7:  return "pentacircumcoronene";
    case 8:  return "hexacircumcoronene";
    case 9:  return "heptacircumcoronene";
    case 10: return "octacircumcoronene";
    case 11: return "nonacircumcoronene";
    default: return "decacircumcoronene";
    }
}

std::string GrapheneOxideBuilder::flakeFormula(int generation)
{
    std::ostringstream out;
    out << 'C' << flakeCarbonCount(generation) << 'H'
        << flakeEdgeCarbonCount(generation);
    return out.str();
}

double GrapheneOxideBuilder::Report::carbonToOxygenRatio() const
{
    if (oxygenAtoms == 0)
        return 0.0;
    return static_cast<double>(totalCarbonAtoms) / oxygenAtoms;
}

Structure GrapheneOxideBuilder::pristine(const Config& config)
{
    Framework fw = buildFramework(config);
    Structure structure = std::move(fw.carbons);
    if (fw.periodic)
        return structure;

    // The pristine flake is the parent hydrocarbon — C54H18 for m = 3, not a
    // bare C54 radical.
    if (config.hydrogenTerminateEdges) {
        const int n = static_cast<int>(structure.size());
        for (int i = 0; i < n; ++i) {
            if (!fw.isEdge[static_cast<std::size_t>(i)])
                continue;
            const Vec3 c = structure.atoms()[static_cast<std::size_t>(i)].position;
            structure.addAtom(makeAtom(
                kZ_H, c + fw.outward[static_cast<std::size_t>(i)] * kCH_edge));
        }
    }
    fitFlakeCell(structure);
    return structure;
}

Structure GrapheneOxideBuilder::build(const Config& config, Report* report)
{
    Framework fw = buildFramework(config);
    Structure structure = std::move(fw.carbons);
    const int carbonCount = static_cast<int>(structure.size());

    Report local;
    local.carbonCount = carbonCount;
    local.basalCarbonCount = fw.basalCount;
    local.edgeCarbonCount = fw.edgeCount;

    std::mt19937 rng(config.seed);

    // --- The reservation table ----------------------------------------------
    // `owner[c]` is the group occupying framework carbon c, or -1. This single
    // array IS the collision guardrail, and every placement below goes through
    // it: a carbon that has been reserved leaves the pool permanently, so no
    // carbon can ever carry two groups. A carbon has one out-of-plane valence
    // after rehybridizing to sp3, and an edge carbon one substitutable
    // hydrogen; a second group on either would make it pentavalent.
    std::vector<int> owner(static_cast<std::size_t>(carbonCount), -1);

    // Shuffled site orders: the decoration is a random sample of the
    // composition, not a pattern. Each pool is walked by a cursor that only
    // ever moves forward — sites are consumed permanently, so a site the cursor
    // has passed can never become free again.
    std::vector<int> basalOrder;
    std::vector<int> edgeOrder;
    for (int i = 0; i < carbonCount; ++i)
        (fw.isEdge[static_cast<std::size_t>(i)] ? edgeOrder : basalOrder)
            .push_back(i);
    std::shuffle(basalOrder.begin(), basalOrder.end(), rng);
    std::shuffle(edgeOrder.begin(), edgeOrder.end(), rng);
    std::shuffle(fw.basalBonds.begin(), fw.basalBonds.end(), rng);

    std::size_t basalCursor = 0;
    std::size_t edgeCursor = 0;
    std::size_t bondCursor = 0;

    const auto advance = [&](const std::vector<int>& order,
                             std::size_t& cursor) {
        while (cursor < order.size()
               && owner[static_cast<std::size_t>(order[cursor])] >= 0)
            ++cursor;
    };
    const auto advanceBonds = [&]() {
        while (bondCursor < fw.basalBonds.size()) {
            const auto& [i, j] = fw.basalBonds[bondCursor];
            if (owner[static_cast<std::size_t>(i)] < 0
                && owner[static_cast<std::size_t>(j)] < 0)
                return;
            ++bondCursor;
        }
    };
    const auto hasSiteFor = [&](Group group) {
        switch (group) {
        case Group::Epoxide:
            advanceBonds();
            return bondCursor < fw.basalBonds.size();
        case Group::Hydroxyl:
            advance(basalOrder, basalCursor);
            return basalCursor < basalOrder.size();
        default:
            advance(edgeOrder, edgeCursor);
            return edgeCursor < edgeOrder.size();
        }
    };

    // Which face each basal group points to. Alternating by draw rather than by
    // position keeps both faces populated without imposing a pattern. Edge
    // groups lie in the plane and never consult this.
    std::bernoulli_distribution coinFlip(0.5);
    std::uniform_real_distribution<double> azimuth(0.0, 2.0 * kPi);
    const auto faceSign = [&]() -> double {
        if (!config.bothFaces)
            return 1.0;
        return coinFlip(rng) ? 1.0 : -1.0;
    };

    std::vector<Atom> attachments;
    // Which attachments hang off which framework carbon — the index that makes
    // the steric test local instead of a scan over the whole structure.
    std::vector<std::vector<int>> hosted(static_cast<std::size_t>(carbonCount));

    const auto carbonAt = [&](int index) {
        return structure.atoms()[static_cast<std::size_t>(index)].position;
    };
    const auto gap = [&](const Vec3& a, const Vec3& b) {
        Vec3 d{a.x - b.x, a.y - b.y, a.z - b.z};
        if (fw.periodic) {
            const auto& v = structure.cell().vectors();
            const double nj = std::round(d.y / v[1].y);
            d.y -= nj * v[1].y;
            d.x -= nj * v[1].x;
            d.x -= std::round(d.x / v[0].x) * v[0].x;
        }
        return d.norm();
    };

    /// Would this group, placed here, land on top of a group already present?
    /// Only groups on carbons within `kStericNeighbourhood` can reach, so this
    /// stays O(1) however large the substrate is.
    const auto collides = [&](const std::vector<Atom>& pending,
                              std::initializer_list<int> hosts) {
        for (int host : hosts) {
            for (int neighbour : fw.nearby[static_cast<std::size_t>(host)]) {
                for (int index : hosted[static_cast<std::size_t>(neighbour)]) {
                    const Vec3& there =
                        attachments[static_cast<std::size_t>(index)].position;
                    for (const Atom& atom : pending) {
                        if (gap(atom.position, there) < kMinContact)
                            return true;
                    }
                }
            }
        }
        return false;
    };
    const auto commit = [&](const std::vector<Atom>& pending,
                            std::initializer_list<int> hosts, Group group) {
        for (int host : hosts)
            owner[static_cast<std::size_t>(host)] = static_cast<int>(group);
        for (const Atom& atom : pending) {
            const int index = static_cast<int>(attachments.size());
            attachments.push_back(atom);
            for (int host : hosts)
                hosted[static_cast<std::size_t>(host)].push_back(index);
        }
    };

    // --- Group geometry ------------------------------------------------------
    // `variant` selects an orientation to try: which face a basal group points
    // to and where its hydroxyl hydrogen swings, or which way an edge group's
    // substituents rotate. A site rejected in one orientation is retried in the
    // others before it is abandoned, which is what keeps a crowded neighbour
    // from ruling out an otherwise perfectly good carbon.
    const auto basalGeometry = [&](int carbon, double sign, double phi) {
        std::vector<Atom> pending;
        const Vec3 c = carbonAt(carbon);
        const Vec3 o{c.x, c.y, c.z + sign * kCO_hydroxyl};
        pending.push_back(makeAtom(kZ_O, o));
        // The O-H points away from the sheet at the usual ~108°. The azimuth is
        // drawn rather than fixed: pointing every hydroxyl the same way would
        // give the cell a spurious in-plane dipole.
        pending.push_back(makeAtom(kZ_H,
                                   Vec3{o.x + kOH * 0.94 * std::cos(phi),
                                        o.y + kOH * 0.94 * std::sin(phi),
                                        o.z + sign * kOH * 0.33}));
        return pending;
    };
    const auto edgeGeometry = [&](Group group, int carbon, double side) {
        // In-plane sp2 chemistry, replacing the edge hydrogen. `u` points away
        // from the flake; `rotateInPlane` swings substituents off it at the
        // trigonal angles.
        std::vector<Atom> pending;
        const Vec3 c = carbonAt(carbon);
        const Vec3 u = fw.outward[static_cast<std::size_t>(carbon)];
        switch (group) {
        case Group::Carbonyl:
            // Quinone-like C=O, collinear with the missing C-H.
            pending.push_back(makeAtom(kZ_O, c + u * kCO_carbonyl));
            break;
        case Group::Carboxyl: {
            // -COOH: it brings its own sp2 carbon, with the two oxygens at the
            // trigonal ±60° off the outward direction and the acidic H in the
            // syn conformation, folded back toward the carbonyl oxygen.
            const Vec3 cc = c + u * kCC_carboxyl;
            pending.push_back(makeAtom(kZ_C, cc));
            pending.push_back(makeAtom(
                kZ_O, cc + rotateInPlane(u, -side * 60.0 * kDeg) * kCO_double));
            const Vec3 singleO =
                cc + rotateInPlane(u, side * 60.0 * kDeg) * kCO_single;
            pending.push_back(makeAtom(kZ_O, singleO));
            pending.push_back(makeAtom(
                kZ_H, singleO + rotateInPlane(u, -side * 14.0 * kDeg) * kOH));
            break;
        }
        default:
            break; // basal groups have their own geometry
        }
        return pending;
    };

    // --- The one place a group is ever attached -----------------------------
    // Every dosing mode funnels through here, so the reservation check, the
    // steric check and the geometry live together and cannot drift apart.
    const auto place = [&](Group group) -> bool {
        if (group == Group::Epoxide) {
            advanceBonds();
            int tried = 0;
            for (std::size_t k = bondCursor;
                 k < fw.basalBonds.size() && tried < kStericTries; ++k) {
                const auto [i, j] = fw.basalBonds[k];
                if (owner[static_cast<std::size_t>(i)] >= 0
                    || owner[static_cast<std::size_t>(j)] >= 0)
                    continue;
                ++tried;
                const Vec3 ci = carbonAt(i);
                const Vec3 cj = carbonAt(j);
                Vec3 d{cj.x - ci.x, cj.y - ci.y, 0.0};
                if (fw.periodic) {
                    const auto& v = structure.cell().vectors();
                    const double nj = std::round(d.y / v[1].y);
                    d.y -= nj * v[1].y;
                    d.x -= nj * v[1].x;
                    d.x -= std::round(d.x / v[0].x) * v[0].x;
                }
                // The bridging O sits above the bond midpoint, at the height
                // that gives the C-O bonds their proper length.
                const double half = 0.5 * std::sqrt(d.x * d.x + d.y * d.y);
                const double height = std::sqrt(
                    std::max(kCO_epoxide * kCO_epoxide - half * half, 0.25));
                double sign = faceSign();
                for (int variant = 0; variant < (config.bothFaces ? 2 : 1);
                     ++variant, sign = -sign) {
                    const std::vector<Atom> pending{makeAtom(
                        kZ_O, Vec3{ci.x + d.x * 0.5, ci.y + d.y * 0.5,
                                   ci.z + sign * height})};
                    if (collides(pending, {i, j}))
                        continue;
                    commit(pending, {i, j}, group);
                    return true;
                }
            }
            return false;
        }

        const bool basal = region(group) == Region::Basal;
        std::vector<int>& order = basal ? basalOrder : edgeOrder;
        std::size_t& cursor = basal ? basalCursor : edgeCursor;
        advance(order, cursor);

        int tried = 0;
        for (std::size_t k = cursor; k < order.size() && tried < kStericTries;
             ++k) {
            const int carbon = order[k];
            if (owner[static_cast<std::size_t>(carbon)] >= 0)
                continue;
            ++tried;

            // Basal: two faces × three hydroxyl azimuths. Edge: the two
            // in-plane senses the substituents can rotate through.
            const int variants = basal ? (config.bothFaces ? 6 : 3) : 2;
            const double sign0 = faceSign();
            const double phi0 = azimuth(rng);
            const double side0 = coinFlip(rng) ? 1.0 : -1.0;
            for (int variant = 0; variant < variants; ++variant) {
                const std::vector<Atom> pending =
                    basal ? basalGeometry(carbon, variant < 3 ? sign0 : -sign0,
                                          phi0 + variant * (2.0 * kPi / 3.0))
                          : edgeGeometry(group, carbon,
                                         variant == 0 ? side0 : -side0);
                if (collides(pending, {carbon}))
                    continue;
                commit(pending, {carbon}, group);
                return true;
            }
        }
        return false;
    };

    // --- Dosing --------------------------------------------------------------
    if (config.dosing == Dosing::ExplicitCoverage) {
        const auto targetCount = [&](Group group) {
            const double fraction = std::clamp(config.coverageFor(group), 0.0, 1.0);
            const int pool = region(group) == Region::Basal ? fw.basalCount
                                                            : fw.edgeCount;
            return static_cast<int>(
                std::llround(fraction * pool / carbonCost(group)));
        };
        // Epoxides first: they need a bonded PAIR of free basal carbons, the
        // most constrained requirement, so satisfying it before the single-site
        // groups eat the lattice gives the requested composition the best
        // chance. The edge groups then compete only with each other.
        const Group order[] = {Group::Epoxide, Group::Carboxyl, Group::Carbonyl,
                               Group::Hydroxyl};
        for (Group group : order) {
            const auto slot = static_cast<std::size_t>(group);
            local.requested[slot] = targetCount(group);
            while (local.placed[slot] < local.requested[slot] && place(group))
                ++local.placed[slot];
        }
    } else {
        // Place groups one at a time until the WHOLE structure reaches the
        // requested C/O. The loop has to be iterative rather than a closed-form
        // count because a carboxyl brings a carbon of its own: it moves both
        // sides of the ratio, so the target for every other group shifts each
        // time one lands.
        local.targetRatio = std::max(0.05, config.targetCarbonToOxygen);
        const double basalShare = std::clamp(config.basalOxygenShare, 0.0, 1.0);

        int oxygens = 0;
        int extraCarbons = 0;
        double delivered[2] = {0.0, 0.0}; // oxygens by region
        std::vector<char> exhausted(GrapheneOxideBuilder::kGroupCount, 0);

        const auto ratioAfter = [&](int addOxygen, int addCarbon) {
            const int o = oxygens + addOxygen;
            if (o == 0)
                return std::numeric_limits<double>::infinity();
            return static_cast<double>(carbonCount + extraCarbons + addCarbon) / o;
        };

        // Terminates: each iteration either places a group (consuming at least
        // one carbon from a finite pool) or marks a group exhausted, and an
        // exhausted group never becomes placeable again because sites are only
        // ever consumed.
        for (;;) {
            const double current = std::abs(ratioAfter(0, 0) - local.targetRatio);

            std::vector<Group> candidates;
            for (std::size_t g = 0; g < GrapheneOxideBuilder::kGroupCount; ++g) {
                const auto group = static_cast<Group>(g);
                if (config.weightFor(group) <= 0.0 || exhausted[g])
                    continue;
                // A share of exactly 0 or 1 is categorical — "no edge
                // chemistry", "no basal chemistry" — and is honored even when
                // that means missing the target. Anything in between is a soft
                // split that the deficit rule below follows, and one region
                // carries on after the other runs out of sites.
                if ((region(group) == Region::Edge && basalShare >= 1.0)
                    || (region(group) == Region::Basal && basalShare <= 0.0))
                    continue;
                if (!hasSiteFor(group)) {
                    exhausted[g] = 1;
                    continue;
                }
                // Stop at the CLOSEST reachable composition rather than the
                // first one past the target: with a carboxyl worth two oxygens,
                // blindly placing until the ratio drops below the target can
                // overshoot by more than stopping short would have missed.
                const double after = std::abs(
                    ratioAfter(oxygensPerGroup(group),
                               group == Group::Carboxyl ? 1 : 0)
                    - local.targetRatio);
                if (after < current)
                    candidates.push_back(group);
            }
            if (candidates.empty())
                break;

            // Split the oxygen budget between basal and edge chemistry by
            // whichever region is furthest behind its share.
            const bool anyBasal =
                std::any_of(candidates.begin(), candidates.end(), [](Group g) {
                    return region(g) == Region::Basal;
                });
            const bool anyEdge =
                std::any_of(candidates.begin(), candidates.end(), [](Group g) {
                    return region(g) == Region::Edge;
                });
            Region pick = Region::Basal;
            if (anyBasal && anyEdge) {
                const double next = oxygens + 1.0;
                const double deficitBasal = basalShare * next - delivered[0];
                const double deficitEdge = (1.0 - basalShare) * next - delivered[1];
                pick = deficitBasal >= deficitEdge ? Region::Basal : Region::Edge;
            } else {
                pick = anyBasal ? Region::Basal : Region::Edge;
            }

            std::vector<Group> pool;
            std::vector<double> weights;
            for (Group group : candidates) {
                if (region(group) != pick)
                    continue;
                pool.push_back(group);
                weights.push_back(config.weightFor(group));
            }
            std::discrete_distribution<int> pickGroup(weights.begin(),
                                                      weights.end());
            const Group group = pool[static_cast<std::size_t>(pickGroup(rng))];

            if (!place(group)) {
                exhausted[static_cast<std::size_t>(group)] = 1;
                continue;
            }
            ++local.placed[static_cast<std::size_t>(group)];
            oxygens += oxygensPerGroup(group);
            if (group == Group::Carboxyl)
                ++extraCarbons;
            delivered[pick == Region::Basal ? 0 : 1] += oxygensPerGroup(group);
        }
    }

    // --- Edge termination ---------------------------------------------------
    // Every edge carbon the chemistry did not claim keeps its hydrogen. Doing
    // this AFTER functionalization is the point: a functionalized edge carbon
    // has had its H substituted, not added to.
    if (!fw.periodic && config.hydrogenTerminateEdges) {
        for (int i = 0; i < carbonCount; ++i) {
            if (!fw.isEdge[static_cast<std::size_t>(i)]
                || owner[static_cast<std::size_t>(i)] >= 0)
                continue;
            attachments.push_back(makeAtom(
                kZ_H, carbonAt(i)
                    + fw.outward[static_cast<std::size_t>(i)] * kCH_edge));
            ++local.hydrogenTerminatedEdges;
        }
    }

    for (const Atom& atom : attachments)
        structure.addAtom(atom);
    if (!fw.periodic)
        fitFlakeCell(structure);

    // --- Accounting ---------------------------------------------------------
    local.functionalizedCarbons = static_cast<int>(
        std::count_if(owner.begin(), owner.end(), [](int o) { return o >= 0; }));
    for (const Atom& atom : structure.atoms()) {
        if (atom.atomicNumber == kZ_C)
            ++local.totalCarbonAtoms;
        else if (atom.atomicNumber == kZ_O)
            ++local.oxygenAtoms;
        else if (atom.atomicNumber == kZ_H)
            ++local.hydrogenAtoms;
    }

    // Carry the classification the whole chemistry rests on into the structure,
    // so it can be seen in the viewport instead of taken on faith.
    std::vector<double> edgeField(structure.size(), 0.0);
    for (int i = 0; i < carbonCount; ++i)
        edgeField[static_cast<std::size_t>(i)] =
            fw.isEdge[static_cast<std::size_t>(i)] ? 1.0 : 0.0;
    structure.setScalarField("edge", std::move(edgeField));

    // --- Shortfalls, reported rather than absorbed --------------------------
    std::vector<std::string> notes;

    if (config.dosing == Dosing::ExplicitCoverage) {
        std::ostringstream shortfall;
        for (std::size_t g = 0; g < kGroupCount; ++g) {
            if (local.placed[g] < local.requested[g])
                shortfall << (shortfall.tellp() > 0 ? "; " : "")
                          << name(static_cast<Group>(g)) << ": placed "
                          << local.placed[g] << " of " << local.requested[g];
        }
        if (shortfall.tellp() > 0) {
            shortfall << ". Sites ran out — epoxides need a bonded pair of "
                         "unfunctionalized basal carbons, and every group "
                         "permanently consumes the carbons it occupies. Lower "
                         "the coverages, or enlarge the sheet / flake.";
            notes.push_back(shortfall.str());
        }
    } else {
        const double achieved = local.carbonToOxygenRatio();
        // A ratio ABOVE the target means too little oxygen: the substrate ran
        // out of sites before the requested oxidation level was reached.
        local.targetReached =
            local.oxygenAtoms > 0 && achieved <= local.targetRatio * 1.02;
        if (!local.targetReached) {
            std::ostringstream missed;
            missed.setf(std::ios::fixed);
            missed.precision(2);
            missed << "Target C/O " << local.targetRatio
                   << " was not reached: the structure came out at ";
            if (local.oxygenAtoms == 0)
                missed << "pristine carbon, with no group placeable at all";
            else
                missed << achieved;
            missed << ". Enable more groups, or use a larger substrate — a "
                      "nanoflake has only 6m edge carbons, which "
                      "caps how much edge chemistry it can carry.";
            notes.push_back(missed.str());
        }
    }

    // Edge chemistry asked for on a substrate that has no edges is the one
    // mistake that would otherwise look like a silent no-op.
    if (fw.edgeCount == 0) {
        bool wantedEdge = false;
        for (std::size_t g = 0; g < kGroupCount; ++g) {
            const auto group = static_cast<Group>(g);
            if (region(group) != Region::Edge)
                continue;
            wantedEdge = wantedEdge
                || (config.dosing == Dosing::ExplicitCoverage
                        ? config.coverageFor(group) > 0.0
                        : config.weightFor(group) > 0.0);
        }
        if (wantedEdge)
            notes.emplace_back(
                "Carboxyl, carbonyl and edge hydroxyl are EDGE chemistry, and a "
                "periodic sheet has no edges — none were placed. Choose the "
                "nanoflake base to use them.");
    }

    for (const std::string& fragment : notes)
        local.note += (local.note.empty() ? "" : " ") + fragment;

    if (report)
        *report = local;
    return structure;
}

} // namespace calango::core
