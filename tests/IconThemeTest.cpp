// Theme-adaptive icon test.
//
// RemixIcon SVGs are monochrome and tinted at load time, so the tint is baked
// into a pixmap against whatever theme was active AT THE MOMENT OF THE CALL.
// That is the white-on-white bug: an icon built under Dark keeps its near-white
// glyph after a switch to Light, where it disappears into the background.
//
// Two things are checked, both by looking at actual rendered pixels rather than
// at the colour-picking function alone:
//   1. The tint for each theme CONTRASTS with that theme's background — dark
//      glyphs on light, light glyphs on dark.
//   2. A bound icon is re-rendered when the theme changes. This is what a
//      colour-table-only test would miss: the table can be right while the
//      widget still shows the pixmap it was given at construction.
//
// Needs a QApplication (QIcon/QPixmap require a GUI application object) but no
// display: runs under the offscreen platform.

#include "gui/ThemeManager.hpp"
#include "ui/IconManager.hpp"

#include <QApplication>
#include <QIcon>
#include <QImage>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>

#include <cstdio>
#include <cstdlib>

using calango::gui::ThemeManager;
using calango::ui::IconManager;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition)
        ++failures;
}

/// Mean luminance of the opaque pixels of `icon` at 24 px. The glyph is a thin
/// stroke on transparency, so averaging the whole image would mostly measure
/// the empty background; only pixels the glyph actually covers are sampled.
double glyphLuminance(const QIcon& icon, bool* anyOpaque = nullptr)
{
    const QImage image = icon.pixmap(24).toImage().convertToFormat(
        QImage::Format_ARGB32);
    double sum = 0.0;
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() < 128)
                continue;
            sum += 0.2126 * pixel.redF() + 0.7152 * pixel.greenF()
                + 0.0722 * pixel.blueF();
            ++count;
        }
    }
    if (anyOpaque)
        *anyOpaque = count > 0;
    return count > 0 ? sum / count : -1.0;
}

void setTheme(ThemeManager::Theme theme)
{
    QSettings settings;
    settings.setValue(QStringLiteral("appearance/theme"),
                      ThemeManager::toString(theme));
    settings.sync();
    ThemeManager::apply(theme);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // Keep the test off the developer's real settings file.
    QCoreApplication::setOrganizationName(QStringLiteral("CalangoTest"));
    QCoreApplication::setApplicationName(QStringLiteral("IconThemeTest"));
    QApplication app(argc, argv);

    const QString name = QStringLiteral("focus-3-line");
    if (!IconManager::has(name)) {
        std::printf("SKIP: icon assets are not bundled in this build\n");
        return EXIT_SUCCESS;
    }

    std::printf("Tint contrasts with the theme it is drawn on:\n");
    setTheme(ThemeManager::Theme::Light);
    bool opaque = false;
    const double lightThemeLuma = glyphLuminance(IconManager::icon(name), &opaque);
    check(opaque, "the glyph renders at all in Light mode");
    // The reported bug: a near-white glyph on a light background. Anything
    // above mid-grey would be invisible on Light Mode's near-white surfaces.
    check(lightThemeLuma >= 0.0 && lightThemeLuma < 0.5,
          "Light mode draws a DARK glyph (not white-on-white)");

    setTheme(ThemeManager::Theme::Dark);
    const double darkThemeLuma = glyphLuminance(IconManager::icon(name), &opaque);
    check(opaque, "the glyph renders at all in Dark mode");
    check(darkThemeLuma > 0.5, "Dark mode draws a LIGHT glyph");
    check(darkThemeLuma > lightThemeLuma,
          "the two themes tint in opposite directions");

    std::printf("A bound icon follows a theme change:\n");
    {
        // Bind under Dark, then switch to Light: this is exactly the sequence
        // that produced invisible icons.
        setTheme(ThemeManager::Theme::Dark);
        QToolButton button;
        IconManager::bind(&button, name);
        const double asBound = glyphLuminance(button.icon());
        check(asBound > 0.5, "binds with the Dark tint");

        setTheme(ThemeManager::Theme::Light);
        IconManager::refreshAll();
        const double afterSwitch = glyphLuminance(button.icon());
        check(afterSwitch < 0.5,
              "re-tints to the Light glyph after the theme changes");
        check(afterSwitch < asBound, "the icon actually changed");
    }

    std::printf("An unbound icon is what the bug looked like:\n");
    {
        // Contrast case, pinning WHY bind() exists: an icon set the plain way
        // keeps its original pixmap across the switch. If this ever stops being
        // true, icon() has gained live theming and bind() is redundant.
        setTheme(ThemeManager::Theme::Dark);
        QPushButton button;
        button.setIcon(IconManager::icon(name));
        const double before = glyphLuminance(button.icon());
        setTheme(ThemeManager::Theme::Light);
        IconManager::refreshAll();
        const double after = glyphLuminance(button.icon());
        check(qFuzzyCompare(before + 1.0, after + 1.0),
              "an unbound icon does NOT follow the theme (hence bind())");
    }

    std::printf("Destroyed widgets do not break a refresh:\n");
    {
        {
            QToolButton temporary;
            IconManager::bind(&temporary, name);
        } // destroyed while still registered
        IconManager::refreshAll(); // must not crash on the dangling entry
        check(true, "refreshAll() survives a destroyed binding target");
    }

    std::printf(failures == 0 ? "\nAll icon theme checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
