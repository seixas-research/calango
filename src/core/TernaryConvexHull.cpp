#include "core/TernaryConvexHull.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace calango::core {

double ternaryFormationEnergyPerAtom(double energyPerAtom, double xB, double xC,
                                     double referenceA, double referenceB,
                                     double referenceC)
{
    const double xA = 1.0 - xB - xC;
    return energyPerAtom - (xA * referenceA + xB * referenceB + xC * referenceC);
}

namespace {

/// Reduce `points` to at most `maxCandidates` indices: the lowest-energy
/// point in each cell of a triangular grid, coarsening the grid until the
/// occupied-cell count fits the cap (or the grid cannot be coarsened
/// further). See computeTernaryConvexHull()'s doc comment for why.
std::vector<std::size_t> binToCandidates(const std::vector<TernaryHullPoint>& points,
                                          int maxCandidates)
{
    int nBinsPerAxis = std::max(
        2, static_cast<int>(std::sqrt(2.0 * std::max(1, maxCandidates))) + 1);
    std::vector<std::size_t> candidates;
    for (; nBinsPerAxis >= 1; --nBinsPerAxis) {
        std::map<std::pair<int, int>, std::size_t> best;
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (!std::isfinite(points[i].formationEnergy))
                continue;
            int bx = static_cast<int>(points[i].xB * nBinsPerAxis);
            int bc = static_cast<int>(points[i].xC * nBinsPerAxis);
            bx = std::clamp(bx, 0, nBinsPerAxis - 1);
            bc = std::clamp(bc, 0, nBinsPerAxis - 1);
            const auto key = std::make_pair(bx, bc);
            const auto it = best.find(key);
            if (it == best.end()
                || points[i].formationEnergy < points[it->second].formationEnergy)
                best[key] = i;
        }
        if (static_cast<int>(best.size()) <= maxCandidates || nBinsPerAxis == 1) {
            candidates.reserve(best.size());
            for (const auto& [key, index] : best)
                candidates.push_back(index);
            break;
        }
    }
    return candidates;
}

/// Barycentric weights of (x, y) in the triangle (a, b, c); NaN-safe only in
/// that a degenerate (zero-area) triangle returns all-zero weights rather
/// than dividing by zero.
struct Barycentric {
    double w0, w1, w2;
    bool valid;
};

Barycentric barycentricWeights(double x, double y, const TernaryHullPoint& a,
                               const TernaryHullPoint& b, const TernaryHullPoint& c)
{
    const double denom =
        (b.xC - c.xC) * (a.xB - c.xB) + (c.xB - b.xB) * (a.xC - c.xC);
    if (std::abs(denom) < 1e-12)
        return {0.0, 0.0, 0.0, false};
    const double w0 = ((b.xC - c.xC) * (x - c.xB) + (c.xB - b.xB) * (y - c.xC)) / denom;
    const double w1 = ((c.xC - a.xC) * (x - c.xB) + (a.xB - c.xB) * (y - c.xC)) / denom;
    return {w0, w1, 1.0 - w0 - w1, true};
}

} // namespace

TernaryConvexHullResult computeTernaryConvexHull(
    std::vector<TernaryHullPoint> points, int maxCandidates)
{
    TernaryConvexHullResult result;
    result.points = points; // copy to annotate; `points` stays the read side below

    const std::vector<std::size_t> candidates = binToCandidates(points, maxCandidates);
    const std::size_t n = candidates.size();
    const double eps = 1e-9;

    // -- Exact lower-hull facet search over the reduced candidate set -------
    for (std::size_t a = 0; a < n; ++a) {
        for (std::size_t b = a + 1; b < n; ++b) {
            for (std::size_t c = b + 1; c < n; ++c) {
                const TernaryHullPoint& A = points[candidates[a]];
                const TernaryHullPoint& B = points[candidates[b]];
                const TernaryHullPoint& C = points[candidates[c]];
                const double ux = B.xB - A.xB, uy = B.xC - A.xC,
                             uz = B.formationEnergy - A.formationEnergy;
                const double vx = C.xB - A.xB, vy = C.xC - A.xC,
                             vz = C.formationEnergy - A.formationEnergy;
                const double nx = uy * vz - uz * vy;
                const double ny = uz * vx - ux * vz;
                const double nz = ux * vy - uy * vx;
                if (std::abs(nz) < eps)
                    continue; // projected triangle has ~zero area

                bool isLowerFacet = true;
                for (std::size_t k = 0; k < n && isLowerFacet; ++k) {
                    if (k == a || k == b || k == c)
                        continue;
                    const TernaryHullPoint& P = points[candidates[k]];
                    // Plane value at P's composition, solved from
                    // nx*dx + ny*dy + nz*dz = 0 about vertex A.
                    const double zPlane = A.formationEnergy
                        - (nx * (P.xB - A.xB) + ny * (P.xC - A.xC)) / nz;
                    if (P.formationEnergy < zPlane - eps)
                        isLowerFacet = false;
                }
                if (isLowerFacet) {
                    result.facets.push_back(
                        {candidates[a], candidates[b], candidates[c]});
                    result.points[candidates[a]].onHull = true;
                    result.points[candidates[b]].onHull = true;
                    result.points[candidates[c]].onHull = true;
                }
            }
        }
    }

    // -- Energy above hull for every point (candidate or not) ---------------
    for (std::size_t i = 0; i < points.size(); ++i) {
        TernaryHullPoint& pt = result.points[i];
        if (!std::isfinite(pt.formationEnergy))
            continue;
        double bestPlane = std::numeric_limits<double>::infinity();
        bool found = false;
        for (const auto& facet : result.facets) {
            const TernaryHullPoint& A = points[facet[0]];
            const TernaryHullPoint& B = points[facet[1]];
            const TernaryHullPoint& C = points[facet[2]];
            const Barycentric w = barycentricWeights(pt.xB, pt.xC, A, B, C);
            const double tol = -1e-6;
            if (!w.valid || w.w0 < tol || w.w1 < tol || w.w2 < tol)
                continue;
            const double z =
                w.w0 * A.formationEnergy + w.w1 * B.formationEnergy
                + w.w2 * C.formationEnergy;
            if (z < bestPlane) {
                bestPlane = z;
                found = true;
            }
        }
        // Left at 0 (the default) when no facet's projection covers this
        // point's composition — an honest gap rather than a fabricated
        // number; see the class doc comment.
        if (found)
            pt.energyAboveHull = std::max(0.0, pt.formationEnergy - bestPlane);
    }

    return result;
}

} // namespace calango::core
