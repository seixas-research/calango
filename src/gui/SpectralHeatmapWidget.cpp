#include "gui/SpectralHeatmapWidget.hpp"

#include "gui/GuiUtils.hpp"
#include "gui/PlotPalette.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QPainter>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace calango::gui {

namespace {

// The canvas, text and spine colours and the tick size that used to live here
// as constants are now Style fields, defaulted to the same PlotPalette values
// in SpectralHeatmapWidget::Style — so the published look is unchanged and it
// is now adjustable, which is what the appearance dialog edits.

QString prettyLabel(const QString& raw)
{
    return raw == QLatin1String("G") ? QString::fromUtf8("Γ") : raw;
}

} // namespace

SpectralHeatmapWidget::SpectralHeatmapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(240);
}

void SpectralHeatmapWidget::clear()
{
    columns_.clear();
    spectral_ = {};
    heatmap_ = QImage();
    specialX_.clear();
    specialLabels_.clear();
    update();
}

bool SpectralHeatmapWidget::loadFromJson(const QString& path)
{
    const QJsonObject root = readJsonObject(path);
    if (root.isEmpty()) {
        clear();
        return false;
    }

    columns_.clear();
    for (const auto& entry : root[QStringLiteral("columns")].toArray()) {
        const QJsonObject object = entry.toObject();
        const QJsonArray energies = object[QStringLiteral("energies")].toArray();
        const QJsonArray weights = object[QStringLiteral("weights")].toArray();
        core::UnfoldedColumn column;
        column.pathCoordinate =
            object[QStringLiteral("path_coordinate")].toDouble();
        // Energies and weights are index-aligned per band; a mismatch means a
        // truncated run, so take only the pairs that exist.
        const int count = std::min(energies.size(), weights.size());
        column.states.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            column.states.push_back({energies.at(i).toDouble(),
                                     weights.at(i).toDouble()});
        }
        columns_.push_back(std::move(column));
    }
    if (columns_.empty()) {
        clear();
        return false;
    }

    fermi_ = root[QStringLiteral("efermi")].toDouble();
    options_.energyMin = root[QStringLiteral("energy_min")].toDouble(-10.0);
    options_.energyMax = root[QStringLiteral("energy_max")].toDouble(10.0);
    options_.energyBins = root[QStringLiteral("energy_bins")].toInt(400);
    options_.sigma = root[QStringLiteral("sigma")].toDouble(0.05);
    options_.weightThreshold =
        root[QStringLiteral("weight_threshold")].toDouble(1e-4);

    specialX_ = toDoubleVector(root[QStringLiteral("special_x")].toArray());
    specialLabels_.clear();
    for (const auto& label : root[QStringLiteral("special_labels")].toArray())
        specialLabels_ << label.toString();

    rebuild();
    return spectral_.valid();
}

void SpectralHeatmapWidget::rebuild()
{
    // The stored weights are the expensive part (a DFT run); broadening them
    // is cheap, so sigma and the energy window stay adjustable here without
    // recomputing anything.
    core::SpectralFunctionOptions options = options_;
    if (shiftFermi_) {
        // Shift the states, not the window: the window the user set is always
        // read in the displayed coordinate.
        std::vector<core::UnfoldedColumn> shifted = columns_;
        for (auto& column : shifted)
            for (auto& state : column.states)
                state.energy -= fermi_;
        spectral_ = core::computeSpectralFunction(shifted, options);
    } else {
        // Absolute energies: slide the window up to bracket E_F.
        options.energyMin += fermi_;
        options.energyMax += fermi_;
        spectral_ = core::computeSpectralFunction(columns_, options);
    }
    rebuildImage();
    update();
}

void SpectralHeatmapWidget::rebuildImage()
{
    if (!spectral_.valid() || spectral_.maxIntensity <= 0.0) {
        heatmap_ = QImage();
        return;
    }
    const int width = static_cast<int>(spectral_.intensity.size());
    const int height = static_cast<int>(spectral_.energies.size());
    // One pixel per (k-column, energy-bin); QPainter scales it to the plot
    // rect with smooth interpolation, so the resolution follows the data
    // rather than the window size.
    heatmap_ = QImage(width, height, QImage::Format_ARGB32);
    heatmap_.fill(style_.background);

    for (int x = 0; x < width; ++x) {
        const auto& profile = spectral_.intensity[static_cast<std::size_t>(x)];
        for (int y = 0; y < height; ++y) {
            const double normalized =
                profile[static_cast<std::size_t>(y)] / spectral_.maxIntensity;
            if (normalized <= style_.intensityThreshold)
                continue; // below the noise floor: leave it as background
            // Rescale the surviving range back to [0, 1] so raising the
            // threshold brightens what remains instead of just clipping it.
            const double t = std::clamp((normalized - style_.intensityThreshold)
                                            / std::max(1e-9,
                                                       1.0
                                                           - style_.intensityThreshold),
                                        0.0, 1.0);
            QColor color =
                render::ColorMap::sample(style_.gradient, static_cast<float>(t));
            // Opacity blended against the plot background rather than written
            // into the alpha channel: the image is drawn opaque over the
            // canvas, so a translucent pixel would composite against whatever
            // QImage was initialised with instead of against the plot.
            if (style_.opacity < 1.0) {
                const double a = std::clamp(style_.opacity, 0.0, 1.0);
                color = QColor(
                    static_cast<int>(std::lround(color.red() * a
                                                 + style_.background.red() * (1.0 - a))),
                    static_cast<int>(std::lround(color.green() * a
                                                 + style_.background.green() * (1.0 - a))),
                    static_cast<int>(std::lround(color.blue() * a
                                                 + style_.background.blue() * (1.0 - a))));
            }
            // Row 0 of the image is the TOP of the plot, but bin 0 is the
            // LOWEST energy — flip so energy increases upward.
            heatmap_.setPixelColor(x, height - 1 - y, color);
        }
    }
}

void SpectralHeatmapWidget::setGradient(render::ColorGradient gradient)
{
    style_.gradient = gradient;
    rebuildImage();
    update();
}

void SpectralHeatmapWidget::setIntensityThreshold(double fraction)
{
    style_.intensityThreshold = std::clamp(fraction, 0.0, 0.99);
    rebuildImage();
    update();
}

void SpectralHeatmapWidget::setStyle(const Style& style)
{
    const bool windowChanged = false; // the window has its own setter
    style_ = style;
    style_.intensityThreshold = std::clamp(style_.intensityThreshold, 0.0, 0.99);
    style_.opacity = std::clamp(style_.opacity, 0.0, 1.0);
    style_.markerSize = std::clamp(style_.markerSize, 0.5, 40.0);
    (void)windowChanged;
    // Only the image depends on the style; the geometry does not, so a repaint
    // plus a re-tint is enough and the spectral function is not recomputed.
    rebuildImage();
    update();
}

void SpectralHeatmapWidget::setEnergyWindow(double minimum, double maximum)
{
    if (!(maximum > minimum))
        return;
    options_.energyMin = minimum;
    options_.energyMax = maximum;
    rebuild(); // re-bins, so this one really does need the full path
}

void SpectralHeatmapWidget::setShiftFermiToZero(bool shift)
{
    if (shiftFermi_ == shift)
        return;
    shiftFermi_ = shift;
    rebuild();
}

void SpectralHeatmapWidget::setSigma(double sigma)
{
    options_.sigma = std::max(sigma, 1e-4);
    rebuild();
}

void SpectralHeatmapWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), style_.background);

    if (!spectral_.valid() || heatmap_.isNull()) {
        painter.setPen(PlotPalette::placeholder);
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("Run Simulation → Effective Bands…\n"
                            "to compute an unfolded spectral function."));
        return;
    }

    QFont font = painter.font();
    font.setPointSizeF(style_.tickPointSize);
    painter.setFont(font);
    const QFontMetricsF metrics(font);

    const QRectF plot =
        rect().adjusted(78, 14, -14 - colorbarWidth(), -52);

    const double eLo0 = spectral_.energies.front();
    const double eHi0 = spectral_.energies.back();
    if (style_.mode == RenderMode::Scatter) {
        // The eigenvalues themselves, unbroadened. Background painted first so
        // the markers land on the same canvas the heatmap would have used.
        painter.fillRect(plot, style_.background);
        paintScatter(painter, plot, eLo0, eHi0);
    } else {
        // Smooth transformation: the data grid is usually coarser than the
        // widget, and nearest-neighbour would turn a physical spectrum into
        // visible blocks.
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(plot, heatmap_);
    }
    painter.setPen(QPen(style_.spineColor, style_.spineWidth));
    painter.drawRect(plot);
    if (style_.showColorbar)
        paintColorbar(painter, plot);

    const double eLo = spectral_.energies.front();
    const double eHi = spectral_.energies.back();
    const auto toY = [&](double e) {
        return plot.bottom() - plot.height() * (e - eLo) / std::max(1e-9, eHi - eLo);
    };

    // Energy ticks.
    painter.setPen(style_.textColor);
    for (int i = 0; i <= 5; ++i) {
        const double e = eLo + (eHi - eLo) * i / 5.0;
        const double y = toY(e);
        painter.drawLine(QPointF(plot.left() - 5, y), QPointF(plot.left(), y));
        painter.drawText(QRectF(6, y - 11, plot.left() - 14, 22),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(e, 'f', 1));
    }

    // High-symmetry verticals. The path coordinate is the x axis, so the
    // special points map through the same span as the image columns.
    if (!spectral_.pathCoordinates.empty()) {
        const double xLo = spectral_.pathCoordinates.front();
        const double xHi = spectral_.pathCoordinates.back();
        const auto toX = [&](double x) {
            return plot.left()
                + plot.width() * (x - xLo) / std::max(1e-12, xHi - xLo);
        };
        for (std::size_t i = 0; i < specialX_.size(); ++i) {
            const double x = toX(specialX_[i]);
            if (x < plot.left() - 0.5 || x > plot.right() + 0.5)
                continue;
            painter.setPen(QPen(QColor(PlotPalette::spine.red(),
                                       PlotPalette::spine.green(),
                                       PlotPalette::spine.blue(), 90),
                                1.0));
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            painter.setPen(style_.textColor);
            QFont labelFont = painter.font();
            labelFont.setPointSizeF(style_.annotationPointSize);
            painter.setFont(labelFont);
            const QString label = prettyLabel(
                i < static_cast<std::size_t>(specialLabels_.size())
                    ? specialLabels_.at(static_cast<int>(i))
                    : QString());
            painter.drawText(QRectF(x - 30, plot.bottom() + 4, 60, 22),
                             Qt::AlignHCenter | Qt::AlignTop, label);
            painter.setFont(font);
        }
    }

    // Fermi / zero reference.
    if (eLo < 0.0 && eHi > 0.0 && shiftFermi_ && style_.showFermi) {
        painter.setPen(QPen(style_.fermiColor, style_.fermiLineWidth,
                            style_.fermiPenStyle));
        painter.drawLine(QPointF(plot.left(), toY(0.0)),
                         QPointF(plot.right(), toY(0.0)));
    }

    // Axis titles.
    painter.setPen(style_.textColor);
    QFont titleFont = painter.font();
    titleFont.setPointSizeF(style_.axisTitlePointSize);
    painter.setFont(titleFont);
    painter.drawText(QRectF(plot.left(), plot.bottom() + 28, plot.width(), 22),
                     Qt::AlignHCenter,
                     tr("k-path (primitive Brillouin zone)"));
    painter.save();
    painter.translate(20, plot.center().y());
    painter.rotate(-90.0);
    // Same typesetting as the band/PDOS plot: a real subscript on E_F.
    drawWithSubscripts(painter,
                       QRectF(-plot.height() / 2.0, -11, plot.height(), 22),
                       shiftFermi_ ? tr("E − E_F (eV)") : tr("E (eV)"));
    painter.restore();
    (void)metrics;
}

double SpectralHeatmapWidget::colorbarWidth() const
{
    // Bar plus its tick labels. Zero when off, so the plot reclaims the space
    // rather than leaving a gap where the scale used to be.
    return style_.showColorbar ? 64.0 : 0.0;
}

void SpectralHeatmapWidget::paintScatter(QPainter& painter, const QRectF& plot,
                                         double eLo, double eHi) const
{
    if (columns_.empty())
        return;

    // Scatter draws the STORED eigenvalues, so it needs the columns rather
    // than the broadened field — which is the point of the mode: no σ, no
    // binning, nothing between the data and the pixel.
    double maxWeight = 0.0;
    for (const auto& column : columns_)
        for (const auto& state : column.states)
            maxWeight = std::max(maxWeight, state.weight);
    if (maxWeight <= 0.0)
        return;

    const double xLo = columns_.front().pathCoordinate;
    const double xHi = columns_.back().pathCoordinate;
    const double xSpan = std::max(1e-12, xHi - xLo);
    const double eSpan = std::max(1e-9, eHi - eLo);

    painter.save();
    painter.setClipRect(plot);
    painter.setPen(Qt::NoPen);
    for (const auto& column : columns_) {
        const double x =
            plot.left() + plot.width() * (column.pathCoordinate - xLo) / xSpan;
        for (const auto& state : column.states) {
            const double normalized = state.weight / maxWeight;
            if (normalized <= style_.intensityThreshold)
                continue;
            // The energies stored in the columns are absolute; the plot axis
            // may be shifted to E_F, and the same shift has to be applied here
            // or the markers land a Fermi level away from the heatmap.
            const double energy =
                shiftFermi_ ? state.energy - fermi_ : state.energy;
            if (energy < eLo || energy > eHi)
                continue;
            const double y = plot.bottom() - plot.height() * (energy - eLo) / eSpan;

            const double t = std::clamp((normalized - style_.intensityThreshold)
                                            / std::max(1e-9,
                                                       1.0
                                                           - style_.intensityThreshold),
                                        0.0, 1.0);
            QColor color =
                render::ColorMap::sample(style_.gradient, static_cast<float>(t));
            color.setAlphaF(std::clamp(style_.opacity, 0.0, 1.0));
            painter.setBrush(color);

            // Area proportional to weight, hence the square root on the
            // diameter: scaling the diameter linearly makes a half-weight
            // state look a quarter as important, which is not what the eye
            // should be told.
            const double diameter =
                style_.markerScalesWithWeight
                ? style_.markerSize * std::sqrt(t)
                : style_.markerSize;
            if (diameter <= 0.0)
                continue;
            painter.drawEllipse(QPointF(x, y), 0.5 * diameter, 0.5 * diameter);
        }
    }
    painter.restore();
}

void SpectralHeatmapWidget::paintColorbar(QPainter& painter,
                                          const QRectF& plot) const
{
    const QRectF bar(plot.right() + 14.0, plot.top(), 16.0, plot.height());

    // Painted top-down, and the gradient runs high-weight at the TOP to match
    // the energy axis beside it running high-energy at the top.
    for (int y = 0; y < static_cast<int>(bar.height()); ++y) {
        const double t = 1.0 - static_cast<double>(y) / std::max(1.0, bar.height());
        painter.setPen(
            render::ColorMap::sample(style_.gradient, static_cast<float>(t)));
        painter.drawLine(QPointF(bar.left(), bar.top() + y),
                         QPointF(bar.right(), bar.top() + y));
    }
    painter.setPen(QPen(style_.spineColor, style_.spineWidth));
    painter.drawRect(bar);

    // Labelled by the FRACTION of the maximum, not by an absolute intensity:
    // the vertical scale of A(k,E) depends on σ and the bin width, so an
    // absolute number here would change when neither the physics nor the
    // colours did.
    QFont font = painter.font();
    font.setPointSizeF(style_.annotationPointSize);
    painter.setFont(font);
    painter.setPen(style_.textColor);
    painter.drawText(QRectF(bar.right() + 4, bar.top() - 10, 44, 20),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("1.0"));
    painter.drawText(QRectF(bar.right() + 4, bar.bottom() - 10, 44, 20),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QString::number(style_.intensityThreshold, 'f', 2));
}

void SpectralHeatmapWidget::exportImage(QWidget* dialogParent)
{
    if (!spectral_.valid()) {
        QMessageBox::information(dialogParent, tr("Export Image"),
                                 tr("No spectral function loaded."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        dialogParent, tr("Export Effective Band Structure"),
        QStringLiteral("effective_bands.png"),
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));
    if (path.isEmpty())
        return;
    // 3x for print, matching the band/PDOS exporter.
    QImage image(width() * 3, height() * 3, QImage::Format_ARGB32);
    image.fill(style_.background);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(3.0, 3.0);
    render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();
    if (!image.save(path)) {
        QMessageBox::critical(dialogParent, tr("Export Image"),
                              tr("Could not write %1").arg(path));
    }
}

void SpectralHeatmapWidget::exportData(QWidget* dialogParent)
{
    if (!spectral_.valid()) {
        QMessageBox::information(dialogParent, tr("Export Data"),
                                 tr("No spectral function loaded."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        dialogParent, tr("Export A(k, E)"),
        QStringLiteral("effective_bands.csv"), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(dialogParent, tr("Export Data"),
                              tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    // A CSV starts with its header row — no '#' comment lines. Whether the
    // energies are Fermi-relative is load-bearing, so it moved into the
    // column NAME rather than being dropped with the comments.
    // Long format: one row per (k, E) cell. Wide format would need one column
    // per k-point, which spreadsheets cap well below a typical path length.
    out << (shiftFermi_
                ? "path_coordinate,energy_minus_ef_eV,intensity\n"
                : "path_coordinate,energy_eV,intensity\n");
    for (std::size_t x = 0; x < spectral_.intensity.size(); ++x) {
        for (std::size_t y = 0; y < spectral_.energies.size(); ++y) {
            out << QString::number(spectral_.pathCoordinates[x], 'f', 6) << ','
                << QString::number(spectral_.energies[y], 'f', 6) << ','
                << QString::number(spectral_.intensity[x][y], 'g', 6) << '\n';
        }
    }
}

} // namespace calango::gui
