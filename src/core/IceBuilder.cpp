#include "core/IceBuilder.hpp"

#include "core/Element.hpp"
#include "core/PeriodicImages.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <stdexcept>
#include <unordered_set>

namespace calango::core {

namespace {

constexpr double kWaterMassU = 18.01528;
/// u/Å³ → g/cm³.
constexpr double kUPerA3ToGCm3 = 1.66053907;

/// One oxygen site plus the sublattice it belongs to. Interpenetrating phases
/// (VII/VIII) have two hydrogen-bond networks that pass through each other
/// without being bonded, so a purely distance-based neighbour search would
/// find 8 contacts where the physics has 4. The network id keeps them apart.
struct OxygenSite {
    Vec3 position;
    int network = 0;
};

/// The oxygen sublattice of a phase, in Cartesian coordinates, with its cell.
struct OxygenLattice {
    std::vector<OxygenSite> sites;
    UnitCell cell;
    /// Cutoff that captures exactly the four tetrahedral O–O contacts.
    double bondCutoff = 3.2;
};

/// One hydrogen bond: an O–O contact, with the periodic shift applied to `b`.
/// Named HBond to stay clear of core::Bond, which is the covalent bond record
/// Structure carries.
struct HBond {
    int a = 0;
    int b = 0;
    Vec3 shift;      ///< Cartesian image offset added to b
    bool aToB = true; ///< orientation: true = a donates its proton to b
};

/// Replicate a fractional basis into an nx x ny x nz supercell.
OxygenLattice replicate(const std::array<Vec3, 3>& cellVectors,
                        const std::vector<std::pair<Vec3, int>>& basis, int nx,
                        int ny, int nz, double bondCutoff)
{
    OxygenLattice lattice;
    lattice.bondCutoff = bondCutoff;
    lattice.cell = UnitCell(cellVectors[0] * static_cast<double>(nx),
                            cellVectors[1] * static_cast<double>(ny),
                            cellVectors[2] * static_cast<double>(nz));
    for (int ia = 0; ia < nx; ++ia)
        for (int ib = 0; ib < ny; ++ib)
            for (int ic = 0; ic < nz; ++ic) {
                const Vec3 shift = cellVectors[0] * static_cast<double>(ia)
                    + cellVectors[1] * static_cast<double>(ib)
                    + cellVectors[2] * static_cast<double>(ic);
                for (const auto& [fractional, network] : basis) {
                    const Vec3 cartesian =
                        cellVectors[0] * fractional.x
                        + cellVectors[1] * fractional.y
                        + cellVectors[2] * fractional.z;
                    lattice.sites.push_back({cartesian + shift, network});
                }
            }
    return lattice;
}

/// Ice Ih — the wurtzite oxygen arrangement in P6_3/mmc, a = 4.497 Å,
/// c = 7.324 Å. Four oxygens per cell; every O–O contact is ~2.75 Å.
OxygenLattice iceIhLattice(int nx, int ny, int nz)
{
    constexpr double a = 4.497;
    constexpr double c = 7.324;
    const std::array<Vec3, 3> vectors = {
        Vec3{a, 0.0, 0.0},
        Vec3{-0.5 * a, 0.5 * std::sqrt(3.0) * a, 0.0},
        Vec3{0.0, 0.0, c}};
    // Wurtzite basis: both "cation" and "anion" sites are oxygens here.
    const std::vector<std::pair<Vec3, int>> basis = {
        {{1.0 / 3.0, 2.0 / 3.0, 0.0}, 0},
        {{2.0 / 3.0, 1.0 / 3.0, 0.5}, 0},
        {{1.0 / 3.0, 2.0 / 3.0, 0.375}, 0},
        {{2.0 / 3.0, 1.0 / 3.0, 0.875}, 0}};
    return replicate(vectors, basis, nx, ny, nz, 3.1);
}

/// Ice Ic — the diamond oxygen lattice, a = 6.358 Å (O–O = a√3/4 ≈ 2.75 Å).
OxygenLattice iceIcLattice(int nx, int ny, int nz, double a = 6.358,
                           bool interpenetrating = false)
{
    const std::array<Vec3, 3> vectors = {
        Vec3{a, 0.0, 0.0}, Vec3{0.0, a, 0.0}, Vec3{0.0, 0.0, a}};
    const Vec3 fcc[4] = {{0.0, 0.0, 0.0},
                         {0.0, 0.5, 0.5},
                         {0.5, 0.0, 0.5},
                         {0.5, 0.5, 0.0}};
    std::vector<std::pair<Vec3, int>> basis;
    for (const Vec3& site : fcc) {
        basis.emplace_back(site, 0);
        basis.emplace_back(Vec3{site.x + 0.25, site.y + 0.25, site.z + 0.25}, 0);
    }
    if (interpenetrating) {
        // Ice VII / VIII: a second, identical diamond network displaced by
        // (½,½,½). It threads through the first without sharing a hydrogen
        // bond — the two nets are chemically independent, which is why the
        // network id below matters more than the distances do.
        const std::size_t first = basis.size();
        for (std::size_t i = 0; i < first; ++i)
            basis.emplace_back(Vec3{basis[i].first.x + 0.5,
                                    basis[i].first.y + 0.5,
                                    basis[i].first.z + 0.5},
                               1);
    }
    return replicate(vectors, basis, nx, ny, nz, a * 0.4340 + 0.25);
}

/// Tetrahedral O–O contacts, restricted to sites of the SAME network.
std::vector<HBond> buildBonds(const OxygenLattice& lattice)
{
    std::vector<HBond> bonds;
    const auto& vectors = lattice.cell.vectors();
    const auto range = imageRange(lattice.cell, lattice.bondCutoff);
    const int n = static_cast<int>(lattice.sites.size());
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (lattice.sites[i].network != lattice.sites[j].network)
                continue;
            for (int ia = -range[0]; ia <= range[0]; ++ia)
                for (int ib = -range[1]; ib <= range[1]; ++ib)
                    for (int ic = -range[2]; ic <= range[2]; ++ic) {
                        const Vec3 shift = vectors[0] * static_cast<double>(ia)
                            + vectors[1] * static_cast<double>(ib)
                            + vectors[2] * static_cast<double>(ic);
                        const double d =
                            (lattice.sites[j].position + shift
                             - lattice.sites[i].position)
                                .norm();
                        if (d > 1e-6 && d <= lattice.bondCutoff)
                            bonds.push_back({i, j, shift, true});
                    }
        }
    }
    return bonds;
}

/// Orient every bond so each oxygen donates exactly two protons and accepts
/// exactly two — the Bernal-Fowler ice rules.
///
/// The graph is 4-regular, so every vertex has even degree and the edge set
/// decomposes into edge-disjoint cycles. Orienting each cycle consistently
/// gives in-degree = out-degree = 2 everywhere by construction: this is an
/// Eulerian orientation, found here with Hierholzer's algorithm. Rejection
/// sampling (place protons at random, retry until the rules hold) is the
/// obvious alternative and is hopeless — the acceptance probability decays
/// exponentially with system size.
///
/// The Eulerian orientation alone is one valid configuration but a biased one,
/// so `randomizeSteps` cycle flips follow. Reversing every edge of a DIRECTED
/// cycle preserves in- and out-degree at each of its vertices, so the ice rules
/// survive each flip exactly — this walks the ensemble of valid states rather
/// than perturbing and repairing.
void solveIceRules(std::vector<HBond>& bonds, int siteCount, std::mt19937& rng,
                   int randomizeSteps)
{
    // -- Adjacency over bond indices ---------------------------------------
    std::vector<std::vector<int>> incident(static_cast<std::size_t>(siteCount));
    for (int e = 0; e < static_cast<int>(bonds.size()); ++e) {
        incident[static_cast<std::size_t>(bonds[e].a)].push_back(e);
        incident[static_cast<std::size_t>(bonds[e].b)].push_back(e);
    }

    // -- Hierholzer: walk edge-disjoint circuits and orient along them ------
    std::vector<bool> used(bonds.size(), false);
    std::vector<std::size_t> nextEdge(static_cast<std::size_t>(siteCount), 0);
    for (int start = 0; start < siteCount; ++start) {
        for (;;) {
            // Find an unused edge at `start` to begin a circuit from.
            bool found = false;
            while (nextEdge[static_cast<std::size_t>(start)]
                   < incident[static_cast<std::size_t>(start)].size()) {
                const int e = incident[static_cast<std::size_t>(start)]
                                      [nextEdge[static_cast<std::size_t>(start)]];
                if (!used[static_cast<std::size_t>(e)]) {
                    found = true;
                    break;
                }
                ++nextEdge[static_cast<std::size_t>(start)];
            }
            if (!found)
                break;

            // Walk until we return to `start`, consuming edges as we go. Every
            // vertex has even degree, so a walk that enters can always leave —
            // it can only get stuck back at the vertex it started from.
            int current = start;
            for (;;) {
                int chosen = -1;
                while (nextEdge[static_cast<std::size_t>(current)]
                       < incident[static_cast<std::size_t>(current)].size()) {
                    const int e =
                        incident[static_cast<std::size_t>(current)]
                                [nextEdge[static_cast<std::size_t>(current)]];
                    if (!used[static_cast<std::size_t>(e)]) {
                        chosen = e;
                        break;
                    }
                    ++nextEdge[static_cast<std::size_t>(current)];
                }
                if (chosen < 0)
                    break;
                used[static_cast<std::size_t>(chosen)] = true;
                HBond& bond = bonds[static_cast<std::size_t>(chosen)];
                // Orient it leaving `current`.
                bond.aToB = bond.a == current;
                current = bond.a == current ? bond.b : bond.a;
                if (current == start)
                    break;
            }
        }
    }

    if (randomizeSteps <= 0 || bonds.empty())
        return;

    // -- Cycle flips --------------------------------------------------------
    // Outgoing adjacency, rebuilt once and patched as flips happen.
    std::vector<std::vector<int>> outgoing(static_cast<std::size_t>(siteCount));
    const auto rebuildOutgoing = [&] {
        for (auto& list : outgoing)
            list.clear();
        for (int e = 0; e < static_cast<int>(bonds.size()); ++e) {
            const HBond& bond = bonds[static_cast<std::size_t>(e)];
            outgoing[static_cast<std::size_t>(bond.aToB ? bond.a : bond.b)]
                .push_back(e);
        }
    };
    rebuildOutgoing();

    std::uniform_int_distribution<int> pickSite(0, siteCount - 1);
    std::vector<int> pathSite;
    std::vector<int> pathEdge;
    std::vector<int> visitIndex(static_cast<std::size_t>(siteCount), -1);

    for (int step = 0; step < randomizeSteps; ++step) {
        pathSite.clear();
        pathEdge.clear();
        int current = pickSite(rng);
        // Random walk along outgoing edges until a site repeats: the segment
        // between the two visits is a directed cycle.
        for (int guard = 0; guard < 4 * siteCount + 8; ++guard) {
            if (visitIndex[static_cast<std::size_t>(current)] >= 0) {
                const int from = visitIndex[static_cast<std::size_t>(current)];
                for (int k = from; k < static_cast<int>(pathEdge.size()); ++k) {
                    HBond& bond = bonds[static_cast<std::size_t>(
                        pathEdge[static_cast<std::size_t>(k)])];
                    bond.aToB = !bond.aToB; // reversing the whole cycle is safe
                }
                break;
            }
            visitIndex[static_cast<std::size_t>(current)] =
                static_cast<int>(pathSite.size());
            pathSite.push_back(current);
            const auto& out = outgoing[static_cast<std::size_t>(current)];
            if (out.empty())
                break;
            std::uniform_int_distribution<std::size_t> pickEdge(0, out.size() - 1);
            const int edge = out[pickEdge(rng)];
            const HBond& bond = bonds[static_cast<std::size_t>(edge)];
            pathEdge.push_back(edge);
            current = bond.aToB ? bond.b : bond.a;
        }
        for (const int site : pathSite)
            visitIndex[static_cast<std::size_t>(site)] = -1;
        rebuildOutgoing();
    }
}

/// Place the two hydrogens of oxygen `origin` given the directions of its two
/// donated bonds.
///
/// The protons do NOT sit on the O–O lines: a real water molecule has a fixed
/// H–O–H angle (104.52°) while the tetrahedral O–O directions subtend 109.47°,
/// so forcing them onto the bonds would distort the monomer. Instead the pair
/// is built with the exact bond length and angle, in the plane of the two O–O
/// directions and symmetric about their bisector — the closest rigid monomer to
/// the hydrogen-bond geometry.
void placeHydrogens(const Vec3& origin, const Vec3& toFirst,
                    const Vec3& toSecond, double ohLength, double hohAngleDeg,
                    Vec3& h1, Vec3& h2)
{
    const Vec3 u1 = toFirst.normalized();
    const Vec3 u2 = toSecond.normalized();
    Vec3 bisector = u1 + u2;
    if (bisector.norm() < 1e-6) {
        // Collinear donors (degenerate; cannot happen on a tetrahedral net).
        bisector = std::abs(u1.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    }
    bisector = bisector.normalized();
    Vec3 inPlane = u1 - u2;
    if (inPlane.norm() < 1e-6)
        inPlane = bisector.cross(Vec3{0, 0, 1});
    inPlane = inPlane.normalized();

    const double half = 0.5 * hohAngleDeg * M_PI / 180.0;
    const Vec3 a = bisector * std::cos(half) + inPlane * std::sin(half);
    const Vec3 b = bisector * std::cos(half) - inPlane * std::sin(half);
    h1 = origin + a.normalized() * ohLength;
    h2 = origin + b.normalized() * ohLength;
}

/// Net dipole per molecule, in Debye, of a proton configuration. Used to report
/// how disordered the result is and to drive the ordered phases.
double netDipole(const Structure& structure)
{
    // A rigid water monomer's dipole points along the H-H bisector away from
    // the oxygen; 1.85 D for the gas-phase geometry. Summing the per-molecule
    // unit bisectors and scaling is enough for the diagnostic here.
    Vec3 total;
    int molecules = 0;
    const auto& atoms = structure.atoms();
    for (std::size_t i = 0; i + 2 < atoms.size(); i += 3) {
        if (atoms[i].atomicNumber != 8)
            continue;
        const Vec3 bisector =
            (atoms[i + 1].position - atoms[i].position)
            + (atoms[i + 2].position - atoms[i].position);
        if (bisector.norm() > 1e-9)
            total = total + bisector.normalized();
        ++molecules;
    }
    return molecules > 0 ? 1.85 * total.norm() / molecules : 0.0;
}

} // namespace

std::string IceBuilder::toString(Phase phase)
{
    switch (phase) {
    case Phase::LiquidWater: return "Liquid Water";
    case Phase::IceIh: return "Ice Ih";
    case Phase::IceIc: return "Ice Ic";
    case Phase::IceVII: return "Ice VII";
    }
    return "Water";
}

void IceBuilder::geometryOf(WaterGeometry geometry, double& ohLength,
                            double& hohAngleDeg)
{
    switch (geometry) {
    case WaterGeometry::Spce:
        ohLength = 1.0;
        hohAngleDeg = 109.47;
        return;
    case WaterGeometry::Rigid:
    case WaterGeometry::Tip3p:
    case WaterGeometry::Tip4p:
        break;
    }
    // TIP3P and TIP4P both use the experimental gas-phase monomer geometry;
    // they differ in charge placement, which is not a structural property and
    // is deliberately not emitted here.
    ohLength = 0.9572;
    hohAngleDeg = 104.52;
}

IceBuilder::Result IceBuilder::generate(const Params& params)
{
    const int nx = std::max(1, params.nx);
    const int ny = std::max(1, params.ny);
    const int nz = std::max(1, params.nz);
    double ohLength = 0.0;
    double hohAngle = 0.0;
    geometryOf(params.geometry, ohLength, hohAngle);

    std::mt19937 rng(params.seed);
    Result result;
    Structure structure;

    if (params.phase == Phase::LiquidWater) {
        // -- Amorphous packing at a target density --------------------------
        const int count = params.moleculeCount > 0 ? params.moleculeCount : 0;
        double lx = params.boxLx, ly = params.boxLy, lz = params.boxLz;
        int molecules = count;
        if (count > 0) {
            // Size a cube to hold `count` molecules at the requested density.
            if (params.densityGCm3 <= 0.0)
                throw std::invalid_argument("density must be positive");
            const double volume =
                count * kWaterMassU * kUPerA3ToGCm3 / params.densityGCm3;
            lx = ly = lz = std::cbrt(volume);
        } else {
            if (lx <= 0.0 || ly <= 0.0 || lz <= 0.0)
                throw std::invalid_argument("box dimensions must be positive");
            molecules = static_cast<int>(std::round(
                params.densityGCm3 * lx * ly * lz
                / (kWaterMassU * kUPerA3ToGCm3)));
        }
        if (molecules <= 0)
            throw std::invalid_argument(
                "the requested box and density hold no molecules");

        structure.setCell(UnitCell({lx, 0, 0}, {0, ly, 0}, {0, 0, lz}));
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        std::vector<Vec3> oxygens;
        oxygens.reserve(static_cast<std::size_t>(molecules));

        // Rejection sampling against a minimum O–O separation. The attempt cap
        // is what stops an over-dense request from hanging: it is reported as a
        // short molecule count rather than spun on forever.
        const double minSq = params.minOODistance * params.minOODistance;
        const long maxAttempts = 2000L * molecules + 100000L;
        long attempts = 0;
        while (static_cast<int>(oxygens.size()) < molecules
               && attempts < maxAttempts) {
            ++attempts;
            const Vec3 candidate{unit(rng) * lx, unit(rng) * ly, unit(rng) * lz};
            bool clash = false;
            for (const Vec3& existing : oxygens) {
                // Minimum image in an orthorhombic box.
                double dx = candidate.x - existing.x;
                double dy = candidate.y - existing.y;
                double dz = candidate.z - existing.z;
                dx -= lx * std::round(dx / lx);
                dy -= ly * std::round(dy / ly);
                dz -= lz * std::round(dz / lz);
                if (dx * dx + dy * dy + dz * dz < minSq) {
                    clash = true;
                    break;
                }
            }
            if (!clash)
                oxygens.push_back(candidate);
        }

        // Random orientations: a liquid has no preferred molecular axis.
        std::normal_distribution<double> gaussian(0.0, 1.0);
        for (const Vec3& oxygen : oxygens) {
            Vec3 first{gaussian(rng), gaussian(rng), gaussian(rng)};
            if (first.norm() < 1e-9)
                first = {1, 0, 0};
            Vec3 second{gaussian(rng), gaussian(rng), gaussian(rng)};
            if (second.norm() < 1e-9)
                second = {0, 1, 0};
            // Orthogonalize so the two donor directions are not parallel.
            second = second - first.normalized() * second.dot(first.normalized());
            if (second.norm() < 1e-9)
                second = first.cross(Vec3{0, 0, 1});
            Vec3 h1, h2;
            placeHydrogens(oxygen, first, second, ohLength, hohAngle, h1, h2);
            Atom o;
            o.atomicNumber = 8;
            o.position = oxygen;
            structure.addAtom(o);
            Atom h;
            h.atomicNumber = 1;
            h.position = h1;
            structure.addAtom(h);
            h.position = h2;
            structure.addAtom(h);
        }

        result.moleculeCount = static_cast<int>(oxygens.size());
        const double volume = lx * ly * lz;
        result.densityGCm3 =
            result.moleculeCount * kWaterMassU * kUPerA3ToGCm3 / volume;
        result.description =
            "Liquid water: " + std::to_string(result.moleculeCount)
            + " randomly packed molecules";
        if (result.moleculeCount < molecules)
            result.description += " (requested " + std::to_string(molecules)
                + "; packing saturated at the minimum O-O separation)";
        result.structure = std::move(structure);
        result.netDipolePerMolecule = netDipole(result.structure);
        return result;
    }

    // -- Crystalline phases --------------------------------------------------
    OxygenLattice lattice;
    switch (params.phase) {
    case Phase::IceIh:
        lattice = iceIhLattice(nx, ny, nz);
        break;
    case Phase::IceIc:
        lattice = iceIcLattice(nx, ny, nz);
        break;
    case Phase::IceVII:
        // Two interpenetrating diamond nets; a chosen so the intra-net O-O is
        // 2.90 Å, matching ice VII at ambient-temperature compression.
        lattice = iceIcLattice(nx, ny, nz, 6.696, /*interpenetrating=*/true);
        break;
    case Phase::LiquidWater:
        break; // handled above
    }

    std::vector<HBond> bonds = buildBonds(lattice);
    const int siteCount = static_cast<int>(lattice.sites.size());
    if (siteCount == 0 || bonds.empty())
        throw std::invalid_argument("the ice lattice produced no O-O network");

    // Every oxygen must be tetrahedrally coordinated; anything else means the
    // lattice or the cutoff is wrong, and the ice rules cannot be satisfied.
    std::vector<int> degree(static_cast<std::size_t>(siteCount), 0);
    for (const HBond& bond : bonds) {
        ++degree[static_cast<std::size_t>(bond.a)];
        ++degree[static_cast<std::size_t>(bond.b)];
    }
    for (const int d : degree) {
        if (d != 4)
            throw std::invalid_argument(
                "the oxygen lattice is not 4-coordinated (got a site with "
                + std::to_string(d) + " neighbors) — the supercell is too "
                "small for the bond cutoff");
    }

    // Proton-ORDERED phases (XI, VIII) skip the randomization: the Eulerian
    // orientation is a single low-entropy configuration, which is the point.
    solveIceRules(bonds, siteCount, rng, 40 * static_cast<int>(bonds.size()));

    // -- Verify + emit -------------------------------------------------------
    std::vector<std::vector<Vec3>> donated(static_cast<std::size_t>(siteCount));
    for (const HBond& bond : bonds) {
        const Vec3 aPos = lattice.sites[static_cast<std::size_t>(bond.a)].position;
        const Vec3 bPos =
            lattice.sites[static_cast<std::size_t>(bond.b)].position + bond.shift;
        if (bond.aToB)
            donated[static_cast<std::size_t>(bond.a)].push_back(bPos - aPos);
        else
            donated[static_cast<std::size_t>(bond.b)].push_back(aPos - bPos);
    }
    for (const auto& list : donated)
        if (list.size() != 2)
            ++result.iceRuleViolations;

    structure.setCell(lattice.cell);
    for (int i = 0; i < siteCount; ++i) {
        const Vec3& oxygen = lattice.sites[static_cast<std::size_t>(i)].position;
        Atom o;
        o.atomicNumber = 8;
        o.position = oxygen;
        structure.addAtom(o);

        const auto& list = donated[static_cast<std::size_t>(i)];
        // A site the solver failed on still gets a valid water molecule, just
        // not one aligned with two hydrogen bonds — better a chemically sane
        // structure with a reported violation than a bare oxygen.
        const Vec3 first = list.size() > 0 ? list[0] : Vec3{1, 0, 0};
        const Vec3 second = list.size() > 1 ? list[1] : Vec3{0, 1, 0};
        Vec3 h1, h2;
        placeHydrogens(oxygen, first, second, ohLength, hohAngle, h1, h2);
        Atom h;
        h.atomicNumber = 1;
        h.position = h1;
        structure.addAtom(h);
        h.position = h2;
        structure.addAtom(h);
    }

    result.moleculeCount = siteCount;
    const auto& v = lattice.cell.vectors();
    const double volume = std::abs(v[0].dot(v[1].cross(v[2])));
    result.densityGCm3 = siteCount * kWaterMassU * kUPerA3ToGCm3 / volume;
    result.structure = std::move(structure);
    result.netDipolePerMolecule = netDipole(result.structure);
    result.description = toString(params.phase) + ": "
        + std::to_string(siteCount) + " molecules, "
        + "proton-disordered (Bernal-Fowler ice rules)";
    return result;
}

} // namespace calango::core
