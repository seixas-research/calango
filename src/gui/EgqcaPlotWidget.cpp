#include "gui/EgqcaPlotWidget.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/PlotPalette.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QMessageBox>
#include <QPainter>
#include <QPaintEvent>
#include <QTextStream>

#include <algorithm>
#include <limits>

namespace calango::gui {

EgqcaPlotWidget::EgqcaPlotWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(480, 340);
}

void EgqcaPlotWidget::setSeries(std::vector<Series> series, QString xLabel,
                                QString yLabel)
{
    series_ = std::move(series);
    for (auto& s : series_)
        std::sort(s.points.begin(), s.points.end(),
                 [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    xLabel_ = std::move(xLabel);
    yLabel_ = std::move(yLabel);
    update();
}

void EgqcaPlotWidget::clear()
{
    series_.clear();
    update();
}

QString EgqcaPlotWidget::toCsv() const
{
    QString out;
    QTextStream stream(&out);
    stream << "# " << xLabel_ << " vs " << yLabel_ << "\n";
    stream << "series," << xLabel_ << "," << yLabel_ << "\n";
    for (const auto& s : series_)
        for (const QPointF& p : s.points)
            stream << s.label << ',' << p.x() << ',' << p.y() << '\n';
    return out;
}

void EgqcaPlotWidget::exportData()
{
    const QString title = tr("Export EGQCA Data");
    if (!hasData()) {
        QMessageBox::information(this, title, tr("No EGQCA result yet — solve first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, title, QStringLiteral("egqca.csv"),
        tr("CSV (*.csv);;Data file (*.dat)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, title, tr("Could not write %1.").arg(path));
        return;
    }
    file.write(toCsv().toUtf8());
}

bool EgqcaPlotWidget::exportImage(const QString& path, double /*scale*/)
{
    if (!hasData())
        return false;
    savePlotImage(this, path, QSize(width(), height()),
                 [this](QPainter& painter, const QSize& size) {
                     render(painter, QRectF(QPointF(0, 0), size));
                 });
    return true;
}

void EgqcaPlotWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    render(painter, QRectF(rect()));
}

void EgqcaPlotWidget::render(QPainter& painter, const QRectF& bounds) const
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(bounds, PlotPalette::canvas);

    if (series_.empty()) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(bounds, Qt::AlignCenter,
                         tr("Set up clusters and press Solve for an EGQCA plot."));
        return;
    }

    double xLo = std::numeric_limits<double>::max();
    double xHi = std::numeric_limits<double>::lowest();
    double yLo = std::numeric_limits<double>::max();
    double yHi = std::numeric_limits<double>::lowest();
    for (const auto& s : series_)
        for (const QPointF& p : s.points) {
            xLo = std::min(xLo, p.x());
            xHi = std::max(xHi, p.x());
            yLo = std::min(yLo, p.y());
            yHi = std::max(yHi, p.y());
        }
    if (xHi - xLo < 1e-12) {
        xLo -= 0.5;
        xHi += 0.5;
    }
    if (yHi - yLo < 1e-12) {
        yLo -= 0.5;
        yHi += 0.5;
    }
    const double yPad = (yHi - yLo) * 0.08;
    yLo -= yPad;
    yHi += yPad;

    // Legend column on the right when there are few enough series to list;
    // beyond that a legend would cost more width than it gives back (EGQCA's
    // multi-temperature plots routinely carry 15+ curves — Fig. 2c/e/f in
    // the working paper are exactly this), so the colour gradient itself
    // (set by the caller, typically cool-to-warm with T) is left to carry
    // the meaning instead, same as the paper's own colourbar-free approach.
    const bool showLegend = series_.size() <= 8;
    const double legendWidth = showLegend ? 130.0 : 0.0;
    const QRectF plot =
        bounds.adjusted(70, 14, -14 - legendWidth, -46);
    const auto toX = [&](double x) {
        return plot.left() + plot.width() * (x - xLo) / (xHi - xLo);
    };
    const auto toY = [&](double y) {
        return plot.bottom() - plot.height() * (y - yLo) / (yHi - yLo);
    };

    painter.setPen(PlotPalette::spine);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(plot);

    const QColor gridColor = PlotPalette::grid;
    const QColor tickColor = PlotPalette::tickText;
    const double xStep = niceTickStep(xHi - xLo, 8, 0.0);
    for (double t = std::ceil(xLo / xStep) * xStep; xStep > 0.0 && t <= xHi;
         t += xStep) {
        const double x = toX(t);
        painter.setPen(gridColor);
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter.setPen(tickColor);
        painter.drawText(QRectF(x - 30, plot.bottom() + 4, 60, 14),
                         Qt::AlignHCenter, QString::number(t, 'g', 4));
    }
    const double yStep = niceTickStep(yHi - yLo, 6, 0.0);
    for (double t = std::ceil(yLo / yStep) * yStep; yStep > 0.0 && t <= yHi;
         t += yStep) {
        const double y = toY(t);
        painter.setPen(gridColor);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(tickColor);
        painter.drawText(QRectF(4, y - 7, plot.left() - 10, 14),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(t, 'g', 4));
    }
    if (yLo < 0.0 && yHi > 0.0) {
        painter.setPen(QPen(PlotPalette::spine, 1.0, Qt::DashLine));
        painter.drawLine(QPointF(plot.left(), toY(0.0)),
                         QPointF(plot.right(), toY(0.0)));
    }

    for (const auto& s : series_) {
        if (s.points.size() < 2)
            continue;
        painter.setPen(QPen(s.color, 1.8));
        for (std::size_t i = 0; i + 1 < s.points.size(); ++i)
            painter.drawLine(toX(s.points[i].x()), toY(s.points[i].y()),
                             toX(s.points[i + 1].x()), toY(s.points[i + 1].y()));
    }

    painter.setPen(PlotPalette::text);
    QFontMetricsF metrics(painter.font());
    // "_x"/"_{mix}" in xLabel_/yLabel_ renders as a real typographic
    // subscript here (drawWithSubscripts), not a literal underscore — the
    // same convention the band/PDOS plot and effective-band heatmap use, so
    // a caller can write "DeltaH_{mix} (kJ/mol)"-style labels and get
    // "ΔH" with a properly subscripted "mix".
    drawWithSubscripts(
        painter, QRectF(plot.left(), bounds.bottom() - 22, plot.width(), 18), xLabel_);
    painter.save();
    painter.translate(14, plot.center().y());
    painter.rotate(-90.0);
    drawWithSubscripts(painter, QRectF(-plot.height() / 2.0, -10, plot.height(), 18),
                       yLabel_);
    painter.restore();

    if (showLegend) {
        double y = plot.top();
        const double lineHeight = metrics.height() + 4.0;
        for (const auto& s : series_) {
            painter.setPen(QPen(s.color, 2.5));
            painter.drawLine(QPointF(plot.right() + 10, y + lineHeight / 2.0),
                             QPointF(plot.right() + 28, y + lineHeight / 2.0));
            painter.setPen(PlotPalette::text);
            // Plain text, not drawWithSubscripts: that helper only centers,
            // and a legend entry has to stay left-aligned next to its
            // colour swatch line.
            painter.drawText(
                QRectF(plot.right() + 32, y, legendWidth - 34, lineHeight),
                Qt::AlignVCenter | Qt::AlignLeft, s.label);
            y += lineHeight;
            if (y > bounds.bottom() - 30)
                break; // more series than the column has room for
        }
    }
}

} // namespace calango::gui
