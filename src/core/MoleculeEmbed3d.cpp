#include "core/MoleculeEmbed3d.hpp"

#include "core/Element.hpp"
#include "core/PhysicalConstants.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

namespace calango::core {
namespace {

/// The bond length a pair of elements takes at a given bond order, from the
/// covalent radii and the same order/ratio table HydrogenCompletion perceives
/// bond orders WITH — read backwards. Keeping the two consistent means a
/// structure exported from the sketcher is perceived back at the orders it was
/// drawn with, rather than coming up one order short in the viewport.
double targetLength(int za, int zb, double order)
{
    const double single = static_cast<double>(Elements::data(za).covalentRadius)
        + static_cast<double>(Elements::data(zb).covalentRadius);
    if (single < 1e-6)
        return 1.5;
    if (order >= 2.9)
        return 0.79 * single;  // C≡C 1.20 Å from 1.52
    if (order >= 1.9)
        return 0.875 * single; // C=C 1.33 Å
    if (order >= 1.4)
        return 0.915 * single; // aromatic C–C 1.39 Å
    return single;             // C–C 1.52 Å, C–H 1.07 Å
}

/// Equilibrium angle at a centre with `sigma` σ-neighbours (hydrogens
/// included) and total bond order `orderSum`.
double targetAngle(int sigma, int orderSum, int smallestRing)
{
    if (smallestRing >= 3 && smallestRing <= 6) {
        // Inside a small ring the internal angle is set by the ring, not by
        // the hybridization — cyclopropane's 60° is the extreme case, and a
        // 109.5° restraint there fights the ring closure into a distorted mess.
        return 180.0 - 360.0 / smallestRing;
    }
    if (sigma <= 1)
        return 180.0;
    if (sigma == 2)
        return orderSum >= 4 ? 180.0 : (orderSum == 3 ? 120.0 : 109.47);
    if (sigma == 3)
        return orderSum >= 4 ? 120.0 : 109.47; // sp2 vs a pyramidal amine
    return 109.47;
}

/// One bond of the assembled 3D system: the sketch's bonds plus the ones the
/// added hydrogens bring with them. `order` is fractional so a perceived
/// aromatic bond can be 1.5.
struct Link {
    int i = 0;
    int j = 0;
    double order = 1.0;
};

/// Order of the link joining `a` and `b`, or 1.0 when they are not bonded.
/// Linear scan: the lists are short and this runs once, while the restraint
/// tables are built, never inside the relaxation loop.
double linkOrder(const std::vector<Link>& links, int a, int b)
{
    for (const Link& link : links) {
        if ((link.i == a && link.j == b) || (link.i == b && link.j == a))
            return link.order;
    }
    return 1.0;
}

/// Stiffness weights. Relative, not physical: what matters is that bonds are
/// stiffer than angles, which are stiffer than the planarity restraint, which
/// is stiffer than the non-bonded term.
constexpr double kBondWeight = 1.0;
constexpr double kAngleWeight = 0.35;
constexpr double kPlanarWeight = 0.55;
constexpr double kTorsionWeight = 0.25;
constexpr double kNonBondedWeight = 0.10;

} // namespace

EmbedResult embed(const MoleculeGraph& graph, Structure& out,
                  const EmbedOptions& options)
{
    return embed(graph, {}, out, options);
}

EmbedResult embed(const MoleculeGraph& graph,
                  const std::vector<int>& atomIndices, Structure& out,
                  const EmbedOptions& options)
{
    EmbedResult result;

    // 1. Reduce to the requested selection (empty = the whole canvas).
    MoleculeGraph work = graph;
    if (!atomIndices.empty())
        work = graph.subgraph(atomIndices);
    if (work.atomCount() == 0) {
        result.error = "there is nothing to send — draw a molecule first";
        return result;
    }

    const std::vector<bool> aromatic = work.perceiveAromaticBonds();
    const std::vector<std::vector<int>> ringList = work.rings();
    std::vector<int> smallestRing(static_cast<std::size_t>(work.atomCount()), 0);
    for (const std::vector<int>& ring : ringList) {
        for (int atom : ring) {
            const int size = static_cast<int>(ring.size());
            int& current = smallestRing[static_cast<std::size_t>(atom)];
            if (current == 0 || size < current)
                current = size;
        }
    }

    // 2. Seed 3D coordinates from the drawing.
    //
    // The 2D layout gives the topology a consistent plane to start from, which
    // is exactly what a distance-geometry embedding would have to work to
    // recover. Sketch units become Å at the standard single C–C length, so a
    // ring is already close to the right size before the first relaxation
    // step.
    const double scale =
        targetLength(6, 6, 1.0) / MoleculeGraph::kBondLength;

    struct Node {
        int z = 6;
        Vec3 position;
        int source = -1; ///< index in `work`, or -1 for an added hydrogen
    };
    std::vector<Node> nodes;
    nodes.reserve(static_cast<std::size_t>(work.atomCount()));
    for (int i = 0; i < work.atomCount(); ++i) {
        const MolAtom& atom = work.atoms()[static_cast<std::size_t>(i)];
        Node node;
        node.z = atom.atomicNumber;
        node.position = {atom.x * scale, atom.y * scale, 0.0};
        node.source = i;
        nodes.push_back(node);
    }
    result.heavyAtoms = work.atomCount();

    // Bonds over `nodes`. Hydrogens are appended below and extend this list.
    std::vector<Link> links;
    for (int b = 0; b < work.bondCount(); ++b) {
        const MolBond& bond = work.bonds()[static_cast<std::size_t>(b)];
        Link link;
        link.i = bond.a;
        link.j = bond.b;
        link.order = aromatic[static_cast<std::size_t>(b)]
            ? 1.5
            : static_cast<double>(bond.order);
        links.push_back(link);
    }

    // 3. Add the implicit hydrogens as real atoms.
    //
    // Placed OUT OF THE DRAWING PLANE where the centre is saturated, in it
    // where the centre is sp2. Two hydrogens on the same sp3 carbon that both
    // start in the plane sit on top of each other, and a purely repulsive
    // relaxation has no gradient to separate them — the pair stays fused and
    // the exported structure has two atoms at one point.
    if (options.addHydrogens) {
        const int heavyCount = static_cast<int>(nodes.size());
        for (int i = 0; i < heavyCount; ++i) {
            const int count = work.implicitHydrogens(i);
            if (count <= 0)
                continue;
            const Vec3 centre = nodes[static_cast<std::size_t>(i)].position;

            // Sum of the directions to the existing neighbours: the new
            // hydrogens go opposite it.
            Vec3 occupied{};
            int neighborCount = 0;
            for (int neighbor : work.neighbors(i)) {
                const Vec3 delta =
                    nodes[static_cast<std::size_t>(neighbor)].position - centre;
                const double length = delta.norm();
                if (length > 1e-9) {
                    occupied += delta / length;
                    ++neighborCount;
                }
            }
            Vec3 away = occupied * -1.0;
            if (away.norm() < 1e-6) {
                // A lone atom, or a linear centre: any direction will do, and
                // the drawing plane's x axis is the least surprising one.
                away = {1.0, 0.0, 0.0};
            }
            away = away.normalized();
            // An axis perpendicular to both the drawing plane and `away`, to
            // spread several hydrogens around.
            const Vec3 planeNormal{0.0, 0.0, 1.0};
            Vec3 side = away.cross(planeNormal);
            if (side.norm() < 1e-6)
                side = away.cross(Vec3{0.0, 1.0, 0.0});
            side = side.normalized();
            const Vec3 up = away.cross(side).normalized();

            const double length = targetLength(
                nodes[static_cast<std::size_t>(i)].z, 1, 1.0);
            const bool planar = targetAngle(neighborCount + count,
                                            work.bondOrderSum(i),
                                            smallestRing[static_cast<std::size_t>(i)])
                > 115.0;
            for (int h = 0; h < count; ++h) {
                Vec3 direction = away;
                if (count > 1) {
                    // Fan the hydrogens about `away`, tilting out of the plane
                    // unless the centre is sp2.
                    const double spread = planar ? 0.0 : 0.95;
                    const double phi = 2.0 * kPi * h / count;
                    const double inPlane = planar ? 0.6 : 0.55;
                    direction = away
                        + side * (inPlane * std::cos(phi))
                        + up * (spread * std::sin(phi));
                    if (planar && count == 2) {
                        // Both in the plane, symmetric about `away` — a
                        // terminal CH2 or an NH2 drawn flat.
                        direction = away + side * (h == 0 ? 0.75 : -0.75);
                    }
                    direction = direction.normalized();
                } else if (!planar) {
                    // A single hydrogen on an sp3 centre: lift it out of the
                    // plane so the whole molecule is not born flat.
                    direction = (away + up * 0.35).normalized();
                }
                Node hydrogen;
                hydrogen.z = 1;
                hydrogen.position = centre + direction * length;
                nodes.push_back(hydrogen);
                Link link;
                link.i = i;
                link.j = static_cast<int>(nodes.size()) - 1;
                link.order = 1.0;
                links.push_back(link);
                ++result.hydrogensAdded;
            }
        }
    }

    const int count = static_cast<int>(nodes.size());

    // 4. Build the restraint lists once, outside the relaxation loop.
    std::vector<std::vector<int>> adjacency(static_cast<std::size_t>(count));
    std::vector<double> orderSum(static_cast<std::size_t>(count), 0.0);
    for (const Link& link : links) {
        adjacency[static_cast<std::size_t>(link.i)].push_back(link.j);
        adjacency[static_cast<std::size_t>(link.j)].push_back(link.i);
        orderSum[static_cast<std::size_t>(link.i)] += link.order;
        orderSum[static_cast<std::size_t>(link.j)] += link.order;
    }

    struct AngleRestraint {
        int i = 0;
        int centre = 0;
        int j = 0;
        double distance = 0.0; ///< the 1-3 separation the angle implies
    };
    std::vector<AngleRestraint> angles;
    for (int centre = 0; centre < count; ++centre) {
        const std::vector<int>& neighborList =
            adjacency[static_cast<std::size_t>(centre)];
        if (neighborList.size() < 2)
            continue;
        const int ring = nodes[static_cast<std::size_t>(centre)].source >= 0
            ? smallestRing[static_cast<std::size_t>(
                  nodes[static_cast<std::size_t>(centre)].source)]
            : 0;
        const double theta = targetAngle(
            static_cast<int>(neighborList.size()),
            static_cast<int>(std::lround(orderSum[static_cast<std::size_t>(centre)])),
            ring);
        for (std::size_t a = 0; a < neighborList.size(); ++a) {
            for (std::size_t b = a + 1; b < neighborList.size(); ++b) {
                const int p = neighborList[a];
                const int q = neighborList[b];
                // 1-3 distance from the law of cosines, using each arm's own
                // equilibrium length. Restraining the DISTANCE rather than the
                // angle keeps the gradient finite at 180°, where an angular
                // one is singular.
                const double ra = targetLength(
                    nodes[static_cast<std::size_t>(p)].z,
                    nodes[static_cast<std::size_t>(centre)].z,
                    linkOrder(links, p, centre));
                const double rb = targetLength(
                    nodes[static_cast<std::size_t>(q)].z,
                    nodes[static_cast<std::size_t>(centre)].z,
                    linkOrder(links, q, centre));
                const double cosine = std::cos(theta * kPi / 180.0);
                AngleRestraint restraint;
                restraint.i = p;
                restraint.centre = centre;
                restraint.j = q;
                restraint.distance =
                    std::sqrt(std::max(1e-6, ra * ra + rb * rb - 2.0 * ra * rb * cosine));
                angles.push_back(restraint);
            }
        }
    }

    // sp2 centres: exactly three σ-neighbours and a π bond. The improper
    // "distance from the plane of the other three" is driven to zero.
    std::vector<int> planarCentres;
    for (int centre = 0; centre < count; ++centre) {
        if (adjacency[static_cast<std::size_t>(centre)].size() != 3)
            continue;
        if (orderSum[static_cast<std::size_t>(centre)] >= 3.9)
            planarCentres.push_back(centre);
    }

    // Double bonds keep their substituents coplanar; this is what stops a ring
    // from folding along a π bond.
    struct TorsionRestraint {
        int a = 0;
        int b = 0;
        int c = 0;
        int d = 0;
    };
    std::vector<TorsionRestraint> torsions;
    for (const Link& link : links) {
        if (link.order < 1.4)
            continue;
        for (int a : adjacency[static_cast<std::size_t>(link.i)]) {
            if (a == link.j)
                continue;
            for (int d : adjacency[static_cast<std::size_t>(link.j)]) {
                if (d == link.i)
                    continue;
                torsions.push_back({a, link.i, link.j, d});
            }
        }
    }

    // Non-bonded pairs: everything more than two bonds apart.
    std::set<std::pair<int, int>> excluded;
    const auto exclude = [&excluded](int a, int b) {
        excluded.insert({std::min(a, b), std::max(a, b)});
    };
    for (const Link& link : links)
        exclude(link.i, link.j);
    for (const AngleRestraint& restraint : angles)
        exclude(restraint.i, restraint.j);

    // 5. Damped steepest descent.
    std::vector<Vec3> force(static_cast<std::size_t>(count));
    double stepSize = 0.05;
    for (int iteration = 0; iteration < options.steps; ++iteration) {
        std::fill(force.begin(), force.end(), Vec3{});

        for (const Link& link : links) {
            const Vec3 delta = nodes[static_cast<std::size_t>(link.j)].position
                - nodes[static_cast<std::size_t>(link.i)].position;
            const double distance = std::max(1e-9, delta.norm());
            const double target = targetLength(
                nodes[static_cast<std::size_t>(link.i)].z,
                nodes[static_cast<std::size_t>(link.j)].z, link.order);
            const Vec3 push = delta * (kBondWeight * (distance - target) / distance);
            force[static_cast<std::size_t>(link.i)] += push;
            force[static_cast<std::size_t>(link.j)] += push * -1.0;
        }

        for (const AngleRestraint& restraint : angles) {
            const Vec3 delta = nodes[static_cast<std::size_t>(restraint.j)].position
                - nodes[static_cast<std::size_t>(restraint.i)].position;
            const double distance = std::max(1e-9, delta.norm());
            const Vec3 push =
                delta * (kAngleWeight * (distance - restraint.distance) / distance);
            force[static_cast<std::size_t>(restraint.i)] += push;
            force[static_cast<std::size_t>(restraint.j)] += push * -1.0;
        }

        for (int centre : planarCentres) {
            const std::vector<int>& neighborList =
                adjacency[static_cast<std::size_t>(centre)];
            const Vec3 a = nodes[static_cast<std::size_t>(neighborList[0])].position;
            const Vec3 b = nodes[static_cast<std::size_t>(neighborList[1])].position;
            const Vec3 c = nodes[static_cast<std::size_t>(neighborList[2])].position;
            Vec3 normal = (b - a).cross(c - a);
            const double area = normal.norm();
            if (area < 1e-9)
                continue;
            normal = normal / area;
            const Vec3 centrePosition = nodes[static_cast<std::size_t>(centre)].position;
            const double height = (centrePosition - a).dot(normal);
            // MINUS the height: the restraint pushes the centre BACK toward
            // the plane of its substituents. With the sign the other way it
            // pushes the centre further out, which is a positive feedback the
            // step cap turns into a permanently distorted geometry rather than
            // a blow-up — acetic acid came out with a 1.79 A C–C bond and a
            // relaxation that plateaued at a residual of 2e-2 instead of
            // converging. Benzene hid it: a drawing starts exactly flat, so
            // height was 0 and the wrong sign only made that an UNSTABLE
            // equilibrium instead of a stable one.
            const Vec3 correction = normal * (-kPlanarWeight * height);
            force[static_cast<std::size_t>(centre)] += correction;
            // The three substituents take the recoil, a third each, so the
            // restraint does not translate the whole group.
            const Vec3 share = correction * (-1.0 / 3.0);
            for (int neighbor : neighborList)
                force[static_cast<std::size_t>(neighbor)] += share;
        }

        for (const TorsionRestraint& torsion : torsions) {
            // Coplanarity of a-b=c-d, as "d lies in the plane of a, b, c".
            const Vec3 a = nodes[static_cast<std::size_t>(torsion.a)].position;
            const Vec3 b = nodes[static_cast<std::size_t>(torsion.b)].position;
            const Vec3 c = nodes[static_cast<std::size_t>(torsion.c)].position;
            const Vec3 d = nodes[static_cast<std::size_t>(torsion.d)].position;
            Vec3 normal = (b - a).cross(c - a);
            const double area = normal.norm();
            if (area < 1e-9)
                continue;
            normal = normal / area;
            const double height = (d - a).dot(normal);
            // Same sign convention as the planarity restraint above: pull `d`
            // back INTO the a-b=c plane.
            const Vec3 correction = normal * (-kTorsionWeight * height);
            force[static_cast<std::size_t>(torsion.d)] += correction;
            force[static_cast<std::size_t>(torsion.a)] += correction * (-1.0 / 3.0);
            force[static_cast<std::size_t>(torsion.b)] += correction * (-1.0 / 3.0);
            force[static_cast<std::size_t>(torsion.c)] += correction * (-1.0 / 3.0);
        }

        for (int i = 0; i < count; ++i) {
            for (int j = i + 1; j < count; ++j) {
                if (excluded.count({i, j}))
                    continue;
                const Vec3 delta = nodes[static_cast<std::size_t>(j)].position
                    - nodes[static_cast<std::size_t>(i)].position;
                double distance = delta.norm();
                const double contact = 0.85
                    * (static_cast<double>(Elements::data(nodes[static_cast<std::size_t>(i)].z).covalentRadius)
                       + static_cast<double>(Elements::data(nodes[static_cast<std::size_t>(j)].z).covalentRadius))
                    + 1.2;
                if (distance > contact)
                    continue;
                if (distance < 1e-6)
                    distance = 1e-6;
                // Purely repulsive: a non-bonded term that also PULLED would
                // be a crude dispersion model, and a crude dispersion model
                // collapses an unsolvated molecule onto itself.
                const Vec3 push =
                    delta * (-kNonBondedWeight * (contact - distance) / distance);
                force[static_cast<std::size_t>(i)] += push;
                force[static_cast<std::size_t>(j)] += push * -1.0;
            }
        }

        double largest = 0.0;
        for (int i = 0; i < count; ++i) {
            const double magnitude = force[static_cast<std::size_t>(i)].norm();
            largest = std::max(largest, magnitude);
        }
        result.residual = largest;
        if (largest < 1e-7)
            break;

        // A cap rather than a line search: the restraints are stiff and a
        // first step from a flat drawing can be large enough to turn the
        // molecule inside out.
        const double cap = 0.15;
        for (int i = 0; i < count; ++i) {
            Vec3 step = force[static_cast<std::size_t>(i)] * stepSize;
            const double magnitude = step.norm();
            if (magnitude > cap)
                step = step * (cap / magnitude);
            nodes[static_cast<std::size_t>(i)].position += step;
        }
        stepSize = std::min(0.2, stepSize * 1.02);
    }

    // 6. Hand back a Structure, centred on the origin.
    Structure structure;
    Vec3 centroid{};
    for (const Node& node : nodes)
        centroid += node.position;
    centroid = centroid / static_cast<double>(count);
    for (const Node& node : nodes) {
        Atom atom;
        atom.atomicNumber = node.z;
        atom.position = node.position - centroid;
        structure.addAtom(atom);
    }

    if (options.vacuum > 0.0) {
        double maxX = 0.0;
        double maxY = 0.0;
        double maxZ = 0.0;
        for (const Atom& atom : structure.atoms()) {
            maxX = std::max(maxX, std::fabs(atom.position.x));
            maxY = std::max(maxY, std::fabs(atom.position.y));
            maxZ = std::max(maxZ, std::fabs(atom.position.z));
        }
        UnitCell cell;
        cell.setVectors({Vec3{2.0 * maxX + options.vacuum, 0.0, 0.0},
                         Vec3{0.0, 2.0 * maxY + options.vacuum, 0.0},
                         Vec3{0.0, 0.0, 2.0 * maxZ + options.vacuum}});
        structure.setCell(cell);
    }

    out = std::move(structure);
    result.ok = true;
    return result;
}

} // namespace calango::core
