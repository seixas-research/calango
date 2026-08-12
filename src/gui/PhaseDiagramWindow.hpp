#pragma once

#include "core/PhaseDiagram.hpp"
#include "core/TdbDatabase.hpp"

#include <QColor>
#include <QDialog>
#include <QList>
#include <QStringList>
#include <QWidget>

#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace calango::gui {

/// What "Customize Appearance…" writes, for both the T-x diagram and the
/// ternary section.
///
/// Defaults come from gui/PlotPalette.hpp — the one light canvas every 2D data
/// plot in Calango starts on, because a figure is expected on white wherever it
/// actually goes (a paper, a slide, a referee reply). This struct is the
/// starting point the user can move, not a ceiling: the same arrangement the
/// band/PDOS, optics and convergence viewers use.
struct PhaseDiagramStyle {
    QColor canvas;
    QColor spine;
    QColor grid;
    QColor text;
    /// Per-phase colours, cycled. Index is the phase's index in the diagram.
    QList<QColor> phaseColors;

    bool showGrid = true;
    /// Filled phase regions. The default view.
    bool showShading = true;
    /// The traced phase-boundary curves that bound those regions.
    bool showBoundaryCurves = true;
    /// Resample the boundaries with a monotone cubic (see
    /// core::monotoneCubicResample). Off gives straight segments between the
    /// computed isotherms — never wrong, just faceted.
    bool smoothBoundaries = true;
    /// Cross-hatch two-phase fields instead of tinting them. The classic
    /// convention, and the way to tell an alpha+alpha miscibility gap from the
    /// single-phase alpha beside it when both carry one colour.
    bool hatchTwoPhase = false;
    /// The raw isotherm stack the shading replaced. Off by default now that
    /// the regions are filled, but kept: it is the only view that shows where
    /// the equilibrium was actually computed rather than interpolated.
    bool showTieLines = false;
    bool showBoundaryPoints = false;
    bool showLegend = true;
    /// Opacity of the two-phase shading, 0-255. Several hundred stacked
    /// tie-lines make a field; one alpha suits a dense sweep and another a
    /// sparse one, so it is a control rather than a constant.
    int tieLineAlpha = 90;
    double tieLineWidth = 1.0;
    /// Opacity of a filled region, 0-255.
    int shadingAlpha = 110;
    /// Stroke width of the phase-boundary curves.
    double boundaryWidth = 1.6;
    /// Points generated per computed isotherm interval when smoothing. Higher
    /// is smoother to look at and no more accurate — the curve is pinned to
    /// the same computed points either way.
    int smoothingSubdivisions = 8;
    double boundaryPointRadius = 1.6;
    /// Point size for axis numbers and labels; 0 keeps the widget default.
    int fontPointSize = 0;

    PhaseDiagramStyle();
    /// A colour for `index`, cycling when there are more phases than colours.
    QColor phaseColor(int index) const;
};

/// "Customize Appearance…" for the phase-diagram viewer.
class PhaseDiagramStyleDialog : public QDialog {
    Q_OBJECT

public:
    PhaseDiagramStyleDialog(const PhaseDiagramStyle& style,
                            const QStringList& phaseNames,
                            QWidget* parent = nullptr);

    const PhaseDiagramStyle& style() const { return style_; }

Q_SIGNALS:
    /// Emitted on every edit, so the plot behind the dialog updates live —
    /// choosing a colour against a static preview is choosing blind.
    void styleChanged(const PhaseDiagramStyle& style);

private:
    QPushButton* colorButton(QColor* target);

    PhaseDiagramStyle style_;
};

/// Binary T-x phase diagram, hand-painted with QPainter like every other
/// Calango plot (no external plotting dependency — see LinePlotWidget).
///
/// The drawing is the construction, made visible: each temperature section
/// contributes one horizontal tie-line per two-phase field, and the tie-line
/// ENDS traced up the temperature axis are the phase boundaries. Drawing the
/// tie-lines rather than only the boundary curves is deliberate — a tie-line
/// is the thing a reader measures a lever rule on, and a diagram that shows
/// only the envelope hides the two-phase fields it is made of.
class BinaryPhaseDiagramWidget : public QWidget {
    Q_OBJECT

public:
    explicit BinaryPhaseDiagramWidget(QWidget* parent = nullptr);

    void setDiagram(core::BinaryPhaseDiagram diagram, const QString& elementA,
                    const QString& elementB);
    void clear();
    bool hasData() const { return !diagram_.sections.empty(); }
    /// The computed diagram, following the same convention as
    /// ConvexHullPlotWidget::result(): the widget owns what it draws and lets
    /// a caller read it rather than recompute it.
    const core::BinaryPhaseDiagram& diagram() const { return diagram_; }

    /// CSV of the tie-lines: one row per two-phase field per temperature,
    /// which is the data the diagram is drawn from and the form a lever-rule
    /// calculation wants. Header lines are '#'-commented so the file loads in
    /// numpy/pandas without an argument.
    QString toCsv() const;

    void setStyle(const PhaseDiagramStyle& style);
    const PhaseDiagramStyle& style() const { return style_; }

    /// Render to `path`; PNG (at `scale`x for print) or SVG by extension.
    bool exportImage(const QString& path, double scale = 3.0);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    /// Paint into `painter` over `bounds`. Shared by paintEvent and the image
    /// export so the exported figure IS the figure on screen — a second
    /// drawing path is a second thing to keep in step, and it drifts.
    void render(QPainter& painter, const QRectF& bounds, bool interactive) const;

    /// One traced region, ready to draw: its two boundaries as polylines in
    /// DATA coordinates (x, T), already smoothed if the style asks for it.
    ///
    /// Cached rather than rebuilt per paint. Tracing and resampling do not
    /// depend on the widget's size, and a resize or a hover must not
    /// re-interpolate several thousand points.
    struct RenderField {
        int phaseA = -1;
        int phaseB = -1;
        bool openBelow = false;
        bool openAbove = false;
        std::vector<QPointF> left;  ///< (x, T)
        std::vector<QPointF> right;
        bool twoPhase() const { return phaseB >= 0; }
    };
    /// One filled cell: the composition span of a band, given the vertical
    /// extent of its own isotherm.
    ///
    /// The regions are filled from these rather than from the field polygons,
    /// and the reason is a real defect that the polygons cannot avoid. A
    /// boundary that moves faster than the temperature sampling — the Nb-Re
    /// melting line jumps from x = 0.465 to x = 0.318 between two adjacent
    /// isotherms — leaves a wedge belonging to neither the field below nor the
    /// field above, and it renders as a white gash across the diagram. Bands
    /// tile each isotherm exactly and the isotherm strips tile the temperature
    /// axis exactly, so cells tile the plane BY CONSTRUCTION: no holes, no
    /// double-painted overlaps, whatever the data does.
    ///
    /// The cost is that a fill edge is stepped at the sampling resolution
    /// instead of smooth. That is paid back by the boundary CURVES drawn over
    /// the top, and it is the honest failure mode: where the steps are visible
    /// the diagram is telling you the temperature grid is too coarse there.
    struct FillCell {
        double xLow = 0.0;
        double xHigh = 0.0;
        double tLow = 0.0;
        double tHigh = 0.0;
        int phaseA = -1;
        int phaseB = -1;
        bool twoPhase() const { return phaseB >= 0; }
    };
    void rebuildFields();

    core::BinaryPhaseDiagram diagram_;
    std::vector<RenderField> fields_;
    std::vector<FillCell> cells_;
    PhaseDiagramStyle style_;
    QString elementA_;
    QString elementB_;
    double minTemperature_ = 0.0;
    double maxTemperature_ = 1.0;
    QPointF hover_{-1.0, -1.0};
    /// Plot rectangle from the last paint, so hit testing matches what is
    /// actually on screen rather than a recomputed guess.
    mutable QRectF plotRect_;
};

/// Ternary isothermal section, drawn on the Gibbs composition triangle.
///
/// Facets are shaded by how many phases their three corners carry — one, two
/// or three — because that count IS the phase rule reading of the section and
/// nothing else on the picture conveys it. Two-phase facets also get their
/// tie-line drawn, joining the two compositions actually in equilibrium.
class TernarySectionWidget : public QWidget {
    Q_OBJECT

public:
    explicit TernarySectionWidget(QWidget* parent = nullptr);

    void setSection(core::TernaryIsothermalSection section,
                    const QStringList& elements);
    void clear();
    bool hasData() const { return section_.ok; }

    /// CSV of the lower-hull facets: one row per triangle with its three
    /// corner compositions and the phases in equilibrium there.
    QString toCsv() const;

    /// The phases this section actually carries, for the appearance dialog.
    QStringList phaseNames() const;

    void setStyle(const PhaseDiagramStyle& style);
    bool exportImage(const QString& path, double scale = 3.0);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// Barycentric composition -> screen. x_A is at the apex, x_B bottom-right,
    /// x_C bottom-left, which is the orientation every published Gibbs
    /// triangle uses.
    QPointF project(double xB, double xC) const;
    void render(QPainter& painter, const QRectF& bounds) const;

    core::TernaryIsothermalSection section_;
    PhaseDiagramStyle style_;
    QStringList elements_;
    mutable QRectF plotRect_;
};

/// "Phase Diagram…" from the CALPHAD dialog: computes and shows the diagram
/// for the currently selected system.
///
/// Everything here is C++. The equilibrium of a binary at a temperature is the
/// lower convex hull of its phases' Gibbs curves and a hull needs no iteration
/// — so no solver, and therefore no pycalphad, is involved in drawing a
/// diagram out of a real database. What the database can contain that this
/// cannot model (phases mixing on two sublattices, the magnetic contribution)
/// is REPORTED, phase by phase, rather than approximated.
class PhaseDiagramWindow : public QDialog {
    Q_OBJECT

public:
    PhaseDiagramWindow(const core::TdbDatabase& database,
                       const QStringList& elements, const QStringList& phases,
                       QWidget* parent = nullptr);

    /// Recompute from the current controls. Public so a test can drive it
    /// without pressing anything.
    void recompute();

    /// Set the T-x window and recompute. The defaults (300-2000 K) suit an
    /// alloy of ordinary metals; a refractory system like Nb-Re melts above
    /// 2700 K and shows nothing but solids in that window.
    void setTemperatureRange(double minimumK, double maximumK);

    /// Write the active tab's data as CSV. Public so a test can drive it
    /// without a file dialog; returns false if there is nothing to write.
    bool exportCsv(const QString& path) const;
    /// Render the active tab to PNG or SVG, chosen by the extension.
    bool exportImage(const QString& path, double scale = 3.0) const;
    /// Apply an appearance to both plots.
    void setStyle(const PhaseDiagramStyle& style);
    const PhaseDiagramStyle& style() const { return style_; }

    /// Status line text — the refusals and the "N phases modelled" summary.
    QString statusText() const;
    /// The binary diagram as computed. Empty for a ternary selection.
    const core::BinaryPhaseDiagram& binaryDiagram() const;

private:
    /// Build a core::GibbsPhase per usable database phase, and collect the
    /// reasons the others were left out.
    std::vector<core::GibbsPhase> buildBinaryPhases(QStringList* skipped) const;
    std::vector<core::TernaryGibbsPhase>
    buildTernaryPhases(double temperatureK, QStringList* skipped) const;

    core::TdbDatabase database_;
    QStringList elements_;
    QStringList phases_;

    QTabWidget* tabs_ = nullptr;
    BinaryPhaseDiagramWidget* binary_ = nullptr;
    TernarySectionWidget* ternary_ = nullptr;
    QLabel* status_ = nullptr;
    QDoubleSpinBox* minTemperature_ = nullptr;
    QDoubleSpinBox* maxTemperature_ = nullptr;
    QSpinBox* temperatureSteps_ = nullptr;
    QSpinBox* compositionSteps_ = nullptr;
    QDoubleSpinBox* sectionTemperature_ = nullptr;
    QSpinBox* ternarySteps_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* imageButton_ = nullptr;
    QPushButton* styleButton_ = nullptr;
    PhaseDiagramStyle style_;
    QString status_text_;
};

} // namespace calango::gui
