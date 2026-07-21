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

    // -- Adsorption & catalysis --------------------------------------------

    struct AdsorptionSite {
        std::string type; ///< "top" | "bridge" | "fcc" | "hcp" | "hollow"
        double x = 0.0, y = 0.0, z = 0.0; ///< Cartesian site position (Å)
    };

    /// Detect high-symmetry adsorption sites on the TOP surface of a slab
    /// (periodic in-plane): top sites on outer-layer atoms, bridge sites
    /// on nearest-neighbor midpoints, and threefold hollows classified as
    /// fcc/hcp by the presence of a second-layer atom underneath.
    static std::vector<AdsorptionSite> detectSites(const core::Structure& slab);

    /// Place one adsorbate copy on each given site, `height` Å above the
    /// surface along +z, anchored by the chemically sensible atom (O for
    /// OH/H2O, C for CO/HCO, ...) with the rest of the molecule upright.
    /// `adsorbate` is an ase.build.molecule name or a chemical formula.
    static core::Structure placeAdsorbates(
        const core::Structure& slab, const std::vector<AdsorptionSite>& sites,
        const std::string& adsorbate, double height);
};

} // namespace calango::pybridge
