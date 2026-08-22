#include "core/GrapheneOxideGroupAnalysis.hpp"

#include "core/AngleGeometry.hpp"

#include <algorithm>

namespace calango::core {

namespace {

using Group = GrapheneOxideBuilder::Group;
using GroupCluster = GrapheneOxideBuilder::GroupCluster;

constexpr int kZ_H = 1;
constexpr int kZ_C = 6;
constexpr int kZ_O = 8;

bool contains(const std::vector<int>& list, int value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

} // namespace

double GrapheneOxideGroupAnalysis::surfaceConcentration(
    GrapheneOxideBuilder::Group g) const
{
    if (frameworkCarbons == 0)
        return 0.0;
    return static_cast<double>(groups[static_cast<std::size_t>(g)].instances)
        / frameworkCarbons;
}

double GrapheneOxideGroupAnalysis::pristineFraction() const
{
    if (frameworkCarbons == 0)
        return 0.0;
    return static_cast<double>(pristineCarbons) / frameworkCarbons;
}

GrapheneOxideGroupAnalysis analyzeGrapheneOxideGroups(const Structure& structure,
                                                       double armCutoff)
{
    GrapheneOxideGroupAnalysis result;
    const std::size_t n = structure.size();
    if (n == 0)
        return result;
    const auto& atoms = structure.atoms();
    const auto z = [&](int atom) {
        return atoms[static_cast<std::size_t>(atom)].atomicNumber;
    };

    // --- Bonding, and which carbons are the honeycomb framework -----------
    // The same test the MDMC script and the builder's own edge/basal split
    // both rest on: a carbon with two or more carbon neighbours is part of
    // the sheet; anything else (a carboxyl's own added carbon, chiefly) is
    // not, whatever chemistry it is part of.
    std::vector<std::vector<int>> neighbours(n);
    for (const Bond& bond : structure.detectBonds()) {
        neighbours[static_cast<std::size_t>(bond.i)].push_back(bond.j);
        neighbours[static_cast<std::size_t>(bond.j)].push_back(bond.i);
    }
    std::vector<char> isFramework(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        if (z(static_cast<int>(i)) != kZ_C)
            continue;
        int carbonNeighbours = 0;
        for (int j : neighbours[i])
            if (z(j) == kZ_C)
                ++carbonNeighbours;
        if (carbonNeighbours >= 2)
            isFramework[i] = 1;
    }
    for (std::size_t i = 0; i < n; ++i)
        if (isFramework[i])
            ++result.frameworkCarbons;

    // --- Classification: findFunctionalGroups(), the one implementation ---
    const std::vector<GroupCluster> clusters =
        GrapheneOxideBuilder::findFunctionalGroups(structure);
    std::vector<int> labelOf(n, -1); // Group kind per atom, -1 = none
    for (const GroupCluster& cluster : clusters) {
        result.groups[static_cast<std::size_t>(cluster.kind)].instances++;
        for (int atom : cluster.atoms)
            labelOf[static_cast<std::size_t>(atom)] =
                static_cast<int>(cluster.kind);
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!isFramework[i])
            continue;
        if (labelOf[i] >= 0)
            result.groups[static_cast<std::size_t>(labelOf[i])]
                .surfaceCarbons++;
        else
            ++result.pristineCarbons;
    }

    // --- Antiposition pairs, the same finder classifyFromBonding() uses ---
    result.antipositionPairs = static_cast<int>(
        GrapheneOxideBuilder::findAntipositionPairs(structure, clusters).size());

    // --- Above/below plane, basal groups only ------------------------------
    // The sheet is assumed planar in xy (z out-of-plane) -- true of every
    // structure this application's builder or GO-MDMC produces; see this
    // struct's own doc comment.
    for (const GroupCluster& cluster : clusters) {
        if (cluster.kind != Group::Epoxide && cluster.kind != Group::Hydroxyl)
            continue;
        int oxygenAtom = -1;
        double hostZ = 0.0;
        int hostCount = 0;
        for (int atom : cluster.atoms) {
            if (z(atom) == kZ_O)
                oxygenAtom = atom;
            else if (z(atom) == kZ_C) {
                hostZ += atoms[static_cast<std::size_t>(atom)].position.z;
                ++hostCount;
            }
        }
        if (oxygenAtom < 0 || hostCount == 0)
            continue;
        hostZ /= hostCount;
        const double oxygenZ =
            atoms[static_cast<std::size_t>(oxygenAtom)].position.z;
        (oxygenZ >= hostZ ? result.abovePlane : result.belowPlane)++;
    }

    // --- C-C bond lengths, resolved by environment -------------------------
    std::vector<double> pristineCC;
    std::vector<double> functionalizedCC;
    for (const Bond& bond : structure.detectBonds()) {
        if (z(bond.i) != kZ_C || z(bond.j) != kZ_C)
            continue;
        if (!isFramework[static_cast<std::size_t>(bond.i)]
            || !isFramework[static_cast<std::size_t>(bond.j)])
            continue;
        const Vec3 dj = atoms[static_cast<std::size_t>(bond.j)].position
            + bond.imageOffset - atoms[static_cast<std::size_t>(bond.i)].position;
        const double length = dj.norm();
        const bool eitherFunctionalized =
            labelOf[static_cast<std::size_t>(bond.i)] >= 0
            || labelOf[static_cast<std::size_t>(bond.j)] >= 0;
        (eitherFunctionalized ? functionalizedCC : pristineCC)
            .push_back(length);
    }
    if (!pristineCC.empty())
        result.ccBondLengths.push_back(
            {"C-C (pristine)", std::move(pristineCC)});
    if (!functionalizedCC.empty())
        result.ccBondLengths.push_back(
            {"C-C (functionalized-adjacent)", std::move(functionalizedCC)});

    // --- C-C-C angles, resolved by whether the CENTER is functionalized ---
    std::vector<double> pristineCCC;
    std::vector<double> functionalizedCCC;
    for (std::size_t i = 0; i < n; ++i) {
        if (!isFramework[i])
            continue;
        std::vector<int> frameworkNeighbours;
        for (int j : neighbours[i])
            if (isFramework[static_cast<std::size_t>(j)])
                frameworkNeighbours.push_back(j);
        if (frameworkNeighbours.size() < 2)
            continue;
        for (std::size_t p = 0; p < frameworkNeighbours.size(); ++p) {
            for (std::size_t q = p + 1; q < frameworkNeighbours.size(); ++q) {
                const double angle =
                    angleBetween(structure, static_cast<int>(i),
                                frameworkNeighbours[p], frameworkNeighbours[q],
                                armCutoff);
                if (angle < 0.0)
                    continue;
                (labelOf[i] >= 0 ? functionalizedCCC : pristineCCC)
                    .push_back(angle);
            }
        }
    }
    if (!pristineCCC.empty())
        result.cccAngles.push_back({"C-C-C (pristine center)", std::move(pristineCCC)});
    if (!functionalizedCCC.empty())
        result.cccAngles.push_back(
            {"C-C-C (functionalized center)", std::move(functionalizedCCC)});

    // --- C-O-C angle, epoxide only ------------------------------------------
    std::vector<double> epoxideAngles;
    for (const GroupCluster& cluster : clusters) {
        if (cluster.kind != Group::Epoxide)
            continue;
        int oxygenAtom = -1;
        std::vector<int> hosts;
        for (int atom : cluster.atoms) {
            if (z(atom) == kZ_O)
                oxygenAtom = atom;
            else if (z(atom) == kZ_C)
                hosts.push_back(atom);
        }
        if (oxygenAtom < 0 || hosts.size() != 2)
            continue;
        const double angle =
            angleBetween(structure, oxygenAtom, hosts[0], hosts[1], armCutoff);
        if (angle >= 0.0)
            epoxideAngles.push_back(angle);
    }
    if (!epoxideAngles.empty())
        result.cocAngles.push_back({"C-O-C (epoxide)", std::move(epoxideAngles)});

    // --- C-O-H angle, every group that carries an explicit hydroxyl -------
    // Generic over the cluster: find an oxygen in the cluster bonded to a
    // hydrogen also in the cluster (the acidic O for a carboxyl, the only O
    // for a hydroxyl), then that oxygen's other heavy-atom neighbour
    // completes the angle. Carbonyl has no such oxygen at all -- it is
    // never a candidate here, and is reported via skippedForNoHydrogen
    // below instead of being silently absent.
    std::vector<double> hydroxylCoh;
    std::vector<double> carboxylCoh;
    bool anyCarbonyl = false;
    for (const GroupCluster& cluster : clusters) {
        if (cluster.kind == Group::Carbonyl) {
            anyCarbonyl = true;
            continue;
        }
        if (cluster.kind != Group::Hydroxyl && cluster.kind != Group::Carboxyl)
            continue;
        int oxygenAtom = -1;
        int hydrogenAtom = -1;
        for (int atom : cluster.atoms) {
            if (z(atom) != kZ_O)
                continue;
            for (int nb : neighbours[static_cast<std::size_t>(atom)]) {
                if (z(nb) == kZ_H && contains(cluster.atoms, nb)) {
                    oxygenAtom = atom;
                    hydrogenAtom = nb;
                    break;
                }
            }
            if (oxygenAtom >= 0)
                break;
        }
        if (oxygenAtom < 0 || hydrogenAtom < 0)
            continue; // no O-H in this cluster; nothing to measure
        int carbonArm = -1;
        for (int nb : neighbours[static_cast<std::size_t>(oxygenAtom)]) {
            if (z(nb) == kZ_C) {
                carbonArm = nb;
                break;
            }
        }
        if (carbonArm < 0)
            continue;
        const double angle = angleBetween(structure, oxygenAtom, carbonArm,
                                          hydrogenAtom, armCutoff);
        if (angle < 0.0)
            continue;
        (cluster.kind == Group::Hydroxyl ? hydroxylCoh : carboxylCoh)
            .push_back(angle);
    }
    if (!hydroxylCoh.empty())
        result.cohAngles.push_back({"C-O-H (hydroxyl)", std::move(hydroxylCoh)});
    if (!carboxylCoh.empty())
        result.cohAngles.push_back({"C-O-H (carboxyl)", std::move(carboxylCoh)});
    if (anyCarbonyl)
        result.skippedForNoHydrogen.emplace_back(
            "carbonyl: the group's oxygen is a double-bonded =O with no "
            "hydrogen, so it has no C-O-H angle to measure");

    return result;
}

} // namespace calango::core
