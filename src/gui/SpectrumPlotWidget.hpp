#pragma once

#include "gui/OpticsPlotStyleDialog.hpp"

#include <QColor>
#include <QPair>
#include <QString>
#include <QPainter>
#include <QWidget>

#include <vector>



namespace calango::gui {

/// A line chart over one shared x axis, styled by OpticsPlotStyle.
///
/// It was file-local to the Optics results window until the XAS module needed
/// exactly the same thing — an energy axis, one or more curves over it, a
/// legend and a PNG/SVG export. Rather than a second copy diverging from the
/// first, it lives here and both windows draw through it.
///
/// Not tied to optics despite the style struct's name: the style is a set of
/// pen widths, fonts and colors, which any spectrum wants.
class SpectrumPlotWidget : public QWidget {
public:
    explicit SpectrumPlotWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(480, 320);
    }

    void setStyle(const OpticsPlotStyle& style)
    {
        style_ = style;
        update();
    }

    /// Explicit x window. An empty (min >= max) range means "fit the data",
    /// which is the default.
    void setXRange(double minimum, double maximum)
    {
        xMinLimit_ = minimum;
        xMaxLimit_ = maximum;
        update();
    }

    void setSeries(const std::vector<double>& x,
                   const std::vector<QPair<QString, std::vector<double>>>& series,
                   const QString& xLabel, const QString& yLabel)
    {
        x_ = x;
        series_ = series;
        xLabel_ = xLabel;
        yLabel_ = yLabel;
        update();
    }

    /// Dashed horizontal reference lines — (label, y) pairs such as a Fermi
    /// level or a vacuum level. Annotations of the y axis rather than curves
    /// over x, so they are drawn dashed and labelled on the line itself, not
    /// in the legend. Included in the vertical autoscale, or a level just
    /// outside the curves' own range would silently be invisible. Empty by
    /// default: existing callers draw exactly what they always did.
    void setReferenceLines(const std::vector<QPair<QString, double>>& lines)
    {
        referenceLines_ = lines;
        update();
    }

    /// Draw the chart into `painter` filling a logical area of `size`. Returns
    /// false (after drawing a placeholder) when there is nothing to plot.
    bool renderTo(QPainter& painter, QSize size) const;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        renderTo(painter, size());
    }

private:
    static QColor seriesColor(int index)
    {
        static const QColor palette[] = {
            QColor(0x1f, 0x77, 0xb4), QColor(0xd6, 0x27, 0x28),
            QColor(0x2c, 0xa0, 0x2c), QColor(0xff, 0x7f, 0x0e),
            QColor(0x94, 0x67, 0xbd), QColor(0x8c, 0x56, 0x4b),
        };
        const int n = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
        return palette[((index % n) + n) % n];
    }

    std::vector<double> x_;
    std::vector<QPair<QString, std::vector<double>>> series_;
    std::vector<QPair<QString, double>> referenceLines_;
    QString xLabel_;
    QString yLabel_;
    OpticsPlotStyle style_;
    double xMinLimit_ = 0.0;
    double xMaxLimit_ = 0.0;
};

} // namespace calango::gui
