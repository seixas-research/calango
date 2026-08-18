#pragma once

#include <QColor>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <vector>

class QPainter;

namespace calango::gui {

/// A generic multi-series line plot for EGQCA results (Task 1) — reused for
/// both of the paper's own characteristic figure shapes: a thermodynamic
/// function against composition with one curve per temperature (Fig. 2c-f
/// style), and cluster occurrence probabilities against temperature at a
/// fixed composition (Fig. 3 style). One widget rather than two purpose-
/// built ones, since both are "several (label, colour, x/y points) series
/// against one pair of axes" and nothing else about them differs.
///
/// Native QPainter, matching every other Calango plot (see PlotPalette.hpp).
class EgqcaPlotWidget : public QWidget {
    Q_OBJECT

public:
    struct Series {
        QString label;
        QColor color;
        std::vector<QPointF> points; ///< (x, y), any order — sorted on entry
    };

    explicit EgqcaPlotWidget(QWidget* parent = nullptr);

    void setSeries(std::vector<Series> series, QString xLabel, QString yLabel);
    void clear();
    bool hasData() const { return !series_.empty(); }

    QString toCsv() const;
    bool exportImage(const QString& path, double scale = 3.0);
    void render(QPainter& painter, const QRectF& bounds) const;

public Q_SLOTS:
    void exportData();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<Series> series_;
    QString xLabel_;
    QString yLabel_;
};

} // namespace calango::gui
