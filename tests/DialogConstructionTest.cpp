// Dialog construction smoke test.
//
// Exists because of a real crash: GrapheneOxideWizard's constructor called
// setChecked() partway through building its widgets. That emitted toggled(),
// which reached a slot that read controls belonging to a group box created
// twenty lines LATER — a null dereference the moment the menu item was
// clicked.
//
// Nothing else in the suite caught it. The offscreen application smoke test
// launches the main window but never opens a dialog, and a compile cannot see
// a signal-ordering hazard. So this constructs each dialog and drives the
// interactions that fire signals during or just after construction.
//
// A dialog is exercised, not merely constructed: toggling every control is
// what re-enters the slots that read half-built state.

#include "core/Structure.hpp"
#include "gui/GeometryConstraintsDialog.hpp"
#include "gui/GrapheneOxideWizard.hpp"
#include "gui/HubbardParametersDialog.hpp"
#include "gui/OpticsPlotStyleDialog.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSpinBox>

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace calango::gui;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition)
        ++failures;
}

/// Toggle every checkbox and nudge every numeric control. Each of these emits
/// a signal, and it is the SLOTS behind them that touch state which may not
/// exist yet — so this is what actually exercises the hazard.
void exerciseControls(QWidget* dialog)
{
    for (QCheckBox* box : dialog->findChildren<QCheckBox*>()) {
        box->toggle();
        box->toggle();
    }
    for (QSpinBox* spin : dialog->findChildren<QSpinBox*>())
        spin->setValue(spin->value() + spin->singleStep() <= spin->maximum()
                           ? spin->value() + spin->singleStep()
                           : spin->minimum());
    for (QDoubleSpinBox* spin : dialog->findChildren<QDoubleSpinBox*>())
        spin->setValue(spin->value() + spin->singleStep() <= spin->maximum()
                           ? spin->value() + spin->singleStep()
                           : spin->minimum());
    for (QComboBox* combo : dialog->findChildren<QComboBox*>())
        for (int i = 0; i < combo->count(); ++i)
            combo->setCurrentIndex(i);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication::setOrganizationName(QStringLiteral("CalangoTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DialogTest"));
    QApplication app(argc, argv);

    std::printf("Graphene Oxide wizard:\n");
    {
        // Constructing it is the exact action that crashed.
        GrapheneOxideWizard wizard;
        check(true, "constructs without dereferencing a half-built widget");

        // The two groups the constructor enables by default must be on, and
        // enabling them must have driven the summary rather than crashing.
        const auto boxes = wizard.findChildren<QCheckBox*>();
        int checked = 0;
        for (QCheckBox* box : boxes)
            if (box->isChecked())
                ++checked;
        // Epoxide + hydroxyl + "decorate both faces".
        check(checked >= 3, "the default group selection was applied");

        exerciseControls(&wizard);
        check(true, "survives every control being toggled");
    }
    {
        // Stage navigation runs refreshSummary() again against a fully built
        // dialog; a second construction confirms no static/global state leaks
        // between instances.
        GrapheneOxideWizard first;
        GrapheneOxideWizard second;
        exerciseControls(&second);
        check(true, "two instances coexist");
    }

    std::printf("Hubbard parameters dialog:\n");
    {
        HubbardParametersDialog dialog(false, {}, {QStringLiteral("Fe"),
                                                   QStringLiteral("O")});
        check(true, "constructs empty");
        exerciseControls(&dialog);
        check(true, "survives control exercise");
    }
    {
        // Pre-populated: appendRow() wires per-row signals that call back into
        // updateState(), which reads the table it is still filling.
        std::vector<calango::core::HubbardU> seeded{{"Fe", "d", 3.5, false},
                                                    {"Ni", "d", 4.6, true}};
        HubbardParametersDialog dialog(true, seeded, {});
        check(dialog.isEnabled(), "reports the enabled state it was given");
        check(dialog.parameters().size() == 2, "round-trips seeded rows");
        exerciseControls(&dialog);
        check(true, "survives control exercise when pre-populated");
    }

    std::printf("Geometry constraints dialog:\n");
    {
        // A null structure is the case a wizard opened with nothing loaded
        // hits, and the per-atom table has to cope with having no rows at all
        // rather than indexing into an absent structure.
        GeometryConstraintsDialog dialog(nullptr, {});
        check(dialog.constraints().empty(), "no structure yields no constraints");
        exerciseControls(&dialog);
        check(true, "survives control exercise with no structure");
    }
    {
        auto structure = std::make_shared<calango::core::Structure>();
        for (int i = 0; i < 6; ++i)
            structure->addAtom({29, {0.0, 0.0, 2.0 * i}});

        // Seeded with one of each rule shape: appendRegionRow() wires per-row
        // signals into updateSummary(), which reads the table it is still
        // filling — the exact hazard this file exists for.
        calango::core::GeometryConstraint fixed;
        fixed.indices = {0, 1};
        calango::core::GeometryConstraint region;
        region.selection = calango::core::GeometryConstraint::Selection::Region;
        region.hasMin = true;
        region.minValue = 4.0;
        region.fix[0] = false;
        region.fix[1] = false;

        GeometryConstraintsDialog dialog(structure, {fixed, region});
        const auto rules = dialog.constraints();
        check(rules.size() == 2, "round-trips one index rule and one region");
        // The two atoms named by the seed must come back as ONE index rule
        // (same mask), not as two — that grouping is what keeps a 400-atom slab
        // from emitting 400 constraint objects.
        int indexRules = 0;
        int constrainedAtoms = 0;
        for (const auto& rule : rules) {
            if (rule.selection
                == calango::core::GeometryConstraint::Selection::Region)
                continue;
            ++indexRules;
            constrainedAtoms += static_cast<int>(rule.indices.size());
        }
        check(indexRules == 1, "atoms sharing a mask collapse into one rule");
        check(constrainedAtoms == 2, "both seeded atoms survive the round trip");

        exerciseControls(&dialog);
        check(true, "survives control exercise when pre-populated");
    }

    std::printf("Optics plot style dialog:\n");
    {
        OpticsPlotStyleDialog dialog{OpticsPlotStyle{}};
        check(true, "constructs from a default style");
        exerciseControls(&dialog);
        // Every control writes through on change; the style must remain
        // self-consistent rather than half-applied.
        const OpticsPlotStyle style = dialog.style();
        check(style.lineWidth > 0.0, "line width stays positive");
        check(style.gridAlpha >= 0.0 && style.gridAlpha <= 1.0,
              "grid alpha stays in range");
        check(style.axisFontSize > 0, "axis font size stays positive");
    }

    std::printf(failures == 0 ? "\nAll dialog construction checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
