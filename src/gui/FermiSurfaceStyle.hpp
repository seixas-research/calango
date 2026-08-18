#pragma once

#include "core/GridInterpolation.hpp"
#include "gui/VolumetricStyle.hpp"
#include "render/ColorMap.hpp"

#include <QColor>
#include <QJsonObject>
#include <QVector>

namespace calango::gui {

/// How the sheets from bands crossing the target energy are drawn.
///
/// Extraction is unaffected either way — each band's sheet still comes from
/// its own eigenvalue grid at E_F, one core::extractIsosurface() call per
/// band. This only decides what happens to the meshes afterward: kept apart
/// (their own colour, independently toggleable — an electron pocket and a
/// hole pocket are different objects) or concatenated into one buffer with
/// one appearance, the way a reader who wants "the Fermi surface" as a
/// single shape rather than a per-band breakdown would rather see it.
enum class FermiSurfaceMeshMode { Separate, Combined };

/// What colours a Fermi-surface sheet.
///
/// ByBand only makes sense in FermiSurfaceMeshMode::Separate (there is
/// nothing to distinguish once the meshes are merged); SingleColor only in
/// Combined (there is only one surface to pick a colour for). ByVelocity
/// works in either — it colours every vertex from |grad E(k)| regardless of
/// how many mesh objects that vertex ended up in.
enum class FermiSurfaceColorMode { ByBand, SingleColor, ByVelocity };

/// Appearance settings for the Fermi-surface viewer (FermiSurfaceWindow),
/// following VolumetricStyle's shape for the controls it shares with the
/// other 3D-surface viewers (shading, opacity, gradient, smoothing) and
/// adding what is specific to a multi-band k-space sheet (mesh/colour mode,
/// per-band colours, the Brillouin-zone wireframe).
///
/// Unlike VolumetricStyle (which is live workspace-tab state, lost on
/// close), this is the first isosurface-appearance style in the app that
/// PERSISTS: see readFermiSurfaceStyle()/writeFermiSurfaceStyle(). A results
/// viewer has no workspace tab to keep it alive between reopenings, so the
/// project needs to remember it itself.
struct FermiSurfaceStyle {
    FermiSurfaceMeshMode meshMode = FermiSurfaceMeshMode::Separate;
    FermiSurfaceColorMode colorMode = FermiSurfaceColorMode::ByBand;

    // -- Material / shading (same vocabulary as the volumetric viewers) ----
    IsoShading shading = IsoShading::Glossy;
    double ambient = 0.28;  ///< matches volume.frag's previous hardcoded floor
    double specular = 0.35; ///< matches volume.frag's previous hardcoded term
    double opacity = 1.0;
    bool wireframeOverlay = false; ///< the sheet's own triangle edges, on top

    // -- Coloring ------------------------------------------------------------
    /// Per-band colour overrides, indexed by band index. An entry at an index
    /// beyond this vector's size (or an invalid QColor) falls back to the
    /// default spread — `bandGradient` sampled at index/(bandCount-1) — see
    /// bandColor() in FermiSurfaceWindow.cpp. Only ever grows to cover a band
    /// the user has explicitly recoloured; most projects keep this empty.
    QVector<QColor> perBandColors;
    render::ColorGradient bandGradient = render::ColorGradient::Turbo;
    QColor combinedColor = QColor(0x4a, 0x9e, 0xe8);
    bool invertGradient = false;
    /// Explicit bounds for velocity coloring — off, the ramp spans the
    /// crossing bands' own |grad E(k)| range. Same on/off-bounds idiom as
    /// VolumetricStyle::potentialUseBounds.
    bool velocityUseBounds = false;
    double velocityMin = 0.0;
    double velocityMax = 1.0;

    // -- Brillouin zone --------------------------------------------------
    bool showZoneEdges = true;
    QColor zoneEdgeColor = QColor(150, 152, 160);
    /// Screen-space pen width in the QPainter overlay the edges are drawn
    /// with — glLineWidth is clamped to 1px under macOS's core GL profile,
    /// same reason StructureRenderer's cell edges are tube geometry instead.
    double zoneEdgeWidth = 1.0;
    bool showAxes = true;
    bool clipToFirstZone = true;

    // -- Quality -----------------------------------------------------------
    /// Grid refinement before marching cubes — the "k-mesh density" this
    /// viewer can actually control; the ORIGINAL sampled grid is fixed by
    /// the completed Wannier interpolation.
    core::GridInterpolation interpolation = core::GridInterpolation::Trilinear;
    int refine = 2;
    /// Laplacian smoothing passes, exactly core::smoothMesh()'s `passes` —
    /// see VolumetricStyle::smoothing for why this helps a coarse k-grid.
    int meshSmoothing = 0;

    /// Energy offset from E_F the sheet is extracted at — not appearance,
    /// but a viewer setting all the same, and one a reopened viewer should
    /// not silently reset to 0.
    double energyOffset = 0.0;
};

/// Read a FermiSurfaceStyle from its sidecar JSON — the `fermi_surface_view`
/// object written by writeFermiSurfaceStyle(), or defaults for any key that
/// is absent (an older or hand-written file), clamped for any that is out of
/// range (a downgrade opening a file written by a newer version).
FermiSurfaceStyle readFermiSurfaceStyle(const QJsonObject& json);

/// The reverse: a JSON object ready to write under a `fermi_surface_view` key
/// (or, for the sidecar file, as the whole document).
QJsonObject writeFermiSurfaceStyle(const FermiSurfaceStyle& style);

} // namespace calango::gui
