// Functional check of the rebuilt k-path table: the row indices the Remove
// button uses changed from QListWidget rows to QTreeWidget top-level indices,
// so the mapping row -> path_ entry has to still hold, breaks included.
#include "core/BrillouinZone.hpp"
#include "core/UnitCell.hpp"
#include "gui/BrillouinZoneWidget.hpp"

#include <QApplication>
#include <QPushButton>
#include <QTreeWidget>

#include <cstdio>
#include <cstdlib>

using namespace calango;

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

QPushButton* buttonWithTip(QWidget* w, const QString& needle)
{
    for (QPushButton* b : w->findChildren<QPushButton*>())
        if (b->toolTip().contains(needle))
            return b;
    return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const double a = 4.05;
    core::UnitCell cell(core::Vec3{0.0, a / 2, a / 2},
                        core::Vec3{a / 2, 0.0, a / 2},
                        core::Vec3{a / 2, a / 2, 0.0});
    pybridge::AseBridge::BandPathInfo info;
    info.suggestedPath = "GXWKGLUWLK,UX";
    info.specialPoints = {{"G", {0.0, 0.0, 0.0}},   {"X", {0.5, 0.0, 0.5}},
                          {"W", {0.5, 0.25, 0.75}}, {"K", {0.375, 0.375, 0.75}},
                          {"L", {0.5, 0.5, 0.5}},   {"U", {0.625, 0.25, 0.625}}};

    gui::BrillouinZoneWidget widget(core::computeBrillouinZone(cell), info, true);
    auto* table = widget.findChild<QTreeWidget*>();

    std::printf("Path table:\n");
    widget.setPathString(QStringLiteral("GXWK,UX"));
    check(table != nullptr, "the sequence is a real table");
    // 6 labels + 1 break.
    check(table && table->topLevelItemCount() == 7,
          "one row per path entry, break included");
    check(widget.pathString() == QStringLiteral("GXWK,UX"),
          "the path string round-trips");

    // The break row spans, carries no number, and is not counted as a step.
    if (table) {
        auto* breakRow = table->topLevelItem(4);
        check(breakRow && breakRow->isFirstColumnSpanned(),
              "the break row spans the columns");
        check(table->topLevelItem(3)->text(0) == QStringLiteral("4"),
              "steps are numbered past the break's row");
        check(table->topLevelItem(5)->text(0) == QStringLiteral("5"),
              "and the break does not consume a step number");
        // Coordinates land in their own columns rather than one packed string.
        check(table->topLevelItem(1)->text(1) == QStringLiteral("X")
                  && table->topLevelItem(1)->text(2) == QStringLiteral("0.500")
                  && table->topLevelItem(1)->text(3) == QStringLiteral("0.000")
                  && table->topLevelItem(1)->text(4) == QStringLiteral("0.500"),
              "label and the three coordinates each have their own column");
    }

    std::printf("Remove maps rows back to path entries:\n");
    if (table) {
        // Remove row 2 (the third entry, "W") — the row index must address the
        // same entry it did when this was a QListWidget.
        table->setCurrentItem(table->topLevelItem(2));
        auto* remove = buttonWithTip(&widget, QStringLiteral("Remove"));
        check(remove != nullptr, "the Remove action is present");
        if (remove)
            remove->click();
        check(widget.pathString() == QStringLiteral("GXK,UX"),
              "removing row 2 removes W, not a neighbour");

        // Removing the entry before a break must not leave a dangling break.
        widget.setPathString(QStringLiteral("GX,UX"));
        table->setCurrentItem(table->topLevelItem(1)); // "X", just before ","
        if (remove)
            remove->click();
        check(widget.pathString() == QStringLiteral("G,UX")
                  || widget.pathString() == QStringLiteral("GUX"),
              "the path stays well-formed after removing beside a break");
        check(!widget.pathString().startsWith(QLatin1Char(','))
                  && !widget.pathString().endsWith(QLatin1Char(',')),
              "no leading or trailing break survives");
    }

    std::printf("Undo / clear:\n");
    widget.setPathString(QStringLiteral("GXW"));
    if (auto* undo = buttonWithTip(&widget, QStringLiteral("Undo")))
        undo->click();
    check(widget.pathString() == QStringLiteral("GX"), "undo drops one point");
    if (auto* clear = buttonWithTip(&widget, QStringLiteral("Clear")))
        clear->click();
    check(widget.pathString().isEmpty(), "clear empties the path");
    check(table && table->topLevelItemCount() == 0, "and empties the table");

    std::printf(failures == 0 ? "\nAll k-path table checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
