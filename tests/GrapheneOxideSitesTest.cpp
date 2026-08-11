// Reactive-site graph: the pool bookkeeping the Monte Carlo sampler stands on.
//
// The interesting failures here are all COUPLING failures — a single site and
// a pair site disagreeing about whether a carbon is free. They do not crash;
// they produce a carbon carrying two functional groups, which is a pentavalent
// carbon, which is chemistry that cannot exist. So every case below drives the
// two site types against each other rather than testing them separately.

#include "core/GrapheneOxideSites.hpp"

#include <cstdio>
#include <random>
#include <set>
#include <vector>

using calango::core::ReactiveSiteGraph;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition)
        ++failures;
}

/// A linear chain of `n` carbons, all basal: 0-1-2-...-(n-1).
/// n-1 bonds, so n-1 pair sites, and every interior carbon is in two of them —
/// which is the overlap that makes the coupling non-trivial.
ReactiveSiteGraph chain(int n, const std::vector<int>& edges = {})
{
    std::vector<std::vector<int>> neighbours(static_cast<std::size_t>(n));
    for (int i = 0; i + 1 < n; ++i) {
        neighbours[static_cast<std::size_t>(i)].push_back(i + 1);
        neighbours[static_cast<std::size_t>(i + 1)].push_back(i);
    }
    std::vector<char> isEdge(static_cast<std::size_t>(n), 0);
    for (int e : edges)
        isEdge[static_cast<std::size_t>(e)] = 1;
    return ReactiveSiteGraph(neighbours, isEdge);
}

} // namespace

int main()
{
    std::printf("Reactive site graph:\n");

    {
        auto graph = chain(5);
        check(graph.freeBasalCount() == 5, "every carbon starts free");
        check(graph.totalPairCount() == 4, "a 5-chain has 4 bonds");
        check(graph.freePairCount() == 4, "and all 4 are sites to begin with");
        check(graph.consistent(), "a fresh graph is self-consistent");
    }

    {
        // Each bond must be registered ONCE. Walking the adjacency naively
        // sees i-j and j-i and doubles the epoxide capacity of the substrate.
        auto graph = chain(4);
        check(graph.totalPairCount() == 3, "bonds are not double counted");
    }

    {
        // THE case this class exists for: occupying a single carbon must kill
        // every pair containing it, including the pair whose OTHER carbon is
        // still perfectly free.
        auto graph = chain(5);
        graph.occupySingle(2, 7);
        check(graph.freeBasalCount() == 4, "the occupied carbon left the pool");
        check(graph.owner(2) == 7, "the tag is what the caller stored");
        // Bonds 1-2 and 2-3 are gone; 0-1 and 3-4 survive.
        check(graph.freePairCount() == 2,
              "both pairs touching the occupied carbon were withdrawn");
        check(graph.consistent(), "consistent after a single occupation");
    }

    {
        // A pair occupation must remove BOTH carbons from the single pool, and
        // every pair either of them touches.
        auto graph = chain(5);
        graph.occupyPair(1, 2, 3);
        check(graph.freeBasalCount() == 3, "both carbons left the single pool");
        check(graph.owner(1) == 3 && graph.owner(2) == 3,
              "both carbons carry the pair's tag");
        // 0-1, 1-2, 2-3 all die; only 3-4 remains.
        check(graph.freePairCount() == 1,
              "a pair occupation withdraws the neighbouring pairs too");
        check(graph.consistent(), "consistent after a pair occupation");
    }

    {
        // Release must restore EXACTLY what occupation removed — no more.
        // A pair revives only when both ends are free, and getting that wrong
        // in the permissive direction is what puts two groups on one carbon.
        // Bonds of a 5-chain: 0-1, 1-2, 2-3, 3-4. Occupying the ADJACENT pair
        // of carbons 1 and 2 kills three of them and leaves 3-4.
        auto graph = chain(5);
        graph.occupySingle(1, 1);
        graph.occupySingle(2, 1);
        check(graph.freePairCount() == 1, "only the bond touching neither survives");

        // Releasing carbon 1 revives 0-1, whose partner 0 is free — but NOT
        // 1-2, whose partner 2 is still occupied. Reviving 1-2 here is the
        // permissive mistake, and it ends with two groups sharing carbon 2.
        graph.releaseSingle(1);
        check(graph.freePairCount() == 2,
              "releasing one carbon revives only the pair whose partner is free");
        check(graph.freeBasalCount() == 4, "and returns it to the single pool");
        graph.releaseSingle(2);
        check(graph.freePairCount() == 4, "releasing the second revives the rest");
        check(graph.consistent(), "consistent after releases");
    }

    {
        // Round trip: occupy and release must return the graph to a state
        // indistinguishable from the start. This is the property the Monte
        // Carlo state reversion depends on — a rejected move must leave no
        // trace, and a pool that drifts by one site per rejected move is a
        // sampler that quietly stops being ergodic.
        auto graph = chain(6);
        const std::size_t basal0 = graph.freeBasalCount();
        const std::size_t pairs0 = graph.freePairCount();
        std::mt19937 rng(12345);
        for (int trial = 0; trial < 2000; ++trial) {
            const auto pair = graph.drawFreePair(rng);
            const auto single = graph.drawFreeBasal(rng);
            if (pair) {
                graph.occupyPair(pair->first, pair->second, 0);
                graph.releasePair(pair->first, pair->second);
            }
            if (single) {
                graph.occupySingle(*single, 0);
                graph.releaseSingle(*single);
            }
        }
        check(graph.freeBasalCount() == basal0 && graph.freePairCount() == pairs0,
              "2000 occupy/release round trips leave the pools exactly as found");
        check(graph.consistent(), "and self-consistent");
    }

    {
        // A long random walk of interleaved occupations and releases, with the
        // invariant checked throughout. This is the case hand-reasoning misses:
        // the states reachable after a few hundred mixed moves are not the ones
        // anyone writes a targeted test for.
        auto graph = chain(24, {0, 23});
        std::mt19937 rng(99);
        std::vector<std::pair<int, int>> heldPairs;
        std::vector<int> heldSingles;
        bool invariant = true;
        bool exclusive = true;
        for (int step = 0; step < 20000 && invariant && exclusive; ++step) {
            std::uniform_int_distribution<int> what(0, 3);
            switch (what(rng)) {
            case 0:
                if (auto pair = graph.drawFreePair(rng)) {
                    graph.occupyPair(pair->first, pair->second, 1);
                    heldPairs.push_back(*pair);
                }
                break;
            case 1:
                if (auto single = graph.drawFreeBasal(rng)) {
                    graph.occupySingle(*single, 2);
                    heldSingles.push_back(*single);
                }
                break;
            case 2:
                if (!heldPairs.empty()) {
                    std::uniform_int_distribution<std::size_t> pick(
                        0, heldPairs.size() - 1);
                    const std::size_t index = pick(rng);
                    graph.releasePair(heldPairs[index].first,
                                      heldPairs[index].second);
                    heldPairs.erase(heldPairs.begin()
                                    + static_cast<long>(index));
                }
                break;
            default:
                if (!heldSingles.empty()) {
                    std::uniform_int_distribution<std::size_t> pick(
                        0, heldSingles.size() - 1);
                    const std::size_t index = pick(rng);
                    graph.releaseSingle(heldSingles[index]);
                    heldSingles.erase(heldSingles.begin()
                                      + static_cast<long>(index));
                }
                break;
            }
            invariant = graph.consistent();
            // No carbon may be spoken for twice — the whole point.
            std::set<int> occupied;
            for (const auto& pair : heldPairs) {
                exclusive = exclusive && occupied.insert(pair.first).second
                    && occupied.insert(pair.second).second;
            }
            for (int carbon : heldSingles)
                exclusive = exclusive && occupied.insert(carbon).second;
        }
        check(invariant, "20000 mixed moves keep every pool invariant");
        check(exclusive, "no carbon is ever occupied by two groups at once");
    }

    {
        // Edge carbons form no pair sites: an epoxide is basal chemistry, and a
        // bridge anchored on the rim is not a structure this builder makes.
        // Carbons 0 and 4 are the rim. Of the four bonds, only 1-2 and 2-3 join
        // two basal carbons, so those are the only pair sites: 0-1 and 3-4 each
        // have a rim carbon at one end.
        auto graph = chain(5, {0, 4});
        check(graph.freeEdgeCount() == 2 && graph.freeBasalCount() == 3,
              "edge and basal carbons land in separate pools");
        check(graph.totalPairCount() == 2,
              "a bond with a rim carbon at either end is not an epoxide site");
        graph.occupySingle(0, 5);
        check(graph.freeEdgeCount() == 1, "occupying an edge carbon draws from the edge pool");
        check(graph.freePairCount() == 2,
              "and cannot disturb the basal pairs, being in none of them");
        check(graph.consistent(), "consistent with mixed regions");
    }

    std::printf(failures == 0 ? "\nAll site-graph checks passed.\n"
                              : "\n%d site-graph check(s) FAILED.\n",
                failures);
    return failures == 0 ? 0 : 1;
}
