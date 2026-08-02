#include "core/StructuralPhase.hpp"

#include "core/PeriodicImages.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace calango::core {

namespace {

/// Neighbours considered by the fcc/hcp/icosahedral test and by the bcc test.
constexpr int kCloseShell = 12; ///< fcc / hcp / ico
constexpr int kBccShell = 14;   ///< bcc: 8 first + 6 second neighbours

/// Where the adaptive bond cutoff is placed, as a multiple of the MEAN distance
/// to the shell being classified.
///
/// Both factors come from the ideal lattice and nothing else, so they are
/// derived here rather than written as magic numbers:
///
///   fcc (lattice parameter a): the 12 first neighbours sit at a/sqrt(2) and
///   the 6 second at a. The cutoff belongs midway between them, (1 + sqrt(2))/2
///   * a/sqrt(2)... expressed against the mean over the 12 (which IS a/sqrt(2),
///   since they are one shell) that is a factor of (1 + sqrt(2))/2.
///
///   bcc: 8 first neighbours at sqrt(3)a/2, 6 second at a, 6 third at
///   sqrt(2) a. The 14 nearest span the first TWO shells, so the cutoff goes
///   between the second and the third — at (1 + sqrt(2))/2 * a — while the mean
///   over the 14 is (8 * sqrt(3)/2 + 6)/14 * a. The factor is the ratio.
const double kCloseCutoffFactor = (1.0 + std::sqrt(2.0)) / 2.0;
const double kBccCutoffFactor =
    kCloseCutoffFactor / ((8.0 * std::sqrt(3.0) / 2.0 + 6.0) / 14.0);

/// The diamond first shell: 4 neighbours at sqrt(3)a/4, second shell 12 at
/// a/sqrt(2). Midway between them, as a multiple of the first-shell distance.
const double kDiamondCutoffFactor =
    ((std::sqrt(3.0) / 4.0) + (1.0 / std::sqrt(2.0))) / 2.0
    / (std::sqrt(3.0) / 4.0);

/// One neighbour of the atom being classified: where it is relative to that
/// atom, and how far away.
struct Neighbor {
    Vec3 offset;
    double distance = 0.0;
};

/// The three CNA indices of one neighbour pair, packed for counting.
struct Signature {
    int common = 0;   ///< neighbours shared by the pair
    int bonds = 0;    ///< bonds among those shared neighbours
    int longest = 0;  ///< longest chain of those bonds

    bool is(int c, int b, int l) const
    {
        return common == c && bonds == b && longest == l;
    }
};

/// The third CNA index: the number of bonds in the LARGEST CONNECTED CLUSTER of
/// the bond graph among the common neighbours.
///
/// Not the longest simple path, which is the reading the name "longest chain of
/// bonds" invites and which gets two of the four signatures wrong. A chain is
/// allowed to close on itself, so the index counts bonds per connected
/// component:
///
///   fcc  (4,2,1)  two disjoint bonds        -> largest component holds 1 bond
///   hcp  (4,2,2)  two bonds sharing an atom -> 2
///   bcc  (4,4,4)  a four-membered ring      -> 4  (a simple path gives 3)
///   ico  (5,5,5)  a five-membered ring      -> 5  (a simple path gives 4)
///
/// `adjacency` is a bitmask per node over at most 8 nodes, which is what the
/// shared-neighbour set of any of these structures fits in.
int largestBondCluster(const std::array<std::uint8_t, 8>& adjacency, int nodes)
{
    std::uint8_t unvisited = 0;
    for (int i = 0; i < nodes; ++i)
        if (adjacency[static_cast<std::size_t>(i)] != 0)
            unvisited = static_cast<std::uint8_t>(unvisited | (1u << i));

    int best = 0;
    while (unvisited != 0) {
        // Flood-fill one component.
        std::uint8_t component = 0;
        std::uint8_t frontier =
            static_cast<std::uint8_t>(unvisited & (~unvisited + 1u)); // lowest set bit
        while (frontier != 0) {
            component = static_cast<std::uint8_t>(component | frontier);
            std::uint8_t next = 0;
            for (int i = 0; i < nodes; ++i)
                if (frontier & (1u << i))
                    next = static_cast<std::uint8_t>(
                        next | adjacency[static_cast<std::size_t>(i)]);
            frontier = static_cast<std::uint8_t>(next & ~component);
        }
        unvisited = static_cast<std::uint8_t>(unvisited & ~component);

        // Bonds inside it: half the summed degree, since each is counted twice.
        int degrees = 0;
        for (int i = 0; i < nodes; ++i)
            if (component & (1u << i))
                degrees += std::popcount(static_cast<unsigned>(
                    adjacency[static_cast<std::size_t>(i)]));
        best = std::max(best, degrees / 2);
    }
    return best;
}

/// CNA signatures of the `count` nearest entries of `neighbors` against a bond
/// cutoff of `cutoff`.
///
/// Returns false when the shell does not hold together at that cutoff — i.e.
/// some of the atoms that had to be in it are outside. That check is what stops
/// a badly distorted or under-coordinated environment from being forced into a
/// signature: without it, an atom with 9 real neighbours would still produce 12
/// "neighbours" and could accidentally match.
bool signaturesFor(const std::vector<Neighbor>& neighbors, int count,
                   double cutoff, std::vector<Signature>& out)
{
    if (static_cast<int>(neighbors.size()) < count)
        return false;
    const auto n = static_cast<std::size_t>(count);
    for (std::size_t i = 0; i < n; ++i)
        if (neighbors[i].distance > cutoff)
            return false;

    // Bond matrix among the shell members. Two neighbours are bonded when they
    // are within the same adaptive cutoff of EACH OTHER — the same criterion
    // that put them in the shell in the first place.
    const double cutoffSq = cutoff * cutoff;
    std::array<std::uint16_t, kBccShell> bonded{};
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            const Vec3 d = neighbors[i].offset - neighbors[j].offset;
            if (d.dot(d) >= cutoffSq)
                continue;
            bonded[i] = static_cast<std::uint16_t>(bonded[i] | (1u << j));
            bonded[j] = static_cast<std::uint16_t>(bonded[j] | (1u << i));
        }
    }

    out.clear();
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        // The neighbours atom i shares with the central atom: everything in the
        // shell that is also bonded to i.
        std::array<std::size_t, 8> shared{};
        int sharedCount = 0;
        for (std::size_t j = 0; j < n; ++j) {
            if (j == i || !(bonded[i] & (1u << j)))
                continue;
            if (sharedCount == static_cast<int>(shared.size()))
                break; // more than 8 shared neighbours is no known structure
            shared[static_cast<std::size_t>(sharedCount++)] = j;
        }

        Signature signature;
        signature.common = sharedCount;
        std::array<std::uint8_t, 8> adjacency{};
        for (int a = 0; a < sharedCount; ++a) {
            for (int b = a + 1; b < sharedCount; ++b) {
                const std::size_t ja = shared[static_cast<std::size_t>(a)];
                const std::size_t jb = shared[static_cast<std::size_t>(b)];
                if (!(bonded[ja] & (1u << jb)))
                    continue;
                ++signature.bonds;
                adjacency[static_cast<std::size_t>(a)] =
                    static_cast<std::uint8_t>(adjacency[static_cast<std::size_t>(a)]
                                              | (1u << b));
                adjacency[static_cast<std::size_t>(b)] =
                    static_cast<std::uint8_t>(adjacency[static_cast<std::size_t>(b)]
                                              | (1u << a));
            }
        }
        signature.longest = largestBondCluster(adjacency, sharedCount);
        out.push_back(signature);
    }
    return true;
}

/// Mean distance of the `count` nearest neighbours (they are pre-sorted).
double meanDistance(const std::vector<Neighbor>& neighbors, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
        sum += neighbors[static_cast<std::size_t>(i)].distance;
    return sum / count;
}

/// fcc / hcp / icosahedral from a 12-neighbour shell, or Other.
///
/// Shared by the direct test on an atom's own first shell and by the diamond
/// test on its second shell, which is the whole reason diamond identification
/// costs so little: the second shell of a diamond atom IS an fcc or hcp shell.
StructuralPhase classifyCloseShell(const std::vector<Neighbor>& neighbors,
                                   std::vector<Signature>& scratch)
{
    if (static_cast<int>(neighbors.size()) < kCloseShell)
        return StructuralPhase::Other;
    const double cutoff =
        kCloseCutoffFactor * meanDistance(neighbors, kCloseShell);
    if (!signaturesFor(neighbors, kCloseShell, cutoff, scratch))
        return StructuralPhase::Other;

    int n421 = 0, n422 = 0, n555 = 0;
    for (const Signature& s : scratch) {
        if (s.is(4, 2, 1))
            ++n421;
        else if (s.is(4, 2, 2))
            ++n422;
        else if (s.is(5, 5, 5))
            ++n555;
    }
    if (n421 == 12)
        return StructuralPhase::Fcc;
    if (n421 == 6 && n422 == 6)
        return StructuralPhase::Hcp;
    if (n555 == 12)
        return StructuralPhase::Icosahedral;
    return StructuralPhase::Other;
}

/// bcc from a 14-neighbour shell, or Other.
StructuralPhase classifyBcc(const std::vector<Neighbor>& neighbors,
                            std::vector<Signature>& scratch)
{
    if (static_cast<int>(neighbors.size()) < kBccShell)
        return StructuralPhase::Other;
    const double cutoff = kBccCutoffFactor * meanDistance(neighbors, kBccShell);
    if (!signaturesFor(neighbors, kBccShell, cutoff, scratch))
        return StructuralPhase::Other;

    int n444 = 0, n666 = 0;
    for (const Signature& s : scratch) {
        if (s.is(4, 4, 4))
            ++n444;
        else if (s.is(6, 6, 6))
            ++n666;
    }
    return (n444 == 6 && n666 == 8) ? StructuralPhase::Bcc
                                    : StructuralPhase::Other;
}

/// A cell-list over the atoms and their periodic images, so the neighbour
/// search is linear in the atom count rather than quadratic.
///
/// Worth the extra code here specifically: phase identification is the analysis
/// people run on a whole MD box, where the existing O(N^2) neighbour loops of
/// the coordination analysis would take minutes on a structure this one handles
/// in well under a second.
class NeighborGrid {
public:
    struct Site {
        Vec3 position;
        int atom = 0; ///< index of the in-cell representative
    };

    NeighborGrid(std::vector<Site> sites, double spacing)
        : sites_(std::move(sites))
        , spacing_(std::max(spacing, 1e-6))
    {
        if (sites_.empty())
            return;
        min_ = max_ = sites_.front().position;
        for (const Site& site : sites_) {
            min_.x = std::min(min_.x, site.position.x);
            min_.y = std::min(min_.y, site.position.y);
            min_.z = std::min(min_.z, site.position.z);
            max_.x = std::max(max_.x, site.position.x);
            max_.y = std::max(max_.y, site.position.y);
            max_.z = std::max(max_.z, site.position.z);
        }
        for (std::size_t index = 0; index < sites_.size(); ++index)
            buckets_[key(sites_[index].position)].push_back(index);
    }

    /// Every site within `radius` of `center`, appended to `out` as offsets
    /// from `center`. The central atom's own site (offset ~0) is skipped.
    void collect(const Vec3& center, double radius,
                 std::vector<Neighbor>& out) const
    {
        const double radiusSq = radius * radius;
        const auto home = cellOf(center);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    const auto it = buckets_.find(hash(home[0] + dx,
                                                       home[1] + dy,
                                                       home[2] + dz));
                    if (it == buckets_.end())
                        continue;
                    for (const std::size_t index : it->second) {
                        const Vec3 d = sites_[index].position - center;
                        const double distSq = d.dot(d);
                        // 0.4 Å floor: the atom itself, and any pathological
                        // overlap that would produce a zero-length offset.
                        if (distSq >= radiusSq || distSq < 0.16)
                            continue;
                        out.push_back({d, std::sqrt(distSq)});
                    }
                }
            }
        }
    }

private:
    std::array<int, 3> cellOf(const Vec3& p) const
    {
        return {static_cast<int>(std::floor((p.x - min_.x) / spacing_)),
                static_cast<int>(std::floor((p.y - min_.y) / spacing_)),
                static_cast<int>(std::floor((p.z - min_.z) / spacing_))};
    }
    static std::int64_t hash(int x, int y, int z)
    {
        // Three 21-bit fields in one 64-bit key; the offset keeps negatives
        // (an image outside the cell's own bounding box) in range.
        constexpr std::int64_t kBias = 1 << 20;
        return ((static_cast<std::int64_t>(x) + kBias) << 42)
            ^ ((static_cast<std::int64_t>(y) + kBias) << 21)
            ^ (static_cast<std::int64_t>(z) + kBias);
    }
    std::int64_t key(const Vec3& p) const
    {
        const auto cell = cellOf(p);
        return hash(cell[0], cell[1], cell[2]);
    }

    std::vector<Site> sites_;
    double spacing_;
    Vec3 min_;
    Vec3 max_;
    std::unordered_map<std::int64_t, std::vector<std::size_t>> buckets_;
};

} // namespace

const char* toString(StructuralPhase phase)
{
    switch (phase) {
    case StructuralPhase::Other:            return "Other";
    case StructuralPhase::Fcc:              return "FCC";
    case StructuralPhase::Hcp:              return "HCP";
    case StructuralPhase::Bcc:              return "BCC";
    case StructuralPhase::Icosahedral:      return "Icosahedral";
    case StructuralPhase::CubicDiamond:     return "Cubic diamond";
    case StructuralPhase::HexagonalDiamond: return "Hexagonal diamond";
    }
    return "Other";
}

StructuralPhaseResult identifyStructuralPhases(
    const Structure& structure, const StructuralPhaseOptions& options)
{
    StructuralPhaseResult result;
    const auto& atoms = structure.atoms();
    const auto n = static_cast<int>(atoms.size());
    if (n == 0)
        return result;

    result.phases.assign(static_cast<std::size_t>(n), StructuralPhase::Other);

    // One global search radius, generous enough for the largest pair in the
    // cell. It only bounds the candidate set; every decision below is taken
    // against a cutoff derived per atom.
    float largestRadius = 0.0f;
    for (const Atom& atom : atoms)
        largestRadius = std::max(largestRadius, atom.covalentRadius());
    const double searchRadius =
        std::max(options.searchScale * 2.0 * largestRadius, 1.0);

    // Sites = the atoms plus every periodic image within reach. Building them
    // once and indexing them spatially is what keeps this linear.
    std::vector<NeighborGrid::Site> sites;
    const auto pbc = structure.cell().pbc();
    const bool usePbc =
        structure.cell().isDefined() && (pbc[0] || pbc[1] || pbc[2]);
    std::vector<Vec3> translations{{0.0, 0.0, 0.0}};
    if (usePbc) {
        translations.clear();
        const auto range = imageRange(structure.cell(), searchRadius);
        const auto& v = structure.cell().vectors();
        for (int i = -range[0]; i <= range[0]; ++i)
            for (int j = -range[1]; j <= range[1]; ++j)
                for (int k = -range[2]; k <= range[2]; ++k)
                    translations.push_back(v[0] * i + v[1] * j + v[2] * k);
    }
    sites.reserve(atoms.size() * translations.size());
    for (int i = 0; i < n; ++i)
        for (const Vec3& t : translations)
            sites.push_back({atoms[static_cast<std::size_t>(i)].position + t, i});

    const NeighborGrid grid(std::move(sites), searchRadius);

    std::vector<Neighbor> neighbors;
    std::vector<Signature> scratch;
    // Four-fold-coordinated atoms and their first shells, kept for the diamond
    // pass. Held as (atom, first-shell atom indices) rather than re-derived,
    // because the second pass needs each neighbour's OWN neighbours and that is
    // where the work is.
    struct DiamondCandidate {
        int atom;
        std::array<int, 4> shell;
        std::array<Vec3, 4> offsets;
    };
    std::vector<DiamondCandidate> diamondCandidates;

    for (int i = 0; i < n; ++i) {
        neighbors.clear();
        grid.collect(atoms[static_cast<std::size_t>(i)].position, searchRadius,
                     neighbors);
        std::sort(neighbors.begin(), neighbors.end(),
                  [](const Neighbor& a, const Neighbor& b) {
                      return a.distance < b.distance;
                  });

        StructuralPhase phase = classifyCloseShell(neighbors, scratch);
        if (phase == StructuralPhase::Other)
            phase = classifyBcc(neighbors, scratch);
        result.phases[static_cast<std::size_t>(i)] = phase;
        if (phase != StructuralPhase::Other || !options.detectDiamond)
            continue;

        // Diamond candidate: exactly four neighbours inside the cutoff that
        // separates the first shell from the second.
        if (neighbors.size() < 4)
            continue;
        const double cutoff = kDiamondCutoffFactor * meanDistance(neighbors, 4);
        int inside = 0;
        for (const Neighbor& neighbor : neighbors) {
            if (neighbor.distance <= cutoff)
                ++inside;
            else
                break; // sorted
        }
        if (inside != 4)
            continue;
        DiamondCandidate candidate{i, {}, {}};
        for (int k = 0; k < 4; ++k)
            candidate.offsets[static_cast<std::size_t>(k)] =
                neighbors[static_cast<std::size_t>(k)].offset;
        diamondCandidates.push_back(candidate);
    }

    // -- Diamond pass -------------------------------------------------------
    //
    // A four-fold-coordinated atom has essentially no common neighbours, so CNA
    // says nothing about it directly. What IS distinctive is its second shell:
    // the twelve next-nearest atoms of a cubic-diamond site form an fcc shell
    // and those of a hexagonal-diamond site an hcp one. So the same signature
    // test runs again, on the second shell, and its verdict is translated.
    for (const DiamondCandidate& candidate : diamondCandidates) {
        const Vec3 center =
            atoms[static_cast<std::size_t>(candidate.atom)].position;
        // The second shell, taken as "every atom in the search radius that is
        // NOT one of the four first neighbours" and then cut to the twelve
        // nearest — which is what the adaptive cutoff of classifyCloseShell
        // will re-derive for itself.
        neighbors.clear();
        grid.collect(center, searchRadius, neighbors);
        std::sort(neighbors.begin(), neighbors.end(),
                  [](const Neighbor& a, const Neighbor& b) {
                      return a.distance < b.distance;
                  });
        std::vector<Neighbor> second;
        second.reserve(neighbors.size());
        for (const Neighbor& neighbor : neighbors) {
            bool isFirstShell = false;
            for (const Vec3& first : candidate.offsets) {
                const Vec3 d = neighbor.offset - first;
                if (d.dot(d) < 1e-6) {
                    isFirstShell = true;
                    break;
                }
            }
            if (!isFirstShell)
                second.push_back(neighbor);
        }
        const StructuralPhase shell = classifyCloseShell(second, scratch);
        if (shell == StructuralPhase::Fcc)
            result.phases[static_cast<std::size_t>(candidate.atom)] =
                StructuralPhase::CubicDiamond;
        else if (shell == StructuralPhase::Hcp)
            result.phases[static_cast<std::size_t>(candidate.atom)] =
                StructuralPhase::HexagonalDiamond;
    }

    for (const StructuralPhase phase : result.phases)
        ++result.counts[static_cast<std::size_t>(phase)];
    return result;
}

} // namespace calango::core
