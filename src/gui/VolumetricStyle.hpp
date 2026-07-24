#pragma once

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

    // -- Color slice -------------------------------------------------------
    int slicePlane = 0;         ///< 0 = XY, 1 = XZ, 2 = YZ
    double sliceOffset = 0.5;   ///< 0 … 1 along the plane normal
    double sliceOpacity = 0.70; ///< 0 (transparent) … 1 (opaque)

    // -- Potential map -----------------------------------------------------
    /// When true the color ramp uses [potentialMin, potentialMax] instead of
    /// the field's own min/max — for electrostatic / work-function maps whose
    /// meaningful range is a chosen window.
    bool potentialUseBounds = false;
    double potentialMin = 0.0;
    double potentialMax = 1.0;
    /// Axis the 1D planar-average profile V̄(z) is taken along (0=x,1=y,2=z).
    int potentialAxis = 2;
};

/// The gradients offered in the Volumetric appearance controls, in combo order:
/// Viridis, Plasma, Coolwarm, Rainbow, Greys.
const QVector<render::ColorGradient>& volumetricGradients();
QStringList volumetricGradientNames();

} // namespace calango::gui
