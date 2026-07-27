#pragma once

#include "core/Coordination.hpp"
#include "core/HydrogenBonds.hpp"
#include "core/Vec3.hpp"
#include "render/Camera.hpp"
#include "render/StructureRenderer.hpp"

#include <QColor>
#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>

#include <array>
#include <memory>
#include <set>
#include <utility>

class QPainter;
class QRubberBand;

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// The 3D canvas: owns the camera and renderer, translates mouse input
/// into camera motion and atom picking, and observes (never mutates) the
/// Structure model. Selection is view state; editing it emits
/// selectionChanged so the controller can act on it.
///
/// Interaction: what a left-drag does depends on the InteractionMode
/// (rotate by default, pan, rubber-band selection, or atom/bond
/// insertion); middle-drag or Shift+left-drag always pans,
/// wheel zooms, double-click reframes. A left *click* (no drag) picks the
/// atom under the cursor (Ctrl/Cmd+click toggles; empty click clears).
class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    /// Rebuilds GPU buffers; clears the selection. `frameCamera` recenters
    /// the view (disable during trajectory playback).
    void setStructure(std::shared_ptr<const core::Structure> structure,
                      bool frameCamera = true);

    /// Call after in-place edits of the current structure (keeps camera).
    void refreshStructure();

    const std::set<int>& selection() const { return selection_; }
    void clearSelection();

    std::shared_ptr<const core::Structure> structure() const { return structure_; }

    void setShowCell(bool show);
    void setRepresentation(render::RepresentationMode mode);

    // -- Scalar color mapping ----------------------------------------------

    render::ColorMode colorMode() const { return renderer_.style().colorMode; }
    /// Switch the atom coloring mode. `customField` names the structure
    /// scalar field to map in CustomScalar mode (ignored otherwise).
    /// CN/GCN scalars are (re)computed here and after every structure
    /// replacement — trajectory playback re-colors frame by frame.
    void setColorMode(render::ColorMode mode, const QString& customField = {});
    /// Recompute the scalar fields every cast's colour mode needs, then
    /// rebuild. Called when a NON-zero cast changes its colour mode — cast 0
    /// goes through setColorMode, which does this as part of its work.
    void refreshColorScalars();
    void setColorGradient(render::ColorGradient gradient);
    /// Reverse the scalar -> color mapping of the current gradient.
    void setGradientInverted(bool inverted);
    QString customScalarField() const { return customField_; }

    void setCoordinationOptions(const core::CoordinationOptions& options);

    /// Min/max of the active scalar mapping (legend range); `valid` is
    /// false in Element mode or when no scalars are available.
    struct ScalarRange {
        bool valid = false;
        float min = 0.0f;
        float max = 1.0f;
    };
    ScalarRange scalarRange() const { return scalarRange_; }

    /// Pin the scalar color ramp to explicit bounds instead of auto-scaling to
    /// the data. Passing enabled=false restores auto-scaling. Re-colors the
    /// atoms (and their bond halves) without touching the scalars themselves.
    void setCustomScalarRange(bool enabled, float min, float max);

    QColor backgroundColor() const { return backgroundColor_; }
    void setBackgroundColor(const QColor& color);

    // -- Lattice Plane / volumetric color-slice overlay --------------------
    /// Push a translucent lattice plane (optionally color-mapped from a
    /// volumetric scalar field) into the 3D scene. `faceTris` / `edgeLines` are
    /// interleaved pos(3)+color(3) streams (GL_TRIANGLES / GL_LINES) built by
    /// LatticePlaneDialog. Passing visible=false (or empty streams) hides it.
    void setLatticePlane(std::vector<float> faceTris, std::vector<float> edgeLines,
                         float alpha, bool visible, bool showEdges);
    /// Remove the lattice-plane overlay.
    void clearLatticePlane();

    /// Push the "Custom Overlay" geometric primitives into the 3D scene.
    /// `faceTris` / `edgeLines` are interleaved pos(3)+color(3) streams;
    /// `faceRanges` slices `faceTris` into per-primitive runs so each blends at
    /// its own opacity. visible=false (or empty streams) hides the overlay.
    void setCustomOverlay(
        std::vector<float> faceTris, std::vector<float> edgeLines,
        std::vector<render::StructureRenderer::OverlayRange> faceRanges,
        bool visible);
    /// Remove the custom-overlay primitives.
    void clearCustomOverlay();

    // -- Hydrogen bonds -----------------------------------------------------
    /// Geometric hydrogen-bond perception (Bond Editor → "Hydrogen Bonds").
    /// Detected contacts are drawn as dashed lines, so they are visually
    /// distinct from the covalent bonds they sit among.
    struct HydrogenBondStyle {
        bool enabled = false;
        core::HydrogenBondOptions options;
        QColor color{120, 200, 255};
        float dashLength = 0.18f; ///< Å per dash (gaps are the same length)
    };
    HydrogenBondStyle& hydrogenBondStyle() { return hbondStyle_; }
    /// Re-run detection on the current structure and push the dashes. Call
    /// after editing hydrogenBondStyle() or replacing the structure.
    void refreshHydrogenBonds();
    /// Number of contacts found by the last refresh — the Bond Editor reports
    /// it so the user can tell "criteria too strict" from "none present".
    int hydrogenBondCount() const { return hbondCount_; }

public Q_SLOTS:
    /// Perspective (default) vs. orthographic projection; the transition
    /// preserves apparent scale (see OrbitCamera::projection).
    void setOrthographic(bool orthographic);

    /// Corner coordinate-triad overlay.
    void setShowAxes(bool show);
    /// Draw arrowheads at the tips of the orientation triad. Off by default:
    /// plain segments read more cleanly at the triad's small on-screen size,
    /// and arrowheads mainly help when the figure must state axis *direction*
    /// unambiguously (e.g. a printed figure).
    void setShowAxesArrows(bool show);
    bool showAxesArrows() const { return axesArrows_; }
    /// false = Cartesian X/Y/Z, true = lattice vectors a1/a2/a3
    /// (falls back to Cartesian when the structure has no cell).
    void setAxesLatticeMode(bool lattice);
    /// On-screen edge length of the axes-triad box (logical pixels).
    void setAxesSize(int px);
    int axesSize() const { return axesSizePx_; }

    /// Per-atom text overlays drawn over the sphere centers (QPainter, like
    /// the axes triad and measurement labels). Independent toggles: the
    /// element symbol ("Fe", "O") and/or the 1-based atom index ("#12").
    void setShowElementLabels(bool show);
    void setShowAtomIndexLabels(bool show);
    bool showElementLabels() const { return showElementLabels_; }
    bool showAtomIndexLabels() const { return showIndexLabels_; }

    /// Live style access for UI panels. Call styleChanged() afterwards:
    /// geometry-affecting edits (scales, colors, mode) rebuild the GPU
    /// buffers; light-only edits just repaint.
    render::StructureRenderer::Style& style() { return renderer_.style(); }
    std::vector<render::Light>& lights() { return renderer_.lights(); }
    void styleChanged(bool rebuildGeometry);

    render::OrbitCamera& camera() { return camera_; }
    const render::OrbitCamera& camera() const { return camera_; }

    /// Apply a stored camera state and repaint. Invalid (never-captured)
    /// points-of-view are ignored, so restoring a tab that has not been shown
    /// yet leaves the camera where it is instead of jumping to a default.
    void setPointOfView(const render::PointOfView& pov);

    /// Film fade: 1 = normal, 0 = a fully black frame. Painted over the whole
    /// canvas including the axis triad and atom labels, because a fade that
    /// left the overlays visible would not read as a cut. Only the Fade in /
    /// Fade out film transition drives this.
    void setFilmFade(float visibility);
    float filmFade() const { return filmFade_; }

    /// Film crossfade: composite `outgoing` over the live render with weight
    /// (1 - `weight`), so weight 0 shows only the cached image and 1 only the
    /// live one.
    ///
    /// A dissolve mixes two complete renders, and re-rendering the outgoing
    /// side every frame would mean two full FBO passes per frame. It does not
    /// have to: both ends of a film transition are STATIC camera poses, so the
    /// outgoing image is constant for the whole dissolve and the caller
    /// captures it once. A null image clears the effect.
    void setFilmCrossfade(const QImage& outgoing, float weight);
    void clearFilmCrossfade();

public:
    // -- Mouse interaction modes -------------------------------------------

    /// What a plain left-drag / left-click does. Regardless of mode,
    /// middle-drag (or Shift+left-drag) pans and the wheel zooms.
    enum class InteractionMode {
        Rotate,          ///< drag orbits the camera (default)
        Pan,             ///< drag translates the scene
        Select,          ///< drag draws a rubber-band box selecting atoms
        Insert,          ///< click empty space -> new atom; drag atom->atom -> bond
        MeasureDistance, ///< click two atoms -> interatomic distance (Å)
        MeasureAngle,    ///< click three atoms -> angle at the middle one (°)
    };
    void setInteractionMode(InteractionMode mode);

    /// Depth-of-field post-processing (View -> Visual Effects). Fog lives
    /// in the renderer Style; DoF is a viewport-level composite pass.
    /// While enabled the scene renders into a non-multisampled offscreen
    /// target, so MSAA is traded for the effect.
    struct DepthOfField {
        bool enabled = false;
        float strength = 6.0f;   ///< max blur radius (pixels)
        float focusRange = 12.0f; ///< Å around the focal plane kept sharp
        float focusOffset = 0.0f; ///< Å shift of the focal plane from the target
    };
    DepthOfField& depthOfField() { return dof_; }

    /// Screen-space ambient occlusion (View -> Visual Effects). Darkens the
    /// contact regions a direct-lighting model leaves fully lit — the creases
    /// between touching spheres, the gap under a bond — which is what makes a
    /// dense structure read as solid geometry rather than a flat cluster of
    /// discs. Like DoF it renders through the offscreen G-buffer, so enabling
    /// either trades MSAA for the effect.
    struct AmbientOcclusion {
        bool enabled = false;
        /// Hemisphere sampling radius in Å. Roughly "how far away can a
        /// neighbour still shade me" — around an atomic radius reads best.
        float radius = 1.2f;
        /// Strength of the darkening: 0 leaves the image untouched, 1 applies
        /// the full occlusion factor.
        float intensity = 0.7f;
        /// Hemisphere samples per pixel. More samples trade frame time for
        /// less noise; the blur pass cleans up what remains, so raising this
        /// mainly helps at large radii where the samples spread thin. Capped
        /// at kMaxSsaoSamples (the shader's uniform array size).
        int samples = 32;
        /// Scale of the tiled rotation-noise lookup. 1.0 tiles the 4x4 texture
        /// pixel-for-pixel — the value the blur radius is matched to. Larger
        /// values rotate over a coarser grid, which trades a subtler dither
        /// pattern for banding the blur can no longer fully remove.
        float noiseScale = 1.0f;
    };
    /// Mirrors MAX_KERNEL in ssao.frag.
    static constexpr int kMaxSsaoSamples = 64;
    AmbientOcclusion& ambientOcclusion() { return ssao_; }

public Q_SLOTS:
    /// Off-screen high-resolution capture of the current scene into a
    /// QImage (used by the image/GIF export engine). `background` with
    /// alpha 0 produces a transparent image; `extraYawDeg` rotates the
    /// camera around the target (turntable animation frames).
    QImage renderToImage(int width, int height, const QColor& background,
                         float extraYawDeg = 0.0f);

public Q_SLOTS:
    void frameStructure();

    /// Align the view with a Cartesian plane (camera looking along the
    /// remaining axis): 0 = XY (along z), 1 = XZ (along y), 2 = YZ (along x).
    void alignWithXY() { alignToPlane(0); }
    void alignWithXZ() { alignToPlane(1); }
    void alignWithYZ() { alignToPlane(2); }
    void alignToPlane(int plane);

    /// Smoothly rotate the scene about world axis 0/1/2 (x/y/z) by
    /// `degrees` (signed). Animated over ~200 ms; concurrent calls
    /// compose, so rapid clicks still sum to exact multiples.
    void rotateSceneAxis(int axis, double degrees);

Q_SIGNALS:
    void selectionChanged(int count);
    /// The camera moved — orbit, pan, zoom, projection toggle or an applied
    /// point-of-view. The Set Point-of-View dialog mirrors it live, and the
    /// host uses it to keep each workspace tab's stored view current.
    void cameraChanged();
    /// Insert mode: the user clicked empty space — create an atom there
    /// (world coordinates on the camera-target plane). The viewport never
    /// mutates the structure; MainWindow owns the edit + undo.
    void atomInsertRequested(const core::Vec3& position);
    /// Insert mode: the user dragged from atom i to atom j — bond them.
    void bondInsertRequested(int i, int j);
    /// Insert mode: Shift+click on an existing atom — substitute it with the
    /// element currently selected in the insertion element picker.
    void atomReplaceRequested(int index);
    /// Translation (Pan) mode: Shift+drag on an atom — move that single atom to
    /// `position`. `begin` is true on the first move of a drag (push one undo).
    void atomTranslateRequested(int index, const core::Vec3& position, bool begin);
    /// A distance/angle measurement completed — `text` is the
    /// human-readable result for the status/log console (the viewport
    /// itself overlays the value on the canvas).
    void measurementMade(const QString& text);
    /// Select mode: Delete/Backspace pressed with atoms selected —
    /// MainWindow deletes them (bonds rebuild via structure refresh).
    void deleteSelectionRequested();
    /// A different Structure is now observed (its scalar fields may differ).
    void structureReplaced();
    /// The scalar color mapping was recomputed (mode, range or data changed).
    void colorMappingChanged();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    /// Ray-cast from a screen position against atom display spheres.
    /// Returns the atom index of the nearest hit, or -1.
    int pickAtom(const QPointF& screenPos) const;

    /// World-space ray under a pixel; false if the view is degenerate.
    bool screenRay(const QPointF& screenPos, QVector3D& origin,
                   QVector3D& direction) const;
    /// Intersection of that ray with the plane through the camera target
    /// perpendicular to the view direction (where inserted atoms land).
    bool unprojectToTargetPlane(const QPointF& screenPos, core::Vec3& out) const;
    /// Intersection of that ray with a viewer-facing plane through an arbitrary
    /// world point (used to drag an atom in the plane through its own depth).
    bool unprojectToPlane(const QPointF& screenPos, const core::Vec3& planePoint,
                          core::Vec3& out) const;
    /// Atoms whose projected centers fall inside the screen-space rect.
    std::set<int> atomsInRect(const QRectF& rect) const;

    /// (Re)create the offscreen G-buffer (color + view normals + depth) and
    /// the SSAO ping-pong targets. Shared by the DoF and SSAO passes: both
    /// need the scene rendered offscreen, and DoF also reads the same depth.
    void ensurePostTarget(int w, int h);
    void destroyPostTarget();
    /// Compile the post-processing programs and build the SSAO sample kernel +
    /// noise texture. Called once from initializeGL.
    void initializePostProcessing();
    /// Run the SSAO + blur passes over the current G-buffer, leaving the
    /// blurred occlusion factor in ssaoBlurTex_. `projection` must be the one
    /// the scene was rendered with.
    void renderSsaoPasses(int w, int h, const QMatrix4x4& projection);
    /// Draw a fullscreen triangle with the currently bound program.
    void drawFullscreenTriangle();
    /// Draw the scene (GL state assumed set) — shared by both paths.
    void renderScene();
    /// The two halves of renderScene(), so the post-processing path can slip a
    /// per-attachment clear between them (glClear fills every attached color
    /// buffer, including the G-buffer normals).
    void clearScene();
    void drawSceneGeometry();
    /// Screen position of an atom center; false if behind the camera.
    bool projectAtomToScreen(int index, QPointF& out) const;
    /// Register a click on `atom` for the active measurement mode.
    void advanceMeasurement(int atom);
    /// Markers, connecting lines and the value label of the running
    /// distance/angle measurement.
    void drawMeasurementOverlay(QPainter& painter);
    /// Per-atom element-symbol and/or index text over the sphere centers.
    void drawAtomLabelsOverlay(QPainter& painter);

    /// Re-upload instance buffers if dirty (requires a current context).
    void ensureUploaded();

    /// Recompute the per-atom scalars for the active color mode and hand
    /// them to the renderer (CN/GCN analysis or field lookup).
    void updateColorScalars();

    /// Axis directions + labels for the triad (Cartesian or lattice).
    std::array<std::pair<QVector3D, QString>, 3> axesVectors() const;
    /// Corner triad, drawn entirely with QPainter: core-profile GL clamps
    /// glLineWidth to 1 px, so painter strokes are the portable way to get
    /// thick, high-DPI-crisp axis lines.
    void drawAxesOverlay(QPainter& painter);

    render::OrbitCamera camera_;
    render::StructureRenderer renderer_;
    std::shared_ptr<const core::Structure> structure_;
    std::set<int> selection_;
    QString customField_;
    core::CoordinationOptions coordinationOptions_;
    ScalarRange scalarRange_;
    QColor backgroundColor_{26, 28, 33};
    bool structureDirty_ = false;

    // Lattice-plane overlay geometry, uploaded lazily in ensureUploaded().
    std::vector<float> latticePlaneFaces_;
    std::vector<float> latticePlaneEdges_;
    float latticePlaneAlpha_ = 0.4f;
    bool latticePlaneVisible_ = false;
    bool latticePlaneEdgesOn_ = true;
    bool latticePlaneDirty_ = false;

    // Custom-overlay primitive geometry, uploaded lazily in ensureUploaded().
    std::vector<float> customOverlayFaces_;
    std::vector<float> customOverlayEdges_;
    std::vector<render::StructureRenderer::OverlayRange> customOverlayRanges_;
    bool customOverlayVisible_ = false;
    bool customOverlayDirty_ = false;
    HydrogenBondStyle hbondStyle_;
    std::vector<float> hydrogenBondSegments_; ///< pre-dashed pos+color stream
    bool hydrogenBondsDirty_ = false;
    int hbondCount_ = 0;
    bool showAxes_ = true;
    bool axesLatticeMode_ = false;
    bool axesArrows_ = false;
    bool showElementLabels_ = false; ///< overlay element symbols on atoms
    bool showIndexLabels_ = false;   ///< overlay 1-based atom indices
    float filmFade_ = 1.0f;          ///< 1 = normal, 0 = black (film fades)
    QImage filmCrossfadeImage_;      ///< cached outgoing render (dissolve)
    float filmCrossfadeWeight_ = 1.0f; ///< weight of the LIVE render
    int axesSizePx_ = 92;
    QPointF lastMousePos_;
    InteractionMode interactionMode_ = InteractionMode::Rotate;
    QRubberBand* rubberBand_ = nullptr; ///< Select-mode drag box
    int insertDragFromAtom_ = -1;       ///< Insert mode: drag start atom
    /// Translation (Pan) mode Shift+drag: the grabbed atom (-1 = none), its
    /// world position at press, the unprojected drag anchor on that plane, and
    /// whether the first move (and undo push) has happened yet.
    int shiftDragAtom_ = -1;
    core::Vec3 shiftDragAtomStart_;
    core::Vec3 shiftDragPlaneStart_;
    bool shiftDragBegan_ = false;
    /// Atoms clicked so far in a measurement (2 = distance, 3 = angle);
    /// kept after completion so the overlay stays until the next click.
    std::vector<int> measureAtoms_;
    QString measurementLabel_; ///< overlay text of a completed measurement

    DepthOfField dof_;
    AmbientOcclusion ssao_;
    QOpenGLShaderProgram dofProgram_;
    QOpenGLShaderProgram ssaoProgram_;
    QOpenGLShaderProgram ssaoBlurProgram_;
    QOpenGLShaderProgram ssaoCompositeProgram_;
    QOpenGLVertexArrayObject dofVao_; ///< empty VAO for the fullscreen triangle

    // Offscreen G-buffer shared by the post-processing passes. `postNormalTex_`
    // is written by the scene shaders through draw buffer 1.
    unsigned postFbo_ = 0;
    unsigned postColorTex_ = 0;
    unsigned postNormalTex_ = 0;
    unsigned postDepthTex_ = 0;
    /// AO ping-pong: raw occlusion, then the bilaterally blurred result.
    unsigned ssaoFbo_ = 0;
    unsigned ssaoTex_ = 0;
    unsigned ssaoBlurFbo_ = 0;
    unsigned ssaoBlurTex_ = 0;
    /// Scene color after the AO multiply, so DoF can blur the composited
    /// image rather than a version with sharp occlusion painted back on.
    unsigned ssaoCompositeFbo_ = 0;
    unsigned ssaoCompositeTex_ = 0;
    /// 4x4 tiled random rotation vectors for the SSAO kernel.
    unsigned ssaoNoiseTex_ = 0;
    std::vector<QVector3D> ssaoKernel_; ///< hemisphere sample offsets
    int postWidth_ = 0;
    int postHeight_ = 0;
    QPointF pressPos_;
};

} // namespace calango::gui
