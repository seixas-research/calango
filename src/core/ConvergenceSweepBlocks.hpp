#pragma once

#include <string>
#include <string_view>

namespace calango::core {

/// The Python blocks the two convergence-sweep generators (plane-wave cutoff
/// and k-point mesh) share verbatim. Both sweeps measure the same physics —
/// ΔE per atom, vector force error, Δfmax and the eigenvalue-fingerprint MAD
/// against the last (best) point — and two copies of that definition would
/// drift apart silently, which for a convergence criterion means two tools
/// quietly disagreeing about what "converged" is.
namespace convergence_sweep {

/// The per-point measurement emitted inside the sweep loop's `try:` —
/// energy, per-atom force norms (the NORM, not the largest component),
/// the k-averaged band-energy fingerprint, the `evaluated` record, the
/// metric callback and the CALANGO_MEMBER line.
///
/// `pointLabel` is the f-string fragment naming the point in log lines
/// ("ecut={ecut:g}" / "kpts={label}"); `eigenvalueComment` carries the
/// sweep-specific rationale printed above the fingerprint (already
/// indented, newline-terminated).
std::string measurementBlock(std::string_view pointLabel,
                             std::string_view eigenvalueComment);

/// The post-loop analysis relative to the reference (last successful)
/// point: ΔE/atom, force error, Δfmax, eigenvalue MAD. `referenceComment`
/// documents why the last point is the reference (already commented,
/// newline-terminated); `failNoun` names the swept quantity in the
/// all-points-failed error ("cutoff" / "mesh").
std::string analysisBlock(std::string_view referenceComment,
                          std::string_view failNoun);

} // namespace convergence_sweep

} // namespace calango::core
