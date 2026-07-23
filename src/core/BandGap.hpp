#pragma once

#include <cstddef>
#include <vector>

namespace calango::core {

/// Band-gap analysis of a computed band structure.
struct BandGapInfo {
    /// No gap: at least one band crosses the Fermi level, so the system
    /// conducts. Every other field is meaningless when this is true.
    bool metallic = false;
    /// Nothing could be determined (empty bands, or every eigenvalue on one
    /// side of E_F — e.g. a band range that does not bracket the gap).
    bool valid = false;

    double gap = 0.0;        ///< fundamental gap, CBM − VBM (eV)
    double vbm = 0.0;        ///< valence band maximum (eV, absolute)
    double cbm = 0.0;        ///< conduction band minimum (eV, absolute)
    std::size_t vbmKPoint = 0;
    std::size_t cbmKPoint = 0;
    std::size_t vbmSpin = 0;
    std::size_t cbmSpin = 0;

    /// True when the VBM and CBM sit at the same k-point.
    bool direct = false;
    /// Smallest vertical (same-k) gap anywhere on the path, and where it is.
    /// For an indirect material this is the optical onset, which is larger
    /// than the fundamental gap — worth reporting alongside it.
    double directGap = 0.0;
    std::size_t directKPoint = 0;
};

/// Locate the fundamental gap in `energies[spin][kpoint][band]` (eV,
/// absolute) given the Fermi level.
///
/// Convention: states at or below E_F are occupied, states above it are
/// empty — the same split every plotting tool uses, and the reason E_F must
/// be the one the calculator reported for THIS calculation. `tolerance` is
/// the width below which a gap counts as metallic (a band crossing E_F
/// leaves VBM and CBM within numerical noise of each other).
///
/// Only k-points sampled on the band path are examined, so the result is the
/// gap *on that path*: a true VBM/CBM lying off the path cannot be found by
/// any band-structure plot, and the caller should say so if the path is
/// sparse.
BandGapInfo analyzeBandGap(
    const std::vector<std::vector<std::vector<double>>>& energies,
    double fermiLevel, double tolerance = 1e-3);

} // namespace calango::core
