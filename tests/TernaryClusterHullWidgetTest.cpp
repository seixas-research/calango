// Plot-generation smoke test for the native ternary Cluster Expansion widget
// (Task 6.3: "a plot-generation smoke test asserting a ... figure is
// produced without error"). Native QPainter, not mpltern, per the project's
// own "no external plotting dependency" convention — see
// TernaryClusterHullWidget.hpp — so there is nothing optional to skip on:
// the widget is always available.
//
// Renders a real ternary Cluster Expansion + hull (the same pipeline
// ClusterExpansionTest.cpp's ternary sections exercise) and checks that
// actual ink lands on the canvas, that CSV export produces one row per
// point, and that image export writes a real, non-empty file.
//
// Needs a QApplication (QPainter/QWidget) but no display: offscreen.

#include "core/ClusterExpansion.hpp"
#include "core/Structure.hpp"
#include "core/TernaryConvexHull.hpp"
#include "core/UnitCell.hpp"
#include "gui/TernaryClusterHullWidget.hpp"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace calango;

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

/// Count pixels that differ meaningfully from the white canvas — proof
/// something was actually drawn, not just a blank rectangle.
int inkPixels(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = image.pixel(x, y);
            if (qRed(px) < 250 || qGreen(px) < 250 || qBlue(px) < 250)
                ++count;
        }
    return count;
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication::setOrganizationName(QStringLiteral("CalangoTest"));
    QCoreApplication::setApplicationName(QStringLiteral("TernaryClusterHullWidgetTest"));
    QApplication app(argc, argv);

    // -- Build a real ternary ensemble + hull, the same way the GUI would --
    const double a = 3.0;
    core::Structure parent;
    parent.addAtom({29, {0, 0, 0}});
    parent.setCell(core::UnitCell({a, 0, 0}, {0, a, 0}, {0, 0, a},
                                  {true, true, true}));

    core::ClusterExpansionOptions opt;
    opt.activeZ = 29;
    opt.speciesZ = {29, 79, 47}; // Cu, Au, Ag
    opt.supercell[0] = 4;
    opt.supercell[1] = 1;
    opt.supercell[2] = 1;
    opt.pairCutoff = 3.5;
    opt.maxConfigs = 500;
    opt.maxEnumeration = 500000;
    const core::ClusterExpansionResult ce = core::generateClusterExpansion(parent, opt);
    check(ce.configs.size() > 10,
          "the ternary ensemble has enough configurations to plot");

    // A simple, deterministic synthetic energy so the test does not depend
    // on an external calculator: formation energy from the Cu-count / Au-
    // count / Ag-count histogram, giving a real (not flat) surface with a
    // clear minimum somewhere in the interior.
    std::vector<core::TernaryHullPoint> points;
    points.reserve(ce.configs.size());
    for (const auto& cfg : ce.configs) {
        int nCu = 0, nAu = 0, nAg = 0;
        for (const auto& at : cfg.structure.atoms()) {
            if (at.atomicNumber == 29)
                ++nCu;
            else if (at.atomicNumber == 79)
                ++nAu;
            else if (at.atomicNumber == 47)
                ++nAg;
        }
        const int n = nCu + nAu + nAg;
        const double xB = n > 0 ? static_cast<double>(nAu) / n : 0.0;
        const double xC = n > 0 ? static_cast<double>(nAg) / n : 0.0;
        // A cupped surface, negative in the interior, zero at the corners —
        // exactly the shape a real ordering alloy's formation energy has.
        const double xA = 1.0 - xB - xC;
        const double e = -0.3 * (xA * xB + xB * xC + xC * xA);
        points.push_back({xB, xC, e, cfg.structure.chemicalFormula(),
                          static_cast<int>(points.size())});
    }

    const core::TernaryConvexHullResult hull = core::computeTernaryConvexHull(points);
    int groundStates = 0;
    for (const auto& pt : hull.points)
        if (pt.onHull)
            ++groundStates;
    check(groundStates >= 3,
          "at least the three pure endpoints are ground states");
    check(!hull.facets.empty(), "the hull has at least one facet to draw");

    // -- The widget itself ----------------------------------------------------
    gui::TernaryClusterHullWidget widget;
    widget.resize(640, 480);
    check(!widget.hasData(), "a fresh widget has no data");
    widget.setData(hull, {QStringLiteral("Cu"), QStringLiteral("Au"),
                         QStringLiteral("Ag")});
    check(widget.hasData(), "setData() marks the widget as having data");

    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        widget.render(painter, QRectF(QPointF(0, 0), widget.size()));
    }
    const int ink = inkPixels(image);
    check(ink > 500,
          "the rendered figure has substantial ink (" + std::to_string(ink)
              + " non-white pixels) — a real plot, not a blank canvas");

    // -- CSV export -------------------------------------------------------------
    const QString csv = widget.toCsv();
    // Two header lines: the leading "# ..." comment and the actual CSV
    // column-header row.
    const int dataRows = static_cast<int>(csv.count(QLatin1Char('\n'))) - 2;
    check(dataRows == static_cast<int>(hull.points.size()),
          "the CSV has exactly one data row per point");

    // -- Image export -----------------------------------------------------------
    {
        QTemporaryDir dir;
        check(dir.isValid(), "a temp directory is available for image export");
        const QString path = dir.path() + QStringLiteral("/ternary_hull.png");
        check(widget.exportImage(path),
              "exportImage() reports success");
        check(QFileInfo::exists(path) && QFileInfo(path).size() > 0,
              "...and actually wrote a non-empty PNG file");
    }

    // -- clear() resets cleanly ---------------------------------------------
    widget.clear();
    check(!widget.hasData(), "clear() resets hasData()");
    check(!widget.exportImage(QStringLiteral("/tmp/should_not_be_written.png")),
          "exportImage() on an empty widget refuses rather than writing a "
          "blank figure");

    if (failures == 0) {
        std::printf("\nAll ternary cluster-hull widget checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d ternary cluster-hull widget check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
