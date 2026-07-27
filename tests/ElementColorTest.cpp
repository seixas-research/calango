// CPK element colours and their label legibility.
//
// The periodic-table dialog and the insertion-element button both paint an
// element's own CPK colour and write its symbol on top. CPK is not a designed
// UI palette — it spans pure white (hydrogen) to deep blue (nitrogen) to
// saturated green (thulium) — so "which text colour" is a real decision, and
// getting it wrong produces a button whose label is simply invisible. Nothing
// throws; the symbol is just unreadable.
//
// So this checks the guarantee directly, over EVERY element: whatever
// readableTextColor() picks must clear the WCAG AA contrast bar for large/bold
// text against that element's swatch. It is a property over the whole table
// rather than a spot check, because the failures are individual elements —
// tuning that fixes tellurium can break thulium.
//
// GUI-free apart from QColor.

#include "core/Element.hpp"
#include "gui/GuiUtils.hpp"

#include <QColor>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace calango;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

/// WCAG 2.x relative luminance. Deliberately re-derived here rather than
/// shared with the implementation: a test that reuses the code under test's
/// own formula cannot detect that the formula is wrong.
double relativeLuminance(const QColor& c)
{
    const auto channel = [](double v) {
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF())
        + 0.0722 * channel(c.blueF());
}

double contrastRatio(const QColor& a, const QColor& b)
{
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

/// WCAG AA for large or bold text. These swatches carry a bold symbol in a
/// 36x32 cell, which is the "large" case.
constexpr double kMinimumContrast = 3.0;

} // namespace

int main()
{
    std::printf("CPK colours come from the element table:\n");
    {
        // Spot-check the convention against its published values, so a
        // reordered or corrupted table is caught rather than merely producing
        // "some colour".
        check(gui::cpkColor(1) == QColor(0xFF, 0xFF, 0xFF),
              "hydrogen is white");
        check(gui::cpkColor(6) == QColor(0x90, 0x90, 0x90),
              "carbon is grey");
        check(gui::cpkColor(7) == QColor(0x30, 0x50, 0xF8),
              "nitrogen is blue");
        check(gui::cpkColor(8) == QColor(0xFF, 0x0D, 0x0D),
              "oxygen is red");
        check(gui::cpkColor(26) == QColor(0xE0, 0x66, 0x33),
              "iron is orange");
        // Out-of-range Z must not index past the table.
        check(gui::cpkColor(0).isValid() && gui::cpkColor(9999).isValid(),
              "an out-of-range Z still yields a valid colour");
    }

    std::printf("Every element's label stays legible:\n");
    {
        double worst = 1e9;
        int worstZ = 0;
        int belowBody = 0;
        for (int z = 1; z <= core::Elements::maxZ; ++z) {
            const QColor background = gui::cpkColor(z);
            const QColor text = gui::readableTextColor(background);
            const double ratio = contrastRatio(text, background);
            if (ratio < worst) {
                worst = ratio;
                worstZ = z;
            }
            if (ratio < 4.5)
                ++belowBody;
        }
        std::printf("       worst case: %s (Z=%d) at ratio %.2f; "
                    "%d of %d below the 4.5 body-text bar\n",
                    core::Elements::data(worstZ).symbol, worstZ, worst,
                    belowBody, core::Elements::maxZ);
        check(worst >= kMinimumContrast,
              "every one of the 118 CPK swatches clears WCAG AA for "
              "large/bold text");
    }

    std::printf("The choice is the better of the two, not a threshold:\n");
    {
        // The property that makes the rule correct by construction: whichever
        // of near-black and white is returned must be the one with MORE
        // contrast. A luminance cut-off satisfies this for most colours and
        // fails for saturated greens, which is why it was replaced.
        const QColor dark(0x20, 0x20, 0x20);
        const QColor light(0xFF, 0xFF, 0xFF);
        int wrong = 0;
        for (int z = 1; z <= core::Elements::maxZ; ++z) {
            const QColor background = gui::cpkColor(z);
            const QColor chosen = gui::readableTextColor(background);
            const QColor other = chosen == dark ? light : dark;
            if (contrastRatio(chosen, background)
                < contrastRatio(other, background))
                ++wrong;
        }
        check(wrong == 0,
              "no element would read better with the opposite text colour");
    }

    std::printf("Extremes behave:\n");
    {
        check(gui::readableTextColor(QColor(0xFF, 0xFF, 0xFF))
                  == QColor(0x20, 0x20, 0x20),
              "white gets dark text");
        check(gui::readableTextColor(QColor(0x00, 0x00, 0x00))
                  == QColor(0xFF, 0xFF, 0xFF),
              "black gets light text");
        // Thulium: the case a Rec. 601 luma threshold got wrong, handing a
        // bright green a white label at a 1.99 ratio.
        const QColor thulium = gui::cpkColor(69);
        check(contrastRatio(gui::readableTextColor(thulium), thulium)
                  >= kMinimumContrast,
              "a saturated bright green (Tm) gets a legible label");
    }

    std::printf(failures == 0 ? "\nAll element-colour checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
