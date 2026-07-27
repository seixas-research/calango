#pragma once

#include "core/GridInterpolation.hpp"
#include "render/ColorMap.hpp"

#include <QColor>
#include <QStringList>
#include <QVector>

namespace calango::gui {

/// Which visualization the Volumetric Data panel renders on the main viewport.
///
/// Two, not three. "Potential map" used to be a third mode, which was a
/// mis-modelling: it IS an isosurface — the same geometry, extracted the same
/// way, at the same isovalue — differing only in what colours it. As a
/// separate mode it duplicated every isosurface control, forced the user to
/// re-pick the base field they had already selected, and made "show me this
/// surface, coloured by the potential" a mode switch rather than a checkbox.
/// It is now an option under Isosurface.
enum class VolumetricRenderMode { Isosurface, ColorSlice };

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

    /// Colour the isosurface by a second field sampled at its vertices — the
    /// electrostatic-potential map. Off, the surface takes the flat phase
    /// colours above.
    bool potentialColoring = false;

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
    /// How far the slice extends, in unit cells across: 1 draws it inside the
    /// cell only, 2 and 3 tile it over the neighbouring cells.
    ///
    /// The plane used to be drawn over the grid's bounding SPHERE, which
    /// overshoots the cell in every direction and, for anything triclinic,
    /// leaves a plane visibly larger than the structure it cuts. Clipping to
    /// whole cells makes the extent mean something, and periodic sampling
    /// makes the replicated copies real rather than a smear.
    int sliceReplicas = 1;
    /// Outline the slice quad, so its extent is visible against the structure.
    bool sliceShowBorder = false;
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

    // -- Potential-map colouring (an Isosurface option) --------------------
    // The secondary scalar field sampled at each surface vertex and mapped
    // through `gradient`. An index into the panel's dataset registry; -1 means
    // "none", which leaves the surface on its flat phase colours.
    //
    // There is no base index any more: the base IS whichever isosurface is
    // being drawn, which is both what the user already selected and what makes
    // colouring several surfaces at once possible.
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
