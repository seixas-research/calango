#include "gui/BandPdosView.hpp"

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



/// Draw `text` centered in `box`, rendering "_x" as a typographic subscript
/// (smaller font, dropped baseline). Qt ships no LaTeX engine and this
/// project has no QCustomPlot / MathJax dependency, so the two-run layout
/// below is what actually produces "E − E_F (eV)" with a proper subscript
/// instead of a literal underscore.
void drawWithSubscripts(QPainter& painter, const QRectF& box,
                        const QString& text)
{
    // Split into (run, isSubscript) pairs: "_" introduces a one-character
    // subscript, "_{...}" a braced multi-character one.
    struct Run {
        QString text;
        bool subscript;
    };
    std::vector<Run> runs;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('_') && i + 1 < text.size()) {
            if (text.at(i + 1) == QLatin1Char('{')) {
                const int close = text.indexOf(QLatin1Char('}'), i + 2);
                if (close > 0) {
                    runs.push_back({text.mid(i + 2, close - i - 2), true});
                    i = close;
                    continue;
                }
            }
            runs.push_back({text.mid(i + 1, 1), true});
            ++i;
            continue;
        }
        if (runs.empty() || runs.back().subscript)
            runs.push_back({QString(), false});
        runs.back().text.append(text.at(i));
    }

    const QFont baseFont = painter.font();
    QFont subFont = baseFont;
    subFont.setPointSizeF(std::max(baseFont.pointSizeF() * 0.72, 6.0));
    const QFontMetricsF baseMetrics(baseFont);
    const QFontMetricsF subMetrics(subFont);

    double width = 0.0;
    for (const Run& run : runs) {
        width += (run.subscript ? subMetrics : baseMetrics)
                     .horizontalAdvance(run.text);
    }

    double x = box.center().x() - width / 2.0;
    const double baseline = box.center().y() + baseMetrics.ascent() / 2.0
        - baseMetrics.descent() / 2.0;
    const double drop = baseMetrics.descent() * 0.75;
    for (const Run& run : runs) {
        painter.setFont(run.subscript ? subFont : baseFont);
        painter.drawText(QPointF(x, run.subscript ? baseline + drop : baseline),
                         run.text);
        x += (run.subscript ? subMetrics : baseMetrics)
                 .horizontalAdvance(run.text);
    }
    painter.setFont(baseFont);
}

QString prettyLabel(const QString& raw)
{
    // ASE spells Gamma as "G"; band plots conventionally use the glyph.
    if (raw == QLatin1String("G") || raw.compare(QLatin1String("Gamma"),
                                                 Qt::CaseInsensitive) == 0)
        return QStringLiteral("Γ");
    return raw;
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
    for (std::size_t i = 0; i < bands_.specialX.size(); ++i) {
        const double px = mapX(bands_.specialX[i]);
        painter.setPen(QPen(style_.gridColor, style_.tickWidth));
        painter.drawLine(QPointF(px, rect.top()), QPointF(px, rect.bottom()));
        painter.setPen(style_.textColor);
        const QString label = prettyLabel(
            i < static_cast<std::size_t>(bands_.specialLabels.size())
                ? bands_.specialLabels[static_cast<int>(i)]
                : QString());
        painter.drawText(QRectF(px - 30 * kLabelScale, rect.bottom() + 2,
                                60 * kLabelScale, 18 * kLabelScale),
                         Qt::AlignHCenter | Qt::AlignTop, label);
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
    painter.setClipping(false);

    // The energy-axis title is drawn once, rotated, in paintEvent's left
    // strip — see the note there.
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
