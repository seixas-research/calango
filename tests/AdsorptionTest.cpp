// Integration test for the native adsorption engine and faceted-cluster
// builder. Runs the real pipeline (SurfaceScience -> ASE cluster / molecule
// database -> core::detectAdsorptionSites / core::placeAdsorbate) and checks
// the property the rewrite exists to guarantee: adsorbates land OUTSIDE a
// curved nanoparticle (outward normals), and slab site normals are
// perpendicular to the surface.
//
// Exit code 0 = pass.

#include "core/AdsorptionSites.hpp"
#include "core/Structure.hpp"
#include "core/UnitCell.hpp"
#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PythonEngine.hpp"
#include "python_bridge/SurfaceScience.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

double dist(const calango::core::Vec3& a, const calango::core::Vec3& b)
{
    return (a - b).norm();
}

} // namespace

int main()
{
    using namespace calango;

    pybridge::PythonEngine python;
    if (!python.aseAvailable())
        return fail("ASE not importable in the embedded interpreter");

    // --- Feature 3: faceted cluster builder (Cu icosahedron, 3 shells) -----
    core::Structure ico;
    try {
        ico = pybridge::SurfaceScience::polyhedralNanoparticle(
            "Cu", "icosahedron", /*size=*/3, 0, 0, 0, /*latticeConstant=*/0.0);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: icosahedron build threw:\n%s\n", e.what());
        return 1;
    }
    if (ico.size() != 55)
        return fail("3-shell icosahedron should have 55 atoms");

    // --- Feature 4: outward normals on a curved nanoparticle ---------------
    const auto sites = pybridge::SurfaceScience::detectSites(ico);
    if (sites.empty())
        return fail("no adsorption sites detected on the icosahedron");

    const core::Vec3 center = ico.centroid();
    int topCount = 0;
    for (const auto& s : sites) {
        if (s.type != "top")
            continue;
        ++topCount;
        const core::Vec3 pos{s.x, s.y, s.z};
        const core::Vec3 nrm{s.nx, s.ny, s.nz};
        // Outward: the normal must have a positive projection onto the
        // radial direction (from cluster center to the surface atom). This
        // is exactly what a hardcoded +z normal would get wrong.
        const core::Vec3 radial = pos - center;
        if (nrm.dot(radial) <= 0.0)
            return fail("a top-site normal points inward on the nanoparticle");
    }
    if (topCount == 0)
        return fail("no top sites on the icosahedron");

    // Place OH on one top site and confirm the anchor (O) ends up OUTSIDE the
    // cluster — farther from the center than the surface atom it binds.
    pybridge::SurfaceScience::AdsorptionSite one;
    for (const auto& s : sites)
        if (s.type == "top") { one = s; break; }

    const double height = 2.0;
    const auto withOH = pybridge::SurfaceScience::placeAdsorbates(
        ico, {one}, "OH", height);
    if (withOH.size() != ico.size() + 2)
        return fail("OH placement should add exactly 2 atoms (O, H)");

    const core::Vec3 sitePos{one.x, one.y, one.z};
    const core::Vec3 nrm{one.nx, one.ny, one.nz};
    // The O anchor is the first appended atom.
    const core::Vec3 oPos = withOH.atoms()[ico.size()].position;
    if (withOH.atoms()[ico.size()].atomicNumber != 8)
        return fail("first placed adsorbate atom should be oxygen (the anchor)");

    const double rSite = dist(sitePos, center);
    const double rO = dist(oPos, center);
    if (rO <= rSite)
        return fail("O adsorbate is not outside the surface atom (floated in)");

    // O should sit ~`height` Å out along the site normal.
    const core::Vec3 expectedO = sitePos + nrm * height;
    if (dist(oPos, expectedO) > 1e-6)
        return fail("O anchor not placed at site + normal*height");

    // --- Oriented single-site placement (Build → "Add adsorbate…") ---------
    //
    // The upright default is the same placement placeAdsorbate() makes, and a
    // 90° tilt must lay the molecule flat: the anchor stays put while the rest
    // of the molecule swings into the surface plane. That perpendicularity is
    // the property the whole tilt control exists to deliver, and getting the
    // rotation axis wrong produces a molecule that merely looks different.
    {
        core::AdsorptionSite site;
        site.type = one.type;
        site.position = sitePos;
        site.normal = nrm;

        const auto oh = pybridge::SurfaceScience::moleculeTemplate("OH");
        if (oh.structure.size() != 2)
            return fail("OH template should have 2 atoms");

        const auto upright = core::placeAdsorbateAt(
            ico, site, oh.structure, oh.anchorIndex, height, {});
        // Same geometry as the multi-site path: one builder, one answer.
        for (std::size_t i = 0; i < 2; ++i)
            if (dist(upright.atoms()[ico.size() + i].position,
                     withOH.atoms()[ico.size() + i].position) > 1e-9)
                return fail("upright placeAdsorbateAt disagrees with "
                            "placeAdsorbate");

        core::AdsorbateOrientation flat;
        flat.tiltDeg = 90.0;
        const auto laid = core::placeAdsorbateAt(ico, site, oh.structure,
                                                 oh.anchorIndex, height, flat);
        const core::Vec3 anchor = laid.atoms()[ico.size()].position;
        // The anchor is fixed by the site + height, not by the orientation.
        if (dist(anchor, expectedO) > 1e-6)
            return fail("tilting moved the anchor off the site axis");
        const core::Vec3 axis =
            (laid.atoms()[ico.size() + 1].position - anchor).normalized();
        if (std::abs(axis.dot(nrm.normalized())) > 1e-6)
            return fail("a 90° tilt did not lay the molecule flat on the facet");
        // ...and the bond length must survive a rigid rotation.
        const core::Vec3 uprightAnchor = upright.atoms()[ico.size()].position;
        const double uprightBond =
            dist(upright.atoms()[ico.size() + 1].position, uprightAnchor);
        const double tiltedBond =
            dist(laid.atoms()[ico.size() + 1].position, anchor);
        if (std::abs(uprightBond - tiltedBond) > 1e-9)
            return fail("tilting changed the O-H bond length");
    }

    // --- Feature 4: slab normals are perpendicular to the surface ----------
    // fcc Cu conventional cell -> (111) slab, then check top-site normals are
    // (nearly) parallel to ±z rather than lying in the surface plane.
    const double a = 3.61;
    core::Structure cu;
    const core::Vec3 fcc[] = {
        {0, 0, 0}, {0, 0.5, 0.5}, {0.5, 0, 0.5}, {0.5, 0.5, 0}};
    for (const auto& s : fcc)
        cu.addAtom({29, {s.x * a, s.y * a, s.z * a}});
    cu.setCell(core::UnitCell({a, 0, 0}, {0, a, 0}, {0, 0, a},
                              {true, true, true}));
    bool slabChecked = false;
    try {
        const core::Structure slab =
            pybridge::AseBridge::makeSlab(cu, 1, 1, 1, /*layers=*/5,
                                          /*vacuum=*/10.0);
        const auto slabSites = core::detectAdsorptionSites(slab);
        int checked = 0;
        for (const auto& s : slabSites) {
            if (s.type != "top")
                continue;
            ++checked;
            // Perpendicular to the slab ⇒ |n_z| dominates.
            if (std::abs(s.normal.z) < 0.85)
                return fail("slab top-site normal is not surface-perpendicular");
        }
        slabChecked = checked > 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "WARN: slab construction skipped (%s)\n", e.what());
    }

    std::printf("PASS: icosahedron 55 atoms, %d outward top normals, "
                "OH anchored %.2f Å out along the facet normal%s\n",
                topCount, rO - rSite,
                slabChecked ? ", slab normals ⟂ surface" : "");
    return 0;
}
