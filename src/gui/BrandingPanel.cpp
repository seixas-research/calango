#include "gui/BrandingPanel.hpp"

#include <QFontMetrics>
#include <QPainter>

#include <algorithm>

namespace calango::gui {

namespace {
/// Breathing room between the logo's bottom edge and the version caption.
constexpr int kVersionGap = 2;
} // namespace

BrandingPanel::BrandingPanel(QWidget* parent)
    : QWidget(parent)
    , source_(QStringLiteral(":/assets/calango/logo_light.png"))
{
    versionText_ = tr("Version: %1").arg(QStringLiteral(CALANGO_VERSION));
    versionFont_ = font();
    // A caption, not a headline: slightly smaller than the UI default so the
    // logo stays the visual anchor of the card.
    versionFont_.setPointSizeF(versionFont_.pointSizeF() * 0.85);

    // The logo is drawn "contain"-style (see paintEvent), so it simply scales
    // down inside whatever height the dock is given — nothing clips. The
    // floor is the 30 px the default layout historically asked for PLUS the
    // version caption drawn beneath the logo: without that allowance, the
    // default strip height would squeeze the artwork down to the leftover
    // pixels above the text. resizeDocks is a hint the splitter rounds, so
    // MainWindow reads this minimum back as the branding row's default
    // height, keeping the two in step while still letting the user drag the
    // card taller.
    setMinimumHeight(30 + kVersionGap + QFontMetrics(versionFont_).height());
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void BrandingPanel::setDarkVariant(bool dark)
{
    if (dark == dark_ && !source_.isNull())
        return;
    dark_ = dark;
    source_ = QPixmap(dark ? QStringLiteral(":/assets/calango/logo_dark.png")
                           : QStringLiteral(":/assets/calango/logo_light.png"));
    scaled_ = QPixmap();   // invalidate the scaled cache
    scaledFor_ = QSize();
    update();
}

void BrandingPanel::paintEvent(QPaintEvent*)
{
    // The logo fits inside the panel: scaled preserving aspect ratio to the
    // *smaller* dimension (contain), so it is never stretched and never
    // cropped — when the panel's aspect ratio differs it is letterboxed and
    // centered. Rendered at device resolution so it stays crisp on high-DPI.
    // The bottom strip is reserved for the version caption, which sits
    // centered directly under the logo's drawn edge.
    if (source_.isNull())
        return;

    const QFontMetrics metrics(versionFont_);
    const int textHeight = metrics.height();
    const int logoHeight =
        std::max(1, height() - textHeight - kVersionGap);

    const qreal dpr = devicePixelRatioF();
    const QSize target = QSize(width(), logoHeight) * dpr;
    // KeepAspectRatio yields a pixmap smaller-or-equal to `target` on one
    // axis, so compare against the target we last built for (not the scaled
    // size) to avoid rescaling on every repaint.
    if (scaled_.isNull() || scaledFor_ != target) {
        scaled_ = source_.scaled(target, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
        scaled_.setDevicePixelRatio(dpr);
        scaledFor_ = target;
    }

    QPainter painter(this);
    const QSizeF drawn = QSizeF(scaled_.size()) / dpr;
    const QPointF logoTopLeft((width() - drawn.width()) / 2.0,
                              (logoHeight - drawn.height()) / 2.0);
    painter.drawPixmap(logoTopLeft, scaled_);

    // Directly below the artwork, not pinned to the panel's bottom edge: on
    // a card dragged taller the caption follows the logo instead of drifting
    // away from it. Clamped so it never leaves the panel.
    const double textTop =
        std::min<double>(logoTopLeft.y() + drawn.height() + kVersionGap,
                         height() - textHeight);
    painter.setFont(versionFont_);
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(QRectF(0.0, textTop, width(), textHeight),
                     Qt::AlignHCenter | Qt::AlignVCenter, versionText_);
}

} // namespace calango::gui
