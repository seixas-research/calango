#pragma once

#include "core/Structure.hpp"

#include <string>
#include <vector>

namespace calango::pybridge {

/// Nanoparticle construction (Wulff shapes, spherical clusters) and
/// surface adsorption tooling (site detection, adsorbate placement) via
/// the embedded interpreter. GUI-thread only; throws std::runtime_error
/// with the Python traceback on failure.
class SurfaceScience {
public:
    // -- Metallic nanoparticles --------------------------------------------

    struct WulffFacet {
        int h = 1, k = 1, l = 1;
        double energy = 1.0; ///< relative surface energy (arbitrary units)
    };

    /// Thermodynamic equilibrium shape via ase.cluster.wulff_construction:
    /// facet surface-energy ratios decide the truncation. `lattice` is
    /// "fcc", "bcc" or "sc"; latticeConstant <= 0 uses the ASE reference
    /// value for the element; `rounding` is "closest", "above" or "below".
    static core::Structure wulffNanoparticle(
        const std::string& symbol, const std::string& lattice,
        const std::vector<WulffFacet>& facets, int atomCount,
        double latticeConstant, const std::string& rounding);

    /// Spherical cluster carved from the bulk lattice ("fcc", "bcc" or
    /// "hcp"): every atom within `radiusA` of the supercell center.
    static core::Structure sphericalNanoparticle(const std::string& symbol,
                                                 const std::string& lattice,
                                                 double radiusA,
                                                 double latticeConstant);

    /// Faceted crystalline cluster via ase.cluster. `shape` selects the
    /// polyhedron (all built on an FCC reference lattice):
    ///   "icosahedron"          — ase.cluster.Icosahedron(noshells = size)
    ///   "octahedron"           — ase.cluster.Octahedron(length = size, cutoff = 0)
    ///   "cuboctahedron"        — Octahedron(length = size, cutoff = (size-1)//2)
    ///   "decahedron"           — ase.cluster.Decahedron(p, q, r)
    ///   "rhombic-dodecahedron" — FaceCenteredCubic({110} surfaces, layers = size)
    /// `size` is the primary size parameter (shells / edge length / layer
    /// count depending on the shape); `p, q, r` are the decahedron-only
    /// shell parameters and are ignored by the other shapes.
    /// latticeConstant <= 0 uses ASE's reference value for the element.
    static core::Structure polyhedralNanoparticle(const std::string& symbol,
                                                  const std::string& shape,
                                                  int size, int p, int q, int r,
                                                  double latticeConstant);

    // -- Adsorption & catalysis --------------------------------------------

    struct AdsorptionSite {
        std::string type; ///< "top" | "bridge" | "fcc" | "hcp" | "hollow"
        double x = 0.0, y = 0.0, z = 0.0; ///< Cartesian site position (Å)
        /// Outward unit surface normal at the site (+z on a slab top layer,
        /// radial on a nanoparticle facet). Drives the placement direction.
        double nx = 0.0, ny = 0.0, nz = 1.0;
    };

    /// Detect high-symmetry adsorption sites (top / bridge / fcc / hcp /
    /// hollow) with correct OUTWARD normals, via the native C++
    /// core::detectAdsorptionSites — works for both periodic slabs and
    /// finite/curved nanoparticles (Wulff, icosahedra, cuboctahedra …). The
    /// per-atom normal is the direction away from the mean of its neighbors,
    /// so adsorbates never end up inside or floating off a curved cluster.
    static std::vector<AdsorptionSite> detectSites(const core::Structure& slab);

    /// Place one adsorbate copy on each given site, `height` Å out along the
    /// site's OUTWARD normal (not a hardcoded +z), anchored by the chemically
    /// sensible atom (O for OH/H2O, C for CO/HCO, ...) with the rest of the
    /// molecule oriented away from the surface. The molecule template comes
    /// from ase.build.molecule; all placement geometry is native C++
    /// (core::placeAdsorbate). `adsorbate` is a molecule name or formula.
    static core::Structure placeAdsorbates(
        const core::Structure& slab, const std::vector<AdsorptionSite>& sites,
        const std::string& adsorbate, double height);
};

} // namespace calango::pybridge
