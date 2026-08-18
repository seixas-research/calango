#include "gui/TernaryClusterHullWidget.hpp"

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

namespace calango::gui {

namespace {

/// ColorBrewer's 11-class "Spectral" scheme, low to high — the exact anchors
/// matplotlib's continuous "Spectral" colormap interpolates between. Used
/// here (not reversed) for formation energy, matching the reference
/// notebook's choice of "Spectral" (not "Spectral_r") for its enthalpy plot.
constexpr std::array<std::array<int, 3>, 11> kSpectralAnchors = {{
    {158, 1, 66},    {213, 62, 79},   {244, 109, 67},  {253, 174, 97},
    {254, 224, 139}, {255, 255, 191}, {230, 245, 152}, {171, 221, 164},
    {102, 194, 165}, {50, 136, 189},  {94, 79, 162},
}};

} // namespace

QColor TernaryClusterHullWidget::spectral(double t)
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

TernaryClusterHullWidget::TernaryClusterHullWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(460, 380);
}

void TernaryClusterHullWidget::setData(core::TernaryConvexHullResult result,
                                       QStringList elements)
{
    result_ = std::move(result);
    elements_ = std::move(elements);
    minEnergy_ = 0.0;
    maxEnergy_ = 0.0;
    bool any = false;
    for (const auto& pt : result_.points) {
        if (!std::isfinite(pt.formationEnergy))
            continue;
        if (!any) {
            minEnergy_ = maxEnergy_ = pt.formationEnergy;
            any = true;
        } else {
            minEnergy_ = std::min(minEnergy_, pt.formationEnergy);
            maxEnergy_ = std::max(maxEnergy_, pt.formationEnergy);
        }
    }
    hasData_ = any;
    update();
}

void TernaryClusterHullWidget::clear()
{
    result_ = {};
    elements_.clear();
    hasData_ = false;
    update();
}

QPointF TernaryClusterHullWidget::project(double xB, double xC) const
{
    // Apex = pure A, bottom-right = pure B, bottom-left = pure C — the same
    // orientation TernarySectionWidget::project() uses.
    const QPointF apex(plotRect_.center().x(), plotRect_.top());
    const QPointF right(plotRect_.right(), plotRect_.bottom());
    const QPointF left(plotRect_.left(), plotRect_.bottom());
    const double xA = 1.0 - xB - xC;
    return QPointF(xA * apex.x() + xB * right.x() + xC * left.x(),
                   xA * apex.y() + xB * right.y() + xC * left.y());
}

QString TernaryClusterHullWidget::toCsv() const
{
    QString out;
    QTextStream stream(&out);
    stream << "# Calango ternary Cluster Expansion — formation energy and "
              "ground-state map\n";
    const QString a = elements_.value(0, QStringLiteral("A"));
    const QString b = elements_.value(1, QStringLiteral("B"));
    const QString c = elements_.value(2, QStringLiteral("C"));
    (void)a; // implied by x_B + x_C, as in TernarySectionWidget::toCsv()
    stream << "label,x_" << b << ",x_" << c << ",formation_energy_eV_per_atom,"
           << "ground_state,energy_above_hull_eV_per_atom\n";
    for (const auto& pt : result_.points) {
        stream << QString::fromStdString(pt.label) << ',' << pt.xB << ','
               << pt.xC << ',' << pt.formationEnergy << ','
               << (pt.onHull ? "1" : "0") << ',' << pt.energyAboveHull << '\n';
    }
    return out;
}

void TernaryClusterHullWidget::exportData()
{
    const QString title = tr("Export Ternary Cluster Expansion Data");
    if (!hasData_) {
        QMessageBox::information(this, title,
                                 tr("No hull data yet — run a ternary "
                                    "Cluster Expansion Calculation first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, title, QStringLiteral("ternary_convex_hull.csv"),
        tr("CSV (*.csv);;Data file (*.dat)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, title,
                             tr("Could not write %1.").arg(path));
        return;
    }
    file.write(toCsv().toUtf8());
}

bool TernaryClusterHullWidget::exportImage(const QString& path, double scale)
{
    if (!hasData_)
        return false;
    savePlotImage(
        this, path, QSize(width(), height()),
        [this](QPainter& painter, const QSize& size) {
            render(painter, QRectF(QPointF(0, 0), size));
        });
    (void)scale; // savePlotImage's own 3x is the print-quality scale used app-wide
    return true;
}

void TernaryClusterHullWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    render(painter, QRectF(rect()));
}

void TernaryClusterHullWidget::render(QPainter& painter, const QRectF& bounds) const
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(bounds, PlotPalette::canvas);

    // Colorbar strip on the right, matching the notebook's cax placement —
    // an inset column reserved before the triangle is sized into the rest.
    const double colorbarWidth = 26.0;
    const double rightMargin = 70.0; // colorbar + its tick labels + gutter
    const QRectF triangleArea(bounds.left(), bounds.top(),
                              bounds.width() - rightMargin, bounds.height());

    const double side =
        std::min(triangleArea.width() - 60.0, triangleArea.height() - 70.0);
    plotRect_ = QRectF(triangleArea.left() + (triangleArea.width() - side) / 2.0,
                       triangleArea.top() + 24.0, std::max(10.0, side),
                       std::max(10.0, side * 0.866));

    if (!hasData_) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(bounds, Qt::AlignCenter,
                         tr("Run a ternary Cluster Expansion for a formation "
                            "energy / ground-state map."));
        return;
    }

    // -- Dashed gridlines at 0.20 composition intervals, all three axes -----
    // Constant x_A (parallel to the B-C base), constant x_B (parallel to the
    // A-C edge) and constant x_C (parallel to the A-B edge) — the three
    // families MultipleLocator(0.20) draws in the reference notebook.
    QPen gridPen(PlotPalette::grid, 1.0, Qt::DashLine);
    painter.setPen(gridPen);
    for (int i = 1; i <= 4; ++i) {
        const double k = i * 0.20;
        painter.drawLine(project(k, 0.0), project(k, 1.0 - k));       // x_B = k
        painter.drawLine(project(0.0, k), project(1.0 - k, k));       // x_C = k
        painter.drawLine(project(1.0 - k, 0.0), project(0.0, 1.0 - k)); // x_A = 1-k
    }

    // -- Scatter: one point per configuration, coloured by formation energy -
    const double span = maxEnergy_ - minEnergy_;
    for (const auto& pt : result_.points) {
        if (!std::isfinite(pt.formationEnergy))
            continue;
        const double t = span > 0.0 ? (pt.formationEnergy - minEnergy_) / span : 0.5;
        const QPointF p = project(pt.xB, pt.xC);
        painter.setBrush(spectral(t));
        if (pt.onHull) {
            // Ground states: larger, black-outlined — the map's whole point.
            painter.setPen(QPen(PlotPalette::spine, 1.6));
            painter.drawEllipse(p, 5.5, 5.5);
        } else {
            painter.setPen(QPen(PlotPalette::spine, 0.5));
            painter.drawEllipse(p, 3.2, 3.2);
        }
    }

    // -- Ground-state wireframe: the lower-hull facets ----------------------
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(PlotPalette::spine, 1.2));
    for (const auto& facet : result_.facets) {
        QPolygonF triangle;
        for (const std::size_t vertex : facet) {
            if (vertex >= result_.points.size())
                continue;
            const auto& pt = result_.points[vertex];
            triangle << project(pt.xB, pt.xC);
        }
        if (triangle.size() == 3)
            painter.drawPolygon(triangle);
    }

    // -- The triangle frame ---------------------------------------------------
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(PlotPalette::spine, 1.8));
    QPolygonF frame;
    frame << project(0.0, 0.0) << project(1.0, 0.0) << project(0.0, 1.0);
    painter.drawPolygon(frame);

    // -- Tick marks at the same 0.20 intervals, along the two lower edges ----
    painter.setPen(QPen(PlotPalette::spine, 1.2));
    const double tickLength = 6.0;
    for (int i = 0; i <= 5; ++i) {
        const double k = i * 0.20;
        // Bottom edge (x_C = 0, varying x_B): ticks drop straight down.
        const QPointF baseTick = project(k, 0.0);
        painter.drawLine(baseTick, baseTick + QPointF(0.0, tickLength));
        // Left edge (x_B = 0, varying x_C): ticks point down-left, normal to
        // the A-C edge's own slope.
        const QPointF leftTick = project(0.0, k);
        painter.drawLine(leftTick, leftTick + QPointF(-tickLength * 0.87,
                                                       tickLength * 0.5));
    }

    // -- Corner labels (species names) ---------------------------------------
    painter.setPen(PlotPalette::text);
    QFont labelFont = painter.font();
    labelFont.setPointSizeF(labelFont.pointSizeF() + 2.0);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    const QFontMetricsF metrics(painter.font());
    const QString a = elements_.value(0, QStringLiteral("A"));
    const QString b = elements_.value(1, QStringLiteral("B"));
    const QString c = elements_.value(2, QStringLiteral("C"));
    const QPointF apex = project(0.0, 0.0);
    painter.drawText(
        QPointF(apex.x() - metrics.horizontalAdvance(a) / 2.0, apex.y() - 8.0), a);
    const QPointF right = project(1.0, 0.0);
    painter.drawText(QPointF(right.x() - metrics.horizontalAdvance(b) / 2.0,
                             right.y() + metrics.height() + 10.0),
                     b);
    const QPointF left = project(0.0, 1.0);
    painter.drawText(QPointF(left.x() - metrics.horizontalAdvance(c) / 2.0,
                             left.y() + metrics.height() + 10.0),
                     c);

    // -- Colorbar --------------------------------------------------------------
    painter.setFont(QFont(painter.font().family(), painter.font().pointSize()));
    const QRectF colorbarRect(bounds.right() - rightMargin + 20.0,
                              plotRect_.top(), colorbarWidth, plotRect_.height());
    QLinearGradient gradient(colorbarRect.topLeft(), colorbarRect.bottomLeft());
    // High energy at the top, low at the bottom — the convention every
    // matplotlib colorbar in the reference notebook uses.
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

    painter.setPen(PlotPalette::tickText);
    QFont tickFont = painter.font();
    tickFont.setPointSizeF(std::max(7.0, tickFont.pointSizeF() - 1.0));
    painter.setFont(tickFont);
    const QFontMetricsF tickMetrics(tickFont);
    const int colorbarTicks = 5;
    for (int i = 0; i <= colorbarTicks; ++i) {
        const double t = static_cast<double>(i) / colorbarTicks;
        const double value = minEnergy_ + (1.0 - t) * span;
        const double y = colorbarRect.top() + t * colorbarRect.height();
        painter.drawLine(QPointF(colorbarRect.right(), y),
                         QPointF(colorbarRect.right() + 4.0, y));
        painter.drawText(
            QPointF(colorbarRect.right() + 7.0, y + tickMetrics.height() / 3.0),
            QString::number(value, 'f', 3));
    }
    painter.save();
    painter.translate(colorbarRect.left() - 30.0,
                      colorbarRect.center().y());
    painter.rotate(-90.0);
    painter.setPen(PlotPalette::text);
    painter.drawText(QPointF(-40.0, 0.0), tr("ΔE_form (eV/atom)"));
    painter.restore();
}

} // namespace calango::gui
