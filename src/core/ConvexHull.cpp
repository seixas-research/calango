#include "core/ConvexHull.hpp"

#include <algorithm>
#include <cmath>

namespace calango::core {

namespace {

/// Cross product of (o->a) and (o->b). Negative => a->b turns clockwise,
/// which for points sorted by increasing x is the "keep" condition for a
/// lower hull.
double cross(const HullPoint& o, const HullPoint& a, const HullPoint& b)
{
    return (a.concentration - o.concentration) * (b.formationEnergy - o.formationEnergy)
        - (a.formationEnergy - o.formationEnergy) * (b.concentration - o.concentration);
}

bool usable(const HullPoint& p)
{
    return std::isfinite(p.concentration) && std::isfinite(p.formationEnergy);
}

} // namespace

double formationEnergyPerAtom(double energyPerAtom, double concentration,
                              double referenceA, double referenceB)
{
    const double x = std::clamp(concentration, 0.0, 1.0);
    return energyPerAtom - ((1.0 - x) * referenceA + x * referenceB);
}

ConvexHullResult computeConvexHull(std::vector<HullPoint> points)
{
    ConvexHullResult result;
    result.points = std::move(points);
    for (HullPoint& p : result.points) {
        p.onHull = false;
        p.energyAboveHull = 0.0;
    }
    if (result.points.empty())
        return result;

    // Work on indices so the caller's ordering (frame order) is preserved in
    // the returned vector while the hull is built on a sorted view.
    std::vector<std::size_t> order;
    order.reserve(result.points.size());
    for (std::size_t i = 0; i < result.points.size(); ++i) {
        if (usable(result.points[i]))
            order.push_back(i);
    }
    if (order.empty())
        return result;

    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const HullPoint& pa = result.points[a];
        const HullPoint& pb = result.points[b];
        if (pa.concentration != pb.concentration)
            return pa.concentration < pb.concentration;
        // At equal concentration the lower energy sorts first, so the
        // monotone-chain scan naturally keeps it as the vertex.
        return pa.formationEnergy < pb.formationEnergy;
    });

    // Andrew's monotone chain, lower half only.
    std::vector<std::size_t> chain;
    for (const std::size_t index : order) {
        // A duplicate concentration can only be a vertex if it is the
        // minimum there; because of the sort, the first one seen already is.
        if (!chain.empty()
            && result.points[chain.back()].concentration
                == result.points[index].concentration) {
            continue;
        }
        // Strictly-clockwise turns only. The usual geometric formulation
        // pops on `<= 0`, which also discards COLLINEAR points — but a
        // configuration sitting exactly on a tie-line has zero energy above
        // hull and is degenerate with the two-phase mixture, which the
        // materials convention (and every published hull diagram) counts as
        // stable. Popping it would report a real ground state as metastable.
        while (chain.size() >= 2
               && cross(result.points[chain[chain.size() - 2]],
                        result.points[chain.back()], result.points[index])
                   < 0.0) {
            chain.pop_back();
        }
        chain.push_back(index);
    }

    for (const std::size_t index : chain)
        result.points[index].onHull = true;
    result.hullIndices = chain;

    // Energy above hull: vertical distance to the tie-line spanning each
    // point's concentration. Points outside the hull's concentration span
    // (impossible here, since the extremes are always vertices) fall back to
    // zero.
    for (HullPoint& p : result.points) {
        if (p.onHull || !usable(p))
            continue;
        // Locate the tie-line segment containing p.concentration.
        for (std::size_t s = 0; s + 1 < chain.size(); ++s) {
            const HullPoint& left = result.points[chain[s]];
            const HullPoint& right = result.points[chain[s + 1]];
            if (p.concentration < left.concentration
                || p.concentration > right.concentration) {
                continue;
            }
            const double span = right.concentration - left.concentration;
            const double onLine = span > 1e-12
                ? left.formationEnergy
                    + (right.formationEnergy - left.formationEnergy)
                        * (p.concentration - left.concentration) / span
                : std::min(left.formationEnergy, right.formationEnergy);
            p.energyAboveHull = std::max(0.0, p.formationEnergy - onLine);
            break;
        }
        // Single-vertex hull (one distinct concentration): everything at that
        // concentration is measured against the minimum.
        if (chain.size() == 1) {
            p.energyAboveHull =
                std::max(0.0, p.formationEnergy
                                  - result.points[chain.front()].formationEnergy);
        }
    }
    return result;
}

} // namespace calango::core
