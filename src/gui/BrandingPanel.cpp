#include "gui/BrandingPanel.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>

namespace calango::gui {

BrandingPanel::BrandingPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(12);

    auto* logo = new QLabel(this);
    const QPixmap pixmap(QStringLiteral(":/assets/calango/icon_transparent.png"));
    const qreal dpr = devicePixelRatioF();
    QPixmap scaled = pixmap.scaled(QSize(56, 56) * dpr, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    logo->setPixmap(scaled);
    logo->setFixedSize(56, 56);
    layout->addWidget(logo);

    auto* name = new QLabel(QStringLiteral("Calango"), this);
    QFont nameFont = name->font();
    nameFont.setPointSizeF(nameFont.pointSizeF() * 1.7);
    nameFont.setBold(true);
    name->setFont(nameFont);
    layout->addWidget(name, 1);

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
}

} // namespace calango::gui
