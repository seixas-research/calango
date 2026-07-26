#include "core/AdsorptionSites.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace calango::core {

namespace {

struct Neighbor {
    int j;      ///< neighbor atom index
    Vec3 delta; ///< vector from atom i to the (imaged) neighbor
};

/// Lattice translations to enumerate when searching for neighbors: {-1,0,1}
/// along each periodic axis with a non-degenerate cell vector, {0} otherwise.
std::vector<Vec3> imageOffsets(const Structure& s)
{
    std::vector<Vec3> images;
    const auto& cell = s.cell();
    const auto& v = cell.vectors();
    const auto pbc = cell.pbc();
    const int ra = (pbc[0] && v[0].norm() > 1e-6) ? 1 : 0;
    const int rb = (pbc[1] && v[1].norm() > 1e-6) ? 1 : 0;
    const int rc = (pbc[2] && v[2].norm() > 1e-6) ? 1 : 0;
    for (int da = -ra; da <= ra; ++da)
        for (int db = -rb; db <= rb; ++db)
            for (int dc = -rc; dc <= rc; ++dc)
                images.push_back(v[0] * da + v[1] * db + v[2] * dc);
    if (images.empty())
        images.push_back(Vec3{}); // no periodicity at all
    return images;
}

/// Shortest vector b - a under the minimum-image convention along the
/// periodic axes (used for dedup and subsurface tests).
Vec3 minImageDelta(const Vec3& a, const Vec3& b, const std::vector<Vec3>& images)
{
    Vec3 best = b - a;
    double bestNorm2 = best.dot(best);
    for (const Vec3& img : images) {
        const Vec3 cand = (b + img) - a;
        const double n2 = cand.dot(cand);
        if (n2 < bestNorm2) {
            bestNorm2 = n2;
            best = cand;
        }
    }
    return best;
}

/// Rotate x by the rotation that takes unit vector u onto unit vector v
/// (Rodrigues' formula). Handles the u == ±v degeneracies.
Vec3 rotateAlign(const Vec3& u, const Vec3& v, const Vec3& x)
{
    const double c = std::clamp(u.dot(v), -1.0, 1.0);
    if (c > 0.999999)
        return x; // already aligned
    if (c < -0.999999) {
        // 180°: reflect through the plane perpendicular to u. Pick any axis
        // not parallel to u to build a perpendicular rotation axis.
        Vec3 ref = std::abs(u.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        const Vec3 axis = u.cross(ref).normalized();
        // Rotation by π about `axis`: R x = 2 (axis·x) axis - x.
        return axis * (2.0 * axis.dot(x)) - x;
    }
    const Vec3 w = u.cross(v);       // = sin(theta) * axis
    const Vec3 kx = w.cross(x);      // K x
    const Vec3 kkx = w.cross(kx);    // K^2 x
    return x + kx + kkx * (1.0 / (1.0 + c));
}

/// Rotate `x` by `angle` radians about the unit axis `axis` (Rodrigues).
Vec3 rotateAbout(const Vec3& axis, double angle, const Vec3& x)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return x * c + axis.cross(x) * s + axis * (axis.dot(x) * (1.0 - c));
}

/// Any unit vector perpendicular to `n`. Which one does not matter — it only
/// fixes where azimuth = 0 points, and the user turns the azimuth to taste.
Vec3 perpendicularTo(const Vec3& n)
{
    const Vec3 ref = std::abs(n.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    return n.cross(ref).normalized();
}

/// The adsorbate's intrinsic axis: anchor → centroid of the remaining atoms,
/// i.e. the direction that should point AWAY from the surface. A single atom
/// has none, so +z stands in and every rotation below is a no-op on it.
Vec3 intrinsicAxis(const std::vector<Atom>& molAtoms, int anchor)
{
    if (molAtoms.size() < 2)
        return {0, 0, 1};
    const Vec3 anchorPos = molAtoms[static_cast<std::size_t>(anchor)].position;
    Vec3 mean{};
    for (int a = 0; a < static_cast<int>(molAtoms.size()); ++a)
        if (a != anchor)
            mean += molAtoms[static_cast<std::size_t>(a)].position;
    mean = mean * (1.0 / static_cast<double>(molAtoms.size() - 1));
    const Vec3 axis = (mean - anchorPos).normalized();
    return axis.norm() > 1e-6 ? axis : Vec3{0, 0, 1};
}

} // namespace

std::vector<AdsorptionSite> detectAdsorptionSites(
    const Structure& structure, const AdsorptionSiteOptions& options)
{
    const auto& atoms = structure.atoms();
    const int n = static_cast<int>(atoms.size());
    std::vector<AdsorptionSite> result;
    if (n == 0)
        return result;

    const std::vector<Vec3> images = imageOffsets(structure);

    // --- Neighbor lists (periodic-image aware) -----------------------------
    std::vector<std::vector<Neighbor>> nbr(n);
    double dmin = std::numeric_limits<double>::max();
    for (int i = 0; i < n; ++i) {
        const double ri = atoms[i].covalentRadius();
        for (int j = 0; j < n; ++j) {
            const double cutoff =
                options.cutoffTolerance * (ri + atoms[j].covalentRadius());
            const double cutoff2 = cutoff * cutoff;
            for (const Vec3& img : images) {
                const Vec3 d = (atoms[j].position + img) - atoms[i].position;
                const double d2 = d.dot(d);
                if (d2 > 1e-8 && d2 < cutoff2) {
                    nbr[i].push_back({j, d});
                    dmin = std::min(dmin, std::sqrt(d2));
                }
            }
        }
    }
    if (dmin == std::numeric_limits<double>::max())
        return result; // no bonds at all — nothing to do

    // --- Coordination, surface flags, outward normals ----------------------
    std::vector<int> cn(n);
    int cnMax = 0;
    int cnMin = std::numeric_limits<int>::max();
    for (int i = 0; i < n; ++i) {
        cn[i] = static_cast<int>(nbr[i].size());
        cnMax = std::max(cnMax, cn[i]);
        cnMin = std::min(cnMin, cn[i]);
    }
    // A slab/nanoparticle has an interior (fully coordinated) core against
    // which the surface stands out. A monolayer, a molecule, or a symmetric
    // two-layer slab has no such core (every atom equally coordinated) — then
    // treat every atom as surface so sites still form.
    const bool hasInterior = cnMin < cnMax;

    // Global "thin axis" (smallest bounding-box extent) is the fallback normal
    // direction for atoms whose neighbor cloud is too symmetric to orient
    // (e.g. atoms in a flat sheet), signed per atom away from the centroid.
    Vec3 lo = atoms[0].position, hi = atoms[0].position;
    for (const Atom& a : atoms) {
        lo.x = std::min(lo.x, a.position.x); hi.x = std::max(hi.x, a.position.x);
        lo.y = std::min(lo.y, a.position.y); hi.y = std::max(hi.y, a.position.y);
        lo.z = std::min(lo.z, a.position.z); hi.z = std::max(hi.z, a.position.z);
    }
    const Vec3 extent = hi - lo;
    Vec3 thinAxis{0, 0, 1};
    if (extent.z <= extent.x && extent.z <= extent.y) thinAxis = {0, 0, 1};
    else if (extent.y <= extent.x && extent.y <= extent.z) thinAxis = {0, 1, 0};
    else thinAxis = {1, 0, 0};

    const Vec3 center = structure.centroid();
    std::vector<Vec3> normal(n, Vec3{0, 0, 1});
    std::vector<char> surface(n, 0);
    for (int i = 0; i < n; ++i) {
        surface[i] = (!hasInterior || cn[i] < cnMax) ? 1 : 0;
        if (!surface[i])
            continue;
        Vec3 sum{};
        for (const Neighbor& nb : nbr[i])
            sum += nb.delta;
        // Outward = away from the mean neighbor direction.
        Vec3 nrm = (sum * (-1.0)).normalized();
        if (nrm.norm() < 1e-6) {
            // Symmetric neighbor cloud: use the thin axis, oriented outward.
            const double side = (atoms[i].position - center).dot(thinAxis);
            nrm = thinAxis * (side >= 0.0 ? 1.0 : -1.0);
        }
        if (nrm.norm() < 1e-6)
            nrm = (atoms[i].position - center).normalized();
        if (nrm.norm() < 1e-6)
            nrm = Vec3{0, 0, 1};
        normal[i] = nrm;
    }

    // Whether any atom sits "below" a facet point (used to decide fcc/hcp vs
    // a generic hollow on a single-layer sheet).
    auto subsurfaceKind = [&](const Vec3& centroid, const Vec3& nrm,
                              const std::array<int, 3>& tri) -> std::string {
        bool anyBelow = false;
        bool directlyUnder = false;
        for (int m = 0; m < n; ++m) {
            if (m == tri[0] || m == tri[1] || m == tri[2])
                continue;
            const Vec3 w = minImageDelta(centroid, atoms[m].position, images);
            const double along = w.dot(nrm);
            if (along >= -0.3)
                continue; // not clearly below the facet
            anyBelow = true;
            const Vec3 inPlane = w - nrm * along;
            if (inPlane.norm() < 0.5 * dmin) {
                directlyUnder = true;
                break;
            }
        }
        if (!anyBelow)
            return "hollow"; // single layer: no fcc/hcp distinction
        return directlyUnder ? "hcp" : "fcc";
    };

    // --- Generate candidate sites (over-generate, dedup afterwards) --------
    std::vector<AdsorptionSite> candidates;

    // Top: one per surface atom.
    for (int i = 0; i < n; ++i)
        if (surface[i])
            candidates.push_back({"top", atoms[i].position, normal[i], {i}});

    // Bridge + threefold hollow.
    for (int i = 0; i < n; ++i) {
        if (!surface[i])
            continue;
        const auto& ni = nbr[i];
        for (std::size_t a = 0; a < ni.size(); ++a) {
            const int j = ni[a].j;
            if (!surface[j])
                continue;
            const Vec3& nj = normal[j];
            if (normal[i].dot(nj) < options.sameFacetCos)
                continue; // opposite facet

            // Bridge (add once per unordered atom pair; self-image bridges
            // included with j == i, and the dedup pass collapses copies).
            if (j >= i) {
                const Vec3 mid = atoms[i].position + ni[a].delta * 0.5;
                candidates.push_back(
                    {"bridge", mid, (normal[i] + nj).normalized(), {i, j}});
            }

            // Hollow: second neighbor k that is also a neighbor of j.
            for (std::size_t b = a + 1; b < ni.size(); ++b) {
                const int k = ni[b].j;
                if (!surface[k] || normal[i].dot(normal[k]) < options.sameFacetCos)
                    continue;
                // j and k must be mutual neighbors (share a facet edge).
                const Vec3 jk = ni[b].delta - ni[a].delta;
                if (jk.norm() > dmin * 1.3)
                    continue;
                const Vec3 centroid =
                    atoms[i].position + (ni[a].delta + ni[b].delta) * (1.0 / 3.0);
                Vec3 nrm = (normal[i] + normal[j] + normal[k]).normalized();
                if (nrm.norm() < 1e-6)
                    nrm = normal[i];
                const std::string kind =
                    subsurfaceKind(centroid, nrm, {i, j, k});
                candidates.push_back({kind, centroid, nrm, {i, j, k}});
            }
        }
    }

    // --- Deduplicate periodic / geometric copies (per type) ----------------
    for (const AdsorptionSite& cand : candidates) {
        bool duplicate = false;
        for (const AdsorptionSite& kept : result) {
            if (kept.type != cand.type)
                continue;
            const Vec3 d =
                minImageDelta(kept.position, cand.position, images);
            if (d.norm() < options.dedupTolerance) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            result.push_back(cand);
    }
    return result;
}

Structure placeAdsorbate(const Structure& substrate,
                         const std::vector<AdsorptionSite>& sites,
                         const Structure& molecule, int anchorIndex,
                         double height)
{
    Structure result = substrate;
    const auto& molAtoms = molecule.atoms();
    if (molAtoms.empty())
        return result;

    const int anchor = (anchorIndex >= 0
                        && anchorIndex < static_cast<int>(molAtoms.size()))
        ? anchorIndex
        : 0;
    const Vec3 anchorPos = molAtoms[anchor].position;

    // Intrinsic molecule axis: anchor → centroid of the remaining atoms. This
    // is the direction we align to the outward surface normal so the molecule
    // points away from the surface. Single atoms need no orientation.
    Vec3 up{0, 0, 1};
    if (molAtoms.size() > 1) {
        Vec3 mean{};
        for (int a = 0; a < static_cast<int>(molAtoms.size()); ++a)
            if (a != anchor)
                mean += molAtoms[a].position;
        mean = mean * (1.0 / static_cast<double>(molAtoms.size() - 1));
        Vec3 axis = (mean - anchorPos).normalized();
        if (axis.norm() > 1e-6)
            up = axis;
    }

    for (const AdsorptionSite& site : sites) {
        const Vec3 normal = site.normal.normalized();
        const Vec3 base = site.position + normal * height; // anchor lands here
        for (const Atom& a : molAtoms) {
            const Vec3 rel = a.position - anchorPos;
            const Vec3 rotated = rotateAlign(up, normal, rel);
            Atom placed;
            placed.atomicNumber = a.atomicNumber;
            placed.position = base + rotated;
            result.addAtom(placed);
        }
    }
    return result;
}

Structure placeAdsorbateAt(const Structure& substrate,
                           const AdsorptionSite& site, const Structure& molecule,
                           int anchorIndex, double height,
                           const AdsorbateOrientation& orientation)
{
    Structure result = substrate;
    const auto& molAtoms = molecule.atoms();
    if (molAtoms.empty())
        return result;

    const int anchor = (anchorIndex >= 0
                        && anchorIndex < static_cast<int>(molAtoms.size()))
        ? anchorIndex
        : 0;
    const Vec3 anchorPos = molAtoms[static_cast<std::size_t>(anchor)].position;
    const Vec3 up = intrinsicAxis(molAtoms, anchor);

    const Vec3 normal = site.normal.normalized();
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    const double tilt = orientation.tiltDeg * kDegToRad;
    const double azimuth = orientation.azimuthDeg * kDegToRad;

    // Target axis: the outward normal tilted by `tilt` toward the in-plane
    // direction picked out by `azimuth`. At tilt = 0 this is the normal itself
    // and the placement is the classic upright one.
    const Vec3 e1 = perpendicularTo(normal);
    const Vec3 e2 = normal.cross(e1).normalized();
    const Vec3 lean = e1 * std::cos(azimuth) + e2 * std::sin(azimuth);
    const Vec3 target =
        (normal * std::cos(tilt) + lean * std::sin(tilt)).normalized();

    const double roll = orientation.rollDeg * kDegToRad;
    // Height is measured along the SURFACE NORMAL, not along the molecule's
    // tilted axis: "3 Å above the surface" is a statement about the surface.
    const Vec3 base = site.position + normal * height;

    for (const Atom& a : molAtoms) {
        const Vec3 rel = a.position - anchorPos;
        Vec3 rotated = rotateAlign(up, target, rel);
        if (std::abs(roll) > 1e-12)
            rotated = rotateAbout(target, roll, rotated);
        Atom placed;
        placed.atomicNumber = a.atomicNumber;
        placed.position = base + rotated;
        result.addAtom(placed);
    }
    return result;
}

} // namespace calango::core
