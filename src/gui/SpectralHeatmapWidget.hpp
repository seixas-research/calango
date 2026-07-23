#pragma once

#include "core/BandUnfolding.hpp"
#include "render/ColorMap.hpp"

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

    double sigma() const { return options_.sigma; }
    double fermiLevel() const { return fermi_; }

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

    std::vector<core::UnfoldedColumn> columns_;
    core::SpectralFunctionOptions options_;
    core::SpectralFunction spectral_;
    QImage heatmap_;

    double fermi_ = 0.0;
    bool shiftFermi_ = true;
    double threshold_ = 0.02;
    render::ColorGradient gradient_ = render::ColorGradient::Viridis;

    std::vector<double> specialX_;
    QStringList specialLabels_;
};

} // namespace calango::gui
