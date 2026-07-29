#include "gui/ConvexHullPlotWidget.hpp"

#include "gui/GuiUtils.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QTextStream>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <limits>

namespace calango::gui {

namespace {

constexpr double kPickRadius = 9.0;

/// Same 1/2/5·10ⁿ axis heuristic as MetricPlotWidget, kept local because the
/// two widgets otherwise share nothing.
double niceTickStep(double range, int maxTicks)
{
    if (range <= 0.0 || maxTicks < 1)
        return 0.0;
    const double rough = range / maxTicks;
    const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
    const double normalized = rough / magnitude;
    const double nice = normalized <= 1.0 ? 1.0
        : normalized <= 2.0              ? 2.0
        : normalized <= 5.0              ? 5.0
                                         : 10.0;
    return nice * magnitude;
}

} // namespace

ConvexHullPlotWidget::ConvexHullPlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
    setMouseTracking(true); // hover readout without a pressed button
}

void ConvexHullPlotWidget::clear()
{
    result_ = {};
    axisSpecies_.clear();
    screenPositions_.clear();
    hoverIndex_ = -1;
    update();
}

void ConvexHullPlotWidget::setResult(core::ConvexHullResult result,
                                     const QString& axisSpecies)
{
    result_ = std::move(result);
    axisSpecies_ = axisSpecies;
    hoverIndex_ = -1;
    update();
}

bool ConvexHullPlotWidget::loadFromJson(const QString& path)
{
    const QJsonObject root = readJsonObject(path);
    if (root.isEmpty()) {
        clear();
        return false;
    }

    std::vector<core::HullPoint> points;
    for (const auto& entry : root[QStringLiteral("configurations")].toArray()) {
        const QJsonObject config = entry.toObject();
        // A configuration whose relaxation failed carries a null formation
        // energy. Keep it out of the diagram rather than plotting a zero that
        // would look like a stable phase.
        const auto formation = config[QStringLiteral("formation_energy")];
        if (formation.isNull() || formation.isUndefined())
            continue;

        core::HullPoint point;
        point.concentration = config[QStringLiteral("concentration")].toDouble();
        point.formationEnergy = formation.toDouble();
        point.energyPerAtom = config[QStringLiteral("energy_per_atom")].toDouble();
        point.frameIndex = config[QStringLiteral("frame")].toInt(-1);
        point.atomCount = static_cast<std::size_t>(
            config[QStringLiteral("natoms")].toInt(0));
        point.label = config[QStringLiteral("formula")].toString().toStdString();
        points.push_back(std::move(point));
    }
    if (points.empty()) {
        clear();
        return false;
    }

    setResult(core::computeConvexHull(std::move(points)),
              root[QStringLiteral("concentration_element")].toString());
    return true;
}

void ConvexHullPlotWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(28, 30, 34));
    screenPositions_.assign(result_.points.size(), QPointF());

    if (result_.points.empty()) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("Run Simulation → Cluster Expansion Calculation…\n"
                            "to build a formation-energy hull."));
        return;
    }

    // --- Ranges -------------------------------------------------------------
    double xLo = std::numeric_limits<double>::max();
    double xHi = std::numeric_limits<double>::lowest();
    double yLo = 0.0; // always include zero: it is the endpoint reference
    double yHi = 0.0;
    for (const auto& p : result_.points) {
        xLo = std::min(xLo, p.concentration);
        xHi = std::max(xHi, p.concentration);
        yLo = std::min(yLo, p.formationEnergy);
        yHi = std::max(yHi, p.formationEnergy);
    }
    if (xHi - xLo < 1e-9) {
        xLo -= 0.05;
        xHi += 0.05;
    }
    if (yHi - yLo < 1e-9) {
        yLo -= 0.05;
        yHi += 0.05;
    }
    const double yPad = (yHi - yLo) * 0.08;
    yLo -= yPad;
    yHi += yPad;

    const QRectF plot = rect().adjusted(76, 14, -14, -46);
    const auto toX = [&](double x) {
        return plot.left() + plot.width() * (x - xLo) / (xHi - xLo);
    };
    const auto toY = [&](double y) {
        return plot.bottom() - plot.height() * (y - yLo) / (yHi - yLo);
    };

    painter.setPen(QColor(90, 95, 105));
    painter.drawRect(plot);

    // --- Ticks ---------------------------------------------------------------
    const QFontMetricsF metrics(painter.font());
    const QColor gridColor(52, 56, 63);
    const QColor tickColor(140, 146, 156);
    {
        const double step = niceTickStep(xHi - xLo, 8);
        for (double t = std::ceil(xLo / step) * step; step > 0.0 && t <= xHi;
             t += step) {
            const double x = toX(t);
            painter.setPen(gridColor);
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            painter.setPen(tickColor);
            painter.drawText(QRectF(x - 30, plot.bottom() + 4, 60, 14),
                             Qt::AlignHCenter, QString::number(t, 'f', 2));
        }
        const double yStep = niceTickStep(yHi - yLo, 6);
        for (double t = std::ceil(yLo / yStep) * yStep; yStep > 0.0 && t <= yHi;
             t += yStep) {
            const double y = toY(t);
            painter.setPen(gridColor);
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(tickColor);
            painter.drawText(QRectF(4, y - 7, plot.left() - 10, 14),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(t, 'f', 3));
        }
    }

    // E_form = 0 is the two-phase mixture line: everything below it is stable
    // against decomposition into the endpoints.
    if (yLo < 0.0 && yHi > 0.0) {
        painter.setPen(QPen(QColor(120, 126, 138), 1.0, Qt::DashLine));
        painter.drawLine(QPointF(plot.left(), toY(0.0)),
                         QPointF(plot.right(), toY(0.0)));
    }

    // --- Tie-lines -----------------------------------------------------------
    const QColor hullColor(90, 200, 140);
    if (result_.hullIndices.size() >= 2) {
        painter.setPen(QPen(hullColor, 2.0));
        for (std::size_t i = 0; i + 1 < result_.hullIndices.size(); ++i) {
            const auto& a = result_.points[result_.hullIndices[i]];
            const auto& b = result_.points[result_.hullIndices[i + 1]];
            painter.drawLine(
                QPointF(toX(a.concentration), toY(a.formationEnergy)),
                QPointF(toX(b.concentration), toY(b.formationEnergy)));
        }
    }

    // --- Points --------------------------------------------------------------
    // Metastable first so stable vertices always draw on top of them.
    const QColor unstableColor(150, 156, 168);
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t i = 0; i < result_.points.size(); ++i) {
            const auto& p = result_.points[i];
            if ((pass == 0) == p.onHull)
                continue;
            const QPointF at(toX(p.concentration), toY(p.formationEnergy));
            screenPositions_[i] = at;
            const bool hovered = static_cast<int>(i) == hoverIndex_;
            if (p.onHull) {
                // Filled = on the hull = thermodynamically stable.
                painter.setBrush(hullColor);
                painter.setPen(QPen(hullColor.darker(140), 1.2));
            } else {
                // Hollow = above the hull = metastable.
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(unstableColor, 1.4));
            }
            painter.drawEllipse(at, hovered ? 6.5 : 4.5, hovered ? 6.5 : 4.5);
        }
    }

    // --- Labels --------------------------------------------------------------
    painter.setPen(QColor(170, 175, 185));
    const int stable = static_cast<int>(result_.hullIndices.size());
    painter.drawText(
        QRectF(plot.left(), plot.bottom() + 20, plot.width(), 16), Qt::AlignHCenter,
        axisSpecies_.isEmpty()
            ? tr("Concentration x   (%1 configurations, %2 on hull)")
                  .arg(result_.points.size())
                  .arg(stable)
            : tr("x(%1)   (%2 configurations, %3 on hull)")
                  .arg(axisSpecies_)
                  .arg(result_.points.size())
                  .arg(stable));
    painter.save();
    painter.translate(14, plot.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-110, -8, 220, 16), Qt::AlignCenter,
                     tr("Formation energy (eV/atom)"));
    painter.restore();
}

int ConvexHullPlotWidget::pointAt(const QPointF& pos) const
{
    int best = -1;
    double bestDistance = kPickRadius;
    for (std::size_t i = 0; i < screenPositions_.size(); ++i) {
        if (screenPositions_[i].isNull())
            continue;
        const double d = std::hypot(screenPositions_[i].x() - pos.x(),
                                    screenPositions_[i].y() - pos.y());
        if (d < bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void ConvexHullPlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    const int index = pointAt(event->position());
    if (index != hoverIndex_) {
        hoverIndex_ = index;
        update();
    }
    if (index < 0) {
        QToolTip::hideText();
        return;
    }
    const auto& p = result_.points[static_cast<std::size_t>(index)];
    QToolTip::showText(
        event->globalPosition().toPoint(),
        tr("%1  (frame %2)\nx = %3\nE_form = %4 eV/atom\n%5")
            .arg(QString::fromStdString(p.label))
            .arg(p.frameIndex)
            .arg(p.concentration, 0, 'f', 4)
            .arg(p.formationEnergy, 0, 'f', 4)
            .arg(p.onHull ? tr("On the hull — stable")
                          : tr("%1 eV/atom above the hull")
                                .arg(p.energyAboveHull, 0, 'f', 4)),
        this);
}

void ConvexHullPlotWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    const int index = pointAt(event->position());
    if (index < 0)
        return;
    const int frame = result_.points[static_cast<std::size_t>(index)].frameIndex;
    if (frame >= 0)
        Q_EMIT frameActivated(frame);
}

void ConvexHullPlotWidget::leaveEvent(QEvent*)
{
    if (hoverIndex_ != -1) {
        hoverIndex_ = -1;
        update();
    }
}

void ConvexHullPlotWidget::exportData()
{
    const QString title = tr("Export Convex Hull Data");
    if (result_.points.empty()) {
        QMessageBox::information(this, title,
                                 tr("No hull data yet — run a Cluster "
                                    "Expansion Calculation first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, title, QStringLiteral("convex_hull.csv"),
        tr("CSV (*.csv);;Data file (*.dat)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, title, tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    // '#' comments only in the .dat flavour — a CSV starts with its header.
    if (path.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive)) {
        out << "# Calango cluster-expansion formation-energy hull\n";
        if (!axisSpecies_.isEmpty())
            out << "# concentration axis: x(" << axisSpecies_ << ")\n";
    }
    out << "frame,formula,natoms,concentration,energy_per_atom_eV,"
           "formation_energy_eV_per_atom,energy_above_hull_eV_per_atom,on_hull\n";
    for (const auto& p : result_.points) {
        out << p.frameIndex << ',' << QString::fromStdString(p.label) << ','
            << p.atomCount << ',' << QString::number(p.concentration, 'f', 6)
            << ',' << QString::number(p.energyPerAtom, 'f', 6) << ','
            << QString::number(p.formationEnergy, 'f', 6) << ','
            << QString::number(p.energyAboveHull, 'f', 6) << ','
            << (p.onHull ? 1 : 0) << '\n';
    }
}

} // namespace calango::gui
