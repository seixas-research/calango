#include "core/GrapheneOxideBuilder.hpp"

#include "core/UnitCell.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <sstream>

namespace calango::core {

namespace {

// Graphene lattice constant (Å) and the derived C-C bond length.
constexpr double kLatticeA = 2.46;
constexpr double kCC = kLatticeA / 1.7320508075688772; // a / sqrt(3) = 1.42 Å

// Vacuum along z. Enough that periodic images of the functional groups do not
// interact — the groups themselves stand ~1.5 Å off the plane, so a thinner
// slab than this would have them talking to their own image.
constexpr double kVacuum = 20.0;

// Bond lengths for the attached groups (Å), from the usual sp3 C-O chemistry.
constexpr double kCO_epoxide = 1.44;
constexpr double kCO_hydroxyl = 1.48;
constexpr double kOH = 0.98;
constexpr double kCO_carbonyl = 1.23;
constexpr double kCC_carboxyl = 1.52;
constexpr double kCO_double = 1.21;
constexpr double kCO_single = 1.34;

constexpr int kZ_H = 1;
constexpr int kZ_C = 6;
constexpr int kZ_O = 8;

/// How many basal carbons one group of each kind consumes. This is what makes
/// the coverages additive: an epoxide bridges a C-C bond and rehybridizes BOTH
/// carbons, so it costs two.
int carbonCost(GrapheneOxideBuilder::Group group)
{
    return group == GrapheneOxideBuilder::Group::Epoxide ? 2 : 1;
}

struct Site {
    Vec3 position;
    int index = 0;
};

} // namespace

const char* GrapheneOxideBuilder::name(Group group)
{
    switch (group) {
    case Group::Epoxide:   return "epoxide";
    case Group::Hydroxyl:  return "hydroxyl";
    case Group::Carboxyl:  return "carboxyl";
    case Group::Carbonyl:  return "carbonyl";
    }
    return "?";
}

double GrapheneOxideBuilder::Report::carbonToOxygenRatio() const
{
    // Oxygens per group: epoxide 1, hydroxyl 1, carbonyl 1, carboxyl 2.
    const int oxygens = placed[0] + placed[1] + 2 * placed[2] + placed[3];
    if (oxygens == 0)
        return 0.0;
    return static_cast<double>(carbonCount) / oxygens;
}

Structure GrapheneOxideBuilder::pristine(const Config& config)
{
    const int nx = std::max(1, config.supercell[0]);
    const int ny = std::max(1, config.supercell[1]);

    Structure structure;
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
        // Orthogonal 4-atom cell: the conventional rectangular tiling, easier
        // to build supercells and slabs from because the axes are orthogonal.
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
                atom.position =
                    Vec3{site.x + shift.x, site.y + shift.y, kVacuum * 0.5};
                structure.addAtom(atom);
            }
        }
    }

    structure.setCell(UnitCell(Vec3{a1.x * nx, a1.y * nx, 0.0},
                               Vec3{a2.x * ny, a2.y * ny, 0.0},
                               Vec3{0.0, 0.0, kVacuum}));
    return structure;
}

Structure GrapheneOxideBuilder::build(const Config& config, Report* report)
{
    Structure structure = pristine(config);
    const int carbonCount = static_cast<int>(structure.size());

    Report local;
    local.carbonCount = carbonCount;

    // --- Neighbour list over the basal carbons ------------------------------
    // Needed for epoxides, which bridge a bonded PAIR. Minimum-image in x/y so
    // pairs across the periodic boundary are found too; the sheet is flat, so
    // z plays no part.
    const auto& cellVectors = structure.cell().vectors();
    const double lx = cellVectors[0].x;
    const double ly = cellVectors[1].y;
    const double shear = cellVectors[1].x; // non-zero for the primitive cell

    const auto minimumImage = [&](Vec3 d) {
        // Reduce along a2 first (it carries the shear), then along a1.
        const double nj = std::round(d.y / ly);
        d.y -= nj * ly;
        d.x -= nj * shear;
        d.x -= std::round(d.x / lx) * lx;
        return d;
    };

    std::vector<std::pair<int, int>> bonds;
    const double bondCut = kCC * 1.25;
    for (int i = 0; i < carbonCount; ++i) {
        for (int j = i + 1; j < carbonCount; ++j) {
            const Vec3 d = minimumImage(Vec3{
                structure.atoms()[j].position.x - structure.atoms()[i].position.x,
                structure.atoms()[j].position.y - structure.atoms()[i].position.y,
                0.0});
            if (std::sqrt(d.x * d.x + d.y * d.y) < bondCut)
                bonds.push_back({i, j});
        }
    }

    // --- Site assignment ----------------------------------------------------
    // `owner[c]` is the group occupying carbon c, or -1. This single array IS
    // the collision guardrail: every placement below tests it first and skips
    // any carbon already spoken for, so no carbon can ever carry two groups.
    // A carbon has one out-of-plane valence after rehybridizing to sp3;
    // stacking a second group on it would make it pentavalent.
    std::vector<int> owner(carbonCount, -1);
    std::mt19937 rng(config.seed);

    // Which face each group points to. Alternating by draw rather than by
    // position keeps both faces populated without imposing a pattern.
    std::bernoulli_distribution coinFlip(0.5);
    const auto faceSign = [&]() -> double {
        if (!config.bothFaces)
            return 1.0;
        return coinFlip(rng) ? 1.0 : -1.0;
    };

    const auto targetCount = [&](Group group) {
        const double fraction = std::clamp(config.coverageFor(group), 0.0, 1.0);
        return static_cast<int>(std::llround(fraction * carbonCount
                                             / carbonCost(group)));
    };

    // Epoxides first: they need a bonded PAIR of free carbons, which is the
    // most constrained requirement, so satisfying it before the single-site
    // groups eat the lattice gives the requested composition the best chance.
    struct Pending {
        Group group;
        int count;
    };
    const Pending order[] = {
        {Group::Epoxide, targetCount(Group::Epoxide)},
        {Group::Carboxyl, targetCount(Group::Carboxyl)},
        {Group::Carbonyl, targetCount(Group::Carbonyl)},
        {Group::Hydroxyl, targetCount(Group::Hydroxyl)},
    };
    for (const Pending& pending : order)
        local.requested[static_cast<std::size_t>(pending.group)] = pending.count;

    // Shuffled carbon and bond orders: the decoration is a random sample of the
    // composition, not a pattern.
    std::vector<int> carbonOrder(carbonCount);
    std::iota(carbonOrder.begin(), carbonOrder.end(), 0);
    std::shuffle(carbonOrder.begin(), carbonOrder.end(), rng);
    std::shuffle(bonds.begin(), bonds.end(), rng);

    std::vector<Atom> attachments;
    const auto carbonAt = [&](int index) {
        return structure.atoms()[static_cast<std::size_t>(index)].position;
    };

    for (const Pending& pending : order) {
        const auto slot = static_cast<std::size_t>(pending.group);
        int placed = 0;

        if (pending.group == Group::Epoxide) {
            for (const auto& [i, j] : bonds) {
                if (placed >= pending.count)
                    break;
                if (owner[i] >= 0 || owner[j] >= 0)
                    continue; // one of the pair is already functionalized
                owner[i] = owner[j] = static_cast<int>(slot);
                const Vec3 ci = carbonAt(i);
                const Vec3 cj = carbonAt(j);
                const Vec3 d = minimumImage(Vec3{cj.x - ci.x, cj.y - ci.y, 0.0});
                // The bridging O sits above the bond midpoint, at the height
                // that gives the C-O bonds their proper length.
                const double half = 0.5 * std::sqrt(d.x * d.x + d.y * d.y);
                const double height =
                    std::sqrt(std::max(kCO_epoxide * kCO_epoxide - half * half,
                                       0.25));
                Atom oxygen;
                oxygen.atomicNumber = kZ_O;
                oxygen.position = Vec3{ci.x + d.x * 0.5, ci.y + d.y * 0.5,
                                       ci.z + faceSign() * height};
                attachments.push_back(oxygen);
                ++placed;
            }
        } else {
            for (int carbon : carbonOrder) {
                if (placed >= pending.count)
                    break;
                if (owner[carbon] >= 0)
                    continue;
                owner[carbon] = static_cast<int>(slot);
                const Vec3 c = carbonAt(carbon);
                const double sign = faceSign();

                switch (pending.group) {
                case Group::Hydroxyl: {
                    Atom oxygen;
                    oxygen.atomicNumber = kZ_O;
                    oxygen.position =
                        Vec3{c.x, c.y, c.z + sign * kCO_hydroxyl};
                    attachments.push_back(oxygen);
                    // The O-H points away from the sheet, tilted so it is not
                    // collinear with the C-O bond (the real angle is ~108°).
                    Atom hydrogen;
                    hydrogen.atomicNumber = kZ_H;
                    hydrogen.position =
                        Vec3{c.x + kOH * 0.94, c.y,
                             oxygen.position.z + sign * kOH * 0.33};
                    attachments.push_back(hydrogen);
                    break;
                }
                case Group::Carbonyl: {
                    Atom oxygen;
                    oxygen.atomicNumber = kZ_O;
                    oxygen.position =
                        Vec3{c.x, c.y, c.z + sign * kCO_carbonyl};
                    attachments.push_back(oxygen);
                    break;
                }
                case Group::Carboxyl: {
                    // -COOH: its own carbon, a double-bonded O, and an -OH.
                    Atom carboxylC;
                    carboxylC.atomicNumber = kZ_C;
                    carboxylC.position =
                        Vec3{c.x, c.y, c.z + sign * kCC_carboxyl};
                    attachments.push_back(carboxylC);
                    Atom doubleO;
                    doubleO.atomicNumber = kZ_O;
                    doubleO.position =
                        Vec3{c.x + kCO_double * 0.87, c.y,
                             carboxylC.position.z + sign * kCO_double * 0.5};
                    attachments.push_back(doubleO);
                    Atom singleO;
                    singleO.atomicNumber = kZ_O;
                    singleO.position =
                        Vec3{c.x - kCO_single * 0.87, c.y,
                             carboxylC.position.z + sign * kCO_single * 0.5};
                    attachments.push_back(singleO);
                    Atom hydrogen;
                    hydrogen.atomicNumber = kZ_H;
                    hydrogen.position =
                        Vec3{singleO.position.x - kOH * 0.8, singleO.position.y,
                             singleO.position.z + sign * kOH * 0.6};
                    attachments.push_back(hydrogen);
                    break;
                }
                case Group::Epoxide:
                    break; // handled above
                }
                ++placed;
            }
        }
        local.placed[slot] = placed;
    }

    for (const Atom& atom : attachments)
        structure.addAtom(atom);

    local.functionalizedCarbons = static_cast<int>(
        std::count_if(owner.begin(), owner.end(), [](int o) { return o >= 0; }));

    // Report any shortfall explicitly. Silently returning fewer groups than
    // asked for would leave the user believing they have a composition they do
    // not have.
    std::ostringstream note;
    for (const Pending& pending : order) {
        const auto slot = static_cast<std::size_t>(pending.group);
        if (local.placed[slot] < local.requested[slot]) {
            note << (note.tellp() > 0 ? "; " : "") << name(pending.group)
                 << ": placed " << local.placed[slot] << " of "
                 << local.requested[slot];
        }
    }
    if (note.tellp() > 0) {
        note << ". The lattice ran out of free carbons — epoxides need a bonded "
                "pair of unfunctionalized sites, and every group permanently "
                "consumes the carbons it occupies. Lower the coverages or "
                "enlarge the supercell.";
        local.note = note.str();
    }

    if (report)
        *report = local;
    return structure;
}

} // namespace calango::core
