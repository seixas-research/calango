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
    int slicePlane = 0;         ///< 0 = XY, 1 = XZ, 2 = YZ
    double sliceOffset = 0.5;   ///< 0 … 1 along the plane normal
    double sliceOpacity = 0.70; ///< 0 (transparent) … 1 (opaque)

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
/// Viridis, Plasma, Coolwarm, Rainbow, Greys.
const QVector<render::ColorGradient>& volumetricGradients();
QStringList volumetricGradientNames();

} // namespace calango::gui
