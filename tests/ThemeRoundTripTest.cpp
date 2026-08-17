// Theme round-trip test.
//
// A Light -> Dark round trip once left QToolTip showing the light theme's
// background/text after switching back to Dark: QApplication::setPalette()
// updated QPalette::ToolTipBase/ToolTipText correctly both directions, but
// the application style sheet only ever named the tooltip's BORDER colour —
// once any style sheet matches a widget class (QToolTip here), Qt's QSS
// engine stops reliably re-resolving that widget's unset background/text
// properties from a later setPalette() call the way an unstyled widget
// would. ThemeManager::apply() now derives the style sheet's tooltip colours
// from the very QPalette object it just applied, so palette and style sheet
// can never disagree — this checks that holds after any number of round
// trips, not just the first switch.
//
// Needs a QApplication (QPalette/style sheet need a GUI application object)
// but no display: runs under the offscreen platform.

#include "gui/ThemeManager.hpp"

#include <QApplication>
#include <QPalette>
#include <QSettings>

#include <cstdio>
#include <cstdlib>

using calango::gui::ThemeManager;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition)
        ++failures;
}

void setTheme(ThemeManager::Theme theme)
{
    QSettings settings;
    settings.setValue(QStringLiteral("appearance/theme"),
                      ThemeManager::toString(theme));
    settings.sync();
    ThemeManager::apply(theme);
}

/// The style sheet's QToolTip rule must name exactly the palette's current
/// ToolTipBase/ToolTipText colours. That agreement is the whole fix: an
/// unset QSS property is what silently stopped tracking a later
/// setPalette() call.
bool styleSheetMatchesPalette()
{
    const QPalette palette = qApp->palette();
    const QString sheet = qApp->styleSheet();
    return sheet.contains(palette.color(QPalette::ToolTipBase).name(),
                          Qt::CaseInsensitive)
        && sheet.contains(palette.color(QPalette::ToolTipText).name(),
                          Qt::CaseInsensitive);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // Keep the test off the developer's real settings file.
    QCoreApplication::setOrganizationName(QStringLiteral("CalangoTest"));
    QCoreApplication::setApplicationName(QStringLiteral("ThemeRoundTripTest"));
    QApplication app(argc, argv);

    std::printf("Each theme's tooltip colours agree with its style sheet:\n");
    setTheme(ThemeManager::Theme::Dark);
    const QColor darkTipBg = qApp->palette().color(QPalette::ToolTipBase);
    const QColor darkTipFg = qApp->palette().color(QPalette::ToolTipText);
    check(styleSheetMatchesPalette(), "Dark: tooltip style sheet matches the palette");

    setTheme(ThemeManager::Theme::Light);
    const QColor lightTipBg = qApp->palette().color(QPalette::ToolTipBase);
    const QColor lightTipFg = qApp->palette().color(QPalette::ToolTipText);
    check(styleSheetMatchesPalette(), "Light: tooltip style sheet matches the palette");
    check(lightTipBg != darkTipBg,
          "Light and Dark tooltip backgrounds actually differ");

    std::printf("Round trips (the reported bug broke on the SECOND switch "
                "back, not the first):\n");
    for (int trip = 0; trip < 3; ++trip) {
        setTheme(ThemeManager::Theme::Dark);
        check(qApp->palette().color(QPalette::ToolTipBase) == darkTipBg
                  && qApp->palette().color(QPalette::ToolTipText) == darkTipFg,
              "Dark tooltip colours unchanged after a round trip");
        check(styleSheetMatchesPalette(),
              "Dark: tooltip style sheet still matches the palette");

        setTheme(ThemeManager::Theme::Light);
        check(qApp->palette().color(QPalette::ToolTipBase) == lightTipBg
                  && qApp->palette().color(QPalette::ToolTipText) == lightTipFg,
              "Light tooltip colours unchanged after a round trip");
        check(styleSheetMatchesPalette(),
              "Light: tooltip style sheet still matches the palette");
    }

    std::printf("Disabled/Highlight is set the same way in both themes (the "
                "other one-way gap apply() had):\n");
    setTheme(ThemeManager::Theme::Dark);
    check(qApp->palette().color(QPalette::Disabled, QPalette::Highlight)
              == QColor(70, 70, 70),
          "Dark sets Disabled/Highlight");
    setTheme(ThemeManager::Theme::Light);
    check(qApp->palette().color(QPalette::Disabled, QPalette::Highlight)
              == QColor(200, 200, 200),
          "Light sets Disabled/Highlight");

    std::printf(failures == 0 ? "\nAll theme round-trip checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
