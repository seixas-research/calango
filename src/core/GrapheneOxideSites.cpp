#include "core/GrapheneOxideSites.hpp"

#include <algorithm>

namespace calango::core {

namespace {

/// Swap-with-last removal from a free list, keeping the position index in step.
void removeAt(std::vector<int>& list, std::vector<int>& slot, int value)
{
    const int position = slot[static_cast<std::size_t>(value)];
    const int last = list.back();
    list[static_cast<std::size_t>(position)] = last;
    slot[static_cast<std::size_t>(last)] = position;
    list.pop_back();
    slot[static_cast<std::size_t>(value)] = -1;
}

} // namespace

ReactiveSiteGraph::ReactiveSiteGraph(
    const std::vector<std::vector<int>>& neighbours,
    const std::vector<char>& isEdge)
    : owner_(neighbours.size(), kFree)
    , edge_(isEdge)
    , neighbours_(neighbours)
{
    edge_.resize(neighbours.size(), 0);
    const int n = static_cast<int>(neighbours.size());

    // edge_ rather than isEdge() throughout this constructor: the parameter of
    // the same name shadows the member function here.
    const auto edgeFlag = [this](int c) {
        return edge_[static_cast<std::size_t>(c)] != 0;
    };

    singleSlot_.assign(neighbours.size(), -1);
    for (int c = 0; c < n; ++c) {
        std::vector<int>& pool = edgeFlag(c) ? freeEdge_ : freeBasal_;
        singleSlot_[static_cast<std::size_t>(c)] = static_cast<int>(pool.size());
        pool.push_back(c);
    }

    // Enumerate each basal-basal bond ONCE, as (min, max). Walking the
    // adjacency would otherwise see i–j and j–i and register two sites on one
    // bond, which double-counts the epoxide capacity of the whole substrate.
    pairsTouching_.assign(neighbours.size(), {});
    for (int i = 0; i < n; ++i) {
        if (edgeFlag(i))
            continue;
        for (int j : neighbours_[static_cast<std::size_t>(i)]) {
            if (j <= i || edgeFlag(j))
                continue;
            const int index = static_cast<int>(pairs_.size());
            pairs_.emplace_back(i, j);
            pairsTouching_[static_cast<std::size_t>(i)].push_back(index);
            pairsTouching_[static_cast<std::size_t>(j)].push_back(index);
        }
    }

    pairSlot_.assign(pairs_.size(), -1);
    freePairs_.reserve(pairs_.size());
    for (std::size_t p = 0; p < pairs_.size(); ++p) {
        pairSlot_[p] = static_cast<int>(freePairs_.size());
        freePairs_.push_back(pairs_[p]);
    }
}

void ReactiveSiteGraph::withdrawPair(std::size_t pairIndex)
{
    const int position = pairSlot_[pairIndex];
    if (position < 0)
        return; // already withdrawn by the other carbon of this pair
    const std::pair<int, int> last = freePairs_.back();
    freePairs_[static_cast<std::size_t>(position)] = last;
    // Find the moved pair's own index to fix its slot. The free list stores
    // endpoints rather than indices, so the moved entry is located through the
    // adjacency of one of its carbons — at most three candidates.
    for (int candidate : pairsTouching_[static_cast<std::size_t>(last.first)]) {
        if (pairs_[static_cast<std::size_t>(candidate)] == last) {
            pairSlot_[static_cast<std::size_t>(candidate)] = position;
            break;
        }
    }
    freePairs_.pop_back();
    pairSlot_[pairIndex] = -1;
}

void ReactiveSiteGraph::restorePair(std::size_t pairIndex)
{
    if (pairSlot_[pairIndex] >= 0)
        return;
    const auto& pair = pairs_[pairIndex];
    if (!isFree(pair.first) || !isFree(pair.second))
        return; // the other end is still spoken for; this is not a site yet
    pairSlot_[pairIndex] = static_cast<int>(freePairs_.size());
    freePairs_.push_back(pair);
}

void ReactiveSiteGraph::occupySingle(int carbon, int tag)
{
    if (!isFree(carbon))
        return;
    owner_[static_cast<std::size_t>(carbon)] = tag;
    removeAt(isEdge(carbon) ? freeEdge_ : freeBasal_, singleSlot_, carbon);
    for (int pairIndex : pairsTouching_[static_cast<std::size_t>(carbon)])
        withdrawPair(static_cast<std::size_t>(pairIndex));
}

void ReactiveSiteGraph::occupyPair(int a, int b, int tag)
{
    occupySingle(a, tag);
    occupySingle(b, tag);
}

void ReactiveSiteGraph::releaseSingle(int carbon)
{
    if (isFree(carbon))
        return;
    owner_[static_cast<std::size_t>(carbon)] = kFree;
    std::vector<int>& pool = isEdge(carbon) ? freeEdge_ : freeBasal_;
    singleSlot_[static_cast<std::size_t>(carbon)] = static_cast<int>(pool.size());
    pool.push_back(carbon);
    for (int pairIndex : pairsTouching_[static_cast<std::size_t>(carbon)])
        restorePair(static_cast<std::size_t>(pairIndex));
}

void ReactiveSiteGraph::releasePair(int a, int b)
{
    // Both carbons first, THEN the pair revival that releaseSingle triggers,
    // is not what happens here — releaseSingle(a) runs its own revival while b
    // is still occupied, and releaseSingle(b) then revives the shared pair. The
    // order is harmless because restorePair re-checks both ends every time.
    releaseSingle(a);
    releaseSingle(b);
}

std::optional<int> ReactiveSiteGraph::drawFreeBasal(std::mt19937& rng) const
{
    if (freeBasal_.empty())
        return std::nullopt;
    std::uniform_int_distribution<std::size_t> pick(0, freeBasal_.size() - 1);
    return freeBasal_[pick(rng)];
}

std::optional<std::pair<int, int>>
ReactiveSiteGraph::drawFreePair(std::mt19937& rng) const
{
    if (freePairs_.empty())
        return std::nullopt;
    std::uniform_int_distribution<std::size_t> pick(0, freePairs_.size() - 1);
    return freePairs_[pick(rng)];
}

bool ReactiveSiteGraph::bonded(int a, int b) const
{
    if (a < 0 || b < 0 || a >= carbonCount() || b >= carbonCount())
        return false;
    const auto& list = neighbours_[static_cast<std::size_t>(a)];
    return std::find(list.begin(), list.end(), b) != list.end();
}

bool ReactiveSiteGraph::consistent() const
{
    // 1. Every free carbon appears exactly once in exactly its region's list,
    //    and no occupied carbon appears in either.
    std::vector<int> seen(owner_.size(), 0);
    const auto walk = [&](const std::vector<int>& pool, bool edgePool) {
        for (std::size_t position = 0; position < pool.size(); ++position) {
            const int carbon = pool[position];
            if (!isFree(carbon) || isEdge(carbon) != edgePool)
                return false;
            if (singleSlot_[static_cast<std::size_t>(carbon)]
                != static_cast<int>(position))
                return false;
            ++seen[static_cast<std::size_t>(carbon)];
        }
        return true;
    };
    if (!walk(freeBasal_, false) || !walk(freeEdge_, true))
        return false;
    for (int c = 0; c < carbonCount(); ++c) {
        if (isFree(c) != (seen[static_cast<std::size_t>(c)] == 1))
            return false;
    }

    // 2. A pair is in the free list exactly when both its carbons are free.
    std::vector<int> pairSeen(pairs_.size(), 0);
    for (std::size_t position = 0; position < freePairs_.size(); ++position) {
        const auto& pair = freePairs_[position];
        if (!isFree(pair.first) || !isFree(pair.second))
            return false;
        bool located = false;
        for (int candidate :
             pairsTouching_[static_cast<std::size_t>(pair.first)]) {
            if (pairs_[static_cast<std::size_t>(candidate)] != pair)
                continue;
            if (pairSlot_[static_cast<std::size_t>(candidate)]
                != static_cast<int>(position))
                return false;
            ++pairSeen[static_cast<std::size_t>(candidate)];
            located = true;
            break;
        }
        if (!located)
            return false;
    }
    for (std::size_t p = 0; p < pairs_.size(); ++p) {
        const bool free =
            isFree(pairs_[p].first) && isFree(pairs_[p].second);
        if (free != (pairSeen[p] == 1))
            return false;
    }
    return true;
}

} // namespace calango::core
