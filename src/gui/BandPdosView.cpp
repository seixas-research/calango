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




/// One sequential colormap: the nine ColorBrewer class colours matplotlib
/// interpolates its map of the same name from, lightest first.
///
/// Nine stops rather than a two-point gradient because these ramps are not
/// linear in RGB — ColorBrewer chose them to be perceptually even, and a
/// straight white-to-navy interpolation goes through a washed-out grey-blue
/// that makes the middle of the weight range unreadable.
struct SequentialColormap {
    const char* name;
    unsigned stops[9]; ///< 0xRRGGBB, light -> saturated
};

constexpr int kColormapStops = 9;

const SequentialColormap kFatbandColormaps[] = {
    {"Greens",  {0xF7FCF5, 0xE5F5E0, 0xC7E9C0, 0xA1D99B, 0x74C476, 0x41AB5D,
                 0x238B45, 0x006D2C, 0x00441B}},
    {"Blues",   {0xF7FBFF, 0xDEEBF7, 0xC6DBEF, 0x9ECAE1, 0x6BAED6, 0x4292C6,
                 0x2171B5, 0x08519C, 0x08306B}},
    {"Reds",    {0xFFF5F0, 0xFEE0D2, 0xFCBBA1, 0xFC9272, 0xFB6A4A, 0xEF3B2C,
                 0xCB181D, 0xA50F15, 0x67000D}},
    {"Oranges", {0xFFF5EB, 0xFEE6CE, 0xFDD0A2, 0xFDAE6B, 0xFD8D3C, 0xF16913,
                 0xD94801, 0xA63603, 0x7F2704}},
    {"Greys",   {0xFFFFFF, 0xF0F0F0, 0xD9D9D9, 0xBDBDBD, 0x969696, 0x737373,
                 0x525252, 0x252525, 0x000000}},
    {"Purples", {0xFCFBFD, 0xEFEDF5, 0xDADAEB, 0xBCBDDC, 0x9E9AC8, 0x807DBA,
                 0x6A51A3, 0x54278F, 0x3F007D}},
};

constexpr int kColormapCount =
    static_cast<int>(sizeof(kFatbandColormaps) / sizeof(kFatbandColormaps[0]));

const SequentialColormap& colormapFor(int index)
{
    // Negative indices reach here only from a caller that lost count; wrap
    // them onto the first map rather than reading out of bounds.
    return kFatbandColormaps[static_cast<std::size_t>(std::max(index, 0))
                             % kColormapCount];
}

QColor stopColor(const SequentialColormap& map, int stop)
{
    const unsigned rgb = map.stops[std::clamp(stop, 0, kColormapStops - 1)];
    return QColor(static_cast<int>((rgb >> 16) & 0xFF),
                  static_cast<int>((rgb >> 8) & 0xFF),
                  static_cast<int>(rgb & 0xFF));
}

/// Mirror a colour's HSL lightness, keeping hue and saturation.
///
/// This is the dark-background rendering of a sequential map: on white paper
/// the ramp runs towards black, and the same ramp on a near-black plot has to
/// run towards white or its high-weight end is invisible. Mirroring lightness
/// (rather than reversing the stop ORDER, which would put the pale end at high
/// weight and invert the whole reading of the figure) preserves both the hue
/// identity and the direction of the ramp.
QColor mirrorLightness(const QColor& color)
{
    // hslHue() reports -1 for achromatic colours (the whole Greys map); hue is
    // irrelevant at zero saturation, so any valid channel will do.
    const int hue = std::max(color.hslHue(), 0);
    return QColor::fromHsl(hue, color.hslSaturation(), 255 - color.lightness());
}

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
    // Opening σ. Derived from the data's own resolution rather than fixed:
    // four bins is wide enough to look like a curve rather than a comb, and it
    // scales correctly across an electronic PDOS in eV and a phonon DOS in
    // cm⁻¹ without either needing to be told which it is. A run that states a
    // width still wins.
    if (pdos_.suggestedWidth > 0.0)
        pdosSigma_ = pdos_.suggestedWidth;
    else if (pdos_.binWidth > 0.0)
        pdosSigma_ = 4.0 * pdos_.binWidth;
    rebuildPdosCurves();
    update();
}

void BandPdosView::setPdosSmearing(double sigmaEv)
{
    if (std::abs(sigmaEv - pdosSigma_) < 1e-9)
        return;
    pdosSigma_ = sigmaEv;
    rebuildPdosCurves();
    update();
}

void BandPdosView::rebuildPdosCurves()
{
    pdosCurves_.clear();
    if (!pdos_.valid())
        return;
    // Already-broadened data (a run from before the smearing moved here) is
    // drawn exactly as stored. Convolving it again would apply a second
    // Gaussian and silently widen every peak by sqrt(sigma1^2 + sigma2^2).
    if (pdos_.broadened || pdos_.binWidth <= 0.0 || pdosSigma_ <= 0.0) {
        pdosCurves_ = pdos_.projections;
        return;
    }

    // A σ below about half a bin cannot be represented on this grid: the
    // kernel collapses onto one sample and the "curve" becomes the raw comb of
    // bins. Clamping is honest — the histogram's bin width IS the resolution
    // limit of everything the viewer can show.
    const double sigma = std::max(pdosSigma_, pdos_.binWidth * 0.5);
    // ±4σ carries 99.994 % of the weight; the tail beyond it is far below the
    // line width it would be drawn with.
    const int half = std::max(
        1, static_cast<int>(std::ceil(4.0 * sigma / pdos_.binWidth)));
    std::vector<double> kernel(static_cast<std::size_t>(2 * half + 1));
    const double norm = 1.0 / (sigma * std::sqrt(2.0 * M_PI));
    for (int k = -half; k <= half; ++k) {
        const double x = k * pdos_.binWidth;
        kernel[static_cast<std::size_t>(k + half)] =
            norm * std::exp(-0.5 * x * x / (sigma * sigma));
    }

    pdosCurves_.reserve(pdos_.projections.size());
    for (const auto& [label, histogram] : pdos_.projections) {
        const int n = static_cast<int>(histogram.size());
        std::vector<double> curve(static_cast<std::size_t>(n), 0.0);
        for (int i = 0; i < n; ++i) {
            // Scattering from the occupied bins rather than gathering into
            // every output bin: a PDOS histogram is mostly empty, and this
            // turns the cost into (states × kernel) instead of
            // (bins × kernel).
            const double weight = histogram[static_cast<std::size_t>(i)];
            if (weight == 0.0)
                continue;
            const int lo = std::max(0, i - half);
            const int hi = std::min(n - 1, i + half);
            for (int j = lo; j <= hi; ++j) {
                curve[static_cast<std::size_t>(j)] +=
                    weight * kernel[static_cast<std::size_t>(j - i + half)];
            }
        }
        pdosCurves_.emplace_back(label, std::move(curve));
    }
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
    // Stop 5 of 9 — the most saturated stop that is still clearly legible on
    // BOTH a white page and the dark default plot background, which is what a
    // list swatch has to be: it is drawn on the widget palette, not on the
    // plot, and the two need not agree.
    return stopColor(colormapFor(index), 5);
}

QString BandPdosView::fatbandColormapName(int index)
{
    return QString::fromLatin1(colormapFor(index).name);
}

QColor BandPdosView::fatbandColorAt(int index, double t, bool darkBackground)
{
    const SequentialColormap& map = colormapFor(index);
    t = std::clamp(t, 0.0, 1.0);

    // Piecewise-linear interpolation between the two bracketing class colours.
    const double scaled = t * (kColormapStops - 1);
    const int low = std::clamp(static_cast<int>(std::floor(scaled)), 0,
                               kColormapStops - 1);
    const int high = std::min(low + 1, kColormapStops - 1);
    const double f = scaled - low;
    QColor a = stopColor(map, low);
    QColor b = stopColor(map, high);
    if (darkBackground) {
        a = mirrorLightness(a);
        b = mirrorLightness(b);
    }
    QColor mixed(static_cast<int>(std::lround(a.red() + (b.red() - a.red()) * f)),
                 static_cast<int>(std::lround(a.green()
                                              + (b.green() - a.green()) * f)),
                 static_cast<int>(std::lround(a.blue()
                                              + (b.blue() - a.blue()) * f)));
    // The modification that makes the maps superimposable: alpha rises
    // linearly with the weight, so t = 0 is fully transparent instead of the
    // opaque white (or, mirrored, opaque black) the map's own low end is.
    // This is exactly matplotlib's cmap[:, -1] = linspace(0, 1, N) recipe.
    mixed.setAlpha(static_cast<int>(std::lround(255.0 * t)));
    return mixed;
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
    // Which end of each sequential map is the "away from the page" end. Keyed
    // off the plot background rather than the application theme: the
    // background is a style setting the user can change independently, and
    // an exported figure carries the plot's colours, not the app's.
    const bool dark = style_.background.lightness() < 128;

    int channel = -1;
    for (const auto& [label, weights] : fatbands_.projections) {
        ++channel;
        const auto shown = fatbandVisible_.find(label);
        if (shown != fatbandVisible_.end() && !shown->second)
            continue;
        // Width-only mode has no weight-to-colour mapping to sample, so it
        // uses the channel's swatch colour throughout — the thickness alone
        // carries the weight there.
        const QColor flat = fatbandColor(channel);

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
                    QColor color = flat;
                    if (fade) {
                        color = fatbandColorAt(channel, fraction, dark);
                        // The style's floor is zero by default, which leaves
                        // the colormap's own ramp untouched; a user who raises
                        // it is asking for weak contributions to stay visible.
                        if (color.alpha() < style_.fatbandMinAlpha)
                            color.setAlpha(std::clamp(style_.fatbandMinAlpha,
                                                      0, 255));
                    } else {
                        color.setAlpha(210);
                    }
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
    for (const auto& [label, curve] : pdosCurves_) {
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
    for (const auto& [label, curve] : pdosCurves_) {
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
