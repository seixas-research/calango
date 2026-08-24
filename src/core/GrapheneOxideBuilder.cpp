#include "core/GrapheneOxideBuilder.hpp"
#include "core/PhysicalConstants.hpp"

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

constexpr double kDeg = kPi / 180.0;

// Graphene lattice constant (Å) and the derived C-C bond length.
constexpr double kLatticeA = 2.46;
constexpr double kCC = kLatticeA / 1.7320508075688772; // a / sqrt(3) = 1.42 Å

// Vacuum CLEARANCE default, in angstrom -- the gap between the structure and
// its nearest periodic image. Configurable per build
// (Config::vacuumAngstrom); this is only the fallback for the callers that
// have no Config in hand.
//
// Enough that periodic images of the functional groups do not interact: the
// groups stand ~1.5 A off the plane, so a hydroxyl and its own image sit
// ~17 A apart here.
constexpr double kVacuumClearance = 10.0;

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
/// expected and fine. What is not fine is a contact shorter than a covalent
/// bond: two groups placed on adjacent sites with their substituents pointing
/// at each other can land oxygens 0.1 Å apart, which is not a strained
/// structure but a fused one, and no optimizer recovers from it. Placements
/// that would do that are refused and the site is tried in another
/// orientation, or skipped.
///
/// This threshold deliberately ADMITS an epoxide next to a same-face hydroxyl
/// — O···O of 1.89 Å on the flat sheet — because that adjacency is a real,
/// common graphene oxide motif (the Lerf–Klinowski picture, with the O–H
/// hydrogen-bonded to the epoxide oxygen) and the builder's job is the
/// composition's motif space, not one calculator's opinion of it. What it
/// must NOT be assumed is that the contact "relaxes apart as the carbons
/// pucker": measured on a 7×4 sheet carrying three of them, MACE-MP-0 (small
/// AND medium) opens the epoxide instead — a gentle force-based relaxation
/// slides the oxygen onto one carbon, and a thermal burst does so faster.
/// Which groups survive is therefore the dynamics' verdict, and
/// GrapheneOxideMcmdScriptGenerator's initial equilibration relocates the
/// ones that do not rather than refusing to start; its own proposal
/// clearance (2.0 Å heavy–heavy) is wider than this one for that reason —
/// it prices a burst, not a placement.
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
    // Clamped rather than trusted: the wizard's spin box has a floor, but a
    // Config can also arrive from a saved project or a headless caller, and
    // a zero or negative clearance would put the sheet on top of its own
    // image.
    const double vacuum = std::max(1.0, config.vacuumAngstrom);

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
                                         vacuum};
                    fw.carbons.addAtom(atom);
                }
            }
        }
        // 2 x the clearance: `vacuum` above the sheet and `vacuum` below,
        // with the sheet itself at the midpoint.
        fw.carbons.setCell(UnitCell(Vec3{a1.x * nx, a1.y * nx, 0.0},
                                    Vec3{a2.x * ny, a2.y * ny, 0.0},
                                    Vec3{0.0, 0.0, 2.0 * vacuum}));
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

/// Fit an orthorhombic box with `pad` of vacuum on every side around a
/// finite structure, translating it to sit inside. A cluster still needs a cell
/// because every plane-wave code demands one; what it must not have is a
/// neighbour.
///
/// `pad` is the SAME number the periodic sheet uses as its clearance above
/// and below (Config::vacuumAngstrom) -- one setting, one meaning: how far
/// apart must a copy be. The sheet spends it on two faces and a flake on
/// six, which is a property of the geometry, not a second decision.
void fitFlakeCell(Structure& structure, double pad = kVacuumClearance)
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
        atom.position.x += pad - lo.x;
        atom.position.y += pad - lo.y;
        atom.position.z += pad - lo.z;
    }
    structure.setCell(UnitCell(Vec3{hi.x - lo.x + 2 * pad, 0.0, 0.0},
                               Vec3{0.0, hi.y - lo.y + 2 * pad, 0.0},
                               Vec3{0.0, 0.0, hi.z - lo.z + 2 * pad},
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
GrapheneOxideBuilder::findFunctionalGroups(const Structure& structure,
                                           double bondTolerance)
{
    const int n = static_cast<int>(structure.size());
    std::vector<GroupCluster> clusters;
    if (n == 0)
        return clusters;

    // Minimum-image aware, so a group bridging the cell boundary of a periodic
    // sheet is found like any other.
    std::vector<std::vector<int>> neighbours(static_cast<std::size_t>(n));
    for (const Bond& bond : structure.detectBonds(bondTolerance)) {
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
GrapheneOxideBuilder::functionalGroupLabels(const Structure& structure,
                                            double bondTolerance)
{
    std::vector<int> labels(structure.size(), -1);
    for (const GroupCluster& cluster :
         findFunctionalGroups(structure, bondTolerance)) {
        for (const int atom : cluster.atoms) {
            if (atom >= 0 && atom < static_cast<int>(labels.size()))
                labels[static_cast<std::size_t>(atom)] =
                    static_cast<int>(cluster.kind);
        }
    }
    return labels;
}

bool GrapheneOxideBuilder::hasClassification(const Structure& structure)
{
    const auto& fields = structure.scalarFields();
    const auto it = fields.find("go_group");
    return it != fields.end() && it->second.size() == structure.size();
}

void GrapheneOxideBuilder::classifyFromBonding(Structure& structure)
{
    const std::size_t n = structure.size();
    std::vector<double> groupField(n, -1.0);
    std::vector<double> groupIdField(n, -1.0);
    std::vector<double> pairIdField(n, -1.0);

    const std::vector<GroupCluster> clusters = findFunctionalGroups(structure);
    // Instance ids in cluster-discovery order — arbitrary but stable within
    // one call, which is all a fallback classification promises; nothing
    // downstream depends on ids matching a prior run's.
    for (std::size_t gid = 0; gid < clusters.size(); ++gid) {
        for (const int atom : clusters[gid].atoms) {
            if (atom < 0 || static_cast<std::size_t>(atom) >= n)
                continue;
            groupField[static_cast<std::size_t>(atom)] =
                static_cast<double>(static_cast<int>(clusters[gid].kind));
            groupIdField[static_cast<std::size_t>(atom)] =
                static_cast<double>(gid);
        }
    }

    // Re-derive antiposition pairs from geometry: two Hydroxyl clusters whose
    // host carbons are bonded to each other, with their oxygens on opposite
    // faces, are treated as one trans-diol pair. A structure that was never
    // built with antiposition simply has none — pairIdField stays -1
    // throughout, which is correct.
    int pairId = 0;
    for (const auto& [a, b] : findAntipositionPairs(structure, clusters)) {
        for (const int atom : clusters[static_cast<std::size_t>(a)].atoms)
            pairIdField[static_cast<std::size_t>(atom)] =
                static_cast<double>(pairId);
        for (const int atom : clusters[static_cast<std::size_t>(b)].atoms)
            pairIdField[static_cast<std::size_t>(atom)] =
                static_cast<double>(pairId);
        ++pairId;
    }

    structure.setScalarField("go_group", std::move(groupField));
    structure.setScalarField("go_group_id", std::move(groupIdField));
    structure.setScalarField("go_pair_id", std::move(pairIdField));
}

std::vector<std::pair<int, int>> GrapheneOxideBuilder::findAntipositionPairs(
    const Structure& structure, const std::vector<GroupCluster>& clusters)
{
    std::vector<std::pair<int, int>> pairs;
    std::vector<std::size_t> hydroxylIds;
    for (std::size_t gid = 0; gid < clusters.size(); ++gid)
        if (clusters[gid].kind == Group::Hydroxyl)
            hydroxylIds.push_back(gid);
    if (hydroxylIds.size() < 2)
        return pairs;

    const std::size_t n = structure.size();
    std::vector<std::vector<int>> neighbours(n);
    for (const Bond& bond : structure.detectBonds()) {
        neighbours[static_cast<std::size_t>(bond.i)].push_back(bond.j);
        neighbours[static_cast<std::size_t>(bond.j)].push_back(bond.i);
    }
    const auto atomOf = [&](const GroupCluster& cluster, int element) {
        for (const int atom : cluster.atoms)
            if (structure.atoms()[static_cast<std::size_t>(atom)].atomicNumber
                == element)
                return atom;
        return -1;
    };

    std::vector<char> paired(hydroxylIds.size(), 0);
    for (std::size_t a = 0; a < hydroxylIds.size(); ++a) {
        if (paired[a])
            continue;
        const GroupCluster& clusterA = clusters[hydroxylIds[a]];
        const int hostA = atomOf(clusterA, kZ_C);
        const int oxygenA = atomOf(clusterA, kZ_O);
        if (hostA < 0 || oxygenA < 0)
            continue;
        for (std::size_t b = a + 1; b < hydroxylIds.size(); ++b) {
            if (paired[b])
                continue;
            const GroupCluster& clusterB = clusters[hydroxylIds[b]];
            const int hostB = atomOf(clusterB, kZ_C);
            const int oxygenB = atomOf(clusterB, kZ_O);
            if (hostB < 0 || oxygenB < 0)
                continue;
            const auto& nb = neighbours[static_cast<std::size_t>(hostA)];
            const bool bonded =
                std::find(nb.begin(), nb.end(), hostB) != nb.end();
            if (!bonded)
                continue;
            const double zHostA =
                structure.atoms()[static_cast<std::size_t>(hostA)].position.z;
            const double zHostB =
                structure.atoms()[static_cast<std::size_t>(hostB)].position.z;
            const double zOxygenA =
                structure.atoms()[static_cast<std::size_t>(oxygenA)].position.z;
            const double zOxygenB =
                structure.atoms()[static_cast<std::size_t>(oxygenB)].position.z;
            const bool oppositeFaces =
                (zOxygenA - zHostA) * (zOxygenB - zHostB) < 0.0;
            if (!oppositeFaces)
                continue;
            pairs.emplace_back(static_cast<int>(hydroxylIds[a]),
                               static_cast<int>(hydroxylIds[b]));
            paired[a] = 1;
            paired[b] = 1;
            break;
        }
    }
    return pairs;
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
    fitFlakeCell(structure, std::max(1.0, config.vacuumAngstrom));
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
    // The "Graphene Oxide Build" contract's per-atom classification, filled
    // in alongside `owner` as groups are committed and written onto the
    // structure as scalar fields at the end of this function — see build()'s
    // doc comment. `carbonGroupId`/`carbonPairId` parallel `owner`;
    // `attachmentGroup`/`attachmentGroupId`/`attachmentPairId` parallel
    // `attachments` below (declared after it, entries pushed in lockstep).
    int nextGroupId = 0;
    int nextPairId = 0;
    std::vector<int> carbonGroupId(static_cast<std::size_t>(carbonCount), -1);
    std::vector<int> carbonPairId(static_cast<std::size_t>(carbonCount), -1);

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
            // Antiposition draws from the bonded-PAIR pool, same as an
            // epoxide — a free single carbon is not enough, it needs a free
            // NEIGHBOUR too.
            if (config.hydroxylAntiposition) {
                advanceBonds();
                return bondCursor < fw.basalBonds.size();
            }
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
    // Parallel to `attachments`, one entry pushed per attachment atom in
    // commit() (or, for an unfunctionalized edge's terminating H, pushed
    // directly as -1/-1/-1 — that atom belongs to no group).
    std::vector<int> attachmentGroup;
    std::vector<int> attachmentGroupId;
    std::vector<int> attachmentPairId;
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
    // `pairId` defaults to -1 (no antiposition pairing); the antiposition
    // hydroxyl branch below is the only caller that passes one, drawn once
    // per pair and passed to BOTH of its two commit() calls.
    const auto commit = [&](const std::vector<Atom>& pending,
                            std::initializer_list<int> hosts, Group group,
                            int pairId = -1) {
        const int groupId = nextGroupId++;
        for (int host : hosts) {
            owner[static_cast<std::size_t>(host)] = static_cast<int>(group);
            carbonGroupId[static_cast<std::size_t>(host)] = groupId;
            carbonPairId[static_cast<std::size_t>(host)] = pairId;
        }
        for (const Atom& atom : pending) {
            const int index = static_cast<int>(attachments.size());
            attachments.push_back(atom);
            attachmentGroup.push_back(static_cast<int>(group));
            attachmentGroupId.push_back(groupId);
            attachmentPairId.push_back(pairId);
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
    //
    // Returns the number of `group` instances actually placed: 0 on failure,
    // 1 for an ordinary single-site (or single-bond) group, and 2 for an
    // antiposition hydroxyl pair — the one case where a single call delivers
    // more than one group. Every call site adds this count rather than
    // assuming 1, precisely so that case is accounted for correctly.
    const auto place = [&](Group group) -> int {
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
                    return 1;
                }
            }
            return 0;
        }

        if (group == Group::Hydroxyl && config.hydroxylAntiposition) {
            // Same bonded-pair pool an epoxide draws from — a "neighbouring
            // carbon pair" is exactly what fw.basalBonds already is — but
            // unlike an epoxide's single bridging oxygen, this commits TWO
            // independent hydroxyls, one per carbon, with their faces forced
            // to opposite signs: that opposition IS the antiposition motif,
            // so it draws a fresh sign here rather than going through
            // faceSign() (which would collapse to the same face on both
            // carbons whenever `bothFaces` is off).
            advanceBonds();
            int tried = 0;
            for (std::size_t k = bondCursor;
                 k < fw.basalBonds.size() && tried < kStericTries; ++k) {
                const auto [i, j] = fw.basalBonds[k];
                if (owner[static_cast<std::size_t>(i)] >= 0
                    || owner[static_cast<std::size_t>(j)] >= 0)
                    continue;
                ++tried;
                const double sign = coinFlip(rng) ? 1.0 : -1.0;
                // A few azimuth draws per pair, same idea as the ordinary
                // hydroxyl site loop below trying several orientations before
                // moving on to the next carbon: a crowded pair should not be
                // abandoned on the first unlucky -OH swing.
                for (int variant = 0; variant < 3; ++variant) {
                    const std::vector<Atom> pendingI =
                        basalGeometry(i, sign, azimuth(rng));
                    const std::vector<Atom> pendingJ =
                        basalGeometry(j, -sign, azimuth(rng));
                    if (collides(pendingI, {i}) || collides(pendingJ, {j}))
                        continue;
                    // `collides` only checks against attachments already
                    // COMMITTED, so the pair's own two brand-new hydroxyls —
                    // one bond apart — must be cross-checked against each
                    // other separately.
                    bool crossClash = false;
                    for (const Atom& a : pendingI) {
                        for (const Atom& b : pendingJ) {
                            if (gap(a.position, b.position) < kMinContact) {
                                crossClash = true;
                                break;
                            }
                        }
                        if (crossClash)
                            break;
                    }
                    if (crossClash)
                        continue;
                    // One pair id, shared by both halves — the antiposition
                    // registry ("go_pair_id") a downstream MCMD or analysis
                    // module reads to recover which two hydroxyls form a
                    // trans-diol.
                    const int pairId = nextPairId++;
                    commit(pendingI, {i}, group, pairId);
                    commit(pendingJ, {j}, group, pairId);
                    return 2;
                }
            }
            return 0;
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
                return 1;
            }
        }
        return 0;
    };

    // --- Dosing --------------------------------------------------------------
    if (config.dosing == Dosing::DecoupledRegions) {
        // The rim and the basal plane are dosed against their OWN pools, in
        // that order. Edges first because the rim is the smaller and more
        // constrained pool: a flake has 6m edge carbons against 6m(m-1) basal
        // ones, so letting basal chemistry run first would never starve, while
        // the reverse can. They do not compete for sites in any case — the two
        // regions are disjoint — so the order only affects the random stream.

        // -- Rim ------------------------------------------------------------
        // Exactly zero is categorical: a strictly hydrogen-terminated edge, not
        // a rounding of "very little". Everything else is a count of edge
        // carbons to functionalize, and the carbons that miss out keep their
        // hydrogen (see hydrogenTerminateEdges below).
        const double edgeFraction = std::clamp(config.edgeOxidation, 0.0, 1.0);
        int edgeTarget = 0;
        if (edgeFraction > 0.0 && fw.edgeCount > 0) {
            edgeTarget = static_cast<int>(
                std::llround(edgeFraction * fw.edgeCount));
            // A nonzero request on a substrate with a rim must place at least
            // one group: rounding a deliberate 2 % down to zero would report a
            // hydrogen-terminated flake as though the user had asked for one.
            edgeTarget = std::max(1, std::min(edgeTarget, fw.edgeCount));
        }
        if (edgeTarget > 0) {
            // Oxygen share -> per-group propensity. f of the edge OXYGEN coming
            // from carboxyls means f/2 carboxyl GROUPS against (1-f) carbonyls,
            // because a carboxyl delivers two oxygens and a carbonyl one.
            const double share = std::clamp(config.carboxylShare, 0.0, 1.0);
            std::vector<double> edgeWeights{share / 2.0, 1.0 - share};
            if (edgeWeights[0] <= 0.0 && edgeWeights[1] <= 0.0)
                edgeWeights = {1.0, 1.0};
            std::discrete_distribution<int> pickEdge(edgeWeights.begin(),
                                                     edgeWeights.end());
            const Group edgeGroups[] = {Group::Carboxyl, Group::Carbonyl};
            int placedEdge = 0;
            int stalled = 0;
            // Terminates: every iteration either places a group (consuming an
            // edge carbon from a finite pool) or increments `stalled`, and two
            // consecutive failures with both groups tried means the rim is out
            // of usable sites.
            while (placedEdge < edgeTarget && stalled < 2) {
                const Group group = edgeGroups[pickEdge(rng)];
                if (place(group)) {
                    ++local.placed[static_cast<std::size_t>(group)];
                    ++placedEdge;
                    stalled = 0;
                    continue;
                }
                // The drawn group did not fit; try the other before giving up,
                // since a carboxyl is far bulkier than a carbonyl and a rim too
                // crowded for one may still take the other.
                //
                // Only if the other group was ASKED FOR. A share of exactly 0
                // or 1 excludes one of them, and a fallback that ignores that
                // answers "carboxyls only" with a rim carrying carbonyls —
                // the request quietly overruled by a packing failure.
                const Group other = group == Group::Carboxyl ? Group::Carbonyl
                                                             : Group::Carboxyl;
                const double otherWeight =
                    other == Group::Carboxyl ? edgeWeights[0] : edgeWeights[1];
                if (otherWeight > 0.0 && place(other)) {
                    ++local.placed[static_cast<std::size_t>(other)];
                    ++placedEdge;
                    stalled = 0;
                    continue;
                }
                ++stalled;
            }
            local.edgeGroupsRequested = edgeTarget;
            local.edgeGroupsPlaced = placedEdge;
        }

        // -- Basal plane -----------------------------------------------------
        // A count of OXYGENS, not of groups: both basal groups carry exactly
        // one oxygen, so the two coincide here, but stating it in oxygen is
        // what keeps the dial meaning the same thing if a two-oxygen basal
        // group is ever added.
        const double basalOc =
            std::clamp(config.basalOxygenToCarbon, 0.0, 0.5);
        const int basalTarget =
            static_cast<int>(std::llround(basalOc * fw.basalCount));
        if (basalTarget > 0) {
            const double hydroxyl =
                std::clamp(config.basalHydroxylShare, 0.0, 1.0);
            std::vector<double> basalWeights{1.0 - hydroxyl, hydroxyl};
            if (basalWeights[0] <= 0.0 && basalWeights[1] <= 0.0)
                basalWeights = {1.0, 1.0};
            std::discrete_distribution<int> pickBasal(basalWeights.begin(),
                                                      basalWeights.end());
            const Group basalGroups[] = {Group::Epoxide, Group::Hydroxyl};
            int placedOxygen = 0;
            int stalled = 0;
            while (placedOxygen < basalTarget && stalled < 2) {
                const Group group = basalGroups[pickBasal(rng)];
                // `n` rather than an assumed 1: an antiposition hydroxyl
                // placement delivers 2 at once, each carrying one oxygen, so
                // both the group tally and the oxygen budget must move by the
                // same amount the call actually placed.
                if (const int n = place(group); n > 0) {
                    local.placed[static_cast<std::size_t>(group)] += n;
                    placedOxygen += n;
                    stalled = 0;
                    continue;
                }
                // Epoxides need a bonded PAIR of free carbons and run out long
                // before single sites do, so falling back to the hydroxyl is
                // what lets a heavily oxidized basal plane reach its target —
                // but only when the hydroxyl was asked for at all, for the same
                // reason as at the rim.
                const Group other = group == Group::Epoxide ? Group::Hydroxyl
                                                            : Group::Epoxide;
                const double otherWeight =
                    other == Group::Epoxide ? basalWeights[0] : basalWeights[1];
                if (otherWeight > 0.0) {
                    if (const int n = place(other); n > 0) {
                        local.placed[static_cast<std::size_t>(other)] += n;
                        placedOxygen += n;
                        stalled = 0;
                        continue;
                    }
                }
                ++stalled;
            }
            local.basalOxygenRequested = basalTarget;
            local.basalOxygenPlaced = placedOxygen;
        }
    } else if (config.dosing == Dosing::ExplicitCoverage) {
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
            // An antiposition hydroxyl call places 2 at once, so a requested
            // count of exactly N is met with N (even) or overshot by one
            // (odd) rather than ever landing on a lone, unpaired hydroxyl.
            while (local.placed[slot] < local.requested[slot]) {
                const int n = place(group);
                if (n <= 0)
                    break;
                local.placed[slot] += n;
            }
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

            // `ratioAfter` above predicted the effect of ONE group; an
            // antiposition hydroxyl call can deliver 2, in which case the
            // actual step past the target is twice what was predicted — the
            // same "closest reachable, not exact" approximation this search
            // already makes for a carboxyl's extra carbon, just from a
            // different group.
            const int n = place(group);
            if (n <= 0) {
                exhausted[static_cast<std::size_t>(group)] = 1;
                continue;
            }
            local.placed[static_cast<std::size_t>(group)] += n;
            oxygens += oxygensPerGroup(group) * n;
            if (group == Group::Carboxyl)
                ++extraCarbons; // carboxyl never places more than one at a time
            delivered[pick == Region::Basal ? 0 : 1] +=
                oxygensPerGroup(group) * n;
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
            // A terminating hydrogen belongs to no group — kept in lockstep
            // with `attachments` so index alignment holds when the
            // classification fields are built below.
            attachmentGroup.push_back(-1);
            attachmentGroupId.push_back(-1);
            attachmentPairId.push_back(-1);
            ++local.hydrogenTerminatedEdges;
        }
    }

    for (const Atom& atom : attachments)
        structure.addAtom(atom);
    if (!fw.periodic)
        fitFlakeCell(structure, std::max(1.0, config.vacuumAngstrom));

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

    // The Graphene Oxide Build contract: "go_group" / "go_group_id" /
    // "go_pair_id", index-aligned with the finished structure, framework
    // carbons first (indices < carbonCount, from `owner`/`carbonGroupId`/
    // `carbonPairId`) then every attachment atom in the order it was
    // appended (from the parallel `attachment*` vectors) — see build()'s doc
    // comment for the encoding.
    std::vector<double> groupField(structure.size(), -1.0);
    std::vector<double> groupIdField(structure.size(), -1.0);
    std::vector<double> pairIdField(structure.size(), -1.0);
    for (int i = 0; i < carbonCount; ++i) {
        const int o = owner[static_cast<std::size_t>(i)];
        if (o < 0)
            continue;
        groupField[static_cast<std::size_t>(i)] = static_cast<double>(o);
        groupIdField[static_cast<std::size_t>(i)] =
            static_cast<double>(carbonGroupId[static_cast<std::size_t>(i)]);
        pairIdField[static_cast<std::size_t>(i)] =
            static_cast<double>(carbonPairId[static_cast<std::size_t>(i)]);
    }
    for (std::size_t k = 0; k < attachments.size(); ++k) {
        const std::size_t idx = static_cast<std::size_t>(carbonCount) + k;
        groupField[idx] = static_cast<double>(attachmentGroup[k]);
        groupIdField[idx] = static_cast<double>(attachmentGroupId[k]);
        pairIdField[idx] = static_cast<double>(attachmentPairId[k]);
    }
    structure.setScalarField("go_group", std::move(groupField));
    structure.setScalarField("go_group_id", std::move(groupIdField));
    structure.setScalarField("go_pair_id", std::move(pairIdField));

    // --- Shortfalls, reported rather than absorbed --------------------------
    std::vector<std::string> notes;

    if (config.dosing == Dosing::DecoupledRegions) {
        // One line per dial that fell short, naming which one — the mode exists
        // to keep the two independent, so a shared "did not reach the target"
        // would undo exactly the distinction the user came here for.
        std::ostringstream missed;
        if (local.basalOxygenPlaced < local.basalOxygenRequested) {
            missed << "The basal plane took " << local.basalOxygenPlaced
                   << " of the " << local.basalOxygenRequested
                   << " oxygens requested: the sheet ran out of room. Epoxides "
                      "need a bonded pair of free basal carbons, and every "
                      "group permanently consumes the carbons it occupies.";
        }
        if (local.edgeGroupsPlaced < local.edgeGroupsRequested) {
            if (missed.tellp() > 0)
                missed << ' ';
            missed << "The rim took " << local.edgeGroupsPlaced << " of the "
                   << local.edgeGroupsRequested
                   << " groups requested — a flake has only 6m edge carbons, "
                      "and carboxyls are bulky enough to block their "
                      "neighbours.";
        }
        if (missed.tellp() > 0)
            notes.push_back(missed.str());
    } else if (config.dosing == Dosing::ExplicitCoverage) {
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
            switch (config.dosing) {
            case Dosing::ExplicitCoverage:
                wantedEdge = wantedEdge || config.coverageFor(group) > 0.0;
                break;
            case Dosing::TargetRatio:
                wantedEdge = wantedEdge || config.weightFor(group) > 0.0;
                break;
            case Dosing::DecoupledRegions:
                // One dial governs the whole rim here, so the per-group weights
                // say nothing about intent — only whether the rim was asked for
                // at all does.
                wantedEdge = wantedEdge || config.edgeOxidation > 0.0;
                break;
            }
        }
        if (wantedEdge)
            notes.emplace_back(
                "Carboxyl and carbonyl are EDGE chemistry, and a periodic "
                "sheet has no edges — none were placed. Choose the nanoflake "
                "base to use them.");
    }

    for (const std::string& fragment : notes)
        local.note += (local.note.empty() ? "" : " ") + fragment;

    if (report)
        *report = local;
    return structure;
}

} // namespace calango::core
