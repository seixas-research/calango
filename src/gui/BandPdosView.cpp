#include "gui/BandPdosView.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace calango::gui {

namespace {

const QColor kBackground(24, 26, 30);
const QColor kFrame(120, 124, 134);
const QColor kGrid(70, 74, 84);
const QColor kText(210, 213, 220);
const QColor kFermi(255, 199, 88);
const QColor kSpinColors[2] = {QColor(102, 163, 255), QColor(235, 110, 96)};

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
    if (on)
        reference_ = 0.0; // frequencies are absolute; ω = 0 is the acoustic line
    update();
}

void BandPdosView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), kBackground);

    const double margin = 8.0;
    QRectF area = rect().adjusted(margin + 34, margin, -margin, -margin - 22);
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
        painter.setPen(kText);
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("No band-structure data loaded"));
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

    painter.setPen(QPen(kFrame, 1.2));
    painter.drawRect(rect);

    // High-symmetry verticals + labels.
    painter.setFont(QFont(painter.font().family(), 10));
    for (std::size_t i = 0; i < bands_.specialX.size(); ++i) {
        const double px = mapX(bands_.specialX[i]);
        painter.setPen(QPen(kGrid, 1.0));
        painter.drawLine(QPointF(px, rect.top()), QPointF(px, rect.bottom()));
        painter.setPen(kText);
        const QString label = prettyLabel(
            i < static_cast<std::size_t>(bands_.specialLabels.size())
                ? bands_.specialLabels[static_cast<int>(i)]
                : QString());
        painter.drawText(QRectF(px - 20, rect.bottom() + 2, 40, 18),
                         Qt::AlignHCenter | Qt::AlignTop, label);
    }

    // Energy ticks every ~2 eV-ish (5 divisions).
    for (int i = 0; i <= 5; ++i) {
        const double e = eMin_ + (eMax_ - eMin_) * i / 5.0;
        const double py = mapY(e);
        painter.setPen(QPen(kGrid, 1.0, Qt::DotLine));
        painter.drawLine(QPointF(rect.left(), py), QPointF(rect.right(), py));
        painter.setPen(kText);
        painter.drawText(QRectF(rect.left() - 40, py - 8, 36, 16),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(e, 'f', phonon_ ? 0 : 1));
    }

    // Reference line: E − E_F = 0 for electrons, ω = 0 (acoustic) for phonons.
    if (eMin_ < 0.0 && eMax_ > 0.0) {
        painter.setPen(QPen(phonon_ ? kGrid : kFermi, 1.4, Qt::DashLine));
        painter.drawLine(QPointF(rect.left(), mapY(0.0)),
                         QPointF(rect.right(), mapY(0.0)));
    }

    painter.setClipRect(rect);
    for (std::size_t spin = 0; spin < bands_.energies.size(); ++spin) {
        painter.setPen(QPen(kSpinColors[spin % 2], 1.4));
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

    painter.setPen(kText);
    painter.drawText(QRectF(rect.left(), rect.top() - 2, rect.width(), 16),
                     Qt::AlignLeft | Qt::AlignTop,
                     phonon_ ? tr("Frequency (cm⁻¹)") : tr("E − E_ref (eV)"));
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

    painter.setPen(QPen(kFrame, 1.2));
    painter.drawRect(rect);
    if (eMin_ < 0.0 && eMax_ > 0.0) {
        painter.setPen(QPen(phonon_ ? kGrid : kFermi, 1.4, Qt::DashLine));
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
        painter.setPen(QPen(projectionColor(index), 1.6));
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
        painter.drawPath(path);
    }
    painter.setClipping(false);

    painter.setPen(kText);
    painter.drawText(QRectF(rect.left(), rect.bottom() + 2, rect.width(), 18),
                     Qt::AlignHCenter | Qt::AlignTop,
                     phonon_ ? tr("PhDOS (states/cm⁻¹)") : tr("PDOS (states/eV)"));
}

} // namespace calango::gui
