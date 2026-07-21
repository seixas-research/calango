#include "gui/BrandingPanel.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>

namespace calango::gui {

BrandingPanel::BrandingPanel(QWidget* parent)
    : QWidget(parent)
{
    // Just the logo, centered both ways — the panel carries no text.
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->addStretch(1);

    auto* logo = new QLabel(this);
    const QPixmap pixmap(QStringLiteral(":/assets/calango/icon_transparent.png"));
    const qreal dpr = devicePixelRatioF();
    QPixmap scaled = pixmap.scaled(QSize(112, 112) * dpr, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    logo->setPixmap(scaled);
    logo->setFixedSize(112, 112);
    layout->addWidget(logo, 0, Qt::AlignVCenter);

    layout->addStretch(1);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
}

} // namespace calango::gui
