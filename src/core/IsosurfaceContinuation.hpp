#pragma once

#include "core/MarchingCubes.hpp"
#include "core/Vec3.hpp"
#include "core/VolumetricData.hpp"

namespace calango::core {

/// WHY THIS EXISTS. A Wannier function is localized in real space, but nothing
/// makes it sit conveniently inside the cell it was computed in: its centre
/// lands wherever the wannierization put it, and its tails cross the cell
/// faces. Extracting its isosurface over the home cell alone therefore cuts
/// the lobe flat at the boundary and drops the rest of it on the far side of
/// the box, where it reads as unrelated debris.
///
/// The data to fix this is already there. The cube is one period of a function
/// defined on the whole crystal, so the neighbouring images ARE the same
/// function continued — no extrapolation, no model, just the periodic
/// continuation the grid already encodes. Rebuilding the field on a window
/// centred on the Wannier centre and extracting THERE shows the function whole
/// and, because the extraction never meets the old cell face, leaves no seam
/// to stitch.

/// Halo around the home cell, in fractional cell units, when the caller does
/// not choose. 0.5 makes the window two cells across, which shows a function
/// whole no matter where in the cell its centre fell.
inline constexpr double kDefaultContinuationMargin = 0.5;
/// Ceiling on the halo. The window's voxel count — and so the extraction cost
/// — grows as (1 + 2·margin)³, which is already 125× here.
inline constexpr double kMaxContinuationMargin = 2.0;

/// Centre of |field|² on the periodic grid.
///
/// Computed as the circular mean along each grid axis rather than the ordinary
/// mean: an ordinary centre of mass of a function split across a boundary
/// lands in the middle of the box, which is exactly where the function is not.
/// This is the same construction that defines a Wannier centre in the first
/// place (Marzari-Vanderbilt take the phase of ⟨w|e^{iG·r}|w⟩), so for a
/// Wannier cube it recovers the centre the wannierization reported — which is
/// what makes it a sound fallback when no centre was recorded.
Vec3 periodicCentroid(const VolumetricData& field);

/// Rebuild `field` on a grid of the SAME spacing covering the home cell plus
/// `margin` cells on every side, centred on `centre` (Cartesian).
///
/// Values are copied at integer index offsets, so the result is the exact
/// periodic continuation of the input — no resampling, no interpolation error,
/// and every node of the window coincides with a node of some periodic image.
/// The recentring is quantized to a node for the same reason.
///
/// The result is NOT periodic in its own dimensions. Extract it with
/// FieldWrap::Clamped.
VolumetricData periodicWindow(const VolumetricData& field, const Vec3& centre,
                              double margin);

/// Drop the connected components of `mesh` that stay outside the single-cell
/// parallelepiped centred on `centre`.
///
/// A window wide enough to hold one function whole also reaches into its
/// neighbours, and drawing those would replace one clipped lobe with a
/// thicket of copies. `cell` supplies the LATTICE VECTORS (spanA/B/C of the
/// original one-period field, not of the window), which is what makes "one
/// cell away" mean the right thing on a triclinic grid.
IsoMesh keepComponentsAroundCentre(const IsoMesh& mesh,
                                   const VolumetricData& cell,
                                   const Vec3& centre);

/// Extract an isosurface of a periodic field, continued into the neighbouring
/// images so a function straddling the cell boundary is shown whole:
/// periodicWindow() → extractIsosurface(Clamped) → keepComponentsAroundCentre().
///
/// The mesh comes out of ONE extraction over ONE continuous grid, so there is
/// no seam at the former cell face — no duplicated vertices to weld, no
/// normals to reconcile, and nothing that can reintroduce the reversed-winding
/// pits the glossy isosurface shader exposes.
IsoMesh extractContinuedIsosurface(const VolumetricData& field, double isovalue,
                                   const Vec3& centre,
                                   double margin = kDefaultContinuationMargin);

} // namespace calango::core
