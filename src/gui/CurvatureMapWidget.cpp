#include "gui/CurvatureMapWidget.hpp"

#include "gui/PlotPalette.hpp"

#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace calango::gui {

CurvatureMapWidget::CurvatureMapWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(420, 360);
}

void CurvatureMapWidget::clear()
{
    hasData_ = false;
    image_ = QImage();
    update();
}

void CurvatureMapWidget::setGradient(render::ColorGradient gradient)
{
    gradient_ = gradient;
    rebuild();
    update();
}

void CurvatureMapWidget::setMap(const core::BerryPhase::CurvatureMap& map)
{
    map_ = map;
    hasData_ = !map_.values.empty() && !map_.values.front().empty();
    rebuild();
    update();
}

void CurvatureMapWidget::rebuild()
{
    if (!hasData_) {
        image_ = QImage();
        return;
    }
    const int w = static_cast<int>(map_.values.size());
    const int h = static_cast<int>(map_.values.front().size());
    image_ = QImage(w, h, QImage::Format_ARGB32);

    // Symmetric about zero: curvature changes sign, and a scale running from
    // its own minimum to its own maximum would put "zero" at an arbitrary
    // colour that moves whenever the data does.
    const double extent =
        std::max(std::abs(map_.minimum), std::abs(map_.maximum));
    const double scale = (extent > 0.0) ? extent : 1.0;

    for (int i = 0; i < w; ++i)
        for (int j = 0; j < h; ++j) {
            const double t = 0.5
                + 0.5
                    * std::clamp(map_.values[static_cast<std::size_t>(i)]
                                         [static_cast<std::size_t>(j)]
                                     / scale,
                                 -1.0, 1.0);
            // Row 0 of the image is the TOP; axis-2 index 0 is the BOTTOM of
            // the plot, so the vertical index is flipped.
            image_.setPixelColor(
                i, h - 1 - j,
                render::ColorMap::sample(gradient_, static_cast<float>(t)));
        }
}

void CurvatureMapWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), PlotPalette::canvas);

    if (!hasData_) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("Compute a Berry curvature map to see it here."));
        return;
    }

    const QRectF plot = rect().adjusted(58, 14, -76, -44);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(plot, image_);
    painter.setPen(QPen(PlotPalette::spine, 1.2));
    painter.drawRect(plot);

    painter.setPen(PlotPalette::text);
    painter.drawText(QRectF(plot.left(), plot.bottom() + 6, plot.width(), 20),
                     Qt::AlignHCenter, tr("k₁ (fractional)"));
    painter.save();
    painter.translate(18, plot.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-plot.height() / 2.0, -10, plot.height(), 20),
                     Qt::AlignHCenter, tr("k₂ (fractional)"));
    painter.restore();

    // Colour scale, labelled with the symmetric extent it actually spans.
    const QRectF bar(plot.right() + 16, plot.top(), 14, plot.height());
    for (int y = 0; y < static_cast<int>(bar.height()); ++y) {
        const double t = 1.0 - static_cast<double>(y) / std::max(1.0, bar.height());
        painter.setPen(render::ColorMap::sample(gradient_, static_cast<float>(t)));
        painter.drawLine(QPointF(bar.left(), bar.top() + y),
                         QPointF(bar.right(), bar.top() + y));
    }
    painter.setPen(QPen(PlotPalette::spine, 1.0));
    painter.drawRect(bar);

    const double extent =
        std::max(std::abs(map_.minimum), std::abs(map_.maximum));
    painter.setPen(PlotPalette::text);
    painter.drawText(QRectF(bar.right() + 3, bar.top() - 9, 70, 18),
                     Qt::AlignLeft, QStringLiteral("+%1").arg(extent, 0, 'g', 3));
    painter.drawText(QRectF(bar.right() + 3, bar.bottom() - 9, 70, 18),
                     Qt::AlignLeft, QStringLiteral("-%1").arg(extent, 0, 'g', 3));
    painter.drawText(QRectF(bar.right() + 3, bar.center().y() - 9, 70, 18),
                     Qt::AlignLeft, QStringLiteral("0"));
}

} // namespace calango::gui
