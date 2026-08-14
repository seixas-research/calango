#pragma once

#include "core/BandUnfolding.hpp"
#include "gui/PlotPalette.hpp"
#include "render/ColorMap.hpp"

#include <QColor>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <vector>

namespace calango::gui {

/// Effective band structure viewer: the Popescu-Zunger spectral function
/// A(k, E) as an intensity heatmap over the primitive-cell k-path.
///
/// Unlike a band plot there are no discrete curves to draw — every supercell
/// eigenstate contributes at every k with a weight in [0, 1], so the natural
/// rendering is a 2D intensity field. Hand-painted with QPainter like the
/// other Calango plots.
class SpectralHeatmapWidget : public QWidget {
    Q_OBJECT

public:
    /// How the spectral weight is drawn.
    ///
    /// Two genuinely different pictures of the same data, not two skins. The
    /// heatmap broadens every state by σ and paints a continuous field, which
    /// is what you want when the bands overlap. Scatter draws the eigenvalues
    /// themselves, one marker per state, with no broadening at all — sharper
    /// where the bands are clean, and the presentation most of the unfolding
    /// literature uses.
    enum class RenderMode { Heatmap, Scatter };

    /// Appearance, deliberately mirroring BandPdosView::Style field for field
    /// wherever the two plots share a concept, so the ordinary band viewer and
    /// this one can be styled the same way and read as one family.
    struct Style {
        // -- Typography (points) -------------------------------------------
        double tickPointSize = 15.0;
        double axisTitlePointSize = 15.0;
        double annotationPointSize = 13.0; ///< high-symmetry k-path labels

        // -- Spectral weight ------------------------------------------------
        render::ColorGradient gradient = render::ColorGradient::Viridis;
        RenderMode mode = RenderMode::Heatmap;
        /// Weight below this fraction of the maximum is not drawn.
        double intensityThreshold = 0.02;
        /// Overall opacity of the weight field, 0-1. Lets a faint unfolded
        /// spectrum be laid under an overlay without hiding it.
        double opacity = 1.0;

        // -- Scatter mode ---------------------------------------------------
        /// Marker diameter in pixels at the maximum spectral weight.
        double markerSize = 3.5;
        /// When true a marker's area tracks its weight, so a weak state is a
        /// small dot rather than a pale one; when false every marker is the
        /// same size and only the colour carries the weight.
        bool markerScalesWithWeight = true;

        // -- Reference line (E_F) -------------------------------------------
        bool showFermi = true;
        QColor fermiColor = PlotPalette::reference;
        Qt::PenStyle fermiPenStyle = Qt::DashLine;
        double fermiLineWidth = 1.4;

        // -- Plot chrome ----------------------------------------------------
        QColor background = PlotPalette::canvas;
        QColor spineColor = PlotPalette::spine;
        QColor textColor = PlotPalette::text;
        double spineWidth = 1.2;

        /// Draw the colour scale beside the plot.
        bool showColorbar = true;
    };

    explicit SpectralHeatmapWidget(QWidget* parent = nullptr);

    /// Load a finished job's effective_bands.json. Returns false (and clears)
    /// when the file is missing or carries no usable column.
    bool loadFromJson(const QString& path);
    void clear();

    bool hasData() const { return !columns_.empty(); }

    // -- View controls -------------------------------------------------------
    void setGradient(render::ColorGradient gradient);
    /// Intensity below this fraction of the maximum is rendered as background.
    /// Suppresses the low-weight haze that unfolding always produces.
    void setIntensityThreshold(double fraction);
    /// Plot E - E_F (true) or absolute energies (false).
    void setShiftFermiToZero(bool shift);
    /// Gaussian broadening; re-derives the spectral function from the stored
    /// weights, so it is adjustable without re-running the calculation.
    void setSigma(double sigma);
    /// Energy window (eV, on the same reference as the plot). Re-bins the
    /// spectral function, so it costs a rebuild rather than only a repaint.
    void setEnergyWindow(double minimum, double maximum);

    const Style& style() const { return style_; }
    void setStyle(const Style& style);

    double sigma() const { return options_.sigma; }
    double fermiLevel() const { return fermi_; }
    double energyMin() const { return options_.energyMin; }
    double energyMax() const { return options_.energyMax; }

    /// Save the heatmap as PNG / JPEG.
    void exportImage(QWidget* dialogParent);
    /// Save the underlying A(k, E) grid as CSV.
    void exportData(QWidget* dialogParent);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// Recompute the spectral function and the cached image.
    void rebuild();
    /// Map the sampled field onto an image, one pixel per (column, bin).
    void rebuildImage();
    /// Draw the eigenvalues as markers instead of a broadened field.
    void paintScatter(QPainter& painter, const QRectF& plot, double eLo,
                      double eHi) const;
    /// The colour scale beside the plot, when enabled.
    void paintColorbar(QPainter& painter, const QRectF& plot) const;
    /// Width reserved on the right for the colorbar (0 when it is off).
    double colorbarWidth() const;

    std::vector<core::UnfoldedColumn> columns_;
    core::SpectralFunctionOptions options_;
    core::SpectralFunction spectral_;
    QImage heatmap_;

    double fermi_ = 0.0;
    bool shiftFermi_ = true;
    Style style_;

    std::vector<double> specialX_;
    QStringList specialLabels_;
};

} // namespace calango::gui
