#include "gui/BandPdosView.hpp"

#include "gui/GuiUtils.hpp"

#include <QFontMetricsF>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPageSize>
#include <QPdfWriter>
#include <QSvgGenerator>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <vector>

namespace calango::gui {

namespace {

/// Axis titles and tick labels are drawn 1.5x larger than Qt's default for
/// legibility (these plots are read in presentations and printed figures).
/// Every margin and label box below is derived from this so the geometry
/// scales with the type rather than clipping it.
/// Geometry (gutters, label boxes) is still derived from this reference
/// scale; the *type* sizes themselves now come from BandPdosView::Style.
constexpr double kLabelScale = 1.5;




QString prettyLabel(const QString& raw)
{
    // ASE spells Gamma as "G"; band plots conventionally use the glyph.
    const auto single = [](const QString& s) -> QString {
        if (s == QLatin1String("G")
            || s.compare(QLatin1String("Gamma"), Qt::CaseInsensitive) == 0)
            return QStringLiteral("Γ");
        return s;
    };
    // A path discontinuity is a single tick where two high-symmetry points meet
    // (ASE joins them with a comma, e.g. "X,U"). Render them as one clean
    // comma-separated pair on a single line rather than two stacked symbols.
    if (raw.contains(QLatin1Char(','))) {
        QStringList parts;
        for (const QString& part : raw.split(QLatin1Char(','), Qt::SkipEmptyParts))
            parts << single(part.trimmed());
        return parts.join(QStringLiteral(", "));
    }
    return single(raw);
}

} // namespace

BandPdosView::BandPdosView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(560, 380);
}

QColor BandPdosView::projectionColor(int index)
{
    static const QColor kPalette[] = {
        {102, 163, 255}, {235, 110, 96},  {110, 210, 130}, {255, 199, 88},
        {188, 140, 255}, {96, 205, 220},  {240, 150, 200}, {170, 180, 100},
        {255, 145, 70},  {140, 150, 250},
    };
    return kPalette[static_cast<std::size_t>(index)
                    % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

void BandPdosView::setBandData(BandData data)
{
    bands_ = std::move(data);
    reference_ = bands_.efermi;
    update();
}

void BandPdosView::setPdosData(PdosData data)
{
    pdos_ = std::move(data);
    for (const auto& [label, curve] : pdos_.projections) {
        (void)curve;
        visible_.emplace(label, true);
    }
    update();
}

void BandPdosView::setReference(double referenceEv)
{
    reference_ = referenceEv;
    update();
}

void BandPdosView::setReferenceIsFermi(bool fermiRelative)
{
    if (referenceIsFermi_ == fermiRelative)
        return;
    referenceIsFermi_ = fermiRelative;
    update();
}

void BandPdosView::setEnergyWindow(double minEv, double maxEv)
{
    eMin_ = std::min(minEv, maxEv - 0.1);
    eMax_ = std::max(maxEv, minEv + 0.1);
    update();
}

void BandPdosView::setProjectionVisible(const QString& label, bool visible)
{
    visible_[label] = visible;
    update();
}

QColor BandPdosView::fatbandColor(int index)
{
    // A palette of its own rather than a reuse of projectionColor: the fatband
    // overlay is drawn ON TOP of the dispersion in the same panel, so its
    // colours have to stay legible against the band colours instead of merely
    // being distinct from each other, and the first PDOS colour is the first
    // band colour.
    static const QColor kPalette[] = {
        {255, 128, 96},  {96, 220, 160},  {180, 140, 255}, {255, 205, 100},
        {120, 200, 255}, {245, 140, 200}, {160, 230, 110}, {255, 170, 130},
    };
    return kPalette[static_cast<std::size_t>(std::max(index, 0))
                    % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

void BandPdosView::setFatbandData(FatbandData data)
{
    fatbands_ = std::move(data);
    for (const auto& [label, weights] : fatbands_.projections) {
        (void)weights;
        fatbandVisible_.emplace(label, true);
    }
    update();
}

void BandPdosView::setFatbandMode(FatbandMode mode)
{
    fatbandMode_ = mode;
    update();
}

void BandPdosView::setFatbandChannelVisible(const QString& label, bool visible)
{
    fatbandVisible_[label] = visible;
    update();
}

void BandPdosView::setSymmetryData(SymmetryData data)
{
    symmetry_ = std::move(data);
    update();
}

void BandPdosView::setSymmetryLabelsVisible(bool visible)
{
    symmetryVisible_ = visible;
    update();
}

void BandPdosView::setSymmetryLineLabelsVisible(bool visible)
{
    symmetryLineLabels_ = visible;
    update();
}

void BandPdosView::setPhononMode(bool on)
{
    phonon_ = on;
    if (on) {
        reference_ = 0.0; // frequencies are absolute; ω = 0 is the acoustic line
        // omega = 0 is a geometric guide, not a physical level like E_F, so it
        // keeps the muted grid tone it always had. Still user-overridable.
        style_.fermiColor = style_.gridColor;
    }
    update();
}

void BandPdosView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    renderTo(painter, QSizeF(width(), height()));
}

void BandPdosView::setStyle(const Style& style)
{
    style_ = style;
    update();
}

void BandPdosView::renderTo(QPainter& painter, const QSizeF& size)
{
    const QRectF canvas(QPointF(0.0, 0.0), size);
    painter.fillRect(canvas, style_.background);

    const double margin = 8.0;
    // Left gutter: 34 px of numeric tick labels plus kAxisTitleStrip for the
    // rotated axis title outside them.
    constexpr double kAxisTitleStrip = 20.0 * kLabelScale;
    constexpr double kTickGutter = 34.0 * kLabelScale;
    constexpr double kBottomStrip = 22.0 * kLabelScale;
    QRectF area = canvas.adjusted(margin + kTickGutter + kAxisTitleStrip, margin,
                                  -margin, -margin - kBottomStrip);
    if (pdos_.valid()) {
        const double bandWidth = bands_.valid() ? area.width() * 0.62
                                                : 0.0;
        if (bands_.valid())
            paintBands(painter,
                       QRectF(area.left(), area.top(), bandWidth - 8,
                              area.height()));
        paintPdos(painter,
                  QRectF(area.left() + bandWidth, area.top(),
                         area.width() - bandWidth, area.height()));
    } else if (bands_.valid()) {
        paintBands(painter, area);
    } else {
        painter.setPen(style_.textColor);
        painter.drawText(canvas, Qt::AlignCenter,
                         tr("No band-structure data loaded"));
        return;
    }

    // Vertical axis title, strictly left of the plot and rotated 90°
    // counter-clockwise so it reads bottom-to-top. Both panels share the
    // energy axis, so it is drawn once for the pair. "E_F" is typeset with a
    // real subscript by drawWithSubscripts (Qt has no LaTeX engine, and this
    // project carries no QCustomPlot/MathJax dependency).
    painter.setPen(style_.textColor);
    painter.save();
    QFont titleFont = painter.font();
    titleFont.setPointSizeF(style_.axisTitlePointSize);
    painter.setFont(titleFont);
    painter.translate(margin + kAxisTitleStrip * 0.5, area.center().y());
    painter.rotate(-90.0);
    drawWithSubscripts(painter, QRectF(-area.height() / 2.0, -9.0,
                                       area.height(), 18.0),
                       phonon_ ? tr("Frequency (cm⁻¹)")
                               : referenceIsFermi_ ? tr("E − E_F (eV)")
                                                   : tr("E (eV)"));
    painter.restore();
}

void BandPdosView::exportImage(QWidget* dialogParent)
{
    if (!bands_.valid() && !pdos_.valid()) {
        QMessageBox::information(dialogParent, tr("Export Image"),
                                 tr("Nothing to export — no plot data loaded."));
        return;
    }

    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(
        dialogParent, tr("Export Plot Image"), QStringLiteral("plot.png"),
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg);;"
           "PDF document (*.pdf);;SVG vector image (*.svg)"),
        &selectedFilter);
    if (path.isEmpty())
        return;

    const QString suffix = QFileInfo(path).suffix().toLower();
    // Vector formats keep text and curves resolution-independent, which is
    // what a journal figure wants; raster formats are rendered at a multiple
    // of the on-screen size so they are usable in print.
    constexpr double kRasterScale = 3.0;
    const QSizeF logical(std::max(width(), 640), std::max(height(), 420));

    if (suffix == QLatin1String("svg")) {
        QSvgGenerator generator;
        generator.setFileName(path);
        generator.setSize(logical.toSize());
        generator.setViewBox(QRectF(QPointF(0, 0), logical));
        generator.setTitle(tr("Calango plot"));
        QPainter painter(&generator);
        painter.setRenderHint(QPainter::Antialiasing);
        renderTo(painter, logical);
        return;
    }
    if (suffix == QLatin1String("pdf")) {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(logical, QPageSize::Point));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        writer.setResolution(300);
        QPainter painter(&writer);
        painter.setRenderHint(QPainter::Antialiasing);
        // The writer's device is in device pixels at 300 dpi while the layout
        // is in points; scale so the figure fills the page exactly.
        const double dpiScale = writer.resolution() / 72.0;
        painter.scale(dpiScale, dpiScale);
        renderTo(painter, logical);
        return;
    }

    QImage image(static_cast<int>(logical.width() * kRasterScale),
                 static_cast<int>(logical.height() * kRasterScale),
                 QImage::Format_ARGB32_Premultiplied);
    // JPEG has no alpha; fill with the plot background so it never composites
    // onto black.
    image.fill(style_.background);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(kRasterScale, kRasterScale);
    renderTo(painter, logical);
    painter.end();
    if (!image.save(path)) {
        QMessageBox::critical(dialogParent, tr("Export Image"),
                              tr("Could not write %1").arg(path));
    }
}

void BandPdosView::paintBands(QPainter& painter, const QRectF& rect)
{
    const double xMax = bands_.x.empty() ? 1.0 : bands_.x.back();
    const auto mapX = [&](double x) {
        return rect.left() + x / std::max(xMax, 1e-12) * rect.width();
    };
    const auto mapY = [&](double e) {
        return rect.bottom()
            - (e - eMin_) / (eMax_ - eMin_) * rect.height();
    };

    painter.setPen(QPen(style_.spineColor, style_.spineWidth));
    painter.drawRect(rect);

    // High-symmetry verticals + labels.
    QFont tickFont = painter.font();
    tickFont.setPointSizeF(style_.tickPointSize);
    painter.setFont(tickFont);
    const auto rawLabel = [&](std::size_t i) {
        return i < static_cast<std::size_t>(bands_.specialLabels.size())
                   ? bands_.specialLabels[static_cast<int>(i)]
                   : QString();
    };
    for (std::size_t i = 0; i < bands_.specialX.size(); ++i) {
        const double px = mapX(bands_.specialX[i]);
        painter.setPen(QPen(style_.gridColor, style_.tickWidth));
        painter.drawLine(QPointF(px, rect.top()), QPointF(px, rect.bottom()));
        painter.setPen(style_.textColor);
        // A path break can arrive as two special points at (essentially) the
        // same x with distinct labels ("X" then "U"). Fold the next one into a
        // single "X, U" tick and skip drawing it separately so the two symbols
        // never stack on top of each other.
        QString raw = rawLabel(i);
        while (i + 1 < bands_.specialX.size()
               && std::abs(bands_.specialX[i + 1] - bands_.specialX[i]) < 1e-6) {
            const QString next = rawLabel(i + 1);
            if (!next.isEmpty())
                raw = raw.isEmpty() ? next : raw + QLatin1Char(',') + next;
            ++i;
        }
        painter.drawText(QRectF(px - 40 * kLabelScale, rect.bottom() + 2,
                                80 * kLabelScale, 18 * kLabelScale),
                         Qt::AlignHCenter | Qt::AlignTop, prettyLabel(raw));
    }

    // Energy ticks every ~2 eV-ish (5 divisions).
    for (int i = 0; i <= 5; ++i) {
        const double e = eMin_ + (eMax_ - eMin_) * i / 5.0;
        const double py = mapY(e);
        painter.setPen(QPen(style_.gridColor, style_.tickWidth, Qt::DotLine));
        painter.drawLine(QPointF(rect.left(), py), QPointF(rect.right(), py));
        painter.setPen(style_.textColor);
        painter.drawText(QRectF(rect.left() - 44 * kLabelScale,
                                py - 8 * kLabelScale, 38 * kLabelScale,
                                16 * kLabelScale),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(e, 'f', phonon_ ? 0 : 1));
    }

    // Reference line: E − E_F = 0 for electrons, ω = 0 (acoustic) for phonons.
    if (style_.showFermi && eMin_ < 0.0 && eMax_ > 0.0) {
        painter.setPen(QPen(style_.fermiColor, style_.fermiLineWidth,
                            style_.fermiPenStyle));
        painter.drawLine(QPointF(rect.left(), mapY(0.0)),
                         QPointF(rect.right(), mapY(0.0)));
    }

    painter.setClipRect(rect);
    for (std::size_t spin = 0; spin < bands_.energies.size(); ++spin) {
        painter.setPen(QPen(style_.bandColors[spin % 2], style_.bandLineWidth,
                            style_.bandPenStyle));
        const auto& kpts = bands_.energies[spin];
        if (kpts.empty())
            continue;
        const std::size_t bandCount = kpts.front().size();
        for (std::size_t band = 0; band < bandCount; ++band) {
            QPainterPath path;
            for (std::size_t k = 0; k < kpts.size() && k < bands_.x.size(); ++k) {
                const QPointF p(mapX(bands_.x[k]),
                                mapY(kpts[k][band] - reference_));
                if (k == 0)
                    path.moveTo(p);
                else
                    path.lineTo(p);
            }
            painter.drawPath(path);
        }
    }
    paintFatbands(painter, rect, mapX, mapY);
    painter.setClipping(false);

    paintSymmetryLabels(painter, rect, mapX, mapY);

    // The energy-axis title is drawn once, rotated, in paintEvent's left
    // strip — see the note there.
}

void BandPdosView::paintFatbands(QPainter& painter, const QRectF& rect,
                                 const std::function<double(double)>& mapX,
                                 const std::function<double(double)>& mapY)
{
    if (fatbandMode_ == FatbandMode::Off || !fatbands_.valid()
        || !bands_.valid())
        return;
    const double norm = std::max(fatbands_.maxWeight, 1e-12);
    const bool widen = fatbandMode_ == FatbandMode::Width
        || fatbandMode_ == FatbandMode::Both;
    const bool fade = fatbandMode_ == FatbandMode::Color
        || fatbandMode_ == FatbandMode::Both;

    int channel = -1;
    for (const auto& [label, weights] : fatbands_.projections) {
        ++channel;
        const auto shown = fatbandVisible_.find(label);
        if (shown != fatbandVisible_.end() && !shown->second)
            continue;
        const QColor base = fatbandColor(channel);

        for (std::size_t spin = 0;
             spin < weights.size() && spin < bands_.energies.size(); ++spin) {
            const auto& kWeights = weights[spin];
            const auto& kEnergies = bands_.energies[spin];
            for (std::size_t k = 0; k + 1 < kEnergies.size()
                 && k + 1 < kWeights.size() && k + 1 < bands_.x.size(); ++k) {
                const std::size_t bandCount =
                    std::min(kEnergies[k].size(), kWeights[k].size());
                for (std::size_t band = 0; band < bandCount; ++band) {
                    if (band >= kEnergies[k + 1].size()
                        || band >= kWeights[k + 1].size())
                        continue;
                    // One segment at a time, because the width IS the data:
                    // a single polyline per band could only carry one width
                    // for the whole path, which is exactly the information a
                    // fatband plot exists to show.
                    const double weight =
                        0.5 * (kWeights[k][band] + kWeights[k + 1][band]);
                    const double fraction =
                        std::clamp(weight / norm, 0.0, 1.0);
                    if (fraction <= 1e-3)
                        continue;
                    // Most of the band manifold is off-screen in a typical
                    // ±10 eV window, and each visible segment costs a pen
                    // change; skip the ones that cannot land in the frame.
                    const double y0 = mapY(kEnergies[k][band] - reference_);
                    const double y1 = mapY(kEnergies[k + 1][band] - reference_);
                    if ((y0 < rect.top() && y1 < rect.top())
                        || (y0 > rect.bottom() && y1 > rect.bottom()))
                        continue;
                    QColor color = base;
                    color.setAlpha(fade
                        ? std::clamp(static_cast<int>(
                              style_.fatbandMinAlpha
                              + (255 - style_.fatbandMinAlpha) * fraction),
                              0, 255)
                        : 210);
                    const double width = style_.bandLineWidth
                        + (widen ? style_.fatbandScale * fraction : 0.0);
                    painter.setPen(QPen(color, width, Qt::SolidLine,
                                        Qt::RoundCap));
                    painter.drawLine(QPointF(mapX(bands_.x[k]), y0),
                                     QPointF(mapX(bands_.x[k + 1]), y1));
                }
            }
        }
    }
}

void BandPdosView::paintSymmetryLabels(
    QPainter& painter, const QRectF& rect,
    const std::function<double(double)>& mapX,
    const std::function<double(double)>& mapY)
{
    if (!symmetryVisible_ || !symmetry_.valid())
        return;

    QFont font = painter.font();
    font.setPointSizeF(style_.symmetryLabelPointSize);
    font.setBold(true);
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    const double lineHeight = metrics.height();

    // Labels are placed by energy, so two multiplets a few meV apart would
    // print on top of each other. Track the last y used at each k-point and
    // drop a label that cannot clear it — dropping is better than overlapping,
    // which makes BOTH unreadable.
    std::map<double, double> lastY;
    for (const SymmetryLabel& label : symmetry_.labels) {
        if (label.onLine && !symmetryLineLabels_)
            continue;
        const double relative = label.energy - reference_;
        if (relative < eMin_ || relative > eMax_)
            continue;
        const double px = mapX(label.x);
        const double py = mapY(relative);
        auto previous = lastY.find(label.x);
        if (previous != lastY.end()
            && std::abs(py - previous->second) < lineHeight)
            continue;
        lastY[label.x] = py;

        QString text = label.text;
        if (label.degeneracy > 1)
            text += QStringLiteral(" (%1)").arg(label.degeneracy);
        const double textWidth = metrics.horizontalAdvance(text) + 6.0;
        // Nudge inward at the panel edges so a label at Γ or at the end of
        // the path is not half outside the frame.
        double left = px + 4.0;
        if (left + textWidth > rect.right())
            left = px - textWidth - 4.0;
        left = std::max(left, rect.left() + 2.0);

        const QRectF box(left, py - lineHeight * 0.5, textWidth, lineHeight);
        // A backing plate: these sit directly on top of the dispersion, and
        // unbacked text over a dense band manifold is unreadable.
        QColor plate = style_.background;
        plate.setAlpha(190);
        painter.fillRect(box, plate);
        painter.setPen(style_.symmetryLabelColor);
        painter.drawText(box, Qt::AlignCenter, text);
    }
}

void BandPdosView::paintPdos(QPainter& painter, const QRectF& rect)
{
    const auto mapY = [&](double e) {
        return rect.bottom()
            - (e - eMin_) / (eMax_ - eMin_) * rect.height();
    };

    // Maximum of the visible curves inside the energy window.
    double dosMax = 1e-12;
    for (const auto& [label, curve] : pdos_.projections) {
        const auto it = visible_.find(label);
        if (it != visible_.end() && !it->second)
            continue;
        for (std::size_t i = 0; i < curve.size() && i < pdos_.energies.size();
             ++i) {
            const double e = pdos_.energies[i] - reference_;
            if (e >= eMin_ && e <= eMax_)
                dosMax = std::max(dosMax, curve[i]);
        }
    }
    const auto mapX = [&](double dos) {
        return rect.left() + dos / dosMax * (rect.width() - 6);
    };

    painter.setPen(QPen(style_.spineColor, style_.spineWidth));
    painter.drawRect(rect);
    if (style_.showFermi && eMin_ < 0.0 && eMax_ > 0.0) {
        painter.setPen(QPen(style_.fermiColor, style_.fermiLineWidth,
                            style_.fermiPenStyle));
        painter.drawLine(QPointF(rect.left(), mapY(0.0)),
                         QPointF(rect.right(), mapY(0.0)));
    }

    painter.setClipRect(rect);
    int colorIndex = 0;
    for (const auto& [label, curve] : pdos_.projections) {
        const int index = colorIndex++;
        const auto it = visible_.find(label);
        if (it != visible_.end() && !it->second)
            continue;
        const QColor curveColor = projectionColor(index);
        painter.setPen(QPen(curveColor, style_.bandLineWidth + 0.2));
        QPainterPath path;
        bool started = false;
        for (std::size_t i = 0; i < curve.size() && i < pdos_.energies.size();
             ++i) {
            const QPointF p(mapX(curve[i]),
                            mapY(pdos_.energies[i] - reference_));
            if (!started) {
                path.moveTo(p);
                started = true;
            } else {
                path.lineTo(p);
            }
        }
        if (style_.fillDos && started) {
            // Close the curve back down the zero-DOS edge so the area under
            // it can be filled — the conventional presentation for a PhDOS.
            QPainterPath filled = path;
            filled.lineTo(mapX(0.0), path.currentPosition().y());
            filled.lineTo(mapX(0.0), mapY(pdos_.energies.front() - reference_));
            filled.closeSubpath();
            QColor fill = curveColor;
            fill.setAlpha(std::clamp(style_.dosFillAlpha, 0, 255));
            painter.fillPath(filled, fill);
        }
        painter.drawPath(path);
    }
    painter.setClipping(false);

    painter.setPen(style_.textColor);
    QFont pdosTitleFont = painter.font();
    pdosTitleFont.setPointSizeF(style_.axisTitlePointSize);
    painter.setFont(pdosTitleFont);
    painter.drawText(QRectF(rect.left(), rect.bottom() + 2, rect.width(),
                            18 * kLabelScale),
                     Qt::AlignHCenter | Qt::AlignTop,
                     phonon_ ? tr("PhDOS (states/cm⁻¹)") : tr("PDOS (states/eV)"));
}

} // namespace calango::gui
