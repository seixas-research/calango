#pragma once

#include "core/Structure.hpp"
#include "core/Vec3.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// A high-symmetry adsorption site with the geometry needed to place an
/// adsorbate correctly on any surface — flat or curved.
struct AdsorptionSite {
    std::string type;         ///< "top" | "bridge" | "fcc" | "hcp" | "hollow"
    Vec3 position;            ///< Cartesian anchor point on the surface (Å)
    Vec3 normal{0, 0, 1};     ///< outward unit surface normal
    std::vector<int> members; ///< substrate atom indices forming the site
};

struct AdsorptionSiteOptions {
    /// Neighbor cutoff between a pair is tol·(r_cov(i) + r_cov(j)).
    double cutoffTolerance = 1.3;
    /// Two same-type sites closer than this (Å, minimum-image) are merged.
    double dedupTolerance = 0.25;
    /// A pair/triple of surface atoms is only a bridge/hollow when their
    /// outward normals agree to better than this cosine — keeps sites on a
    /// single facet and off the opposite face of a thin slab.
    double sameFacetCos = 0.2;
};

/// Detect adsorption sites on `structure` with correct OUTWARD normals.
///
/// This is a native C++ reimplementation of the geometric core of ACAT's
/// `add_adsorbate` site model, with no dependency on the (frequently broken)
/// `acat` Python package. The essential fix over a naive slab detector: the
/// outward normal of a surface atom is taken as the direction *away from the
/// mean of its neighbors*. On a slab top layer that is +z; on a nanoparticle
/// facet it is the local radial-outward direction. Sites are therefore placed
/// on the true outer surface of icosahedra, cuboctahedra and Wulff shapes
/// instead of being pushed through a hardcoded +z axis.
///
/// Works for periodic slabs (images enumerated along the periodic axes) and
/// for finite clusters (no periodicity). Surface atoms are the
/// undercoordinated ones (coordination below the structure's maximum).
std::vector<AdsorptionSite> detectAdsorptionSites(
    const Structure& structure, const AdsorptionSiteOptions& options = {});

/// Build substrate + one copy of `molecule` on each site. The molecule is
/// rotated so its intrinsic axis (from `anchorIndex` toward the rest of the
/// molecule) aligns with the site's outward normal, and its anchor atom is
/// placed `height` Å out along that normal. Reproduces the classic "upright"
/// placement on a slab (normal = +z) while following curved facets on a
/// nanoparticle.
Structure placeAdsorbate(const Structure& substrate,
                         const std::vector<AdsorptionSite>& sites,
                         const Structure& molecule, int anchorIndex,
                         double height);

} // namespace calango::core
