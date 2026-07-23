#include "gui/SpectralHeatmapWidget.hpp"

#include "gui/GuiUtils.hpp"

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

const QColor kBackground(18, 20, 24);
const QColor kText(210, 213, 220);
const QColor kFrame(120, 124, 134);
constexpr double kTickPointSize = 15.0; // matches the band/PDOS plots

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
    heatmap_.fill(kBackground);

    for (int x = 0; x < width; ++x) {
        const auto& profile = spectral_.intensity[static_cast<std::size_t>(x)];
        for (int y = 0; y < height; ++y) {
            const double normalized =
                profile[static_cast<std::size_t>(y)] / spectral_.maxIntensity;
            if (normalized <= threshold_)
                continue; // below the noise floor: leave it as background
            // Rescale the surviving range back to [0, 1] so raising the
            // threshold brightens what remains instead of just clipping it.
            const double t = std::clamp(
                (normalized - threshold_) / std::max(1e-9, 1.0 - threshold_),
                0.0, 1.0);
            // Row 0 of the image is the TOP of the plot, but bin 0 is the
            // LOWEST energy — flip so energy increases upward.
            heatmap_.setPixelColor(
                x, height - 1 - y,
                render::ColorMap::sample(gradient_, static_cast<float>(t)));
        }
    }
}

void SpectralHeatmapWidget::setGradient(render::ColorGradient gradient)
{
    gradient_ = gradient;
    rebuildImage();
    update();
}

void SpectralHeatmapWidget::setIntensityThreshold(double fraction)
{
    threshold_ = std::clamp(fraction, 0.0, 0.99);
    rebuildImage();
    update();
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
    painter.fillRect(rect(), kBackground);

    if (!spectral_.valid() || heatmap_.isNull()) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("Run Simulation → Effective Bands…\n"
                            "to compute an unfolded spectral function."));
        return;
    }

    QFont font = painter.font();
    font.setPointSizeF(kTickPointSize);
    painter.setFont(font);
    const QFontMetricsF metrics(font);

    const QRectF plot = rect().adjusted(78, 14, -14, -52);
    // Smooth transformation: the data grid is usually coarser than the widget,
    // and nearest-neighbour would turn a physical spectrum into visible blocks.
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(plot, heatmap_);
    painter.setPen(QPen(kFrame, 1.2));
    painter.drawRect(plot);

    const double eLo = spectral_.energies.front();
    const double eHi = spectral_.energies.back();
    const auto toY = [&](double e) {
        return plot.bottom() - plot.height() * (e - eLo) / std::max(1e-9, eHi - eLo);
    };

    // Energy ticks.
    painter.setPen(kText);
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
            painter.setPen(QPen(QColor(210, 213, 220, 110), 1.0));
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            painter.setPen(kText);
            const QString label = prettyLabel(
                i < static_cast<std::size_t>(specialLabels_.size())
                    ? specialLabels_.at(static_cast<int>(i))
                    : QString());
            painter.drawText(QRectF(x - 30, plot.bottom() + 4, 60, 22),
                             Qt::AlignHCenter | Qt::AlignTop, label);
        }
    }

    // Fermi / zero reference.
    if (eLo < 0.0 && eHi > 0.0 && shiftFermi_) {
        painter.setPen(QPen(QColor(255, 199, 88), 1.4, Qt::DashLine));
        painter.drawLine(QPointF(plot.left(), toY(0.0)),
                         QPointF(plot.right(), toY(0.0)));
    }

    // Axis titles.
    painter.setPen(kText);
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
    image.fill(kBackground);
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
    out << "# Calango effective band structure — spectral function A(k, E)\n";
    out << "# Popescu-Zunger unfolding; sigma = " << options_.sigma << " eV";
    out << (shiftFermi_ ? ", energies relative to E_F\n"
                        : ", absolute energies\n");
    out << "# E_F = " << QString::number(fermi_, 'f', 6) << " eV\n";
    // Long format: one row per (k, E) cell. Wide format would need one column
    // per k-point, which spreadsheets cap well below a typical path length.
    out << "path_coordinate,energy_eV,intensity\n";
    for (std::size_t x = 0; x < spectral_.intensity.size(); ++x) {
        for (std::size_t y = 0; y < spectral_.energies.size(); ++y) {
            out << QString::number(spectral_.pathCoordinates[x], 'f', 6) << ','
                << QString::number(spectral_.energies[y], 'f', 6) << ','
                << QString::number(spectral_.intensity[x][y], 'g', 6) << '\n';
        }
    }
}

} // namespace calango::gui
