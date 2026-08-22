#include "core/GrapheneOxidePairCorrelation.hpp"

#include "core/AngleGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace calango::core {

namespace {

using Group = GrapheneOxideBuilder::Group;
using GroupCluster = GrapheneOxideBuilder::GroupCluster;

/// Internal-only fake atomic numbers standing in for functionalization
/// state — chosen well above any real element (Z <= 118) so they can never
/// collide with an actual atom, and never emitted, exported or shown to the
/// user (see speciesNames in the header).
constexpr int kFakeZPristine = 900;
int fakeZFor(Group group)
{
    return 901 + static_cast<int>(group);
}

std::string capitalized(const char* name)
{
    std::string s(name);
    if (!s.empty())
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

} // namespace

GrapheneOxidePairCorrelationResult analyzeGrapheneOxidePairCorrelation(
    const Structure& structure, const std::vector<double>& shellCutoffs)
{
    GrapheneOxidePairCorrelationResult result;
    if (shellCutoffs.empty() || structure.empty())
        return result;

    const std::size_t n = structure.size();
    const auto& atoms = structure.atoms();

    // Framework membership and classification — the SAME two tests
    // GrapheneOxideGroupAnalysis uses (>= 2 carbon-carbon bonds; bonding-
    // based findFunctionalGroups()), so a carbon's "species" here can never
    // disagree with its census/geometry classification elsewhere.
    std::vector<std::vector<int>> neighbours(n);
    for (const Bond& bond : structure.detectBonds()) {
        neighbours[static_cast<std::size_t>(bond.i)].push_back(bond.j);
        neighbours[static_cast<std::size_t>(bond.j)].push_back(bond.i);
    }
    std::vector<char> isFramework(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        if (atoms[i].atomicNumber != 6)
            continue;
        int carbonNeighbours = 0;
        for (int j : neighbours[i])
            if (atoms[static_cast<std::size_t>(j)].atomicNumber == 6)
                ++carbonNeighbours;
        if (carbonNeighbours >= 2)
            isFramework[i] = 1;
    }

    std::vector<int> labelOf(n, -1);
    for (const GroupCluster& cluster : GrapheneOxideBuilder::findFunctionalGroups(structure))
        for (int atom : cluster.atoms)
            labelOf[static_cast<std::size_t>(atom)] = static_cast<int>(cluster.kind);

    // A carbon-only structure, one atom per FRAMEWORK carbon, same position
    // and cell, species encoded as a fake atomic number —
    // computeWarrenCowley() groups by atomicNumber and needs nothing else
    // changed to treat "functionalization state" as its species axis.
    Structure speciesStructure;
    speciesStructure.setCell(structure.cell());
    for (std::size_t i = 0; i < n; ++i) {
        if (!isFramework[i])
            continue;
        Atom atom;
        atom.atomicNumber =
            labelOf[i] < 0 ? kFakeZPristine : fakeZFor(static_cast<Group>(labelOf[i]));
        atom.position = atoms[i].position;
        speciesStructure.addAtom(atom);
    }

    WarrenCowleyOptions options;
    options.shellCutoffs = shellCutoffs;
    result.wc = computeWarrenCowley(speciesStructure, options);

    result.speciesNames.reserve(result.wc.species.size());
    for (const int z : result.wc.species) {
        if (z == kFakeZPristine)
            result.speciesNames.emplace_back("Pristine");
        else
            result.speciesNames.push_back(
                capitalized(GrapheneOxideBuilder::name(static_cast<Group>(z - 901))));
    }
    return result;
}

std::vector<double> honeycombShellCutoffs(int shellCount)
{
    std::vector<double> cutoffs;
    if (shellCount <= 0)
        return cutoffs;

    // A generous, purely empirical search radius: shell k's true radius
    // grows roughly like sqrt(k) times the C-C bond length (~1.42 A), so
    // this comfortably covers `shellCount` shells with margin to spare —
    // verified, not assumed, by the shell-enumeration test.
    const double searchRadius = 1.42 * std::sqrt(2.0 * shellCount + 6.0) * 1.6;
    const int n =
        std::max(10, static_cast<int>(std::ceil(searchRadius / 2.46)) * 2 + 8);

    GrapheneOxideBuilder::Config config;
    config.lattice = GrapheneOxideBuilder::Lattice::Primitive;
    config.supercell[0] = config.supercell[1] = n;
    const Structure sheet = GrapheneOxideBuilder::pristine(config);
    const auto& atoms = sheet.atoms();
    if (atoms.empty())
        return cutoffs;

    // Any atom does as the "center" — minimumImageVector() below is PBC-
    // aware, so it is not necessary (and was a real bug in an earlier
    // version of this function) for the chosen atom to sit away from the
    // tile's own periodic seam: a raw, non-wrapped distance from a center
    // atom near one edge of the tile would silently miss the true nearest
    // periodic images on the OTHER side, undercounting exactly the shells
    // this function exists to measure.
    const std::size_t centerIndex = atoms.size() / 2;

    std::vector<double> distances;
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        if (i == centerIndex)
            continue;
        const double d = minimumImageVector(sheet, static_cast<int>(centerIndex),
                                            static_cast<int>(i), searchRadius)
                             .norm();
        if (d < searchRadius)
            distances.push_back(d);
    }
    std::sort(distances.begin(), distances.end());

    // Cluster consecutive distances into shells: a real honeycomb shell's
    // members are exactly degenerate (to floating-point precision), so a
    // tight tolerance is enough to separate genuinely distinct shells
    // without splitting one shell into two by rounding noise.
    std::vector<double> shellRadii;
    for (const double d : distances) {
        if (shellRadii.empty() || d - shellRadii.back() > 0.02)
            shellRadii.push_back(d);
    }
    if (shellRadii.size() > static_cast<std::size_t>(shellCount))
        shellRadii.resize(static_cast<std::size_t>(shellCount));

    cutoffs.reserve(shellRadii.size());
    for (std::size_t k = 0; k < shellRadii.size(); ++k) {
        const double next = (k + 1 < shellRadii.size())
            ? shellRadii[k + 1]
            : shellRadii[k] * 1.15; // headroom for the outermost requested shell
        cutoffs.push_back(0.5 * (shellRadii[k] + next));
    }
    return cutoffs;
}

} // namespace calango::core
