#pragma once

#include <QString>
#include <QWidget>

#include <vector>

namespace calango::gui {

/// Interactive 2D line chart (QPainter, dependency-free): autoscaled axes
/// with ticks and grid, and a hover crosshair that reads out the nearest
/// data point. Swappable for QCustomPlot later without touching callers.
class LinePlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit LinePlotWidget(QWidget* parent = nullptr);

    void setData(std::vector<double> x, std::vector<double> y);
    void setAxisLabels(const QString& xLabel, const QString& yLabel);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRectF plotRect() const;

    std::vector<double> x_;
    std::vector<double> y_;
    QString xLabel_;
    QString yLabel_;
    double xMin_ = 0.0, xMax_ = 1.0, yMin_ = 0.0, yMax_ = 1.0;
    int hoverIndex_ = -1;
};

} // namespace calango::gui
