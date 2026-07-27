#pragma once

#include "core/Structure.hpp"
#include "core/Vec3.hpp"
#include "render/StructureRenderer.hpp"

#include <QColor>
#include <QFont>
#include <QString>

#include <vector>

namespace calango::gui {

/// One entry of the "Additional overlays" dock: anything drawn over the atomic
/// structure that is not the structure itself.
///
/// A single struct with a `kind` tag rather than a class hierarchy. The overlay
/// list is edited, reordered and round-tripped through one dialog, and the
/// fields genuinely overlap — every kind has a position, a colour and an
/// opacity — so a hierarchy would buy virtual dispatch at the cost of making
/// the list heterogeneous and the editor a cast-fest. The unused fields of a
/// given kind cost a few dozen bytes each.
///
/// This replaces the two standalone dialogs (Lattice Plane Settings and the
/// Custom Overlay Manager), which each owned their own private list, their own
/// viewport channel and their own idea of what an overlay was. A user wanting
/// a plane and a sphere had to keep two modeless windows open and could not see
/// them in one place.
struct Overlay {
    /// Enum order is the type-combo order in the edit dialog.
    enum class Kind {
        LatticePlane, ///< (hkl) crystallographic plane, optionally field-sliced
        Text,         ///< a painted annotation pinned to a scene position
        Box,
        Sphere,
        Ellipsoid,
        Tube,         ///< cylinder between two points
        Cone,
        Plane,        ///< free plane, oriented by an explicit normal
        Disk,
    };

    /// How a primitive's surface is tinted.
    enum class TextureStyle { Solid, Checkerboard, Wireframe, Glassy, Gradient };
    /// Smooth, or displaced by a periodic ripple.
    enum class SurfaceFinish { Smooth, Corrugated };

    /// Stable identity, so the viewport can report "this text was dragged"
    /// without the dock having to match on position or index.
    int id = 0;
    Kind kind = Kind::Sphere;
    QString name;
    bool visible = true;

    // -- shared ------------------------------------------------------------
    core::Vec3 center{0, 0, 0}; ///< centre / origin / text anchor
    QColor color{210, 180, 90};
    /// Overall transparency of the whole overlay, in [0, 1]. For a primitive
    /// it is the fill alpha; for text it scales BOTH the glyphs and their
    /// background, so one control fades the annotation as a unit.
    double opacity = 0.6;

    // -- primitives --------------------------------------------------------
    core::Vec3 size{2, 2, 2};        ///< box dimensions / ellipsoid radii
    core::Vec3 endPoint{0, 0, 4};    ///< tube / cone far end
    core::Vec3 normal{0, 0, 1};      ///< plane / disk normal
    core::Vec3 rotationDeg{0, 0, 0}; ///< box Euler rotation (deg, XYZ)
    double radius = 1.5;             ///< tube / cone / disk / plane extent
    QColor color2{70, 90, 160};      ///< checkerboard 2nd / gradient end
    TextureStyle texture = TextureStyle::Glassy;
    SurfaceFinish finish = SurfaceFinish::Smooth;
    int resolution = 32;

    // -- lattice plane -----------------------------------------------------
    /// Miller indices (h, k, l). The plane normal is the reciprocal-lattice
    /// vector G = h·b1 + k·b2 + l·b3.
    int miller[3] = {0, 0, 1};
    double offset = 0.0;    ///< displacement along the normal (Å)
    /// Half-width and half-height of the drawn quad (Å), along the plane's own
    /// in-plane axes. Two dimensions rather than one `extent`, because a plane
    /// cutting a slab wants to span the surface without running metres into the
    /// vacuum above it.
    double width = 10.0;
    double height = 10.0;
    bool showEdges = true;

    // Volumetric colour-slicing was removed from this overlay. It duplicated
    // the Volumetric Data dock, which owns field loading, its own colour ramp
    // and the isosurface view — two places to load the same .cube, and only one
    // of them could show it properly. A plane here is a geometric annotation;
    // slicing a field is the Volumetric dock's job.

    // -- text --------------------------------------------------------------
    /// May contain newlines: the editor is a multi-line box, and an annotation
    /// is routinely two or three lines (a label plus a value plus a unit).
    QString text;
    QFont font;
    /// Pill drawn behind the glyphs. Its own colour and its own alpha, so a
    /// label can sit on a solid plate over busy geometry or float with no
    /// backing at all.
    QColor backgroundColor{20, 22, 26};
    /// Background alpha in [0, 1]. Separate from `opacity` below, which fades
    /// the whole overlay: a fully opaque label on a barely-there plate is a
    /// common annotation style and needs the two to move independently.
    double backgroundOpacity = 0.6;

    /// Human-readable type name, for the list and the dialog's combo.
    static QString kindName(Kind kind);
    /// The label shown in the dock's list.
    QString displayName() const;
    /// True for the kinds drawn as GL geometry (everything but Text, which is
    /// painted over the canvas).
    bool isGeometry() const { return kind != Kind::Text; }
};

/// Append one overlay's geometry to the shared streams.
///
/// `faces` / `edges` are interleaved pos(3)+color(3); `ranges` gains one entry
/// per overlay so each blends at its own opacity. `structure` is needed only by
/// LatticePlane (its normal is defined by the lattice); a null structure, or
/// one with no cell, contributes nothing rather than guessing an orientation.
///
/// Text overlays append nothing — they are painted by the viewport.
void appendOverlayGeometry(
    const Overlay& overlay, const core::Structure* structure,
    std::vector<float>& faces, std::vector<float>& edges,
    std::vector<render::StructureRenderer::OverlayRange>& ranges);

} // namespace calango::gui
