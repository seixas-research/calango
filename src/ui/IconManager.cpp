#include "ui/IconManager.hpp"

#include "gui/ThemeManager.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QSvgRenderer>

#include <algorithm>

namespace calango::ui {
namespace {

QString assetPath(const QString& name)
{
    return QStringLiteral(":/assets/.internal/icons/%1.svg").arg(name);
}

// Supersample factor: render the SVG a few times larger than the logical size
// and stamp a matching device-pixel-ratio, so a single cached pixmap stays
// crisp on 1x through 3x displays.
constexpr int kSupersample = 3;

} // namespace

QColor IconManager::color(State state)
{
    const bool dark =
        gui::ThemeManager::isEffectivelyDark(gui::ThemeManager::current());
    switch (state) {
    case State::Active:
        return dark ? QColor(0xE8, 0xEA, 0xED) : QColor(0x3C, 0x40, 0x43);
    case State::Disabled:
        return dark ? QColor(0x5F, 0x63, 0x68) : QColor(0xBD, 0xC1, 0xC6);
    case State::Hovered:
        return dark ? QColor(0xFF, 0xFF, 0xFF) : QColor(0x20, 0x21, 0x24);
    case State::Pressed:
        return dark ? QColor(0x8A, 0xB4, 0xF8) : QColor(0x1A, 0x73, 0xE8);
    }
    return dark ? QColor(0xE8, 0xEA, 0xED) : QColor(0x3C, 0x40, 0x43);
}

bool IconManager::has(const QString& name)
{
    return QFile::exists(assetPath(name));
}

QPixmap IconManager::pixmap(const QString& name, const QColor& color, int px)
{
    const QString path = assetPath(name);
    QSvgRenderer renderer(path);
    if (!renderer.isValid())
        return {};

    const int side = std::max(1, px) * kSupersample;
    QPixmap pm(side, side);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        renderer.render(&p, QRectF(0, 0, side, side));
        // RemixIcon glyphs paint as currentColor (black by default); keep only
        // their alpha shape and flood it with the requested tint.
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), color);
    }
    pm.setDevicePixelRatio(kSupersample);
    return pm;
}

QIcon IconManager::icon(const QString& name, int px)
{
    QIcon result;
    const QPixmap active = pixmap(name, color(State::Active), px);
    if (active.isNull())
        return result; // missing asset — empty icon rather than a wrong glyph
    result.addPixmap(active, QIcon::Normal);
    result.addPixmap(pixmap(name, color(State::Disabled), px), QIcon::Disabled);
    // Qt uses the Active mode when a control is hovered/focused.
    result.addPixmap(pixmap(name, color(State::Hovered), px), QIcon::Active);
    // Selected (e.g. pressed/checked toolbuttons in some styles).
    result.addPixmap(pixmap(name, color(State::Pressed), px), QIcon::Selected);
    return result;
}

QIcon IconManager::icon(const QString& name, const QColor& color, int px)
{
    const QPixmap pm = pixmap(name, color, px);
    return pm.isNull() ? QIcon() : QIcon(pm);
}

} // namespace calango::ui
