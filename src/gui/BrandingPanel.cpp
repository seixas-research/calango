#include "gui/BrandingPanel.hpp"

#include <QPainter>

namespace calango::gui {

BrandingPanel::BrandingPanel(QWidget* parent)
    : QWidget(parent)
    , source_(QStringLiteral(":/assets/.internal/logo_light.png"))
{
    setMinimumHeight(60);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void BrandingPanel::setDarkVariant(bool dark)
{
    if (dark == dark_ && !source_.isNull())
        return;
    dark_ = dark;
    source_ = QPixmap(dark ? QStringLiteral(":/assets/.internal/logo_dark.png")
                           : QStringLiteral(":/assets/.internal/logo_light.png"));
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
    if (source_.isNull())
        return;

    const qreal dpr = devicePixelRatioF();
    const QSize target = size() * dpr;
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
    painter.drawPixmap(QPointF((width() - drawn.width()) / 2.0,
                               (height() - drawn.height()) / 2.0),
                       scaled_);
}

} // namespace calango::gui
