#pragma once

#include <cstddef>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace calango::core {

/// Dynamic bookkeeping of the reactive sites on a carbon framework.
///
/// WHY THIS EXISTS AS ITS OWN THING.
///
/// Decorating a lattice once is easy: walk a shuffled list of carbons and take
/// the free ones. That is what the builder used to do, with a cursor that only
/// ever moved forward — sites were consumed permanently, so a site the cursor
/// had passed could never come back. Monte Carlo breaks that assumption in the
/// first move it makes: a move REMOVES a group, and the carbons it was sitting
/// on must return to the pool immediately, correctly, and cheaply, or the
/// sampler explores a site space that does not match the structure it is
/// carrying. So the pool needs to be a data structure rather than a cursor.
///
/// THE TWO SITE TYPES, AND WHY THEY CANNOT BE ONE LIST.
///
///   * A SINGLE site is one carbon. Hydroxyls take one basal carbon; carboxyls
///     and carbonyls take one edge carbon.
///   * A PAIR site is two BONDED carbons. An epoxide bridges a C–C bond, so it
///     needs both, and it needs them adjacent — a pair of free carbons on
///     opposite sides of the flake is not a site.
///
/// The two are coupled, which is the whole difficulty: occupying one carbon
/// invalidates every pair that contains it, and freeing one carbon may or may
/// not revive a pair depending on whether its partner is still free. Keeping
/// two independent lists in step by hand is the kind of bookkeeping that is
/// wrong in one branch out of six and produces a pentavalent carbon nobody
/// notices. So it is done in one place, here, and the invariant is checked by
/// tests rather than by reading.
///
/// COST. Every operation is O(degree) — at most three neighbours in graphene,
/// so O(1) in practice, independent of substrate size. Free lists are kept as
/// vectors with a position index, so removal is a swap-with-last and a draw is
/// one random index. That matters: a Monte Carlo run does millions of these,
/// and a linear scan for a free site would dominate the calculation over the
/// energy evaluations it exists to feed.
///
/// WHAT THIS CLASS DOES NOT KNOW. It has no idea what a hydroxyl is, where an
/// oxygen goes, or what anything costs in energy. It tracks which carbons are
/// spoken for and which adjacency survives — the chemistry lives in
/// GrapheneOxideBuilder, and keeping the two apart is what lets the sampler
/// reuse this without dragging the geometry generator along.
class ReactiveSiteGraph {
public:
    /// `owner()` of a carbon nothing is attached to.
    static constexpr int kFree = -1;

    ReactiveSiteGraph() = default;

    /// Build from the carbon framework's adjacency and edge classification.
    ///
    /// `neighbours[c]` lists the carbons bonded to carbon c; `isEdge[c]` is
    /// nonzero for a carbon with a substitutable hydrogen (fewer than three
    /// carbon neighbours). Pair sites are enumerated from the adjacency once,
    /// here, over BASAL–BASAL bonds only: an epoxide is basal-plane chemistry,
    /// and a bridge anchored on an edge carbon is not a structure this builder
    /// produces.
    ReactiveSiteGraph(const std::vector<std::vector<int>>& neighbours,
                      const std::vector<char>& isEdge);

    int carbonCount() const { return static_cast<int>(owner_.size()); }
    bool isEdge(int carbon) const
    {
        return edge_[static_cast<std::size_t>(carbon)] != 0;
    }
    /// The tag occupying this carbon, or kFree. The tag is opaque here — the
    /// builder stores the functional group, the sampler stores whatever it
    /// needs to undo the move.
    int owner(int carbon) const { return owner_[static_cast<std::size_t>(carbon)]; }
    bool isFree(int carbon) const { return owner(carbon) == kFree; }

    // -- The pools ---------------------------------------------------------

    std::size_t freeBasalCount() const { return freeBasal_.size(); }
    std::size_t freeEdgeCount() const { return freeEdge_.size(); }
    std::size_t freePairCount() const { return freePairs_.size(); }
    /// Every basal–basal bond, free or not. The denominator when reporting how
    /// saturated the substrate is.
    std::size_t totalPairCount() const { return pairs_.size(); }

    // -- Mutation ----------------------------------------------------------

    /// Occupy one carbon. Precondition: it is free.
    ///
    /// Removes it from its region's free list and withdraws every pair that
    /// contains it — including pairs whose OTHER carbon is still free, which is
    /// the case that is easy to forget and produces two groups sharing a
    /// carbon.
    void occupySingle(int carbon, int tag);

    /// Occupy a bonded pair with one group. Precondition: both are free and
    /// bonded to each other.
    void occupyPair(int a, int b, int tag);

    /// Release one carbon back to the pool.
    ///
    /// A pair returns to the free list only when BOTH its carbons are free, so
    /// releasing one end of an epoxide site whose other end now carries a
    /// hydroxyl correctly revives nothing.
    void releaseSingle(int carbon);
    /// Release both carbons of a pair.
    void releasePair(int a, int b);

    // -- Drawing -----------------------------------------------------------
    //
    // Uniform over what is currently available, which is the distribution a
    // Metropolis move needs: any bias here is a bias in the sampled ensemble,
    // not merely in the starting structure.

    std::optional<int> drawFreeBasal(std::mt19937& rng) const;
    std::optional<std::pair<int, int>> drawFreePair(std::mt19937& rng) const;

    /// Are these two carbons bonded to each other? Used to validate a move
    /// that names a pair rather than drawing one.
    bool bonded(int a, int b) const;

    /// Every invariant this class promises, checked at once. Returns true when
    /// the free lists agree with `owner_` and with the adjacency.
    ///
    /// Exposed rather than kept in the test file because it is the cheapest
    /// possible guard for a long Monte Carlo run: the structures are big, the
    /// moves are millions, and a corrupted pool shows up as chemistry that is
    /// subtly wrong rather than as a crash. A caller can afford to assert this
    /// every few thousand moves.
    bool consistent() const;

private:
    void withdrawPair(std::size_t pairIndex);
    void restorePair(std::size_t pairIndex);

    std::vector<int> owner_;
    std::vector<char> edge_;
    std::vector<std::vector<int>> neighbours_;

    /// All basal–basal bonds, indexed once and never reordered — every other
    /// structure here refers to a pair by this index.
    std::vector<std::pair<int, int>> pairs_;
    /// Indices into `pairs_` for the pairs containing each carbon.
    std::vector<std::vector<int>> pairsTouching_;

    std::vector<int> freeBasal_;
    std::vector<int> freeEdge_;
    /// Position of each carbon within freeBasal_/freeEdge_, or -1 when it is
    /// occupied. This is what turns removal from a scan into a swap.
    std::vector<int> singleSlot_;

    std::vector<std::pair<int, int>> freePairs_;
    /// Position of each pair within freePairs_, or -1 when withdrawn.
    std::vector<int> pairSlot_;
};

} // namespace calango::core
