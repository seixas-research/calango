#include "core/GrapheneOxidePercolation.hpp"

#include "core/GrapheneOxideBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace calango::core {

namespace {

/// An integer count of lattice-vector translations along each cell axis —
/// the abelian-group label a bond carries when it crosses a periodic
/// boundary. Purely a bookkeeping device for the winding-number check
/// below; never exposed outside this file.
struct LatticeShift {
    long long a = 0, b = 0, c = 0;

    bool operator==(const LatticeShift& other) const
    {
        return a == other.a && b == other.b && c == other.c;
    }
};

LatticeShift operator+(const LatticeShift& lhs, const LatticeShift& rhs)
{
    return LatticeShift{lhs.a + rhs.a, lhs.b + rhs.b, lhs.c + rhs.c};
}

LatticeShift operator-(const LatticeShift& lhs, const LatticeShift& rhs)
{
    return LatticeShift{lhs.a - rhs.a, lhs.b - rhs.b, lhs.c - rhs.c};
}

LatticeShift operator-(const LatticeShift& shift)
{
    return LatticeShift{-shift.a, -shift.b, -shift.c};
}

/// Bond::imageOffset is a Cartesian shift; round it to the nearest integer
/// combination of lattice vectors. It is exactly integer by construction
/// (Structure::detectBonds() builds it from std::round()'d fractional
/// shifts), so the rounding here only guards against floating-point noise.
LatticeShift toLatticeShift(const UnitCell& cell, const Vec3& cartesianOffset)
{
    if (cartesianOffset.dot(cartesianOffset) < 1e-12)
        return LatticeShift{};
    const Vec3 frac = cell.cartesianToFractional(cartesianOffset);
    return LatticeShift{std::llround(frac.x), std::llround(frac.y), std::llround(frac.z)};
}

struct CarbonEdge {
    int to = 0;
    LatticeShift shift; // translation applied when walking FROM this edge's owning atom TO `to`
};
using CarbonAdjacency = std::vector<std::vector<CarbonEdge>>;

CarbonAdjacency buildCarbonAdjacency(const Structure& structure)
{
    CarbonAdjacency adjacency(structure.size());
    for (const Bond& bond : structure.detectBonds()) {
        const auto& atoms = structure.atoms();
        if (atoms[static_cast<std::size_t>(bond.i)].atomicNumber != 6
            || atoms[static_cast<std::size_t>(bond.j)].atomicNumber != 6)
            continue;
        const LatticeShift forward = toLatticeShift(structure.cell(), bond.imageOffset);
        adjacency[static_cast<std::size_t>(bond.i)].push_back({bond.j, forward});
        adjacency[static_cast<std::size_t>(bond.j)].push_back({bond.i, -forward});
    }
    return adjacency;
}

/// A found six-cycle together with the per-edge lattice shift used to close
/// it — kept alongside the public CarbonRing so later stages never have to
/// re-derive which of several possible images a ring edge actually used
/// (ambiguous the moment two atoms share more than one bond, which is the
/// normal case for a minimal-basis cell such as graphene's own primitive
/// cell: every "A-B" ring edge is a DIFFERENT physical bond).
struct RawRing {
    std::array<int, 6> atoms{};
    std::array<LatticeShift, 6> edgeShift{}; // edgeShift[k]: atoms[k] -> atoms[(k+1)%6]
};

bool isChordless(const CarbonAdjacency& adjacency, const std::array<int, 6>& ring)
{
    const auto bonded = [&](int a, int b) {
        for (const CarbonEdge& edge : adjacency[static_cast<std::size_t>(a)])
            if (edge.to == b)
                return true;
        return false;
    };
    // Cyclic distance 2 (six pairs) and distance 3 (three pairs, k<3 avoids
    // checking each twice): any such pair being bonded means this is not an
    // induced hexagon — either a fused-ring artefact or a false positive
    // from a more exotic local topology.
    for (int k = 0; k < 6; ++k)
        if (bonded(ring[static_cast<std::size_t>(k)], ring[static_cast<std::size_t>((k + 2) % 6)]))
            return false;
    for (int k = 0; k < 3; ++k)
        if (bonded(ring[static_cast<std::size_t>(k)], ring[static_cast<std::size_t>((k + 3) % 6)]))
            return false;
    return true;
}

/// Direction- and rotation-independent key for deduplicating a found ring
/// (the same physical hexagon is found once per starting atom, in both
/// directions — 12 times over). Used only as a map key; the ring's own
/// atoms/edgeShift are kept in whatever order first discovered them.
std::array<int, 6> canonicalKey(const std::array<int, 6>& atoms)
{
    std::array<int, 6> best{};
    bool haveBest = false;
    for (int direction = 0; direction < 2; ++direction) {
        std::array<int, 6> base{};
        for (int k = 0; k < 6; ++k)
            base[static_cast<std::size_t>(k)]
                = (direction == 0) ? atoms[static_cast<std::size_t>(k)]
                                    : atoms[static_cast<std::size_t>((6 - k) % 6)];
        for (int rot = 0; rot < 6; ++rot) {
            std::array<int, 6> candidate{};
            for (int k = 0; k < 6; ++k)
                candidate[static_cast<std::size_t>(k)] = base[static_cast<std::size_t>((k + rot) % 6)];
            if (!haveBest || candidate < best) {
                best = candidate;
                haveBest = true;
            }
        }
    }
    return best;
}

std::vector<RawRing> findCarbonRings(const CarbonAdjacency& adjacency)
{
    std::map<std::array<int, 6>, RawRing> found; // canonical key -> first discovery, order-preserving on insert order is irrelevant here
    const auto n = adjacency.size();

    std::vector<int> path;
    std::vector<LatticeShift> shiftPath; // shiftPath[k]: path[k] -> path[k+1]
    std::vector<bool> visited(n, false);

    for (std::size_t start = 0; start < n; ++start) {
        if (adjacency[start].empty())
            continue;
        visited[start] = true;
        path = {static_cast<int>(start)};

        std::function<void(int, LatticeShift)> dfs = [&](int current, LatticeShift cumulative) {
            if (path.size() == 6) {
                for (const CarbonEdge& edge : adjacency[static_cast<std::size_t>(current)]) {
                    if (edge.to != static_cast<int>(start))
                        continue;
                    // Close only when the net lattice translation around the
                    // loop is exactly zero: a genuine hexagon is a closed
                    // shape in real space, so its six bond vectors must sum
                    // to zero. A non-zero net shift means the walk wound
                    // around the periodic cell instead of closing a real
                    // face — reject it, or every ring would also be
                    // "found" again shifted by a lattice vector.
                    if (!((cumulative + edge.shift) == LatticeShift{}))
                        continue;
                    std::array<int, 6> atoms{};
                    std::array<LatticeShift, 6> shifts{};
                    for (int k = 0; k < 6; ++k)
                        atoms[static_cast<std::size_t>(k)] = path[static_cast<std::size_t>(k)];
                    for (int k = 0; k < 5; ++k)
                        shifts[static_cast<std::size_t>(k)] = shiftPath[static_cast<std::size_t>(k)];
                    shifts[5] = edge.shift;
                    if (!isChordless(adjacency, atoms))
                        continue;
                    found.emplace(canonicalKey(atoms), RawRing{atoms, shifts});
                }
                return;
            }
            for (const CarbonEdge& edge : adjacency[static_cast<std::size_t>(current)]) {
                if (visited[static_cast<std::size_t>(edge.to)])
                    continue;
                visited[static_cast<std::size_t>(edge.to)] = true;
                path.push_back(edge.to);
                shiftPath.push_back(edge.shift);
                dfs(edge.to, cumulative + edge.shift);
                shiftPath.pop_back();
                path.pop_back();
                visited[static_cast<std::size_t>(edge.to)] = false;
            }
        };

        dfs(static_cast<int>(start), LatticeShift{});
        path.clear();
        visited[start] = false;
    }

    std::vector<RawRing> rings;
    rings.reserve(found.size());
    for (auto& [key, raw] : found)
        rings.push_back(raw);
    return rings;
}

} // namespace

RingPercolationResult analyzeRingPercolation(const Structure& structure)
{
    RingPercolationResult result;

    const std::vector<int> labels = GrapheneOxideBuilder::functionalGroupLabels(structure);

    // sp2 carbon fraction: independent of ring membership, reusing the
    // builder's own classification directly (no second chemistry method).
    int carbonCount = 0;
    int sp2Count = 0;
    for (std::size_t i = 0; i < structure.size(); ++i) {
        if (structure.atoms()[i].atomicNumber != 6)
            continue;
        ++carbonCount;
        if (labels[i] == -1)
            ++sp2Count;
    }
    result.sp2CarbonFraction = carbonCount > 0 ? static_cast<double>(sp2Count) / carbonCount : 0.0;

    const auto pbc = structure.cell().pbc();
    result.periodicAxis = {pbc[0], pbc[1], pbc[2]};

    const CarbonAdjacency adjacency = buildCarbonAdjacency(structure);
    const std::vector<RawRing> rawRings = findCarbonRings(adjacency);

    result.rings.reserve(rawRings.size());
    for (const RawRing& raw : rawRings) {
        CarbonRing ring;
        ring.atoms = raw.atoms;
        ring.intact = true;
        for (const int atom : ring.atoms) {
            if (labels[static_cast<std::size_t>(atom)] != -1) {
                ring.intact = false;
                break;
            }
        }
        result.rings.push_back(ring);
    }

    int intactCount = 0;
    for (const CarbonRing& ring : result.rings)
        if (ring.intact)
            ++intactCount;
    result.intactRingFraction
        = !result.rings.empty() ? static_cast<double>(intactCount) / result.rings.size() : 0.0;

    // Restricted adjacency: only the six bonds bordering each INTACT ring —
    // exactly "the graph of intact rings joined by a shared C-C bond",
    // expressed at the atom level (atoms have unambiguous positions; ring
    // centroids under periodic wrap do not).
    CarbonAdjacency restricted(structure.size());
    for (std::size_t r = 0; r < result.rings.size(); ++r) {
        if (!result.rings[r].intact)
            continue;
        const RawRing& raw = rawRings[r];
        for (int k = 0; k < 6; ++k) {
            const int a = raw.atoms[static_cast<std::size_t>(k)];
            const int b = raw.atoms[static_cast<std::size_t>((k + 1) % 6)];
            const LatticeShift shift = raw.edgeShift[static_cast<std::size_t>(k)];
            restricted[static_cast<std::size_t>(a)].push_back({b, shift});
            restricted[static_cast<std::size_t>(b)].push_back({a, -shift});
        }
    }

    // Union-find-by-BFS with an integer displacement carried per atom,
    // relative to an arbitrary root in its component: the standard way to
    // detect a periodic-boundary-spanning (percolating) cluster. Any edge
    // — tree or not — whose two endpoints already carry inconsistent
    // displacements closes a cycle that winds around the cell a non-zero
    // number of times along some axis: that axis percolates.
    std::vector<int> atomComponent(structure.size(), -1);
    struct ComponentInfo {
        std::array<bool, 3> percolates{false, false, false};
    };
    std::vector<ComponentInfo> components;

    for (std::size_t start = 0; start < structure.size(); ++start) {
        if (restricted[start].empty() || atomComponent[start] != -1)
            continue;
        const int compId = static_cast<int>(components.size());
        components.emplace_back();
        std::map<int, LatticeShift> displacement;
        displacement[static_cast<int>(start)] = LatticeShift{};
        atomComponent[start] = compId;
        std::vector<int> queue{static_cast<int>(start)};
        std::size_t qi = 0;
        while (qi < queue.size()) {
            const int current = queue[qi++];
            for (const CarbonEdge& edge : restricted[static_cast<std::size_t>(current)]) {
                const LatticeShift wanted = displacement[current] + edge.shift;
                auto it = displacement.find(edge.to);
                if (it == displacement.end()) {
                    displacement[edge.to] = wanted;
                    atomComponent[static_cast<std::size_t>(edge.to)] = compId;
                    queue.push_back(edge.to);
                } else {
                    const LatticeShift delta = wanted - it->second;
                    if (delta.a != 0)
                        components[static_cast<std::size_t>(compId)].percolates[0] = true;
                    if (delta.b != 0)
                        components[static_cast<std::size_t>(compId)].percolates[1] = true;
                    if (delta.c != 0)
                        components[static_cast<std::size_t>(compId)].percolates[2] = true;
                }
            }
        }
    }

    std::vector<int> componentToDomain(components.size(), -1);
    for (std::size_t r = 0; r < result.rings.size(); ++r) {
        if (!result.rings[r].intact)
            continue;
        const int atom0 = rawRings[r].atoms[0];
        const int comp = atomComponent[static_cast<std::size_t>(atom0)];
        int domainIdx = componentToDomain[static_cast<std::size_t>(comp)];
        if (domainIdx == -1) {
            domainIdx = static_cast<int>(result.domains.size());
            componentToDomain[static_cast<std::size_t>(comp)] = domainIdx;
            SP2Domain domain;
            domain.percolates = components[static_cast<std::size_t>(comp)].percolates;
            result.domains.push_back(std::move(domain));
        }
        result.domains[static_cast<std::size_t>(domainIdx)].rings.push_back(static_cast<int>(r));
        result.rings[r].domain = domainIdx;
    }

    for (std::size_t d = 0; d < result.domains.size(); ++d) {
        const SP2Domain& domain = result.domains[d];
        if (result.largestDomain == -1
            || domain.rings.size() > result.domains[static_cast<std::size_t>(result.largestDomain)].rings.size())
            result.largestDomain = static_cast<int>(d);
        for (int axis = 0; axis < 3; ++axis)
            if (domain.percolates[static_cast<std::size_t>(axis)])
                result.percolatesAxis[static_cast<std::size_t>(axis)] = true;
    }

    return result;
}

PiPercolationResult analyzePiPercolation(const Structure& structure)
{
    PiPercolationResult result;

    const auto pbc = structure.cell().pbc();
    result.periodicAxis = {pbc[0], pbc[1], pbc[2]};
    result.atomDomain.assign(structure.size(), -1);
    if (structure.empty())
        return result;

    // The SAME classification the ring analysis reads. Nothing here decides
    // sp2 vs sp3 by a second method.
    const std::vector<int> labels =
        GrapheneOxideBuilder::functionalGroupLabels(structure);

    // Sigma-neighbour count per atom, over EVERY element: a carbon's
    // hybridization is set by how many things it is bonded to, and a
    // terminating hydrogen counts exactly as much as a carbon does. Counted
    // from the same detectBonds() pass buildCarbonAdjacency() uses, so the
    // two can never disagree about what is bonded to what.
    std::vector<int> sigmaNeighbors(structure.size(), 0);
    for (const Bond& bond : structure.detectBonds()) {
        ++sigmaNeighbors[static_cast<std::size_t>(bond.i)];
        ++sigmaNeighbors[static_cast<std::size_t>(bond.j)];
    }

    int carbonCount = 0;
    std::vector<bool> isPi(structure.size(), false);
    for (std::size_t i = 0; i < structure.size(); ++i) {
        if (structure.atoms()[i].atomicNumber != 6)
            continue;
        ++carbonCount;
        // No oxygen group, and still three-coordinate: a p_z survives. The
        // second half is what separates this from sp2CarbonFraction, which
        // asks only about the oxygen — an unoxidized but FOUR-coordinate
        // carbon (a CH2 in a hydrogenated defect) has no pi orbital and must
        // not join the network.
        if (labels[i] == -1 && sigmaNeighbors[i] <= 3) {
            isPi[i] = true;
            result.piCarbons.push_back(static_cast<int>(i));
        }
    }
    result.piCarbonFraction = carbonCount > 0
        ? static_cast<double>(result.piCarbons.size()) / carbonCount
        : 0.0;
    if (result.piCarbons.empty())
        return result;

    // The conjugation graph: a C-C bond between two pi carbons. No ring
    // requirement — that is the whole difference from the ring analysis.
    const CarbonAdjacency carbon = buildCarbonAdjacency(structure);
    CarbonAdjacency conjugated(structure.size());
    for (std::size_t a = 0; a < carbon.size(); ++a) {
        if (!isPi[a])
            continue;
        for (const CarbonEdge& edge : carbon[a]) {
            if (!isPi[static_cast<std::size_t>(edge.to)])
                continue;
            conjugated[a].push_back(edge);
        }
    }

    // Connected components with a per-atom integer displacement relative to
    // an arbitrary root — the same winding-number construction the ring
    // analysis uses, on a different graph. An edge whose endpoints already
    // carry inconsistent displacements closes a cycle that wraps the cell,
    // and the axis it wraps along is one this network percolates.
    std::vector<int> component(structure.size(), -1);
    for (std::size_t start = 0; start < structure.size(); ++start) {
        if (!isPi[start] || component[start] != -1)
            continue;
        const int domainIndex = static_cast<int>(result.domains.size());
        result.domains.emplace_back();
        std::map<int, LatticeShift> displacement;
        displacement[static_cast<int>(start)] = LatticeShift{};
        component[start] = domainIndex;
        std::vector<int> queue{static_cast<int>(start)};
        std::size_t head = 0;
        while (head < queue.size()) {
            const int current = queue[head++];
            result.domains[static_cast<std::size_t>(domainIndex)].atoms
                .push_back(current);
            result.atomDomain[static_cast<std::size_t>(current)] = domainIndex;
            for (const CarbonEdge& edge :
                 conjugated[static_cast<std::size_t>(current)]) {
                const LatticeShift wanted = displacement[current] + edge.shift;
                const auto it = displacement.find(edge.to);
                if (it == displacement.end()) {
                    displacement[edge.to] = wanted;
                    component[static_cast<std::size_t>(edge.to)] = domainIndex;
                    queue.push_back(edge.to);
                    continue;
                }
                const LatticeShift delta = wanted - it->second;
                auto& percolates =
                    result.domains[static_cast<std::size_t>(domainIndex)]
                        .percolates;
                if (delta.a != 0)
                    percolates[0] = true;
                if (delta.b != 0)
                    percolates[1] = true;
                if (delta.c != 0)
                    percolates[2] = true;
            }
        }
    }

    for (std::size_t d = 0; d < result.domains.size(); ++d) {
        PiDomain& domain = result.domains[d];
        std::sort(domain.atoms.begin(), domain.atoms.end());
        if (result.largestDomain == -1
            || domain.atoms.size()
                > result.domains[static_cast<std::size_t>(result.largestDomain)]
                      .atoms.size())
            result.largestDomain = static_cast<int>(d);
        for (int axis = 0; axis < 3; ++axis) {
            // A non-periodic axis can never percolate, whatever the winding
            // count says: without periodicity there is no image to wind onto.
            if (!result.periodicAxis[static_cast<std::size_t>(axis)])
                domain.percolates[static_cast<std::size_t>(axis)] = false;
            if (domain.percolates[static_cast<std::size_t>(axis)])
                result.percolatesAxis[static_cast<std::size_t>(axis)] = true;
        }
    }
    if (result.largestDomain >= 0) {
        result.largestDomainFraction =
            static_cast<double>(
                result.domains[static_cast<std::size_t>(result.largestDomain)]
                    .atoms.size())
            / result.piCarbons.size();
    }
    return result;
}

std::vector<PiPercolationResult>
analyzePiPercolationTrajectory(const std::vector<Structure>& frames)
{
    std::vector<PiPercolationResult> results;
    results.reserve(frames.size());
    for (const Structure& frame : frames)
        results.push_back(analyzePiPercolation(frame));
    return results;
}

std::vector<RingPercolationResult>
analyzeRingPercolationTrajectory(const std::vector<Structure>& frames)
{
    std::vector<RingPercolationResult> results;
    results.reserve(frames.size());
    for (const Structure& frame : frames)
        results.push_back(analyzeRingPercolation(frame));
    return results;
}

} // namespace calango::core
