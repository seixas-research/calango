#include "gui/ThemeManager.hpp"

#include "gui/SettingsManager.hpp"

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleHints>

namespace calango::gui {

namespace {

QPalette darkPalette()
{
    QPalette p;
    const QColor window(53, 53, 53);
    const QColor base(35, 35, 35);
    const QColor text(220, 220, 220);
    const QColor disabled(120, 120, 120);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(66, 150, 250));
    p.setColor(QPalette::Highlight, QColor(66, 150, 250));
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::PlaceholderText, QColor(150, 150, 150));
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(70, 70, 70));
    return p;
}

QPalette lightPalette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(240, 240, 240));
    p.setColor(QPalette::WindowText, Qt::black);
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::AlternateBase, QColor(245, 245, 245));
    p.setColor(QPalette::ToolTipBase, QColor(255, 255, 220));
    p.setColor(QPalette::ToolTipText, Qt::black);
    p.setColor(QPalette::Text, Qt::black);
    p.setColor(QPalette::Button, QColor(240, 240, 240));
    p.setColor(QPalette::ButtonText, Qt::black);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(0, 102, 204));
    p.setColor(QPalette::Highlight, QColor(51, 153, 255));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(120, 120, 120));
    const QColor disabled(160, 160, 160);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    // Symmetric with darkPalette()'s Disabled/Highlight override just below
    // it — leaving this one out was the one role apply() did not set the
    // same way in both directions.
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(200, 200, 200));
    return p;
}

bool osPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto* hints = QGuiApplication::styleHints())
        return hints->colorScheme() == Qt::ColorScheme::Dark;
#endif
    // Fallback: infer from the current default palette's window lightness.
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

} // namespace

ThemeManager::Theme ThemeManager::fromString(const QString& text)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("dark"))
        return Theme::Dark;
    if (t == QLatin1String("light"))
        return Theme::Light;
    return Theme::System;
}

QString ThemeManager::toString(Theme theme)
{
    switch (theme) {
    case Theme::Dark:
        return QStringLiteral("dark");
    case Theme::Light:
        return QStringLiteral("light");
    case Theme::System:
        break;
    }
    return QStringLiteral("system");
}

ThemeManager::Theme ThemeManager::current()
{
    return fromString(
        QSettings().value(QLatin1String(SettingsManager::kTheme),
                          QStringLiteral("system")).toString());
}

bool ThemeManager::isEffectivelyDark(Theme theme)
{
    switch (theme) {
    case Theme::Dark:
        return true;
    case Theme::Light:
        return false;
    case Theme::System:
        return osPrefersDark();
    }
    return false;
}

bool ThemeManager::apply(Theme theme)
{
    const bool dark = isEffectivelyDark(theme);

    // Fusion honors QPalette identically on macOS/Windows/Linux, so the
    // Dark/Light switch is actually visible (the native macOS style ignores
    // most palette roles).
    if (auto* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);

    const QPalette palette = dark ? darkPalette() : lightPalette();
    QApplication::setPalette(palette);

    // QToolTip is matched by this style sheet's "QToolTip" selector, and once
    // a style sheet matches a widget class, Qt's QSS engine does not reliably
    // keep resolving THAT widget's unset properties from a later
    // QApplication::setPalette() call the way an unstyled widget would — this
    // is what left tooltips on the light-theme background/text after a
    // Light -> Dark round trip even though the border colour (which IS set
    // here) updated correctly both ways. Declaring background/text
    // explicitly, read from the very palette just applied above, closes that
    // gap for any number of round trips: nothing about a tooltip is left to
    // implicit re-propagation.
    const QColor tooltipBg = palette.color(QPalette::ToolTipBase);
    const QColor tooltipFg = palette.color(QPalette::ToolTipText);
    const QString border = dark ? QStringLiteral("#555") : QStringLiteral("#b0b0b0");
    qApp->setStyleSheet(
        QStringLiteral("QToolTip { background-color: %1; color: %2; "
                       "border: 1px solid %3; }")
            .arg(tooltipBg.name(), tooltipFg.name(), border));

    return dark;
}

} // namespace calango::gui
