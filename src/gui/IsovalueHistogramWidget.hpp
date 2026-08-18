#pragma once

#include <QWidget>

#include <vector>

class QSlider;

namespace calango::gui {

/// A voxel-count histogram of a volumetric field, drawn directly above the
/// isovalue slider it shares an x-axis with, in the "Edit Volumetric Render"
/// dialog's Isosurfaces page.
///
/// The bins are fixed by setData() and never recomputed as the isovalue
/// moves — only the marker line, and, on a click or drag, the picked value,
/// change per frame after that. Binning a field of a few million voxels on
/// every slider tick would turn each repaint into an O(N) pass over the
/// whole volume; this way that cost is paid once per LOADED VOLUME instead.
///
/// The x-axis is pinned to a reference slider's own groove — queried through
/// QStyle on every paint rather than assumed as a fixed inset — so the
/// histogram lines up with the slider under it to the pixel, regardless of
/// style, platform or theme. It follows the widget palette rather than
/// PlotPalette's fixed white canvas: this is a control decoration living
/// inside a themed settings dialog, not an exportable results plot.
class IsovalueHistogramWidget : public QWidget {
    Q_OBJECT

public:
    explicit IsovalueHistogramWidget(QWidget* parent = nullptr);

    /// Bin `values` into a few hundred bins spanning [dataMin, dataMax] and
    /// cache the counts. Call this once per loaded volume — never on every
    /// isovalue change.
    void setData(const std::vector<double>& values, double dataMin, double dataMax);

    /// The slider this histogram sits above. Its groove geometry (queried via
    /// QStyle on every paint, not copied once) is what the histogram's x-axis
    /// is pinned to, so a style or width change moves both together.
    void setReferenceSlider(QSlider* slider);

    /// Where the marker line is drawn. Does not touch the cached bins.
    void setCurrentValue(double value);

    void setLogScale(bool on);
    bool logScale() const { return logScale_; }

    /// Whether the distribution given to the last setData() call looked
    /// peaked enough that log-scale counts read better by default — the
    /// caller decides what to actually do with that (e.g. seed a checkbox).
    bool logScaleSuggested() const { return logSuggested_; }

Q_SIGNALS:
    /// A click or drag picked a new isovalue. Reports it exactly the way
    /// QSlider::valueChanged does for the slider this histogram sits above —
    /// connect it the same way.
    void valueEdited(double value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QRectF plotRect() const;
    double valueFromX(double x) const;
    void dragTo(const QPointF& pos);

    QSlider* referenceSlider_ = nullptr;
    std::vector<double> counts_;
    double dataMin_ = 0.0;
    double dataMax_ = 1.0;
    double maxCount_ = 0.0;
    double currentValue_ = 0.0;
    bool logScale_ = false;
    bool logSuggested_ = false;
};

} // namespace calango::gui
