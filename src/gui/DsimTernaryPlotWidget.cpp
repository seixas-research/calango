#include "gui/DsimTernaryPlotWidget.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/PlotPalette.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QMessageBox>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace calango::gui {

namespace {

/// Same ColorBrewer "Spectral" anchors as TernaryClusterHullWidget, for a
/// consistent look between Calango's two ternary composition-triangle
/// plots.
constexpr std::array<std::array<int, 3>, 11> kSpectralAnchors = {{
    {158, 1, 66},    {213, 62, 79},   {244, 109, 67},  {253, 174, 97},
    {254, 224, 139}, {255, 255, 191}, {230, 245, 152}, {171, 221, 164},
    {102, 194, 165}, {50, 136, 189},  {94, 79, 162},
}};

} // namespace

QColor DsimTernaryPlotWidget::spectral(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    const double scaled = t * static_cast<double>(kSpectralAnchors.size() - 1);
    const int lo = std::clamp(static_cast<int>(std::floor(scaled)), 0,
                              static_cast<int>(kSpectralAnchors.size()) - 2);
    const double frac = scaled - lo;
    const auto& a = kSpectralAnchors[static_cast<std::size_t>(lo)];
    const auto& b = kSpectralAnchors[static_cast<std::size_t>(lo + 1)];
    const auto lerp = [frac](int x, int y) {
        return static_cast<int>(std::lround(x + frac * (y - x)));
    };
    return QColor(lerp(a[0], b[0]), lerp(a[1], b[1]), lerp(a[2], b[2]));
}

DsimTernaryPlotWidget::DsimTernaryPlotWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(460, 380);
}

void DsimTernaryPlotWidget::setData(std::vector<GridPoint> grid, int resolution,
                                    QStringList species, QString unitLabel)
{
    grid_ = std::move(grid);
    resolution_ = resolution;
    species_ = std::move(species);
    unitLabel_ = std::move(unitLabel);
    minEnthalpy_ = 0.0;
    maxEnthalpy_ = 0.0;
    bool any = false;
    for (const GridPoint& p : grid_) {
        if (!std::isfinite(p.enthalpy))
            continue;
        if (!any) {
            minEnthalpy_ = maxEnthalpy_ = p.enthalpy;
            any = true;
        } else {
            minEnthalpy_ = std::min(minEnthalpy_, p.enthalpy);
            maxEnthalpy_ = std::max(maxEnthalpy_, p.enthalpy);
        }
    }
    hasData_ = any && resolution_ > 0;
    update();
}

void DsimTernaryPlotWidget::setShowColorbar(bool show)
{
    if (showColorbar_ == show)
        return;
    showColorbar_ = show;
    update();
}

void DsimTernaryPlotWidget::clear()
{
    grid_.clear();
    resolution_ = 0;
    hasData_ = false;
    update();
}

QPointF DsimTernaryPlotWidget::project(double xB, double xC) const
{
    // Apex = pure A (species_[0]), bottom-right = pure B, bottom-left =
    // pure C — the same orientation TernaryClusterHullWidget::project()
    // uses, so the two plots read the same way side by side.
    const QPointF apex(plotRect_.center().x(), plotRect_.top());
    const QPointF right(plotRect_.right(), plotRect_.bottom());
    const QPointF left(plotRect_.left(), plotRect_.bottom());
    const double xA = 1.0 - xB - xC;
    return QPointF(xA * apex.x() + xB * right.x() + xC * left.x(),
                   xA * apex.y() + xB * right.y() + xC * left.y());
}

QString DsimTernaryPlotWidget::toCsv() const
{
    QString out;
    QTextStream stream(&out);
    const QString a = species_.value(0, QStringLiteral("A"));
    const QString b = species_.value(1, QStringLiteral("B"));
    const QString c = species_.value(2, QStringLiteral("C"));
    stream << "# Calango DSIM ternary mixing enthalpy (" << a << "-" << b << "-" << c << ")\n";
    stream << "x_" << b << ",x_" << c << ",x_" << a << ",DeltaH_mix_" << unitLabel_ << "\n";
    for (const GridPoint& p : grid_)
        stream << p.xB << ',' << p.xC << ',' << (1.0 - p.xB - p.xC) << ',' << p.enthalpy << '\n';
    return out;
}

void DsimTernaryPlotWidget::exportData()
{
    const QString title = tr("Export DSIM Ternary Data");
    if (!hasData_) {
        QMessageBox::information(this, title, tr("No ternary result yet."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, title, QStringLiteral("dsim_ternary.csv"),
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

bool DsimTernaryPlotWidget::exportImage(const QString& path, double /*scale*/)
{
    if (!hasData_)
        return false;
    savePlotImage(this, path, QSize(width(), height()), [this](QPainter& painter, const QSize& size) {
        render(painter, QRectF(QPointF(0, 0), size));
    });
    return true;
}

void DsimTernaryPlotWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    render(painter, QRectF(rect()));
}

void DsimTernaryPlotWidget::render(QPainter& painter, const QRectF& bounds) const
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(bounds, PlotPalette::canvas);

    const double colorbarWidth = 26.0;
    // Reserved only when the colorbar actually draws — otherwise the
    // triangle gets the full width, and either way the colorbar itself is
    // positioned off plotRect_'s own right edge below (not off `bounds`),
    // so it sits right next to whatever the triangle actually rendered at
    // rather than leaving a gap sized by how much of triangleArea the
    // (height-constrained, typically) triangle did NOT fill.
    const double rightMargin = showColorbar_ ? 70.0 : 0.0;
    const QRectF triangleArea(bounds.left(), bounds.top(), bounds.width() - rightMargin,
                              bounds.height());
    const double side = std::min(triangleArea.width() - 60.0, triangleArea.height() - 70.0);
    plotRect_ = QRectF(triangleArea.left() + (triangleArea.width() - side) / 2.0,
                       triangleArea.top() + 24.0, std::max(10.0, side), std::max(10.0, side * 0.866));

    if (!hasData_) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(bounds, Qt::AlignCenter,
                         tr("Run a ternary DSIM composition for a mixing-enthalpy map."));
        return;
    }

    // -- Filled mesh: reconstruct the grid's own small-triangle connectivity
    // from its integer barycentric counts (i = round(xB*R), j = round(xC*R),
    // a = R-i-j), then flat-fill each cell with the average of its three
    // corners' enthalpy — the "similar to mpltern" continuous-looking
    // contour, with no interpolation-from-scattered-points needed since
    // the grid IS already a regular lattice on the simplex.
    std::map<std::pair<int, int>, double> byIndex;
    for (const GridPoint& p : grid_) {
        const int i = static_cast<int>(std::lround(p.xB * resolution_));
        const int j = static_cast<int>(std::lround(p.xC * resolution_));
        byIndex[{i, j}] = p.enthalpy;
    }
    const auto valueAt = [&](int i, int j) -> const double* {
        const auto it = byIndex.find({i, j});
        return it != byIndex.end() ? &it->second : nullptr;
    };
    const auto colorFor = [&](double h) {
        const double span = maxEnthalpy_ - minEnthalpy_;
        const double t = span > 0.0 ? (h - minEnthalpy_) / span : 0.5;
        return spectral(t);
    };
    const auto fillTriangle = [&](int i0, int j0, int i1, int j1, int i2, int j2) {
        const double* h0 = valueAt(i0, j0);
        const double* h1 = valueAt(i1, j1);
        const double* h2 = valueAt(i2, j2);
        if (!h0 || !h1 || !h2)
            return;
        QPolygonF triangle;
        triangle << project(static_cast<double>(i0) / resolution_, static_cast<double>(j0) / resolution_)
                 << project(static_cast<double>(i1) / resolution_, static_cast<double>(j1) / resolution_)
                 << project(static_cast<double>(i2) / resolution_, static_cast<double>(j2) / resolution_);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colorFor((*h0 + *h1 + *h2) / 3.0));
        painter.drawPolygon(triangle);
    };
    for (int i = 0; i < resolution_; ++i) {
        for (int j = 0; i + j < resolution_; ++j) {
            fillTriangle(i, j, i + 1, j, i, j + 1); // "upward" cell
            if (i + j < resolution_ - 1)
                fillTriangle(i + 1, j, i, j + 1, i + 1, j + 1); // "downward" cell
        }
    }

    // -- Dashed gridlines at 0.20 composition intervals, all three axes -----
    QPen gridPen(PlotPalette::grid, 1.0, Qt::DashLine);
    painter.setPen(gridPen);
    painter.setBrush(Qt::NoBrush);
    for (int i = 1; i <= 4; ++i) {
        const double k = i * 0.20;
        painter.drawLine(project(k, 0.0), project(k, 1.0 - k));
        painter.drawLine(project(0.0, k), project(1.0 - k, k));
        painter.drawLine(project(1.0 - k, 0.0), project(0.0, 1.0 - k));
    }

    // -- The triangle frame ---------------------------------------------------
    painter.setPen(QPen(PlotPalette::spine, 1.8));
    QPolygonF frame;
    frame << project(0.0, 0.0) << project(1.0, 0.0) << project(0.0, 1.0);
    painter.drawPolygon(frame);

    // -- Tick marks at the same 0.20 intervals -------------------------------
    painter.setPen(QPen(PlotPalette::spine, 1.2));
    const double tickLength = 6.0;
    for (int i = 0; i <= 5; ++i) {
        const double k = i * 0.20;
        const QPointF baseTick = project(k, 0.0);
        painter.drawLine(baseTick, baseTick + QPointF(0.0, tickLength));
        const QPointF leftTick = project(0.0, k);
        painter.drawLine(leftTick, leftTick + QPointF(-tickLength * 0.87, tickLength * 0.5));
    }

    // -- Corner labels (species names) ---------------------------------------
    painter.setPen(PlotPalette::text);
    QFont labelFont = painter.font();
    labelFont.setPointSizeF(labelFont.pointSizeF() + 2.0);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    const QFontMetricsF metrics(painter.font());
    const QString a = species_.value(0, QStringLiteral("A"));
    const QString b = species_.value(1, QStringLiteral("B"));
    const QString c = species_.value(2, QStringLiteral("C"));
    const QPointF apex = project(0.0, 0.0);
    painter.drawText(QPointF(apex.x() - metrics.horizontalAdvance(a) / 2.0, apex.y() - 8.0), a);
    const QPointF right = project(1.0, 0.0);
    painter.drawText(
        QPointF(right.x() - metrics.horizontalAdvance(b) / 2.0, right.y() + metrics.height() + 10.0), b);
    const QPointF left = project(0.0, 1.0);
    painter.drawText(
        QPointF(left.x() - metrics.horizontalAdvance(c) / 2.0, left.y() + metrics.height() + 10.0), c);

    // -- Colorbar --------------------------------------------------------------
    // Anchored off plotRect_'s own right edge (the triangle's ACTUAL
    // rendered boundary), not `bounds`, so it sits right next to the
    // triangle regardless of how much of triangleArea the (typically
    // height-constrained) triangle left unfilled.
    if (showColorbar_) {
        constexpr double kGapFromTriangle = 14.0;
        const QRectF colorbarRect(plotRect_.right() + kGapFromTriangle, plotRect_.top(),
                                  colorbarWidth, plotRect_.height());
        QLinearGradient gradient(colorbarRect.topLeft(), colorbarRect.bottomLeft());
        const int steps = 32;
        for (int i = 0; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            gradient.setColorAt(t, spectral(1.0 - t));
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawRect(colorbarRect);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(PlotPalette::spine, 1.4));
        painter.drawRect(colorbarRect);

        // Text colour, not the fainter tick colour: a colorbar's numbers
        // ARE its axis, unlike the triangle's own edges (which carry no
        // numeric labels at all, only the dashed gridlines + corner names).
        painter.setPen(PlotPalette::text);
        QFont tickFont = painter.font();
        tickFont.setPointSizeF(std::max(7.0, tickFont.pointSizeF() - 1.0));
        painter.setFont(tickFont);
        const QFontMetricsF tickMetrics(tickFont);
        const int colorbarTicks = 5;
        const double span = maxEnthalpy_ - minEnthalpy_;
        for (int i = 0; i <= colorbarTicks; ++i) {
            const double t = static_cast<double>(i) / colorbarTicks;
            const double value = minEnthalpy_ + (1.0 - t) * span;
            const double y = colorbarRect.top() + t * colorbarRect.height();
            painter.drawLine(QPointF(colorbarRect.right(), y), QPointF(colorbarRect.right() + 4.0, y));
            painter.drawText(QPointF(colorbarRect.right() + 7.0, y + tickMetrics.height() / 3.0),
                             QString::number(value, 'f', 2));
        }
        painter.save();
        painter.translate(colorbarRect.left() - 30.0, colorbarRect.center().y());
        painter.rotate(-90.0);
        painter.setPen(PlotPalette::text);
        // Real Delta, not the literal "DH_mix" this used to read.
        drawWithSubscripts(painter, QRectF(-60.0, -12.0, 120.0, 24.0),
                           tr("ΔH_{mix} (%1)").arg(unitLabel_));
        painter.restore();
    }
}

} // namespace calango::gui
