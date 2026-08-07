#pragma once

#include "core/StructuralPhase.hpp"
#include "core/UnitCell.hpp"

#include "render/ColorMap.hpp"

#include <QColor>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QVector3D>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace calango::core {
class Structure;
}

class QOpenGLFunctions_3_3_Core;

namespace calango::render {

/// Per-atom vector property drawn as 3D arrows over the atomic sites.
/// Enum order is the "Vector overlay" combo order in the Representation
/// panel; each entry names one of the structure's vector fields, populated
/// from the extended-XYZ per-atom columns (or from ASE momenta, for
/// velocities).
enum class VectorOverlay {
    None,
    Velocity,       ///< "velocities" (Å/fs·√amu)
    Force,          ///< "forces" (eV/Å)
    MagneticMoment, ///< "magmoms" (μB) — non-collinear (N,3), see below
    /// "initial_magmoms" (μB) — the moments SET on the structure (Edit
    /// Structure → Spin polarization), as opposed to the ones a calculation
    /// produced.
    ///
    /// A separate entry rather than folding them into MagneticMoment, because
    /// the two answer different questions: one is an input guess, the other a
    /// result, and drawing a guess as though it were an outcome is how a
    /// seeded antiferromagnet gets reported as a converged one.
    InitialMagneticMoment,
};

/// Vector field backing an overlay, and the label/unit used in the UI.
/// Empty field name for None.
const char* vectorFieldName(VectorOverlay overlay);

/// Surface material for the instanced meshes (atoms, bonds, cell tubes).
/// Enum order is the "Surface finish" combo order in the Representation
/// panel and matches the FINISH_* constants in mesh.frag.
enum class SurfaceFinish {
    Standard, ///< Blinn-Phong with specular highlights, opaque
    Shiny,    ///< high specular, tight highlight (low roughness)
    Matte,    ///< diffuse only, no specular ("fosco")
    Glassy,   ///< alpha-blended, tight highlight, Fresnel rim
};

/// How the unit-cell wireframe is stroked.
///
/// Broken strokes are built by CUTTING each of the 12 edges into pieces on the
/// CPU, not by a line-stipple mode: core-profile GL removed glLineStipple, and
/// a shader-based dash needs a per-vertex arc-length attribute the cell buffer
/// does not carry. Splitting the geometry costs a few dozen extra vertices
/// once per rebuild and works identically on the thin-line path and the lit
/// tube path, which a fragment-stage trick would not.
enum class CellLineStyle {
    Solid,  ///< one segment per edge (the historical output)
    Dashed, ///< long marks, short gaps
    Dotted, ///< short marks, long gaps
};

enum class RepresentationMode {
    BallAndStick,
    SpaceFilling, ///< van-der-Waals-sized spheres, no bonds
    Wireframe,    ///< bonds as colored lines, isolated atoms as points
    Polyhedral,   ///< coordination polyhedra (translucent faces + edges) on
                  ///< atoms with >= 4 bonded neighbors, plus atom spheres
    // -- Macromolecular representations ------------------------------------
    // Both answer the same problem: a 15 000-atom protein drawn atom by atom
    // is an opaque hairball. They abstract it — one to the fold, one to the
    // shape — and both need the residue annotation a PDB/PDBx file carries.
    Ribbon,           ///< smooth tube through each chain's α-carbon trace
    MolecularSurface, ///< the molecule's outer envelope as a solid isosurface
    /// Uniform-thickness tubes with no atom spheres: the bonds ARE the model.
    /// Standard for organics and biomolecules, where ball-and-stick's spheres
    /// crowd together until the connectivity — the thing being looked at —
    /// disappears behind them.
    Licorice,
};

/// True for the representations that replace the per-atom geometry entirely
/// rather than decorating it: their atoms get no spheres and no bonds, because
/// the whole point is to stop drawing 15 000 of them.
constexpr bool isMacromolecularMode(RepresentationMode mode)
{
    return mode == RepresentationMode::Ribbon
        || mode == RepresentationMode::MolecularSurface;
}

/// One directional light, defined in VIEW space (camera-relative), so the
/// lighting stays fixed with respect to the viewer while orbiting.
/// Colors encode both hue and intensity of each Blinn-Phong component.
struct Light {
    QVector3D direction{-0.4f, -0.5f, -1.0f}; ///< direction the light travels
    QColor ambient = QColor::fromRgbF(0.28f, 0.28f, 0.28f);
    QColor diffuse = QColor::fromRgbF(0.72f, 0.72f, 0.72f);
    QColor specular = QColor::fromRgbF(0.30f, 0.30f, 0.30f);
};

/// Hard cap mirrored by MAX_LIGHTS in mesh.frag.
inline constexpr int kMaxLights = 4;

/// Which periodic images of the cell are drawn, as a window in fractional
/// coordinates — the "Show neighboring cells…" setting.
///
/// The default 0 → 1 along each axis is the home cell alone, i.e. exactly what
/// was drawn before this existed. Widening an axis to 0 → 2 adds the image one
/// lattice vector along it, and the window may run negative (−1 → 1 shows the
/// home cell and its neighbour on the low side).
///
/// A cell is drawn whenever the window touches it AT ALL, so the drawn set is
/// always whole cells: 0 → 1.5 draws two complete cells rather than slicing
/// the second one open. Cutting atoms mid-cell would need a per-atom
/// fractional test, which is a different feature — and one that would hide
/// atoms of an unwrapped structure whose coordinates already lie outside
/// [0, 1).
///
/// Purely a rendering duplication, like showNeighborCellAtoms: the images are
/// extra GPU instances and never enter the Structure, so the atom count, the
/// chemical formula and every exported POSCAR/CIF are unchanged.
struct NeighborCellRange {
    double min[3] = {0.0, 0.0, 0.0};
    double max[3] = {1.0, 1.0, 1.0};
    /// Draw the 12 wireframe edges around each image as well. Off draws only
    /// the atoms and bonds of the neighbours, leaving the home cell's own box
    /// (which `showCell` governs) as the only visible boundary.
    ///
    /// Off by default: a figure of an extended structure wants the atoms to
    /// read as one continuous lattice, and a box drawn around every image cuts
    /// it back into tiles. The home cell keeps its own outline either way.
    bool showEdges = false;

    /// Integer lattice translations (i, j, k) the window covers, always at
    /// least one entry.
    ///
    /// Defined inline so the rule can be pinned by a test without linking the
    /// renderer (and the GL context it needs): it is pure arithmetic, and its
    /// off-by-one behaviour at integer bounds is the whole contract.
    std::vector<std::array<int, 3>> cellOffsets() const
    {
        // Cell index i along an axis spans fractional [i, i+1), and is drawn
        // when the window touches it at all. The epsilon keeps an exact
        // integer bound from pulling in the cell that merely starts there:
        // with max = 1, ceil(1) - 1 = 0, so 0 -> 1 is the home cell alone
        // rather than two.
        constexpr double kEps = 1e-9;
        int lo[3];
        int hi[3];
        for (int axis = 0; axis < 3; ++axis) {
            const double a = std::min(min[axis], max[axis]);
            const double b = std::max(min[axis], max[axis]);
            lo[axis] = static_cast<int>(std::floor(a + kEps));
            hi[axis] = static_cast<int>(std::ceil(b - kEps)) - 1;
            if (hi[axis] < lo[axis])
                hi[axis] = lo[axis]; // a degenerate window still draws one cell
        }
        std::vector<std::array<int, 3>> offsets;
        for (int i = lo[0]; i <= hi[0]; ++i)
            for (int j = lo[1]; j <= hi[1]; ++j)
                for (int k = lo[2]; k <= hi[2]; ++k)
                    offsets.push_back({i, j, k});
        if (offsets.empty())
            offsets.push_back({0, 0, 0});
        return offsets;
    }

    /// True when the window selects the home cell and nothing else — the
    /// default, for which the renderer skips replication entirely.
    bool homeCellOnly() const
    {
        const auto offsets = cellOffsets();
        return offsets.size() == 1
            && offsets.front() == std::array<int, 3>{0, 0, 0};
    }
};

/// Draws a core::Structure. Depending on the representation mode:
///   - atoms  -> instanced unit spheres (covalent- or vdW-scaled)
///   - bonds  -> instanced half-cylinders, colored per atom; bond order
///               n renders as n parallel cylinders offset perpendicular
///               to the bond axis; or colored GL_LINES in wireframe
///   - cell   -> 12 wireframe edges
///
/// Strictly a View in MVC terms: it holds no reference to the Structure,
/// only GPU buffers derived from it. Call setStructure() again after the
/// model or style changes (a current GL context is required).
class StructureRenderer {
public:
    /// Everything a CAST owns — the settings that describe how one group of
    /// atoms is drawn, as opposed to the scene-wide settings (background, fog,
    /// cell, lights) that belong to the whole figure.
    ///
    /// A cast is a group of atoms drawn in its own right, so one scene can show
    /// a metal surface as space-filling spheres and the molecule adsorbed on it
    /// as ball-and-stick, each with its own material, colouring and scale — the
    /// standard way a surface-science figure separates substrate from
    /// adsorbate. Sharing any of these across casts would defeat that: a
    /// substrate wants matte and big, an adsorbate shiny and small.
    struct CastStyle {
        RepresentationMode mode = RepresentationMode::BallAndStick;
        /// Shiny rather than Standard: a tight, bright highlight is what makes
        /// a sphere read as a sphere rather than as a flat disc of colour, and
        /// a structure is almost entirely spheres. Standard's broad, weak
        /// highlight leaves a crowded model looking washed out.
        SurfaceFinish surfaceFinish = SurfaceFinish::Shiny;
        ColorMode colorMode = ColorMode::Element;
        float atomScaleFactor = 1.0f; ///< sphere-radius multiplier
        float bondWidthFactor = 1.0f; ///< cylinder-width multiplier
        /// Per-cast opacity in [0, 1]; 1 is fully opaque (the default).
        ///
        /// Independent of `surfaceFinish`: Glassy is a MATERIAL (Fresnel rim,
        /// view-dependent alpha) whereas this is a flat transparency the user
        /// dials. The pair that makes casts worth having is a faded substrate
        /// behind a solid adsorbate, and that wants a plain, predictable alpha
        /// rather than a glass look.
        float opacity = 1.0f;
        /// Flat colour this cast's atoms (and their bond halves) take under
        /// ColorMode::Cast. Invalid — the default — means "no explicit pick":
        /// the cast falls back to its slot in the default qualitative cycle,
        /// so a fresh cast is already distinguishable before anyone opens the
        /// Cast Colors dialog. See StructureRenderer::castColor().
        QColor castColor;
        /// What this cast is, when something knows. Casts made by hand in the
        /// Cast Setup dialog have no name and read as "Cast 3"; casts a module
        /// created because it understood the structure — "Epoxide", "Carboxyl"
        /// — say so, which is the difference between a list of groups the user
        /// can act on and five numbered buckets they have to identify by
        /// clicking each one. Empty means "no name": see
        /// StructureRenderer::castLabel().
        QString name;
    };

    struct Style {
        // -- Cast 0 --------------------------------------------------------
        //
        // Cast 0 always exists, and every atom starts in it. Its settings live
        // here as plain members rather than as castStyles[0] so that every
        // existing reader of style.mode / style.colorMode / style.atomScaleFactor
        // (ray-trace export, viewport picking, the Element Settings dialog,
        // saved projects) stays correct for the default single-cast scene.
        RepresentationMode mode = RepresentationMode::BallAndStick;
        /// Cast 0's opacity; see CastStyle::opacity.
        float opacity = 1.0f;
        /// Cast 0's flat colour under ColorMode::Cast; see
        /// CastStyle::castColor.
        QColor castColor;
        /// Cast 0's name; see CastStyle::name. Held here for the same reason
        /// its colour is — cast 0 has exactly one copy of its state.
        QString castName;

        // -- Casts (per-atom representation groups) ------------------------

        /// Cast index per atom, index-aligned with the structure's atoms().
        /// Empty — or any size that disagrees with the atom count, which is
        /// what a structure replacement leaves behind — means every atom is in
        /// cast 0.
        std::vector<int> atomCasts;

        // -- Local structural phase (ColorMode::Phase) ----------------------

        /// Identified local structure per atom, index-aligned with atoms().
        ///
        /// Kept beside `atomCasts` rather than in the scalar map because it is
        /// a NOMINAL label, exactly like a cast: there is no ordering between
        /// fcc and bcc to map onto a gradient. The viewport fills it from
        /// core::identifyStructuralPhases() whenever some cast asks for Phase
        /// colouring; an empty vector (or one whose size disagrees with the
        /// atom count, which is what a structure replacement leaves behind)
        /// makes every atom read as "Other".
        std::vector<core::StructuralPhase> atomPhases;

        /// Colour per StructuralPhase, indexed by the enum value. An invalid
        /// entry means "no explicit pick" and falls back to the default below,
        /// so a fresh scene is already readable before anyone opens the Phase
        /// Colors dialog.
        std::array<QColor, core::kStructuralPhaseCount> phaseColors{};
        /// Settings of casts 1..N; cast 0's are the members of this struct.
        std::vector<CastStyle> castStyles;

        /// Number of casts, cast 0 included — always at least 1.
        int castCount() const
        {
            return 1 + static_cast<int>(castStyles.size());
        }
        /// Settings of `cast`, falling back to cast 0's for an index outside
        /// the current set.
        CastStyle castStyle(int cast) const
        {
            if (cast > 0 && cast <= static_cast<int>(castStyles.size()))
                return castStyles[static_cast<std::size_t>(cast - 1)];
            return {mode, surfaceFinish, colorMode, atomScaleFactor,
                    bondWidthFactor, opacity, castColor, castName};
        }
        /// Write `value` back into `cast`. Cast 0 writes through to the
        /// members above, which is what keeps the two representations of it in
        /// step — there is only ever one copy of cast 0's state.
        void setCastStyle(int cast, const CastStyle& value)
        {
            if (cast > 0 && cast <= static_cast<int>(castStyles.size())) {
                castStyles[static_cast<std::size_t>(cast - 1)] = value;
                return;
            }
            mode = value.mode;
            surfaceFinish = value.surfaceFinish;
            colorMode = value.colorMode;
            atomScaleFactor = value.atomScaleFactor;
            bondWidthFactor = value.bondWidthFactor;
            opacity = value.opacity;
            castColor = value.castColor;
            castName = value.name;
        }
        /// Representation of `cast` — the most-asked-for field of castStyle().
        RepresentationMode castMode(int cast) const
        {
            return castStyle(cast).mode;
        }
        /// True when any cast in use needs the blended pass — either the
        /// Glassy material or an opacity below 1. Both end up in the same
        /// second, depth-write-off draw.
        bool anyTranslucentCast() const
        {
            const auto translucent = [](SurfaceFinish finish, float alpha) {
                return finish == SurfaceFinish::Glassy || alpha < 0.999f;
            };
            if (translucent(surfaceFinish, opacity))
                return true;
            for (const CastStyle& cast : castStyles)
                if (translucent(cast.surfaceFinish, cast.opacity))
                    return true;
            return false;
        }

        float atomScaleFactor = 1.0f; ///< global sphere-radius multiplier (UI)
        float bondWidthFactor = 1.0f; ///< global cylinder-width multiplier (UI)
        float bondRadius = 0.078f;    ///< Å, base radius of a single bond
        float bondTolerance = 1.15f;  ///< bond-detection cutoff factor
        bool autoBonds = true;        ///< distance-based bond perception on/off
                                      ///< (manual bond overrides always render)
        /// Smooth axial color gradient across each bond (atom A color at one
        /// end blending to atom B color at the other); off = classic
        /// half-and-half coloring.
        bool gradientBonds = true;
        /// Draw hydrogen atoms at all. Off hides every H sphere, the bonds
        /// that terminate on one, and the H-bond dashes — the standard way to
        /// read a crowded organic or protein structure, where the hydrogens
        /// outnumber the heavy atoms they are attached to and hide the
        /// skeleton that carries the chemistry.
        ///
        /// Purely a display filter: the hydrogens stay in the Structure, so
        /// the formula, every calculation and every exported file are
        /// unchanged by hiding them.
        bool showHydrogens = true;
        // -- Coordination polyhedra (Polyhedral mode) ----------------------
        /// Face opacity. Translucent by default so the coordinated atoms stay
        /// visible through the hull — an opaque polyhedron hides exactly the
        /// geometry it is drawn to explain.
        float polyhedronOpacity = 0.38f;
        /// Draw the hull's edge wireframe. On by default: the edges are what
        /// make a translucent polyhedron read as a solid rather than a smear.
        bool polyhedronEdges = true;
        /// Edge width. Core-profile GL clamps glLineWidth on most drivers, so
        /// values much above ~2 may not visibly thicken.
        float polyhedronEdgeWidth = 1.5f;
        /// Per-element coordination cutoff override (Z -> Å). A central cation
        /// absent from the map uses the global bondTolerance rule; an entry
        /// here fixes ITS coordination shell at an absolute radius, which is
        /// what a cation whose covalent radii give the wrong shell needs.
        std::map<int, float> polyhedronCutoffOverrides;
        /// Per-atom vector overlays drawn as 3D arrows from each atom
        /// center (mesh representations only). Data comes from the
        /// structure's vector fields "forces" / "velocities".
        VectorOverlay vectorOverlay = VectorOverlay::None;
        /// Kept in step with CastStyle::surfaceFinish above.
        SurfaceFinish surfaceFinish = SurfaceFinish::Shiny;
        // -- Directional shadow mapping (Visual Effects -> Shadow) ---------
        /// Off by default: the depth pass roughly doubles draw calls, and
        /// shadows help far more in a figure than while orbiting a structure.
        bool shadowsEnabled = false;
        /// How dark an occluded fragment becomes (0 = no visible shadow,
        /// 1 = full loss of direct light; ambient is never attenuated).
        float shadowStrength = 0.55f;
        /// PCF kernel half-width in shadow-map texels. 0 = hard edges,
        /// larger = softer and more expensive ((2r+1)^2 taps per fragment).
        int shadowSoftness = 2;
        /// Base opacity of the Glassy finish at face-on incidence (the
        /// Fresnel term drives edges toward opaque). Ignored otherwise.
        float glassOpacity = 0.45f;
        /// Normalized vector-overlay length: 1.0 is the calibrated baseline
        /// (kVectorBaseScale Å of arrow per field unit), not a raw Å factor.
        float vectorScale = 1.0f;
        /// Arrow shaft thickness, relative to the calibrated baseline (1.0).
        /// The head scales with it, so an arrow stays proportioned.
        ///
        /// This replaced the "draw arrowheads" toggle. A headless arrow is
        /// ambiguous about direction — the one thing a vector overlay exists to
        /// state — and the reason the toggle existed (heads merging into
        /// clutter on a dense magnetic structure) is better answered by making
        /// the whole arrow thinner than by removing the head that carries the
        /// meaning.
        float vectorWidth = 1.0f;
        /// Hide arrows whose field magnitude is below this, in the FIELD's own
        /// units (eV/Å, μB, …) — not in Å of drawn arrow, so the filter does
        /// not shift when the length scale is changed.
        float vectorMinMagnitude = 0.0f;
        QColor forceColor{242, 92, 54};
        QColor velocityColor{54, 166, 242};
        QColor magmomColor{168, 120, 240};
        /// Distinct from magmomColor on purpose: a guess and a result should
        /// not be the same colour when they can both be on screen.
        QColor initialMagmomColor{240, 160, 90};
        bool showCell = true;
        /// "Show atoms of the neighboring unit cell": draw the periodic images
        /// that terminate the bonds leaving the cell — and only those.
        ///
        /// One image per end of every wrapped bond, so a bond crossing the
        /// boundary terminates ON AN ATOM instead of stopping in mid-air.
        ///
        /// It used to also duplicate every atom lying on a face, edge or
        /// vertex, and then complete those copies' own bonds — which filled
        /// the view with periodic repetition nobody had asked for (a
        /// face-centred cell grew by more than half again). What the setting
        /// is actually for is the dangling bonds, so that is all it does.
        ///
        /// With the setting off, wrapped bonds are still drawn as the
        /// conventional pair of half-length stubs — the standard depiction of
        /// periodicity when the neighbouring cell is not shown at all.
        ///
        /// Purely a rendering duplication — the images are extra GPU instances
        /// and never enter the Structure, so the atom count, the chemical
        /// formula and every exported POSCAR/CIF are unchanged.
        bool showNeighborCellAtoms = false;
        /// Isosurface shading, read by the "Lit surface" profile. Mirrors the
        /// Edit Volumetric Render dialog: 0 = Flat, 1 = Diffuse, 2 = Glossy.
        /// The legacy profile ignores these and uses the baked colours.
        /// PBR material defaults (Preferences -> Rendering). Global rather
        /// than per-cast for now: they describe how the installation renders,
        /// and a per-object override belongs on the cast alongside the surface
        /// finish when PBR stops being opt-in.
        float metallic = 0.15f;
        float roughness = 0.35f;
        /// Toon quantization.
        int toonBands = 4;
        float toonRim = 0.35f;
        int isoShadingMode = 0;
        float isoAmbient = 0.35f;
        float isoSpecular = 0.35f;
        float isoShininess = 48.0f;
        /// Which periodic images of the whole cell are drawn ("Show
        /// neighboring cells…"). Independent of showNeighborCellAtoms above,
        /// which completes individual wrapped BONDS: this repeats the entire
        /// contents of the cell, atoms, bonds and all.
        NeighborCellRange neighborCells;
        QColor cellColor{166, 166, 178};
        /// 1 = plain GL lines; > 1 renders the edges as thin lit tubes
        /// (core-profile GL clamps glLineWidth, so tubes are the portable
        /// way to get thick cell wireframes).
        float cellLineWidth = 2.0f;
        /// Solid, dashed or dotted. Applies to both the thin-line and the lit
        /// tube path, because the break is cut into the geometry rather than
        /// painted by the fragment stage.
        CellLineStyle cellLineStyle = CellLineStyle::Solid;
        /// Fill the cell's six faces with a translucent solid, so the box
        /// reads as a VOLUME rather than as twelve lines. A wireframe alone is
        /// ambiguous about which way it encloses — the classic Necker-cube
        /// flip — and a faint tinted fill resolves it, which is what a figure
        /// of a slab or a molecule inside its box actually needs.
        ///
        /// Independent of `showCell`: the fill and the edges are two
        /// depictions of the same box and either may be wanted alone (a solid
        /// block with no wireframe, or the historical wireframe with no fill).
        bool fillCell = false;
        /// Draw the Wigner-Seitz (Voronoi) cell of the lattice in place of the
        /// parallelepiped: the set of points closer to the lattice point at
        /// the origin than to any other.
        ///
        /// It is the SAME lattice shown a different way. The parallelepiped
        /// depends on which basis vectors were chosen and generally hides the
        /// lattice's point symmetry; the Wigner-Seitz cell has the full point
        /// group by construction, which is why a hexagonal lattice reads as
        /// hexagonal in it and as an oblique box in the other.
        ///
        /// Every other cell setting — line style, line width, colour, fill and
        /// fill alpha — applies to it unchanged: the two shapes are built into
        /// one outline representation and share every style path.
        bool showVoronoiCell = false;
        /// Fill tint. Distinct from `cellColor` on purpose — the edge colour
        /// is chosen to READ against the atoms, whereas the fill is chosen to
        /// stay behind them, and one value cannot do both.
        QColor cellFillColor{110, 150, 210};
        /// Fill opacity in [0, 1]. Low by default: the fill exists to say
        /// where the box is, and anything much above ~0.3 starts washing out
        /// the structure it is drawn around.
        float cellFillAlpha = 0.15f;
        std::map<int, QColor> colorOverrides;      ///< Z -> user color
        std::map<int, float> radiusScaleOverrides; ///< Z -> per-element radius factor
        /// Scalar color mapping: Element uses the CPK palette; the other
        /// modes color atoms (and their bond halves) by the per-atom
        /// scalars passed to setAtomScalars(), sampled along `gradient`.
        ColorMode colorMode = ColorMode::Element;
        ColorGradient gradient = ColorGradient::Viridis;
        /// Reverse the scalar -> color mapping of `gradient` (minima get
        /// the high end of the palette), like matplotlib's "_r" maps.
        bool invertGradient = false;
        /// Fixed color-scale bounds. Off by default, so the ramp auto-scales
        /// to the data's own min/max; on, the ramp is pinned to
        /// [customScalarMin, customScalarMax], which is what comparing frames
        /// or structures on one scale requires (auto-scaling silently
        /// renormalizes every frame and makes them incomparable). Values
        /// outside the window clamp to the ramp ends.
        bool useCustomScalarRange = false;
        float customScalarMin = 0.0f;
        float customScalarMax = 1.0f;
        /// Distance fog (View -> Visual Effects): 0 = off, 1 = linear
        /// between fogStart/fogEnd, 2 = exponential with fogDensity.
        /// fogColor tracks the viewport background. Off by default; the
        /// exponential density/start/end below are the values used once the
        /// user enables it in the Visual Effects panel.
        int fogMode = 0;
        float fogStart = 15.0f; ///< Å (view-space distance)
        float fogEnd = 80.0f;   ///< Å
        float fogDensity = 0.300f;
        QColor fogColor{26, 28, 33};
    };

    /// Display radius of an atom (Å) — the single source of truth shared
    /// by instance building and by ray-cast picking in the viewport. The
    /// two-argument form uses cast 0's settings; pass an explicit CastStyle
    /// for an atom in another cast, since both the mode and the scale factor
    /// are per-cast.
    static float displayRadius(int atomicNumber, const Style& style);
    static float displayRadius(int atomicNumber, const CastStyle& cast);

    /// Element color after applying user overrides (default: Jmol CPK).
    static QColor atomColor(int atomicNumber, const Style& style);

    /// Colour of `cast` under ColorMode::Cast: the cast's explicit pick when
    /// one is set, otherwise the default qualitative palette cycled by cast
    /// index. Public for the same reason atomColor() is — the Cast Colors
    /// dialog must show in its swatches exactly what the renderer will draw,
    /// defaults included, rather than keep a second copy of the palette.
    static QColor castColor(int cast, const Style& style);

    /// How to refer to `cast` in the UI: its name when it has one, else
    /// "Cast N". One implementation, so the Cast Setup table, the Cast Colors
    /// dialog and any future list all call the same cast the same thing.
    static QString castLabel(int cast, const Style& style);

    /// Colour of `phase` under ColorMode::Phase: the explicit pick when the
    /// style carries one, else the default. Public for the same reason
    /// castColor() is — the Phase Colors dialog has to show exactly what the
    /// renderer will draw, defaults included, rather than keep a second copy
    /// of the palette that can drift.
    static QColor phaseColor(core::StructuralPhase phase, const Style& style);

    /// The built-in colour of `phase`.
    ///
    /// The OVITO/AtomEye convention — green fcc, red hcp, blue bcc, yellow
    /// icosahedral, cyan diamond, white-grey other — because that is what
    /// anyone reading a CNA figure already has in their eye. Inventing a new
    /// palette here would make every published comparison one step harder.
    static QColor defaultPhaseColor(core::StructuralPhase phase);

    /// Settings each atom of `structure` is drawn with, resolved from the
    /// style's cast assignment. Falls back to a uniform cast-0 style when the
    /// assignment is absent or does not match the atom count (which is what a
    /// structure replacement leaves behind). Public so the viewport's picking
    /// and the ray-trace exporter resolve radii exactly as the renderer does.
    static std::vector<CastStyle> atomCastStyles(const core::Structure* structure,
                                                 const Style& style);

    // -- Macromolecular geometry (GL-free, testable in isolation) -----------
    //
    // Public because they are pure geometry over a Structure — no GL context,
    // no member state beyond the style — which is what lets them be checked
    // directly instead of only through a rendered frame.

    /// Smooth tube through each chain's α-carbon trace (Ribbon mode). Emits
    /// cylinder + sphere instances so the ribbon shares the lit pipeline and
    /// the shadow pass with everything else.
    void buildRibbon(const core::Structure* structure,
                     const std::vector<CastStyle>& casts,
                     const std::set<int>* selection,
                     std::vector<float>& bondInstances,
                     std::vector<float>& atomInstances) const;
    /// Van-der-Waals envelope of the atoms in a MolecularSurface cast
    /// (Gaussian density splatted onto a grid, then marching cubes). Emits
    /// interleaved pos(3)+color(3) triangle vertices.
    void buildMolecularSurface(const core::Structure* structure,
                               const std::vector<CastStyle>& casts,
                               std::vector<float>& faceVertices) const;

    /// Must be called once with a current GL context (from initializeGL).
    void initialize(QOpenGLFunctions_3_3_Core* gl);

    /// Rebuild instance/vertex buffers from the model. nullptr clears the
    /// scene. Atoms whose index is in `selection` are drawn highlighted.
    void setStructure(const core::Structure* structure,
                      const std::set<int>* selection = nullptr);


    /// Per-atom scalars driving one non-Element color mode (CN, GCN, a custom
    /// field). Values are normalized to their own [min, max] internally; an
    /// empty vector (or a size mismatch with the current structure) makes that
    /// mode fall back to element colors.
    ///
    /// Keyed BY MODE because casts colour independently: a scene can hold a
    /// coordination-coloured slab and a custom-property-coloured adsorbate at
    /// once, and the two fields have unrelated ranges that must not share a
    /// normalization. Call setStructure() afterwards to rebuild the colors.
    void setAtomScalars(ColorMode mode, std::vector<float> scalars);
    /// Drop every stored field (a structure replacement invalidates them all).
    void clearAtomScalars();

    /// One mode's stored per-atom field in its OWN units — CN counts, GCN
    /// values, the raw custom property — or nullptr when that mode carries no
    /// data. Distinct from what the colours use, which is this field mapped
    /// onto [0, 1]: the viewport's numeric overlay has to print the physical
    /// value, not the colour coordinate.
    const std::vector<float>* atomScalars(ColorMode mode) const;

    /// Data range of one mode's field, for the colour-scale legend. `valid` is
    /// false in Element mode or when that mode has no field.
    struct ScalarRange {
        bool valid = false;
        float min = 0.0f;
        float max = 1.0f;
    };
    ScalarRange scalarRangeFor(ColorMode mode) const;

    void render(const QMatrix4x4& view, const QMatrix4x4& projection);

    /// Interactive "Lattice Plane" overlay: a translucent, per-vertex-colored
    /// quad (a Miller-index plane, optionally color-mapped from a volumetric
    /// scalar field) plus its edge outline. `faceTris` / `edgeLines` are
    /// interleaved pos(3)+color(3) streams (GL_TRIANGLES / GL_LINES). Pass empty
    /// streams (or visible=false) to hide it. Requires a current GL context.
    void setLatticePlane(const std::vector<float>& faceTris,
                         const std::vector<float>& edgeLines, float alpha,
                         bool visible, bool showEdges);

    /// A contiguous run of triangles in the custom-overlay face buffer that
    /// share one opacity — one per user primitive, so each can blend
    /// independently.
    struct OverlayRange {
        int first = 0;    ///< first vertex (not byte / not float) in the buffer
        int count = 0;    ///< number of vertices
        float alpha = 1.0f;
    };

    /// "Custom Overlay" geometric primitives (spheres, boxes, cylinders,
    /// planes…) drawn over the structure. `faces`/`edges` are interleaved
    /// pos(3)+color(3) streams; `faceRanges` slices `faces` into per-primitive
    /// runs so each blends at its own opacity.
    ///
    /// `edgeAlpha` blends the whole edge stream; it defaults to opaque, which
    /// is right for a wireframe drawn as an outline. The Volumetric panel's
    /// mesh and dot styles pass the isosurface's own opacity instead — there
    /// the lines ARE the surface, and an opaque one would ignore the opacity
    /// the user set.
    /// Upload a scalar field as a 3D texture for direct volume rendering.
    ///
    /// A different object from the isosurface overlay, not a mode of it: an
    /// isosurface is geometry extracted once and drawn like anything else,
    /// while this is the whole field resampled per pixel per frame. `values`
    /// is normalized to [0,1] by the caller against the range it wants mapped;
    /// `transfer` is a 256-entry RGBA lookup the shader indexes with that
    /// normalized value.
    void setVolumeField(int nx, int ny, int nz, const std::vector<float>& values,
                        const std::vector<float>& transfer,
                        const QMatrix4x4& boxTransform);
    void clearVolumeField();
    /// Ray-march parameters, applied on the next draw.
    void setVolumeParams(int steps, float density, float isoLevel, bool lit);

    /// Floats per vertex in the `faces` stream of setCustomOverlay():
    /// pos(3) + normal(3) + color(3).
    ///
    /// Public because the PRODUCERS need it. When this widened from 6 to 9 the
    /// Volumetric panel kept emitting 6 and computing its range offsets as
    /// size/6, while the VAO read 9 — so every position was sampled from the
    /// wrong offset and the isosurface exploded into a fan of triangles. The
    /// two ends now share one number instead of each spelling their own.
    static constexpr int kOverlayFaceFloats = 9;
    /// Floats per vertex in the `edges` stream: pos(3) + color(3). A line has
    /// no orientation to light, so it carries no normal.
    static constexpr int kOverlayEdgeFloats = 6;

    /// `faces` is interleaved pos(3) + normal(3) + color(3); `edges` stays
    /// pos(3) + color(3), since a line has no orientation to light.
    ///
    /// The normal channel is what the "Lit surface" isosurface profile shades
    /// from. It replaces the CPU-side baking the Volumetric panel used to do,
    /// which froze the highlight to a fixed direction and left the surface out
    /// of the SSAO G-buffer entirely.
    void setCustomOverlay(const std::vector<float>& faces,
                          const std::vector<float>& edges,
                          const std::vector<OverlayRange>& faceRanges,
                          bool visible, float edgeAlpha = 1.0f);

    /// The "Additional overlays" dock's geometry — the user's lattice planes
    /// and primitives — in the same interleaved pos(3)+color(3) form as
    /// setCustomOverlay().
    ///
    /// A SEPARATE channel from setCustomOverlay() on purpose. That one is
    /// already written by the Volumetric Data panel (isosurfaces) and the MLWF
    /// viewer (orbital meshes), and each call replaces the whole buffer — so
    /// sharing it would mean adding a text label silently erased a displayed
    /// isosurface. Two channels cost two draw blocks; one channel would cost
    /// the user their figure.
    void setManagedOverlay(const std::vector<float>& faces,
                           const std::vector<float>& edges,
                           const std::vector<OverlayRange>& faceRanges,
                           bool visible);

    /// Hydrogen-bond overlay: an interleaved pos(3)+color(3) GL_LINES stream
    /// of PRE-DASHED segments (see buildHydrogenBondDashes). Empty clears it.
    void setHydrogenBonds(const std::vector<float>& segments);

    /// Split each D-H···A contact into dashes of `dashLength` Å separated by
    /// equal gaps, appending an interleaved pos+color stream. Static so the
    /// geometry can be built without a current GL context.
    static void buildHydrogenBondDashes(
        const std::vector<std::pair<QVector3D, QVector3D>>& contacts,
        const QColor& color, float dashLength, std::vector<float>& out);

    Style& style() { return style_; }
    const Style& style() const { return style_; }

    /// 1..kMaxLights directional lights (extra entries are ignored).
    std::vector<Light>& lights() { return lights_; }
    const std::vector<Light>& lights() const { return lights_; }

    /// The two-light studio default (warm key + soft cool fill) every renderer
    /// starts with. Public so "Reset lights" restores exactly the set a fresh
    /// viewport has, rather than a second hand-written copy of it that could
    /// drift from this one.
    static std::vector<Light> defaultLights();

private:
    struct InstancedMesh {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vertexBuffer{QOpenGLBuffer::VertexBuffer};
        QOpenGLBuffer indexBuffer{QOpenGLBuffer::IndexBuffer};
        QOpenGLBuffer instanceBuffer{QOpenGLBuffer::VertexBuffer};
        int indexCount = 0;
        int instanceCount = 0;

        // Impostor path: a second VAO over a 2-triangle quad and THE SAME
        // instance buffer.
        //
        // A second binding rather than a second copy of the instances, and
        // rather than swapping the base geometry in place. Both alternatives
        // were worse: duplicating the instance data costs a megabyte per
        // 10 000 atoms for no reason, and swapping the buffer would drag the
        // shadow pass along with it — which must keep casting from real
        // tessellated geometry, because the depth-only pass has no fragment
        // stage in which to ray-trace an impostor.
        QOpenGLVertexArrayObject impostorVao;
        QOpenGLBuffer quadVertexBuffer{QOpenGLBuffer::VertexBuffer};
        QOpenGLBuffer quadIndexBuffer{QOpenGLBuffer::IndexBuffer};
        int quadIndexCount = 0;
    };

    /// Build the impostor quad + its VAO over `mesh`'s existing instance
    /// buffer. Called once, beside createMesh().
    void createImpostorQuad(InstancedMesh& mesh);
    /// Whether the impostor profile is active for `slot` AND its program
    /// linked. Links lazily, falling back to the tessellated path with a
    /// warning if the driver rejects it.
    bool useImpostors(int slot);
    /// Shared per-frame uniforms for an impostor program (lights, shadow, fog,
    /// material) — the same values mesh.frag receives, so the two profiles
    /// agree about the scene.
    void uploadImpostorUniforms(QOpenGLShaderProgram& program,
                                const QMatrix4x4& view,
                                const QMatrix4x4& projection,
                                const QMatrix4x4& lightSpace);

    struct ColoredVertexBuffer { // pos(3) + color(3) per vertex
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        int vertexCount = 0;
    };

    /// pos(3) + normal(3) + color(3). A separate type from
    /// ColoredVertexBuffer rather than a widening of it: the wireframe,
    /// polyhedra and lattice-plane streams genuinely have no surface normal,
    /// and giving them a zeroed one would only cost bandwidth and invite a
    /// future reader to light geometry that has no orientation.
    struct LitVertexBuffer {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        int vertexCount = 0;
    };
    void createLitBuffer(LitVertexBuffer& buffer);
    /// Whether the lit isosurface program should be used: the Preferences
    /// selection says so AND the program linked. Links it on first ask.
    bool useLitIsosurface();
    /// Link (once) the lit surface program and report whether this driver
    /// accepted it. Separate from useLitIsosurface(), which additionally
    /// honours the Preferences isosurface profile: the unit cell is lit
    /// whatever that profile says, but still has to degrade on a driver that
    /// rejects the shader.
    bool ensureLitSurfaceProgram();
    /// Push the scene lights into the bound isosurface program, in the same
    /// view-space convention mesh.frag uses — so an isosurface and the atoms
    /// inside it are lit by one set of lights rather than two.
    void uploadIsosurfaceLights();
    void uploadLitBuffer(LitVertexBuffer& buffer, const std::vector<float>& data);

    /// Color of atom `index` under `colorMode`: that mode's scalar mapping
    /// when it has data, otherwise the (possibly overridden) element color.
    QColor resolvedAtomColor(std::size_t index, int atomicNumber,
                             ColorMode colorMode) const;


    void createMesh(InstancedMesh& mesh,
                    const std::vector<float>& vertices,
                    const std::vector<unsigned int>& indices);
    /// Coordination-polyhedra geometry (Polyhedral mode): translucent hull
    /// faces (pos+color triangles) and solid hull edges (pos+color lines) for
    /// every atom with >= 4 bonded neighbors. Emits into the two vertex vectors.
    void buildPolyhedra(const core::Structure* structure,
                        const std::set<int>* selection,
                        std::vector<float>& faceVertices,
                        std::vector<float>& edgeVertices) const;
    void createColoredBuffer(ColoredVertexBuffer& buffer);
    void uploadColoredBuffer(ColoredVertexBuffer& buffer, const std::vector<float>& data);
    void uploadLights();

    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    bool initialized_ = false;
    Style style_;
    std::vector<Light> lights_ = defaultLights();

    /// One per-atom field per non-Element color mode, each normalized against
    /// its OWN range — see setAtomScalars().
    struct ScalarField {
        std::vector<float> values;
        float min = 0.0f;
        float max = 1.0f;
    };
    std::map<ColorMode, ScalarField> scalars_;

    /// Fit an orthographic light frustum around the current scene and return
    /// the world -> light-clip matrix used by both the depth pass and the
    /// lookup in mesh.frag.
    QMatrix4x4 lightSpaceMatrix() const;
    /// Lazily create the depth FBO + texture; returns false if unavailable.
    bool ensureShadowTarget();
    /// Depth-only pass over every instanced mesh from the light's viewpoint.
    void renderShadowMap(const QMatrix4x4& lightSpace);

    QOpenGLShaderProgram meshProgram_;
    QOpenGLShaderProgram shadowProgram_; ///< depth-only, light's-eye pass
    QOpenGLShaderProgram wireProgram_;
    /// "Lit surface" isosurface profile. Linked lazily on first use so a
    /// driver that rejects it falls back to the legacy path with a warning
    /// instead of taking the whole viewport down.
    QOpenGLShaderProgram isosurfaceProgram_;
    bool isosurfaceProgramReady_ = false;
    bool isosurfaceProgramTried_ = false;
    /// Impostor programs, indexed by render::ShaderSlot (0 = atoms/sphere,
    /// 1 = bonds/cylinder). Linked lazily on first use, like the isosurface
    /// one, so a driver that rejects them degrades instead of failing.
    /// Direct volume rendering. Lazily linked like the others.
    QOpenGLShaderProgram raymarchProgram_;
    bool raymarchReady_ = false;
    bool raymarchTried_ = false;
    unsigned volumeTexture_ = 0;
    unsigned transferTexture_ = 0;
    QOpenGLVertexArrayObject volumeVao_;
    QOpenGLBuffer volumeVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer volumeIbo_{QOpenGLBuffer::IndexBuffer};
    QMatrix4x4 volumeTransform_;
    bool volumeVisible_ = false;
    int volumeSteps_ = 256;
    float volumeDensity_ = 1.0f;
    float volumeIsoLevel_ = 0.02f;
    bool volumeLit_ = true;
    void drawVolume(const QMatrix4x4& view, const QMatrix4x4& projection);

    QOpenGLShaderProgram impostorProgram_[2];
    bool impostorReady_[2] = {false, false};
    bool impostorTried_[2] = {false, false}; ///< per-vertex-color lines/points

    InstancedMesh sphere_;
    InstancedMesh cylinder_;
    InstancedMesh cone_;     ///< arrowheads of force/velocity vectors
    InstancedMesh cellTube_; ///< thick cell wireframe (cellLineWidth > 1)
    /// The cell faces as triangles — the parallelepiped's six or the
    /// Wigner-Seitz cell's however many.
    ///
    /// A LIT buffer (position, normal, colour), so the fill is shaded by the
    /// scene lights like every other surface instead of reading as a flat
    /// wash. Blended at Style::cellFillAlpha without writing depth, like the
    /// polyhedra faces: the cell is scene furniture and must never occlude the
    /// atoms it encloses.
    LitVertexBuffer cellFaces_;
    ColoredVertexBuffer wireBonds_;  ///< GL_LINES
    ColoredVertexBuffer wireAtoms_;  ///< GL_POINTS (isolated atoms visible)
    ColoredVertexBuffer polyhedronFaces_; ///< GL_TRIANGLES (translucent)
    ColoredVertexBuffer polyhedronEdges_; ///< GL_LINES (opaque outline)
    /// Molecular-surface envelope, GL_TRIANGLES with per-vertex colours taken
    /// from the nearest atom. Drawn through the flat wire program rather than
    /// the lit one: the surface is a coloured shape, and the marching-cubes
    /// normals are noisy enough at this grid spacing that specular highlights
    /// on them read as artifacts.
    ColoredVertexBuffer molecularSurface_;
    ColoredVertexBuffer latticePlaneFaces_; ///< GL_TRIANGLES (translucent slice)
    ColoredVertexBuffer latticePlaneEdges_; ///< GL_LINES (plane border)
    float latticePlaneAlpha_ = 0.4f;
    bool latticePlaneVisible_ = false;
    bool latticePlaneEdgesOn_ = true;
    /// Isosurface / primitive faces. Carries normals so the lit isosurface
    /// program can shade them on the GPU; the legacy profile ignores them.
    LitVertexBuffer customOverlayFaces_;     ///< GL_TRIANGLES (custom primitives)
    ColoredVertexBuffer customOverlayEdges_; ///< GL_LINES (primitive wireframes)
    std::vector<OverlayRange> customOverlayRanges_;
    bool customOverlayVisible_ = false;
    float customOverlayEdgeAlpha_ = 1.0f;
    ColoredVertexBuffer managedOverlayFaces_; ///< GL_TRIANGLES (overlay dock)
    ColoredVertexBuffer managedOverlayEdges_; ///< GL_LINES (overlay dock)
    std::vector<OverlayRange> managedOverlayRanges_;
    bool managedOverlayVisible_ = false;
    /// Hydrogen bonds, GL_LINES. The dash pattern is BAKED INTO THE GEOMETRY
    /// (many short segments) rather than drawn with line stipple: core-profile
    /// GL removed glLineStipple, so this is the portable way to get a dashed
    /// line — and it keeps the dashes a fixed length in Å, so they do not
    /// stretch or crowd as the camera zooms.
    ColoredVertexBuffer hydrogenBonds_;

    // -- Shadow map --------------------------------------------------------
    /// 2048² is the sweet spot here: structures are compact, so the fitted
    /// light frustum is small and this resolves individual atoms cleanly
    /// without the memory of a 4k map.
    static constexpr int kShadowMapSize = 2048;
    unsigned shadowFbo_ = 0;
    /// Per-frame shadow state, produced by render() and consumed by
    /// uploadLights() (which runs once per mesh-program pass).
    bool shadowsActive_ = false;
    QMatrix4x4 lightSpace_;
    unsigned shadowTexture_ = 0;
    /// A 1x1 white texture kept bound to unit 0 whenever the real shadow map
    /// is not.
    ///
    /// uShadowMap is a sampler2D on the mesh program, which draws every frame
    /// whether or not shadows are on. A sampler pointing at a unit with no
    /// complete texture is what makes the macOS GL driver log
    ///
    ///     UNSUPPORTED (log once): POSSIBLE ISSUE: unit 0
    ///     GLD_TEXTURE_INDEX_2D is unloadable and bound to sampler type
    ///     (Float) - using zero texture because texture unloadable
    ///
    /// The shader's `uShadowEnabled == 0` early-out does not prevent it: the
    /// driver validates sampler-to-unit completeness at DRAW time, regardless
    /// of which branch the shader takes. White = depth 1.0 = "nothing
    /// occludes", so it is also the harmless answer if it ever were sampled.
    unsigned dummyTexture_ = 0;
    /// Scene bounds in world space, refreshed on every setStructure(); the
    /// light frustum is fitted to this sphere so the map's depth precision
    /// tracks the actual model rather than a fixed guess.
    QVector3D sceneCenter_;
    float sceneRadius_ = 1.0f;

};

} // namespace calango::render
