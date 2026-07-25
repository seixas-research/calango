#pragma once

#include "core/GridInterpolation.hpp"
#include "render/ColorMap.hpp"

#include <QColor>
#include <QStringList>
#include <QVector>

namespace calango::gui {

/// Which visualization the Volumetric Data panel renders on the main viewport.
enum class VolumetricRenderMode { Isosurface, ColorSlice, PotentialMap };

/// Appearance settings for a rendered volumetric field, shared between the
/// Volumetric Data panel (which applies it) and the "Edit Volumetric Render"
/// dialog (which edits it). The specular material term is carried for the lit
/// volume viewers; the flat main-viewport overlay honors the colormap,
/// isovalue and opacities but not the material finish.
struct VolumetricStyle {
    // -- Isosurfaces -------------------------------------------------------
    render::ColorGradient gradient = render::ColorGradient::Viridis;
    double isovalue = 0.0;      ///< absolute threshold in the field's units
    double isoOpacity = 0.85;   ///< 0 (transparent) … 1 (opaque)
    double specular = 0.30;     ///< specular material term (lit viewers only)
    QColor positiveColor = QColor(0xff, 0xb3, 0x47); ///< positive phase (+ψ)
    QColor negativeColor = QColor(0x47, 0x82, 0xff); ///< negative phase (−ψ)
    /// Grid refinement applied before marching cubes (smoother meshes).
    core::GridInterpolation gridInterpolation = core::GridInterpolation::None;

    // -- Color slice -------------------------------------------------------
    /// Plane orientation as Miller indices (h k l) relative to the grid's own
    /// lattice: the slice normal is the reciprocal-lattice vector
    /// G = h·(b×c) + k·(c×a) + l·(a×b), so (0 0 1) is the ab plane, (1 0 0) the
    /// bc plane, and any oblique family — (1 1 1), (1 -1 0) — is expressible.
    /// (0 0 0) is degenerate and falls back to the c-axis normal.
    int millerH = 0;
    int millerK = 0;
    int millerL = 1;
    double sliceOffset = 0.5;   ///< 0 … 1 sweeping the cell along the normal
    double sliceOpacity = 0.70; ///< 0 (transparent) … 1 (opaque)
    /// Reverse the value → color mapping (t → 1 − t), matplotlib's "_r" maps.
    /// Honored by every mode that samples `gradient` (slice + potential map).
    bool invertGradient = false;
    /// Explicit color-mapping bounds for the slice. Off by default, so the ramp
    /// spans the field's own min/max; on, it is pinned to [sliceMin, sliceMax]
    /// with out-of-range values clamped — which is what comparing slices from
    /// different fields (or different offsets through one field) requires, and
    /// what saturates a ramp that a few outlier voxels would otherwise flatten.
    bool sliceUseBounds = false;
    double sliceMin = 0.0;
    double sliceMax = 1.0;
    /// Voxel-grid interpolation applied to the field before it is sampled onto
    /// the slice plane. The plane already samples trilinearly between voxels;
    /// refining first is what makes a coarse grid read as a smooth field rather
    /// than visible voxel facets.
    core::GridInterpolation sliceInterpolation = core::GridInterpolation::None;

    // -- Potential map -----------------------------------------------------
    // A base isosurface (geometry) colored by a secondary scalar field mapped
    // onto its vertices via `gradient`. Indices are into the panel's dataset
    // registry; -1 for the base means "the current selection", -1 for the
    // secondary means "none" (uniform positive color).
    int potentialBaseIndex = -1;
    int potentialSecondaryIndex = -1;
    /// When true the color ramp uses [potentialMin, potentialMax] instead of
    /// the secondary field's own min/max — for electrostatic / work-function
    /// maps whose meaningful range is a chosen window.
    bool potentialUseBounds = false;
    double potentialMin = 0.0;
    double potentialMax = 1.0;
};

/// The gradients offered in the Volumetric appearance controls, in combo order:
/// Viridis, Plasma, Inferno, Magma, Cividis, Afmhot, Hot, Spectral, Greys,
/// Rainbow, Gnuplot — the perceptually-uniform matplotlib family, the
/// black-body ramps, one diverging map and one classic spectral ramp, with no
/// two entries duplicating the same ramp.
const QVector<render::ColorGradient>& volumetricGradients();
QStringList volumetricGradientNames();

} // namespace calango::gui
