#include "gui/BrandingPanel.hpp"

#include <QPainter>
#include <QPixmap>

namespace calango::gui {

BrandingPanel::BrandingPanel(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(60);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void BrandingPanel::paintEvent(QPaintEvent*)
{
    // The banner covers the whole panel: scaled preserving aspect ratio
    // to the larger dimension (cover), center-cropped, at device
    // resolution so it stays crisp on high-DPI displays.
    static const QPixmap source(
        QStringLiteral(":/assets/calango/panel.png"));
    if (source.isNull())
        return;

    const qreal dpr = devicePixelRatioF();
    const QSize target = size() * dpr;
    if (scaled_.isNull() || scaled_.size() != target) {
        scaled_ = source.scaled(target, Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation);
        scaled_.setDevicePixelRatio(dpr);
    }

    QPainter painter(this);
    const QSizeF drawn = QSizeF(scaled_.size()) / dpr;
    painter.drawPixmap(QPointF((width() - drawn.width()) / 2.0,
                               (height() - drawn.height()) / 2.0),
                       scaled_);
}

} // namespace calango::gui
