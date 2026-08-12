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

#include "core/CalculatorConfig.hpp"
#include "core/Structure.hpp"
#include "gui/FilmTimelineWidget.hpp"
#include "gui/DatabaseImportDialog.hpp"
#include "gui/GeometryConstraintsDialog.hpp"
#include "gui/GrapheneOxideMdmcWizard.hpp"
#include "gui/WorkflowReportDialog.hpp"
#include "gui/GrapheneOxideWizard.hpp"
#include "gui/HubbardParametersDialog.hpp"
#include "gui/OpticsPlotStyleDialog.hpp"
#include "gui/OverlayEditDialog.hpp"
#include "gui/CddWizard.hpp"
#include "gui/XasResultsWindow.hpp"
#include "gui/XasWizard.hpp"
#include "gui/EditVolumetricRenderDialog.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/CutoffConvergenceWizard.hpp"
#include "gui/FermiSurfaceDialog.hpp"
#include "gui/KpointsConvergenceWizard.hpp"
#include "gui/MlwfSourceSelector.hpp"
#include "gui/TopologyDialog.hpp"
#include "gui/MaceTrainerDialog.hpp"
#include "gui/ElectronicBandsWizard.hpp"
#include "gui/DislocationWizard.hpp"
#include "gui/LiquidInterfaceWizard.hpp"
#include "gui/SolidInterfaceWizard.hpp"
#include "gui/StructureEditorDialog.hpp"
#include "gui/MagneticSpaceGroupDialog.hpp"
#include "gui/RandomNoiseViewer.hpp"
#include "gui/RandomNoiseWizard.hpp"
#include "gui/SimulationWizardBase.hpp"
#include "gui/MolecularDynamicsWizard.hpp"
#include "gui/SinglePointWizard.hpp"
#include "gui/NonlinearOpticsWizard.hpp"
#include "gui/OpticsWizard.hpp"
#include "gui/ProcessManagerPanel.hpp"
#include "gui/RamanIrWizard.hpp"
#include "gui/Defect2dWizard.hpp"
#include "gui/TwoDBandsWizard.hpp"
#include "python_bridge/PythonEngine.hpp"
#include "render/StructureRenderer.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QComboBox>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QEventLoop>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
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

/// For checks whose label is built at runtime — a wizard exercised in more
/// than one mode has to say which mode failed, or the output names a
/// condition without naming the case.
void check(bool condition, const std::string& what)
{
    check(condition, what.c_str());
}

/// Toggle every checkbox and nudge every numeric control. Each of these emits
/// a signal, and it is the SLOTS behind them that touch state which may not
/// exist yet — so this is what actually exercises the hazard.
/// A wizard whose settings page refreshes the script preview while it is still
/// being built.
///
/// That is not a contrived shape — it is what any control does that seeds a
/// default during construction (a setChecked(), a setValue(), a row appended to
/// a table), and roughly thirty wizards contain one. The trap is that the
/// review page, and with it the QPlainTextEdit the preview writes into, is
/// built LAST: every settings page runs before it exists.
///
/// This crashed for real. The Electronic Structure wizard seeds one orbital-
/// projection row per element in the structure, each refreshing the preview,
/// and the first one dereferenced a null QPlainTextEdit before the window had
/// finished opening.
class PreviewDuringConstructionWizard : public SimulationWizardBase {
public:
    PreviewDuringConstructionWizard() { buildUi(); }

    /// True if the settings page was built and its refresh survived.
    bool refreshedWhileBuilding() const { return refreshed_; }
    /// refreshPreview() is a protected slot; expose it so the test can check
    /// that the guard skipped the call rather than disabling it for good.
    void refreshPreviewForTest() { refreshPreview(); }

protected:
    QString wizardTitle() const override { return QStringLiteral("Probe"); }
    QString settingsHeader() const override { return QStringLiteral("Probe"); }
    QWidget* buildSettingsPage() override
    {
        auto* page = new QWidget;
        auto* box = new QCheckBox(QStringLiteral("seeded"), page);
        connect(box, &QCheckBox::toggled, this, [this] { refreshPreview(); });
        // The seeding assignment, mid-construction, exactly as a real wizard
        // does it. It emits toggled() straight back into refreshPreview().
        box->setChecked(true);
        refreshed_ = true;
        return page;
    }
    QString generateScript() const override
    {
        // Reached only if the guard lets it through. A subclass mid-
        // construction cannot promise its controls exist, which is the second
        // reason the base class must not call this yet.
        return QStringLiteral("# probe");
    }

private:
    bool refreshed_ = false;
};

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

    // No editable item view may keep the AnyKeyPressed edit trigger.
    //
    // A crash, not a style rule. On macOS a dead key (´ ` ~ ^ — unavoidable on
    // a Portuguese or Spanish layout) reaches an item view as a
    // QInputMethodEvent, and QAbstractItemView::inputMethodEvent answers it
    // with edit(currentIndex(), AnyKeyPressed, event). The setFocus() on the
    // new editor makes the Cocoa input context commit the pending dead key,
    // which re-delivers the event to the VIEW — which edits again. It recursed
    // 5215 times and exhausted the stack before the key could be released.
    // gui::disableTypeToEdit() breaks it; this makes sure nobody forgets.
    //
    // Checked here rather than once at the end so it covers every dialog the
    // test exercises, including the ones added later.
    for (QAbstractItemView* view : dialog->findChildren<QAbstractItemView*>()) {
        if (view->editTriggers() == QAbstractItemView::NoEditTriggers)
            continue;
        if (!view->editTriggers().testFlag(QAbstractItemView::AnyKeyPressed))
            continue;
        std::printf("  FAIL %s in %s still has AnyKeyPressed — a dead key "
                    "will recurse until the stack is gone\n",
                    view->metaObject()->className(),
                    dialog->metaObject()->className());
        ++failures;
    }
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication::setOrganizationName(QStringLiteral("CalangoTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DialogTest"));
    // Sandbox every ~/.calango file the exercised dialogs touch. The org/app
    // names above already isolate QSettings, but SettingsManager mirrors to
    // a JSON file at a fixed path — without this, running the test rewrote
    // the developer's real settings.json. Belt and braces with the ctest
    // ENVIRONMENT property, so a bare ./calango_dialog_test is safe too.
    if (qgetenv("CALANGO_CONFIG_DIR").isEmpty())
        qputenv("CALANGO_CONFIG_DIR",
                (QDir::tempPath() + QStringLiteral("/calango-dialog-test"))
                    .toLocal8Bit());
    QApplication app(argc, argv);

    std::printf("Graphene Oxide wizard:\n");
    {
        // Constructing it is the exact action that crashed.
        GrapheneOxideWizard wizard;
        check(true, "constructs without dereferencing a half-built widget");

        // Stage 2 is driven entirely by ratio sliders, each paired with a spin
        // box that types the same number. The pair must AGREE — a slider that
        // cannot reach the value shown beside it is the bug this checks for,
        // and it is exactly what a naive two-way connection produces once the
        // two controls quantize differently.
        const QStringList ratios{QStringLiteral("basalOxidation"),
                                 QStringLiteral("basalHydrogen"),
                                 QStringLiteral("edgeOxidation"),
                                 QStringLiteral("edgeCarboxyl")};
        bool found = true;
        bool paired = true;
        for (const QString& name : ratios) {
            auto* slider = wizard.findChild<QSlider*>(name + "Slider");
            auto* box = wizard.findChild<QDoubleSpinBox*>(name + "Box");
            if (!slider || !box) {
                found = false;
                continue;
            }
            // Drive from each side and require the other to follow EXACTLY.
            // Both are quantized to thousandths precisely so this can be an
            // equality rather than a tolerance.
            for (int position : {0, slider->maximum() / 3, slider->maximum()}) {
                slider->setValue(position);
                paired = paired
                    && std::abs(box->value() * 1000.0 - position) < 1e-6;
            }
            for (double typed : {0.0, box->maximum() / 4.0, box->maximum()}) {
                box->setValue(typed);
                paired = paired
                    && std::abs(slider->value() / 1000.0 - typed) < 1e-9;
            }
        }
        check(found, "the four ratio controls are present and named");
        check(paired, "every slider and its spin box track each other exactly");
        // The legacy per-group coverage table is gone, not merely hidden: it
        // and the sliders answered the same question two different ways, and
        // only the sliders are read by the build.
        check(wizard.findChildren<QSlider*>().size() == 4,
              "stage 2 has exactly the four ratio sliders");

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
    {
        // What the sliders say must be what the BUILD gets. Driving the wizard
        // through its own navigation is the only way to check that: config()
        // is where the binding lives, and a test that reassembles the Config
        // itself would pass no matter what the dialog sends.
        //
        // The periodic case is the one that regressed. A sheet has no rim, so
        // the edge controls are hidden — but the config still carried nonzero
        // carboxyl and carbonyl WEIGHTS from those hidden sliders, and the
        // builder reads exactly that to decide whether edge chemistry was
        // asked for. Every periodic build came back complaining that the edge
        // groups the user never requested could not be placed.
        using Group = calango::core::GrapheneOxideBuilder::Group;
        // Base structure, functionalization, MDMC opt-in.
        constexpr int kGrapheneOxideStages = 3;
        const auto buildWith = [](int baseIndex, double edgeOxidation = -1.0) {
            GrapheneOxideWizard wizard;
            auto* base = wizard.findChild<QComboBox*>(QStringLiteral("baseCombo"));
            if (base)
                base->setCurrentIndex(baseIndex);
            if (edgeOxidation >= 0.0) {
                if (auto* box = wizard.findChild<QDoubleSpinBox*>(
                        QStringLiteral("edgeOxidationBox")))
                    box->setValue(edgeOxidation);
            }
            // Walk to the last stage and press Build. Driven off the stage
            // COUNT rather than a literal number of presses: the wizard has
            // grown a stage once already, and a hard-coded two left this
            // helper silently returning an empty report instead of building.
            for (int stage = 0; stage < kGrapheneOxideStages; ++stage)
                QMetaObject::invokeMethod(&wizard, "goNext");
            return wizard.report();
        };

        const auto sheet = buildWith(0);
        check(sheet.oxygenAtoms > 0, "periodic sheet: the O/C slider placed oxygen");
        check(QString::fromStdString(sheet.note)
                  .contains(QStringLiteral("EDGE chemistry")) == false,
              "periodic sheet: no complaint about edge groups never requested");
        check(sheet.placedFor(Group::Carboxyl) == 0
                  && sheet.placedFor(Group::Carbonyl) == 0,
              "periodic sheet: no edge groups placed");
        check(sheet.placedFor(Group::Epoxide) > 0
                  && sheet.placedFor(Group::Hydroxyl) > 0,
              "periodic sheet: the H/O slider's 50/50 default gave both basal groups");

        // The rim defaults to zero oxidation, which is CATEGORICAL: strictly
        // hydrogen-terminated, as in the parent hydrocarbon. Basal chemistry
        // must still happen — that is the decoupling.
        const auto flake = buildWith(1);
        check(flake.edgeCarbonCount > 0, "nanoflake: the substrate has a rim");
        check(flake.placedFor(Group::Carboxyl) + flake.placedFor(Group::Carbonyl)
                  == 0,
              "nanoflake: edge oxidation defaults to a strictly hydrogen rim");
        check(flake.hydrogenTerminatedEdges == flake.edgeCarbonCount,
              "nanoflake: every rim carbon keeps its hydrogen at zero edge oxidation");
        check(flake.basalOxygenPlaced > 0,
              "nanoflake: the basal plane is oxidized regardless of the rim");

        // Turning the rim dial up must move edge chemistry and NOTHING else.
        const auto oxidizedRim = buildWith(1, 0.5);
        check(oxidizedRim.placedFor(Group::Carboxyl)
                      + oxidizedRim.placedFor(Group::Carbonyl)
                  > 0,
              "nanoflake: raising edge oxidation puts groups on the rim");
        check(oxidizedRim.hydrogenTerminatedEdges < flake.hydrogenTerminatedEdges,
              "nanoflake: and takes those rim hydrogens away");
        check(oxidizedRim.basalOxygenPlaced == flake.basalOxygenPlaced,
              "nanoflake: while leaving the basal plane exactly as it was");
    }

    std::printf("Graphene Oxide MDMC wizard:\n");
    {
        // A SimulationWizardBase resolves its interpreter through
        // PythonEngine, which asserts rather than lazily constructing. Scoped
        // so the runtime is finalized before the app exits.
        calango::pybridge::PythonEngine python;
        GrapheneOxideMdmcWizard wizard;
        check(true, "constructs");
        // setSubstrate runs BEFORE the pages it writes into may exist in a
        // future edit, and it is called by the host immediately after
        // construction — exactly the ordering that has crashed wizards here.
        wizard.setSubstrate(24, 60, 18, false);
        check(true, "accepts a substrate without dereferencing a missing page");
        // A flake has no cell, so constant pressure must not be selectable.
        // Offering NPT on a molecule is meaningless, not merely wasteful.
        const auto* ensemble = wizard.findChildren<QComboBox*>().isEmpty()
            ? nullptr
            : wizard.findChildren<QComboBox*>().first();
        (void)ensemble;
        exerciseControls(&wizard);
        check(true, "survives every control being exercised");
        wizard.setSubstrate(0, 60, 18, false);
        check(true, "handles a substrate with no groups to move");
    }

    std::printf("Workflow report dialog:\n");
    {
        calango::core::WorkflowReport report;
        report.batchTotal = 2;
        report.startedUtc = QStringLiteral("2026-08-11T10:00:00Z");
        report.finishedUtc = QStringLiteral("2026-08-11T11:00:00Z");
        const auto make = [](const char* title, const char* status, int pass,
                             const char* label, bool withEnergy) {
            calango::core::NodeOutcome outcome;
            outcome.title = QLatin1String(title);
            outcome.status = QLatin1String(status);
            outcome.batchIndex = pass;
            outcome.batchLabel = QLatin1String(label);
            outcome.engine = QStringLiteral("GPAW");
            if (withEnergy) {
                calango::core::ReportMetric energy;
                energy.key = QStringLiteral("final_energy_ev");
                energy.label = QStringLiteral("Final energy");
                energy.unit = QStringLiteral("eV");
                energy.number = -12.3456;
                outcome.metrics.append(energy);
            }
            return outcome;
        };
        report.outcomes = {make("Relax", "done", 0, "Si", true),
                           make("Bands", "done", 0, "Si", false),
                           make("Relax", "failed", 1, "Ge", false),
                           make("Bands", "skipped", 1, "Ge", false)};
        WorkflowReportDialog dialog(report);
        check(true, "constructs from a report alone");
        const auto trees = dialog.findChildren<QTreeWidget*>();
        check(trees.size() == 1, "shows exactly one table");
        if (!trees.isEmpty()) {
            // Two batch groups, each holding its own nodes — the per-structure
            // split is the whole reason this is a tree and not a flat list.
            check(trees.first()->topLevelItemCount() == 2,
                  "one top-level row per structure");
            check(trees.first()->topLevelItem(0)->childCount() == 2,
                  "with that structure's nodes beneath it");
            // A metric column exists only because a metric was extracted.
            check(trees.first()->columnCount() == 3,
                  "columns are Node, Status and the one metric present");
        }
        exerciseControls(&dialog);
        check(true, "survives control exercise");
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

    // The Edit Structure dialog's atom table mixes editable geometry with the
    // extended per-atom arrays the structure arrived with. The contract pinned
    // here: velocities and forces are EDITABLE and write through to the
    // structure's vector fields (they are MD initial conditions and training-
    // data forces as much as results), every other array stays read-only, and
    // an edited cell follows its atom through a sort — the sort permutes the
    // structure, not the view.
    std::printf("Structure editor dialog:\n");
    {
        // Three atoms along z with one computed scalar (charges), a derived
        // magnitude (|forces|), and the two editable vector arrays.
        calango::core::Structure crystal;
        for (int i = 0; i < 3; ++i)
            crystal.addAtom({14, {0.0, 0.0, 2.0 * i}});
        crystal.setCell(calango::core::UnitCell({6, 0, 0}, {0, 6, 0},
                                                {0, 0, 6}, {true, true, true}));
        crystal.setScalarField("charges", {0.1, 0.2, 0.3});
        crystal.setVectorField("forces", {{1, 0, 0}, {2, 0, 0}, {3, 0, 0}});
        crystal.setScalarField("|forces|", {1.0, 2.0, 3.0});
        crystal.setVectorField("velocities",
                               {{0.001, 0, 0}, {0.002, 0, 0}, {0.003, 0, 0}});

        StructureEditorDialog dialog(crystal);
        auto* table = dialog.findChild<QTableWidget*>();
        check(table != nullptr, "exposes the atom table");
        // 4 fixed columns + charges + |forces| + forces xyz + velocities xyz.
        // (Scalars precede vectors, each map-ordered: "charges" < "|forces|",
        // "forces" < "velocities".)
        check(table && table->columnCount() == 12,
              "every extended array appears as a property column");
        if (table) {
            const auto header = [table](int column) {
                const QTableWidgetItem* item =
                    table->horizontalHeaderItem(column);
                return item ? item->text() : QString();
            };
            const auto editable = [table](int column) {
                const QTableWidgetItem* item = table->item(0, column);
                return item != nullptr
                    && item->flags().testFlag(Qt::ItemIsEditable);
            };
            check(header(4) == QStringLiteral("charges")
                      && header(5) == QStringLiteral("|forces|")
                      && header(6) == QStringLiteral("forces x")
                      && header(9) == QStringLiteral("velocities x"),
                  "property columns arrive in field order");
            check(!editable(4) && !editable(5),
                  "computed arrays (charges, |forces|) stay read-only");
            check(editable(6) && editable(9),
                  "forces and velocities are editable");

            // An edit commits to the vector field at once — the same
            // write-through a coordinate edit gets — and the derived |forces|
            // magnitude follows rather than going stale.
            if (QTableWidgetItem* cell = table->item(0, 6))
                cell->setText(QStringLiteral("4.0"));
            check(std::abs(dialog.result()->vectorFields().at("forces")[0].x
                           - 4.0) < 1e-9,
                  "a force edit lands in vectorFields()[\"forces\"]");
            check(std::abs(dialog.result()->scalarFields().at("|forces|")[0]
                           - 4.0) < 1e-9,
                  "the derived |forces| magnitude follows the edit");

            // Sort by z descending: the edited atom (z = 0) moves to the last
            // row, and the edited force has to move with it.
            QComboBox* sortKey = nullptr;
            for (QComboBox* combo : dialog.findChildren<QComboBox*>())
                if (combo->itemText(0) == QStringLiteral("Element"))
                    sortKey = combo;
            QCheckBox* descending = nullptr;
            for (QCheckBox* box : dialog.findChildren<QCheckBox*>())
                if (box->text() == QStringLiteral("descending"))
                    descending = box;
            QPushButton* sortButton = nullptr;
            for (QPushButton* button : dialog.findChildren<QPushButton*>())
                if (button->text() == QStringLiteral("Sort"))
                    sortButton = button;
            check(sortKey && descending && sortButton,
                  "sort controls are present");
            if (sortKey && descending && sortButton) {
                sortKey->setCurrentIndex(3); // z
                descending->setChecked(true);
                sortButton->click();
                const auto sorted = dialog.result();
                check(std::abs(sorted->atoms()[2].position.z) < 1e-9,
                      "descending z sort reversed the rows");
                check(std::abs(sorted->vectorFields().at("forces")[2].x - 4.0)
                          < 1e-9,
                      "the edited force followed its atom through the sort");
                check(table->item(2, 6)
                          && std::abs(table->item(2, 6)->text().toDouble()
                                      - 4.0) < 1e-6,
                      "and the table shows it on the atom's new row");
            }
        }
    }
    {
        // A structure with no extended arrays: the columns are simply absent —
        // editing never invents a velocities or forces array.
        calango::core::Structure bare;
        bare.addAtom({6, {0.0, 0.0, 0.0}});
        StructureEditorDialog dialog(bare);
        auto* table = dialog.findChild<QTableWidget*>();
        check(table && table->columnCount() == 4,
              "no extended arrays means no property columns");
        exerciseControls(&dialog);
        check(true, "survives every control being toggled");
    }

    // The simulation wizards build a dozen per-engine group boxes and connect
    // the engine combo to a slot that shows and hides them — while those groups
    // are still being constructed. That is precisely the hazard this file
    // exists for, and it is re-armed every time an engine is added.
    std::printf("Simulation wizard engine switching:\n");
    {
        // The wizards resolve their interpreter through PythonEngine, which
        // asserts rather than lazily constructing — so the embedded runtime has
        // to exist before one is built. Scoped to this block so the interpreter
        // is finalized before the app exits.
        calango::pybridge::PythonEngine python;
        SinglePointWizard wizard;
        check(true, "constructs");
        auto* engine = wizard.findChild<QComboBox*>();
        check(engine != nullptr, "has an engine combo");
        if (engine) {
            // Walking every engine drives updateCalculatorEnabled() against
            // each one's group set. A group read before it is built crashes
            // here rather than on the user's first click.
            const int count = engine->count();
            check(count > 0, "offers at least one engine");

            // -- Engine ORDER, which is engine DEFAULT -----------------------
            // A combo opens on index 0, so whatever leads this list is what an
            // unmodified run uses. Single-Point is the wizard that matters
            // here: it is the only one that offers the built-in engine at all,
            // and that engine used to lead — meaning the out-of-the-box run of
            // the most-used module was the one that cannot return a number.
            check(engine->currentIndex() == 0,
                  "the engine combo opens on its first entry");
            check(engine->findData(static_cast<int>(
                      calango::core::CalculatorKind::Gpaw)) == 0,
                  "GPAW is first, and therefore the default engine");
            check(engine->itemData(engine->currentIndex()).toInt()
                      == static_cast<int>(calango::core::CalculatorKind::Gpaw),
                  "so an untouched wizard runs GPAW");

            const int native = engine->findData(
                static_cast<int>(calango::core::CalculatorKind::CalangoDft));
            check(native > 0, "the built-in engine is offered, but not first");
            if (native > 0) {
                // Last, past the classical potentials. Not merely "not first":
                // it produces no energy yet, so anywhere a user could land on
                // it by scrolling past the DFT codes is too high.
                check(native == count - 1,
                      "the built-in engine is the LAST entry in the list");
                check(engine->itemText(native)
                          == QStringLiteral("Calango Native DFT (experimental)"),
                      "and its label carries the experimental warning");
            }

            // Walking every engine drives updateCalculatorEnabled() against
            // each one's group set. A group read before it is built crashes
            // here rather than on the user's first click.
            for (int i = 0; i < count; ++i)
                engine->setCurrentIndex(i);
            check(true, "switching through every engine does not crash");
            engine->setCurrentIndex(0);

            // LAMMPS specifically: its settings group must appear when it is
            // selected, since the pair style IS the physics and a hidden group
            // would silently ship the default Lennard-Jones coefficients.
            const int lammps = engine->findData(
                static_cast<int>(calango::core::CalculatorKind::Lammps));
            check(lammps >= 0, "LAMMPS is offered as an engine");
            if (lammps >= 0) {
                engine->setCurrentIndex(lammps);
                const auto boxes = wizard.findChildren<QGroupBox*>();
                const auto named = std::find_if(
                    boxes.begin(), boxes.end(), [](const QGroupBox* box) {
                        return box->title().contains(QStringLiteral("LAMMPS"));
                    });
                check(named != boxes.end(), "a LAMMPS settings group exists");
                if (named != boxes.end()) {
                    check((*named)->isVisibleTo(&wizard),
                          "and is shown when LAMMPS is the selected engine");
                    engine->setCurrentIndex(lammps == 0 ? 1 : 0);
                    check(!(*named)->isVisibleTo(&wizard),
                          "and hidden again for another engine");
                }
            }
        }
    }

    // The Random Noise wizard is the only four-stage flow built on the shared
    // base (settings, calculator, second settings, review), and it generates
    // its ensemble from a button on stage 1 rather than on OK — so both the
    // stage assembly and the generate-then-run path are worth a construction
    // check.
    std::printf("Random Noise wizard:\n");
    {
        calango::pybridge::PythonEngine python;
        auto reference = std::make_shared<calango::core::Structure>();
        reference->setCell(calango::core::UnitCell({4, 0, 0}, {0, 4, 0},
                                                   {0, 0, 4}));
        for (int i = 0; i < 4; ++i) {
            calango::core::Atom atom;
            atom.atomicNumber = 14;
            atom.position = {i * 1.0, 0.5 * i, 0.25 * i};
            reference->addAtom(atom);
        }

        RandomNoiseWizard wizard(reference);
        check(true, "constructs");

        int generated = 0;
        QObject::connect(
            &wizard, &RandomNoiseWizard::structuresGenerated,
            [&generated](
                const std::vector<std::shared_ptr<calango::core::Structure>>& f) {
                generated = static_cast<int>(f.size());
            });

        // Find the generate button by text; pressing it must publish an
        // ensemble whose first frame is the untouched reference.
        const auto buttons = wizard.findChildren<QPushButton*>();
        const auto generate = std::find_if(
            buttons.begin(), buttons.end(), [](const QPushButton* button) {
                return button->text().contains(QStringLiteral("Generate"));
            });
        check(generate != buttons.end(), "offers a Generate structures button");
        if (generate != buttons.end()) {
            (*generate)->click();
            check(generated > 1, "generating publishes the ensemble");
            check(static_cast<int>(wizard.frames().size()) == generated,
                  "and keeps it");
            if (!wizard.frames().empty()) {
                const auto& first = wizard.frames().front()->atoms();
                const auto& source = reference->atoms();
                bool untouched = first.size() == source.size();
                for (std::size_t i = 0; untouched && i < first.size(); ++i)
                    untouched = (first[i].position - source[i].position).norm()
                        < 1e-12;
                check(untouched,
                      "frame 0 is the unperturbed reference, so the spread has "
                      "something to be measured against");
            }
            // A later frame must actually have moved, or the "ensemble" is a
            // stack of identical structures and every statistic is zero.
            if (wizard.frames().size() > 1) {
                const auto& moved = wizard.frames().back()->atoms();
                const auto& source = reference->atoms();
                bool differs = false;
                for (std::size_t i = 0; i < moved.size(); ++i)
                    differs = differs
                        || (moved[i].position - source[i].position).norm() > 1e-9;
                check(differs, "and the other frames are actually displaced");
            }
        }
        check(!wizard.script().isEmpty(),
              "the review stage has a script to show");
    }

    // The Cutoff Convergence wizard restricts the engine combo to GPAW and
    // generates a sweep script whose loop variable replaces the calculator
    // page's single cutoff — both worth pinning at construction, since either
    // regressing would produce a sweep that quietly varies nothing.
    std::printf("Cutoff Convergence wizard:\n");
    {
        calango::pybridge::PythonEngine python;
        CutoffConvergenceWizard wizard;
        check(true, "constructs");
        auto* engine = wizard.findChild<QComboBox*>();
        check(engine != nullptr && engine->count() == 2
                  && engine->findData(static_cast<int>(
                         calango::core::CalculatorKind::Gpaw)) == 0
                  && engine->findData(static_cast<int>(
                         calango::core::CalculatorKind::Vasp)) >= 0,
              "offers GPAW (default) and VASP");
        // The preview is generated on entering the review stage or on a sweep
        // edit — drive the latter, as a user adjusting the stride would.
        const auto spins = wizard.findChildren<QDoubleSpinBox*>();
        for (QDoubleSpinBox* spin : spins) {
            if (spin->suffix().contains(QStringLiteral("eV"))) {
                spin->setValue(spin->value() + spin->singleStep());
                break;
            }
        }
        const QString script = wizard.script();
        check(script.contains(QStringLiteral("mode=PW(ecut)")),
              "the script sweeps the cutoff, not a fixed PW(...)");
        check(script.contains(QStringLiteral("cutoff_convergence.json")),
              "and writes the results file the viewer reads");
        check(script.contains(QStringLiteral("CUTOFFS = [")),
              "with an explicit cutoff list");
        // The three convergence metrics the viewer plots: ΔE/atom, the
        // atom-wise force error, and the band-energy MAD.
        check(script.contains(QStringLiteral("delta_energy_per_atom_eV"))
                  && script.contains(QStringLiteral("force_error_eV_per_A"))
                  && script.contains(QStringLiteral("eigenvalue_mad_eV")),
              "computes all three convergence metrics vs the reference");
        // The sweep owns the cutoff, so the calculator page must not offer a
        // second, single-value cutoff field the script would ignore. The
        // page is stage 2 — advance to it, or every widget on it reports
        // invisible regardless of the row state and the check is vacuous.
        wizard.show(); // isVisibleTo() needs the page realized
        for (QPushButton* button : wizard.findChildren<QPushButton*>())
            if (button->text().contains(QStringLiteral("Next"))) {
                button->click();
                break;
            }
        bool cutoffRowShown = false;
        for (const QDoubleSpinBox* spin :
             wizard.findChildren<QDoubleSpinBox*>())
            cutoffRowShown = cutoffRowShown
                || (spin->suffix().contains(QStringLiteral("eV"))
                    && spin->maximum() == 2000.0 && spin->isVisibleTo(&wizard));
        check(!cutoffRowShown,
              "the calculator page hides the single-cutoff row");
        // Selecting VASP swaps the sweep script onto ASE's Vasp calculator
        // with ENCUT as the loop variable.
        if (engine) {
            engine->setCurrentIndex(engine->findData(
                static_cast<int>(calango::core::CalculatorKind::Vasp)));
            const QString vaspScript = wizard.script();
            check(vaspScript.contains(
                      QStringLiteral("atoms.calc.set(encut=float(ecut)"))
                      && vaspScript.contains(QStringLiteral("istart=0")),
                  "VASP sweeps ENCUT with fresh restarts per point");
        }
    }

    // The K-points Convergence wizard is the cutoff sweep's sibling: GPAW
    // only, mesh list generated from min/max/stride and the axis toggles,
    // k-grid row hidden on the calculator page for the same reason the
    // cutoff sweep hides the cutoff row.
    std::printf("K-points Convergence wizard:\n");
    {
        calango::pybridge::PythonEngine python;
        KpointsConvergenceWizard wizard;
        check(true, "constructs");
        auto* engine = wizard.findChild<QComboBox*>();
        check(engine != nullptr && engine->count() == 2
                  && engine->findData(static_cast<int>(
                         calango::core::CalculatorKind::Gpaw)) == 0
                  && engine->findData(static_cast<int>(
                         calango::core::CalculatorKind::Vasp)) >= 0,
              "offers GPAW (default) and VASP");
        // Drive a sweep control so the preview regenerates (see above).
        const auto spins = wizard.findChildren<QSpinBox*>();
        check(!spins.isEmpty(), "has sweep spin boxes");
        if (!spins.isEmpty())
            spins.first()->setValue(spins.first()->value() + 1);
        const QString script = wizard.script();
        check(script.contains(QStringLiteral("kpts=tuple(kpts)"))
                  || script.contains(
                      QStringLiteral("kpts={\"size\": tuple(kpts)")),
              "the script sweeps the mesh, not a fixed kpts=(…)");
        check(script.contains(QStringLiteral("kpoints_convergence.json")),
              "and writes the results file the viewer reads");
        check(script.contains(QStringLiteral("MESHES = [")),
              "with an explicit mesh list");
        check(script.contains(QStringLiteral("delta_energy_per_atom_eV"))
                  && script.contains(QStringLiteral("force_error_eV_per_A"))
                  && script.contains(QStringLiteral("eigenvalue_mad_eV")),
              "computes all three convergence metrics vs the reference");
        // The sweep stage's Γ toggle must reach the generated calculator.
        // The base class builds its own (hidden) "Gamma-centered Grid" box
        // with the same label, so check every match — the sweep stage's one
        // is what runConfig() reads.
        int gammaBoxes = 0;
        for (QCheckBox* box : wizard.findChildren<QCheckBox*>())
            if (box->text().contains(QStringLiteral("Gamma"))) {
                box->setChecked(true);
                ++gammaBoxes;
            }
        check(gammaBoxes > 0, "offers Γ-centering on the sweep stage");
        if (gammaBoxes > 0)
            check(wizard.script().contains(
                      QStringLiteral("kpts={\"size\": tuple(kpts), "
                                     "\"gamma\": True}")),
                  "and checking it emits a Γ-centered mesh");
        // Selecting VASP swaps the sweep script onto ASE's Vasp calculator
        // with the mesh (and the Γ toggle) as KPOINTS keywords.
        if (engine) {
            engine->setCurrentIndex(engine->findData(
                static_cast<int>(calango::core::CalculatorKind::Vasp)));
            const QString vaspScript = wizard.script();
            check(vaspScript.contains(
                      QStringLiteral("atoms.calc.set(kpts=tuple(kpts)"))
                      && vaspScript.contains(QStringLiteral("gamma=True")),
                  "VASP sweeps KPOINTS, honoring the Γ toggle");
        }
    }

    // Element-aware suggested defaults: a calculator_parameters.json in the
    // (sandboxed) config dir must repopulate the shared cutoff / k-grid
    // spins for the elements the host announces, and an element without an
    // entry must leave the hardcoded defaults alone.
    std::printf("Calculator parameter suggestions:\n");
    {
        calango::pybridge::PythonEngine python;
        const QString configDir =
            qEnvironmentVariable("CALANGO_CONFIG_DIR");
        QDir().mkpath(configDir);
        const QString parametersPath =
            configDir + QStringLiteral("/calculator_parameters.json");
        {
            QFile file(parametersPath);
            file.open(QIODevice::WriteOnly | QIODevice::Truncate);
            file.write("{\"GPAW\": {\"elements\": {\"Fe\": "
                       "{\"pw\": 750, \"kpts\": [11, 9, 7]}}}}");
        }

        SinglePointWizard wizard;
        // Suggestions resolve for the SELECTED engine; the file only knows
        // GPAW, so pick it the way a user would.
        if (auto* engine = wizard.findChild<QComboBox*>()) {
            const int gpaw = engine->findData(
                static_cast<int>(calango::core::CalculatorKind::Gpaw));
            if (gpaw >= 0)
                engine->setCurrentIndex(gpaw);
        }
        wizard.setStructureElements({QStringLiteral("Fe")});
        QDoubleSpinBox* cutoff = nullptr;
        for (QDoubleSpinBox* spin : wizard.findChildren<QDoubleSpinBox*>())
            if (spin->maximum() == 2000.0) {
                cutoff = spin;
                break;
            }
        check(cutoff != nullptr && cutoff->value() == 750.0,
              "Fe pulls its suggested plane-wave cutoff");
        bool k11 = false;
        bool k9 = false;
        for (const QSpinBox* spin : wizard.findChildren<QSpinBox*>()) {
            k11 = k11 || spin->value() == 11;
            k9 = k9 || spin->value() == 9;
        }
        check(k11 && k9, "and its suggested k-grid");

        SinglePointWizard fallback;
        fallback.setStructureElements({QStringLiteral("H")});
        QDoubleSpinBox* fallbackCutoff = nullptr;
        for (QDoubleSpinBox* spin :
             fallback.findChildren<QDoubleSpinBox*>())
            if (spin->maximum() == 2000.0) {
                fallbackCutoff = spin;
                break;
            }
        check(fallbackCutoff != nullptr && fallbackCutoff->value() == 500.0,
              "an element without an entry keeps the hardcoded default");

        QFile::remove(parametersPath); // leave no state for later blocks
    }

    // The plane-wave cutoff is only a parameter of the plane-wave basis. In FD
    // the basis is the real-space grid and in LCAO the atomic orbital set, so
    // the row has to go — and the check is worth automating because it was
    // being set in two places, with the later one silently undoing the first.
    std::printf("GPAW cutoff visibility:\n");
    {
        calango::pybridge::PythonEngine python;
        SinglePointWizard wizard;
        wizard.show(); // isVisibleTo() needs the page realized

        // The mode/basis group only shows for a DFT engine, so the engine has
        // to be GPAW before any of this means anything.
        auto* engine = wizard.findChild<QComboBox*>();
        const int gpaw = engine
            ? engine->findData(static_cast<int>(calango::core::CalculatorKind::Gpaw))
            : -1;
        check(gpaw >= 0, "GPAW is offered as an engine");
        if (gpaw >= 0)
            engine->setCurrentIndex(gpaw);

        // Find the cutoff spin box by its suffix, and the GPAW mode combo by
        // its entries.
        const auto spins = wizard.findChildren<QDoubleSpinBox*>();
        const auto cutoff = std::find_if(
            spins.begin(), spins.end(), [](const QDoubleSpinBox* spin) {
                return spin->suffix().contains(QStringLiteral("eV"))
                    && spin->maximum() >= 2000.0;
            });
        const auto combos = wizard.findChildren<QComboBox*>();
        // The combo order is core::GpawMode: FD, PW, LCAO.
        const auto modeCombo = std::find_if(
            combos.begin(), combos.end(), [](const QComboBox* combo) {
                return combo->count() == 3
                    && combo->itemText(0).startsWith(QStringLiteral("FD"))
                    && combo->itemText(2).startsWith(QStringLiteral("LCAO"));
            });
        check(cutoff != spins.end(), "the cutoff spin box is present");
        check(modeCombo != combos.end(), "and the GPAW mode combo");
        if (cutoff != spins.end() && modeCombo != combos.end()) {
            const int fd = static_cast<int>(calango::core::GpawMode::FiniteDifference);
            const int pw = static_cast<int>(calango::core::GpawMode::PlaneWave);
            const int lcao = static_cast<int>(calango::core::GpawMode::Lcao);
            (*modeCombo)->setCurrentIndex(pw);
            check((*cutoff)->isVisibleTo(&wizard),
                  "shown in plane-wave mode, where it means something");
            (*modeCombo)->setCurrentIndex(fd);
            check(!(*cutoff)->isVisibleTo(&wizard),
                  "hidden in FD, where the basis is the real-space grid");
            (*modeCombo)->setCurrentIndex(lcao);
            check(!(*cutoff)->isVisibleTo(&wizard),
                  "and in LCAO, where it is the orbital set");
            (*modeCombo)->setCurrentIndex(pw);
            check((*cutoff)->isVisibleTo(&wizard), "and comes back");
        }
        wizard.hide();
    }

    // The eigensolver combo carries its enum as itemData rather than as the
    // row number. Reordering the display — which this change does — would
    // otherwise select a different solver than the one named, silently.
    std::printf("GPAW eigensolver combo:\n");
    {
        calango::pybridge::PythonEngine python;
        SinglePointWizard wizard;
        auto* engine = wizard.findChild<QComboBox*>();
        if (engine) {
            const int gpaw = engine->findData(
                static_cast<int>(calango::core::CalculatorKind::Gpaw));
            if (gpaw >= 0)
                engine->setCurrentIndex(gpaw);
        }
        const auto combos = wizard.findChildren<QComboBox*>();
        const auto solver = std::find_if(
            combos.begin(), combos.end(), [](const QComboBox* combo) {
                return combo->count() == 4
                    && combo->itemText(0) == QStringLiteral("Davidson");
            });
        check(solver != combos.end(), "the eigensolver combo is present");
        if (solver != combos.end()) {
            check((*solver)->itemText(1) == QStringLiteral("RMM-DIIS")
                      && (*solver)->itemText(2) == QStringLiteral("CG")
                      && (*solver)->itemText(3) == QStringLiteral("Direct"),
                  "capitalized, in the listed order");
            // Row 2 is CG, whose enum value is 1 — the case a row-number cast
            // would get wrong.
            check((*solver)->itemData(2).toInt()
                      == static_cast<int>(
                          calango::core::GpawEigensolver::ConjugateGradient),
                  "and each row carries its own enum value, not its index");
            (*solver)->setCurrentIndex(2);
            check(wizard.script().contains(QStringLiteral("eigensolver=\"cg\"")),
                  "so selecting CG generates cg, not rmm-diis");
        }
    }

    // The CDD wizard's two columns ARE its input: the generated script names
    // subsystem B by atom index, and an index taken from a row number rather
    // than from the item's own data would silently difference the wrong atoms.
    // Nothing about that fails loudly, so it is checked here.
    std::printf("Charge Density Difference wizard:\n");
    {
        calango::pybridge::PythonEngine python;
        CddWizard wizard;
        check(true, "constructs with no baselines");
        check(wizard.baselineDirectory().isEmpty(),
              "and reports no baseline directory");

        // Both columns exist and start empty (no structure to partition).
        const auto lists = wizard.findChildren<QListWidget*>();
        check(lists.size() >= 2, "offers two subsystem columns");

        // With no baseline the script must still be generatable rather than
        // crashing — the review stage renders it on arrival.
        check(!wizard.script().isEmpty(), "generates a script");
        const QString script = wizard.script();
        check(script.contains(QStringLiteral("subsystem_b = []")),
              "an unpartitioned system yields an empty B");
        check(script.contains(QStringLiteral("rho_ab - rho_a - rho_b")),
              "and the difference it is named for");
        check(script.contains(QStringLiteral("get_all_electron_density")),
              "defaulting to the all-electron density");

        // The density-type radio must actually reach the generator.
        const auto radios = wizard.findChildren<QRadioButton*>();
        const auto pseudo = std::find_if(
            radios.begin(), radios.end(), [](const QRadioButton* button) {
                return button->text().contains(QStringLiteral("Pseudo"));
            });
        check(pseudo != radios.end(), "offers a pseudodensity option");
        if (pseudo != radios.end()) {
            (*pseudo)->setChecked(true);
            check(wizard.script().contains(QStringLiteral("get_pseudo_density")),
                  "and selecting it switches the generated density");
        }
    }
    {
        // With a structure attached, the partition is the thing that has to be
        // right. Moving rows reorders the lists, so an index taken from the
        // row number instead of the item's data would difference the wrong
        // atoms — and nothing about that fails loudly.
        calango::pybridge::PythonEngine python;
        auto structure = std::make_shared<calango::core::Structure>();
        structure->setCell(calango::core::UnitCell({8, 0, 0}, {0, 8, 0},
                                                   {0, 0, 8}));
        for (const int z : {6, 8, 1, 1, 7}) { // C, O, H, H, N
            calango::core::Atom atom;
            atom.atomicNumber = z;
            atom.position = {0.5 * z, 0.0, 0.0};
            structure->addAtom(atom);
        }

        CddWizard wizard;
        CddWizard::Baseline baseline;
        baseline.label = QStringLiteral("#1 — Single-Point [GPAW]");
        baseline.directory = QStringLiteral("/tmp/does-not-need-to-exist");
        baseline.structure = structure;
        wizard.setDensityBaselines({baseline});

        check(wizard.baselineDirectory() == baseline.directory,
              "selecting a baseline reports its directory");

        const auto lists = wizard.findChildren<QListWidget*>();
        check(lists.size() >= 2, "has both columns");
        if (lists.size() >= 2) {
            QListWidget* a = lists.at(0);
            QListWidget* b = lists.at(1);
            check(a->count() == 5 && b->count() == 0,
                  "every atom starts in subsystem A");

            // Move atoms 2 and 4 (0-based indices 1 and 3) across, out of
            // order, so a row-number bug cannot coincide with the right answer.
            a->item(3)->setSelected(true);
            a->item(1)->setSelected(true);
            const auto buttons = wizard.findChildren<QPushButton*>();
            const auto toB = std::find_if(
                buttons.begin(), buttons.end(), [](const QPushButton* button) {
                    return button->text() == QString::fromUtf8("→");
                });
            check(toB != buttons.end(), "offers a move-to-B button");
            if (toB != buttons.end()) {
                (*toB)->click();
                check(a->count() == 3 && b->count() == 2,
                      "the selection moved across");
                const QString script = wizard.script();
                check(script.contains(QStringLiteral("subsystem_b = [1, 3]")),
                      "and the script names the atoms' own indices, in order");
                check(script.contains(
                          QStringLiteral("subsystem_a = [i for i in "
                                         "range(len(atoms)) if i not in")),
                      "with A derived as the complement, so the split is "
                      "exhaustive");
            }

            // And back again: moving one atom home must remove exactly it.
            b->item(0)->setSelected(true);
            const auto buttons2 = wizard.findChildren<QPushButton*>();
            const auto toA = std::find_if(
                buttons2.begin(), buttons2.end(), [](const QPushButton* button) {
                    return button->text() == QString::fromUtf8("←");
                });
            if (toA != buttons2.end()) {
                (*toA)->click();
                check(wizard.script().contains(
                          QStringLiteral("subsystem_b = [3]")),
                      "moving one back leaves the other behind");
            }
        }
    }

    // The XAS wizard's absorbing-atom selector is filled from the structure
    // and must carry ATOM INDICES, not row numbers: the generated script keys
    // the core-hole setup on that index, and getting it wrong puts the hole on
    // the wrong atom — which produces a perfectly plausible spectrum of
    // something else.
    std::printf("XAS wizard:\n");
    {
        calango::pybridge::PythonEngine python;
        auto structure = std::make_shared<calango::core::Structure>();
        structure->setCell(calango::core::UnitCell({10, 0, 0}, {0, 10, 0},
                                                   {0, 0, 10}));
        // H, O, H, O — so the second O is at index 3, not at row 1.
        for (const int z : {1, 8, 1, 8}) {
            calango::core::Atom atom;
            atom.atomicNumber = z;
            atom.position = {0.7 * z, 0.0, 0.0};
            structure->addAtom(atom);
        }

        XasWizard wizard(structure);
        check(true, "constructs");

        const auto combos = wizard.findChildren<QComboBox*>();
        // The element list is sorted, so it opens on H; the interesting case
        // is O, whose atoms are at indices 1 and 3 — deliberately NOT rows
        // 0 and 1.
        const auto elementCombo = std::find_if(
            combos.begin(), combos.end(), [](const QComboBox* combo) {
                return combo->count() == 2
                    && combo->itemText(0) == QStringLiteral("H")
                    && combo->itemText(1) == QStringLiteral("O");
            });
        check(elementCombo != combos.end(), "offers the structure's elements");
        if (elementCombo != combos.end())
            (*elementCombo)->setCurrentIndex(1); // O

        const auto atomCombo = std::find_if(
            combos.begin(), combos.end(), [](const QComboBox* combo) {
                return combo->count() > 0
                    && combo->itemText(0).startsWith(QStringLiteral("#"));
            });
        check(atomCombo != combos.end(), "offers an absorbing-atom selector");
        if (atomCombo != combos.end()) {
            check((*atomCombo)->count() == 2,
                  "listing only the atoms of the chosen element");
            check((*atomCombo)->itemData(0).toInt() == 1
                      && (*atomCombo)->itemData(1).toInt() == 3,
                  "with their real indices, not their row numbers");
            (*atomCombo)->setCurrentIndex(1);
            check(wizard.script().contains(QStringLiteral("absorbing_atom = 3")),
                  "and the script names the selected atom's index");
        }
        check(wizard.script().contains(QStringLiteral("from gpaw.xas import XAS")),
              "the generated script is an XAS run");
    }

    std::printf("XAS results window:\n");
    {
        // A spectrum on a RELATIVE energy scale must say so: plotted as though
        // it were absolute it is off by hundreds of eV, and nothing about the
        // curve reveals that.
        const QString path = QDir::temp().filePath(QStringLiteral("calango_xas.json"));
        QFile file(path);
        check(file.open(QIODevice::WriteOnly), "a results file can be staged");
        file.write(R"({"element":"O","absorbing_atom":0,"setup":"hch1s",
                       "core_hole":0.5,"dks_energy_eV":0.0,"fwhm_eV":0.5,
                       "energy_eV":[0,1,2],"isotropic":[0,1,0],
                       "polarization_x":[0,1,0],"polarization_y":[0,0.5,0],
                       "polarization_z":[0,0.2,0],
                       "stick_energy_eV":[1.0],"stick_isotropic":[1.0]})");
        file.close();

        XasResultsWindow window;
        check(window.loadResults(path), "and loaded");
        const auto labels = window.findChildren<QLabel*>();
        const bool warns = std::any_of(
            labels.begin(), labels.end(), [](const QLabel* label) {
                return label->text().contains(QStringLiteral("RELATIVE"));
            });
        check(warns, "a relative energy scale is called out, not implied");
        check(!window.loadResults(QStringLiteral("/nonexistent/xas.json")),
              "a missing file is reported rather than shown empty");
    }

    // The overlay editor swaps its property page on every type change, and
    // three pages' worth of widgets are all constructed up front while the
    // change handler is already connected — the same construction-order hazard
    // this file exists for, re-armed once per overlay type.
    std::printf("Overlay edit dialog:\n");
    {
        Overlay overlay;
        OverlayEditDialog dialog(overlay, /*structureHasCell=*/true);
        check(true, "constructs");
        auto* kindCombo = dialog.findChild<QComboBox*>();
        check(kindCombo != nullptr, "has a type combo");
        if (kindCombo) {
            // Walk every type: each swaps the stacked page and re-runs the
            // per-kind row visibility against widgets built at different times.
            for (int i = 0; i < kindCombo->count(); ++i)
                kindCombo->setCurrentIndex(i);
            check(true, "switching through every overlay type does not crash");
            check(kindCombo->count() == 9, "offers all nine overlay types");
        }
        exerciseControls(&dialog);
        check(true, "survives every control being exercised");
        // The working copy must still be coherent after all that.
        check(dialog.overlay().resolution >= 4,
              "the edited overlay keeps a usable tessellation");
    }
    {
        // A structure with no cell: the lattice-plane page has to say so
        // rather than silently drawing nothing.
        Overlay plane;
        plane.kind = Overlay::Kind::LatticePlane;
        OverlayEditDialog dialog(plane, /*structureHasCell=*/false);
        check(dialog.overlay().kind == Overlay::Kind::LatticePlane,
              "opens on the kind it was given");
        exerciseControls(&dialog);
        check(true, "survives control exercise with no cell");
    }

    // The volumetric render dialog dropped its third mode and folded the
    // potential map into the isosurface page. Both halves of that are worth
    // pinning: the mode list is now what the panel's enum can represent, and
    // the folded controls have to actually reach the style.
    std::printf("Volumetric render dialog:\n");
    {
        VolumetricStyle style;
        EditVolumetricRenderDialog dialog(style,
                                          VolumetricRenderMode::Isosurface,
                                          -1.0, 1.0);
        check(true, "constructs");

        const auto combos = dialog.findChildren<QComboBox*>();
        const auto modeCombo = std::find_if(
            combos.begin(), combos.end(), [](const QComboBox* combo) {
                return combo->count() > 0
                    && combo->itemText(0).contains(QStringLiteral("Isosurface"));
            });
        check(modeCombo != combos.end(), "has a render-mode combo");
        if (modeCombo != combos.end()) {
            // The combo index is cast straight to VolumetricRenderMode, so
            // the count must equal the enumerator count exactly: one entry
            // past the end casts to an invalid enumerator the panel then
            // switches on, and one short leaves a mode unreachable.
            check((*modeCombo)->count() == 3,
                  "offers exactly three modes, matching the enum");
            check((*modeCombo)->itemText(1).contains(QStringLiteral("Slice")),
                  "Isosurfaces, Color Slice");
            check((*modeCombo)->itemText(2).contains(QStringLiteral("Volume")),
                  "and Direct Volume");
            // Every index must round-trip to the enumerator it names — the
            // failure this guards is a reordering that silently maps one mode
            // onto another's renderer.
            (*modeCombo)->setCurrentIndex(1);
            check(dialog.mode() == VolumetricRenderMode::ColorSlice,
                  "index 1 reports ColorSlice");
            (*modeCombo)->setCurrentIndex(2);
            check(dialog.mode() == VolumetricRenderMode::DirectVolume,
                  "index 2 reports DirectVolume");
            (*modeCombo)->setCurrentIndex(0);
            check(dialog.mode() == VolumetricRenderMode::Isosurface,
                  "index 0 reports Isosurface");
        }

        // Potential-map colouring is a checkable group on the Isosurfaces
        // page now, not a mode.
        const auto boxes = dialog.findChildren<QGroupBox*>();
        const auto potential = std::find_if(
            boxes.begin(), boxes.end(), [](const QGroupBox* box) {
                return box->title().contains(QStringLiteral("Potential Map"));
            });
        check(potential != boxes.end(),
              "the potential map is a group on the isosurface page");
        if (potential != boxes.end()) {
            check((*potential)->isCheckable(), "and it is switchable");
            check(!dialog.style().potentialColoring, "off by default");
            (*potential)->setChecked(true);
            check(dialog.style().potentialColoring,
                  "turning it on reaches the style");
        }

        // Slice extent: the values are what the renderer clips against, so a
        // combo whose data does not carry them would silently draw one cell.
        const auto extent = std::find_if(
            combos.begin(), combos.end(), [](const QComboBox* combo) {
                return combo->count() > 0
                    && combo->itemText(0).contains(QStringLiteral("unit cell"));
            });
        check(extent != combos.end(), "the slice offers an extent selector");
        if (extent != combos.end()) {
            check((*extent)->itemData(0).toInt() == 1,
                  "whose first entry is the single unit cell");
            (*extent)->setCurrentIndex((*extent)->count() - 1);
            check(dialog.style().sliceReplicas > 1,
                  "and picking a replicated extent reaches the style");
        }

        exerciseControls(&dialog);
        check(true, "survives every control being exercised");
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

    // The film timeline is where a film's seconds become frames and back.
    // Both directions have to agree, and re-ranging it (which the production
    // dialog does on every keystroke) must hold the playhead's POSITION in the
    // film rather than its frame number — otherwise editing the duration
    // yanks the live preview back to the start.
    std::printf("Film timeline widget:\n");
    {
        FilmTimelineWidget timeline;
        timeline.setFilm(10.0, 30);
        check(!timeline.isPlaying(), "does not autoplay");

        timeline.setCurrentTime(5.0);
        check(std::fabs(timeline.currentTime() - 5.0) < 0.02,
              "seconds round-trip through the frame slider");

        timeline.setCurrentTime(0.0);
        check(timeline.currentTime() == 0.0, "rewinds exactly to zero");
        timeline.setCurrentTime(10.0);
        check(std::fabs(timeline.currentTime() - 10.0) < 1e-9,
              "the end is exactly the duration, not one frame short");

        // Halfway through a 10 s film, then re-timed to 20 s: the playhead
        // must still be halfway (10 s), not still at 5 s.
        timeline.setCurrentTime(5.0);
        timeline.setFilm(20.0, 30);
        check(std::fabs(timeline.currentTime() - 10.0) < 0.05,
              "re-ranging holds the position in the film, not the frame index");

        // Out-of-range requests clamp instead of running off the slider.
        timeline.setCurrentTime(999.0);
        check(std::fabs(timeline.currentTime() - 20.0) < 1e-9,
              "past the end clamps to the last frame");
        timeline.setCurrentTime(-5.0);
        check(timeline.currentTime() == 0.0, "before the start clamps to zero");

        timeline.stop();
        check(!timeline.isPlaying() && timeline.currentTime() == 0.0,
              "stop rewinds and halts");
    }

    // The 2D Bands wizard hides the whole engine/DFT chrome (it inherits the
    // SCF from its baseline) and builds its extras page on top of that, which
    // is exactly the construction-order shape this file exists to catch.
    std::printf("2D Bands wizard:\n");
    {
        calango::pybridge::PythonEngine python;
        auto structure = std::make_shared<calango::core::Structure>();
        TwoDBandsWizard wizard(structure);
        check(true, "constructs");
        wizard.setDensityBaselines(
            {{QStringLiteral("proc_1 — Si"), QStringLiteral("/jobs/1/sp.gpw")}});
        check(true, "accepts a baseline list");

        // The grid spin box is the one control that changes the cost
        // quadratically; driving it exercises the note it refreshes.
        const auto spins = wizard.findChildren<QSpinBox*>();
        const auto grid = std::find_if(
            spins.begin(), spins.end(), [](const QSpinBox* spin) {
                return spin->minimum() == 3 && spin->maximum() == 512;
            });
        check(grid != spins.end(), "has the N x N grid control");
        if (grid != spins.end()) {
            (*grid)->setValue(48);
            check((*grid)->value() == 48, "which takes a new value");
        }

        // The script must name the selected baseline and the chosen grid --
        // a wizard that generates a script ignoring its own controls is the
        // failure mode worth pinning here. Read off the live preview, which is
        // what the user actually reviews before the job is staged.
        const auto* preview = wizard.findChild<QPlainTextEdit*>();
        check(preview != nullptr, "has a script preview");
        if (preview) {
            const QString script = preview->toPlainText();
            check(script.contains(QStringLiteral("/jobs/1/sp.gpw")),
                  "the previewed script restarts from the selected baseline");
            check(script.contains(QStringLiteral("_n = 48")),
                  "and uses the grid the dialog shows");
        }
    }

    // The 2D Charged Defects wizard. Same construction-order shape as 2D Bands
    // — no engine chrome, an extras page built on top — plus TWO baseline
    // selectors that must reach the script separately. A wizard that wired both
    // combos to the same field would pass every substring test on the generator
    // and still emit a script comparing a cell with itself.
    std::printf("2D Charged Defects wizard:\n");
    {
        calango::pybridge::PythonEngine python;
        auto structure = std::make_shared<calango::core::Structure>();
        Defect2dWizard wizard(structure);
        check(true, "constructs");
        wizard.setDensityBaselines(
            {{QStringLiteral("proc_1 — host"), QStringLiteral("/jobs/1/host.gpw")},
             {QStringLiteral("proc_2 — vacancy"), QStringLiteral("/jobs/2/def.gpw")}});
        check(true, "accepts a baseline list");

        // The two dielectric constants are the module's whole reason to exist,
        // so they are driven to DIFFERENT values: a wizard that collapsed the
        // profile to a scalar would still produce a plausible script.
        const auto spins = wizard.findChildren<QDoubleSpinBox*>();
        int epsilonBoxes = 0;
        for (QDoubleSpinBox* spin : spins)
            if (spin->maximum() >= 1000.0 && spin->minimum() == 1.0)
                spin->setValue(++epsilonBoxes == 1 ? 6.9 : 2.8);
        check(epsilonBoxes == 2, "has separate in-plane and out-of-plane boxes");

        const auto* preview = wizard.findChild<QPlainTextEdit*>();
        check(preview != nullptr, "has a script preview");
        if (preview) {
            const QString script = preview->toPlainText();
            // Two DIFFERENT baselines: the defaulting is what makes the wizard
            // usable, and picking the same run twice is never what is wanted.
            check(script.contains(QStringLiteral("/jobs/1/host.gpw")),
                  "the previewed script names the host baseline");
            check(script.contains(QStringLiteral("/jobs/2/def.gpw")),
                  "and a DIFFERENT one as the neutral defect");
            check(script.contains(QStringLiteral("EPS_PAR = 6.9"))
                      && script.contains(QStringLiteral("EPS_PERP = 2.8")),
                  "and carries both dielectric constants, separately");
        }
    }

    // The Processes dock. Two things here cannot be seen by construction
    // alone: the walltime column is driven by a live QTimer, and the status
    // column's correctness is now about what it does NOT contain.
    std::printf("Processes panel:\n");
    {
        ProcessManagerPanel panel;
        auto* tree = panel.findChild<QTreeWidget*>();
        check(tree != nullptr, "has a task tree");
        if (tree) {
            check(tree->columnCount() == 4, "four columns");
            check(tree->headerItem()->text(3) == QStringLiteral("Walltime"),
                  "the fourth of which is Walltime");

            const int id = panel.registerTask(QStringLiteral("job"),
                                              QStringLiteral("/tmp/proc_0"));
            check(tree->topLevelItemCount() == 1, "registering adds a row");
            QTreeWidgetItem* row = tree->topLevelItem(0);

            // Status is a GLYPH now. The text is gone — that is the horizontal
            // space this change was for — and the word moved to the tooltip so
            // the meaning did not go with it.
            check(row->text(1).isEmpty(),
                  "the status column carries no text at all");
            // The DECORATION ROLE is what is asserted, not a non-null pixmap:
            // this binary does not link the icon resource (IconManager reads
            // it from qrc), so the glyph renders empty here while the role is
            // still populated. Asserting the pixmap would make this test a
            // check on the test target's resource list rather than on the
            // panel.
            check(row->data(1, Qt::DecorationRole).isValid(),
                  "the status is carried by the decoration role instead");
            check(!row->toolTip(1).isEmpty(),
                  "with the word preserved in its tooltip");

            // A queued task has not run for zero seconds; it has not run.
            check(row->text(3).isEmpty(),
                  "a queued task shows no walltime yet");

            panel.setTaskStatus(id, ProcessManagerPanel::Status::Running);
            check(row->text(3) == QStringLiteral("0:00"),
                  "the clock starts when the task starts running");
            check(row->text(1).isEmpty(),
                  "and the status column stays text-free when it changes");

            // The live part. Nothing but a real event loop proves the timer is
            // running, connected, and ticking the right rows.
            const auto spin = [](int ms) {
                QEventLoop loop;
                QTimer::singleShot(ms, &loop, &QEventLoop::quit);
                loop.exec();
            };
            spin(1200);
            // "advanced", not "reads 0:01". Under `ctest -j` a 1200 ms wait
            // can land well past 2 s, and pinning the exact digit would make
            // this a test of the machine's load rather than of the timer.
            const QString ticked = row->text(3);
            check(ticked != QStringLiteral("0:00") && !ticked.isEmpty(),
                  "and advances on its own while the task runs");

            panel.setTaskStatus(id, ProcessManagerPanel::Status::Completed);
            const QString frozen = row->text(3);
            check(!frozen.isEmpty(), "a finished task keeps its duration");
            spin(1200);
            check(row->text(3) == frozen,
                  "frozen at the total, not still counting");

            // A task that never left the queue has no duration to report, and
            // 0:00 would be a claim that it ran instantly.
            const int aborted = panel.registerTask(QStringLiteral("never ran"),
                                                   QString());
            panel.setTaskStatus(aborted, ProcessManagerPanel::Status::Failed);
            check(tree->topLevelItem(1)->text(3).isEmpty(),
                  "a task aborted while queued reports no walltime");
        }
    }

    // The Optics wizard, in both of its modes. "2D Optics" is the SAME class
    // with twoDimensional=true, so the 2D variant inherits every response
    // option rather than reimplementing any — which is worth pinning, because
    // the cheapest way to break it is a 2D-only branch that quietly skips one.
    //
    // Also pinned: the relaxation-time row hides when it does not apply. It
    // sits inside a nested layout, and QFormLayout can only hide a row through
    // a widget it holds DIRECTLY — addressed through the spin box it would
    // silently stay visible, showing a number that is not being used.
    std::printf("Optics wizard (3D and 2D):\n");
    for (const bool twoD : {false, true}) {
        calango::pybridge::PythonEngine python;
        auto structure = std::make_shared<calango::core::Structure>();
        OpticsWizard wizard(structure, twoD);
        wizard.setDensityBaselines(
            {{QStringLiteral("proc_1"), QStringLiteral("/jobs/1/sp.gpw")}});
        wizard.show();
        const char* mode = twoD ? "2D" : "3D";
        check(true, std::string(mode) + ": constructs");

        const auto boxes = wizard.findChildren<QCheckBox*>();
        const auto boxNamed = [&boxes](const QString& fragment) {
            return std::find_if(boxes.begin(), boxes.end(),
                                [&fragment](const QCheckBox* box) {
                                    return box->text().contains(fragment);
                                });
        };
        const auto drude = boxNamed(QStringLiteral("Drude"));
        const auto tetra = boxNamed(QStringLiteral("Tetrahedron"));
        const auto ibz = boxNamed(QStringLiteral("irreducible BZ"));
        check(drude != boxes.end(),
              std::string(mode) + ": offers the Drude term");
        check(tetra != boxes.end(),
              std::string(mode) + ": offers tetrahedron integration");
        check(ibz != boxes.end(),
              std::string(mode) + ": offers the symmetry reduction");
        if (drude == boxes.end() || tetra == boxes.end())
            continue;
        check((*drude)->isChecked(),
              std::string(mode)
                  + ": the Drude term is on by default (GPAW gates it on "
                    "metallicity, so it is a no-op when gapped)");

        // The relaxation time is only meaningful once the rate is untied from
        // eta, so its row must appear and disappear with that choice.
        auto* tau = wizard.findChild<QDoubleSpinBox*>();
        const auto combos = wizard.findChildren<QComboBox*>();
        const auto rateCombo = std::find_if(
            combos.begin(), combos.end(), [](const QComboBox* box) {
                return box->count() == 2
                    && box->itemText(0).contains(QStringLiteral("broadening"));
            });
        check(rateCombo != combos.end(),
              std::string(mode) + ": has the Drude rate-source selector");
        if (rateCombo == combos.end())
            continue;
        const auto tauSpins = wizard.findChildren<QDoubleSpinBox*>();
        const auto tauSpin = std::find_if(
            tauSpins.begin(), tauSpins.end(), [](const QDoubleSpinBox* spin) {
                return spin->suffix().contains(QStringLiteral("fs"));
            });
        check(tauSpin != tauSpins.end(),
              std::string(mode) + ": has the relaxation-time spin box");
        if (tauSpin != tauSpins.end()) {
            check(!(*tauSpin)->isVisibleTo(&wizard),
                  std::string(mode)
                      + ": tau is hidden while the rate follows eta");
            (*rateCombo)->setCurrentIndex(1);  // "From a relaxation time"
            check((*tauSpin)->isVisibleTo(&wizard),
                  std::string(mode) + ": and appears once it does not");
            (*drude)->setChecked(false);
            check(!(*tauSpin)->isVisibleTo(&wizard),
                  std::string(mode)
                      + ": turning the term off hides it again");
            (*drude)->setChecked(true);
        }

        // Both advanced options at once, reaching the script — the combination
        // a metallic monolayer needs.
        (*tetra)->setChecked(true);
        const auto* preview = wizard.findChild<QPlainTextEdit*>();
        check(preview != nullptr, std::string(mode) + ": has a preview");
        if (preview) {
            const QString script = preview->toPlainText();
            check(script.contains(
                      QStringLiteral("integrationmode = \"tetrahedron")),
                  std::string(mode) + ": tetrahedron reaches the script");
            check(script.contains(QStringLiteral("_drude_tau_fs = 10")),
                  std::string(mode)
                      + ": so does the relaxation time");
            check(twoD == script.contains(QStringLiteral("twod_observables")),
                  std::string(mode)
                      + ": the sheet observables follow the mode");
        }
    }

    // The Nonlinear Optics wizard. Two things about it are unusual enough to
    // pin: it hides a form row from a signal its own constructor triggers, and
    // it is the only wizard whose generated script depends on GPAW keywords
    // the GENERATOR overrides rather than reads off the calculator page. A
    // construction that skipped either would produce a script that runs and
    // then aborts inside make_nlodata's assert.
    std::printf("Nonlinear Optics wizard:\n");
    {
        calango::pybridge::PythonEngine python;
        auto structure = std::make_shared<calango::core::Structure>();
        NonlinearOpticsWizard wizard(structure);
        wizard.show();  // isVisibleTo() needs the pages realized
        check(true, "constructs");

        const auto* preview = wizard.findChild<QPlainTextEdit*>();
        check(preview != nullptr, "has a script preview");
        if (preview) {
            const QString script = preview->toPlainText();
            // The three method requirements, in the script the user reviews.
            check(script.contains(QStringLiteral("\"point_group\": False")),
                  "the previewed ground state drops the point group");
            check(script.contains(QStringLiteral("nbands=\"nao\"")),
                  "and carries a band set large enough to sum over");
            check(script.contains(QStringLiteral("make_nlodata")),
                  "and the script really reaches gpaw.nlopt");
        }

        // The component list is validated live, and an invalid entry has to be
        // NAMED: silently dropping "yy" makes it look like a component that
        // produced nothing rather than one that was never asked for.
        const auto edits = wizard.findChildren<QLineEdit*>();
        const auto components = std::find_if(
            edits.begin(), edits.end(), [](const QLineEdit* edit) {
                return edit->text() == QStringLiteral("yyy");
            });
        check(components != edits.end(), "has the tensor-component field");
        if (components != edits.end()) {
            (*components)->setText(QStringLiteral("yyy, xxy"));
            const auto* liveScript = wizard.findChild<QPlainTextEdit*>();
            check(liveScript
                      && liveScript->toPlainText().contains(
                          QStringLiteral("COMPONENTS = ['yyy', 'xxy']")),
                  "typing a second component reaches the script");
            (*components)->setText(QStringLiteral("yyy, qqq"));
            bool named = false;
            for (const auto* label : wizard.findChildren<QLabel*>())
                if (label->text().contains(QStringLiteral("qqq")))
                    named = true;
            check(named, "and an invalid one is named rather than dropped");
        }

        // The gauge belongs to the SHG sum; with SHG off the row describes
        // nothing, and get_shift would reject the argument outright.
        const auto boxes = wizard.findChildren<QCheckBox*>();
        const auto shg = std::find_if(
            boxes.begin(), boxes.end(), [](const QCheckBox* box) {
                return box->text().contains(QStringLiteral("Second-harmonic"));
            });
        check(shg != boxes.end(), "has the SHG toggle");
        const auto combos = wizard.findChildren<QComboBox*>();
        const auto gauge = std::find_if(
            combos.begin(), combos.end(), [](const QComboBox* combo) {
                return combo->itemText(0).contains(QStringLiteral("Length"));
            });
        check(gauge != combos.end(), "and the gauge selector");
        // Visibility is measured against the group box, not the wizard: this
        // wizard puts its settings page AFTER Calculator Settings, so the whole
        // page is on a non-current stack widget and everything on it reports
        // hidden relative to the dialog. What is being asserted is the row's
        // own explicit show/hide, which is exactly what isVisibleTo(group)
        // reports.
        const auto groups = wizard.findChildren<QGroupBox*>();
        const auto responseGroup = std::find_if(
            groups.begin(), groups.end(), [](const QGroupBox* group) {
                return group->title() == QStringLiteral("Response");
            });
        check(responseGroup != groups.end(), "and the Response group");
        if (shg != boxes.end() && gauge != combos.end()
            && responseGroup != groups.end()) {
            check((*gauge)->isVisibleTo(*responseGroup),
                  "the gauge is offered while SHG is on");
            (*shg)->setChecked(false);
            check(!(*gauge)->isVisibleTo(*responseGroup),
                  "and withdrawn when it is off");
            (*shg)->setChecked(true);
            check((*gauge)->isVisibleTo(*responseGroup), "and comes back");
        }
    }

    // The Raman / IR wizard now serves three engines whose stage-1 content has
    // nothing in common: GPAW selects three inherited runs, VASP and Quantum
    // ESPRESSO converge their own ground state. Switching between them shows
    // and hides whole group boxes from a signal the constructor triggers.
    std::printf("Raman / IR wizard engine switching:\n");
    {
        calango::pybridge::PythonEngine python;
        auto structure = std::make_shared<calango::core::Structure>();
        RamanIrWizard wizard(structure);
        wizard.show();
        check(true, "constructs");
        wizard.setDensityBaselines(
            {{QStringLiteral("proc_1 — MgO"), QStringLiteral("/jobs/1/sp.gpw")}});

        const auto groupNamed = [&wizard](const QString& title) {
            const auto groups = wizard.findChildren<QGroupBox*>();
            const auto it = std::find_if(
                groups.begin(), groups.end(), [&title](const QGroupBox* group) {
                    return group->title() == title;
                });
            return it == groups.end() ? nullptr : *it;
        };
        auto* inherited = groupNamed(QStringLiteral("Inherited Results"));
        auto* vaspGroup = groupNamed(QStringLiteral("VASP Ground State"));
        auto* qeGroup =
            groupNamed(QStringLiteral("Quantum ESPRESSO Ground State"));
        check(inherited && vaspGroup && qeGroup,
              "all three engines' groups exist");

        const auto combos = wizard.findChildren<QComboBox*>();
        const auto engine = std::find_if(
            combos.begin(), combos.end(), [](const QComboBox* combo) {
                return combo->count() == 3
                    && combo->itemText(0).startsWith(QStringLiteral("GPAW"));
            });
        check(engine != combos.end(), "and the engine selector");
        if (engine != combos.end() && inherited && vaspGroup && qeGroup) {
            check(inherited->isVisibleTo(&wizard) && !vaspGroup->isVisibleTo(&wizard),
                  "GPAW shows the inherited runs and nothing else");
            (*engine)->setCurrentIndex((*engine)->findData(
                static_cast<int>(calango::core::CalculatorKind::Vasp)));
            check(vaspGroup->isVisibleTo(&wizard)
                      && !inherited->isVisibleTo(&wizard)
                      && !qeGroup->isVisibleTo(&wizard),
                  "VASP swaps them for its own ground state");
            const auto* preview = wizard.findChild<QPlainTextEdit*>();
            check(preview
                      && preview->toPlainText().contains(
                          QStringLiteral("ibrion=8")),
                  "and the previewed script takes the DFPT route");
            (*engine)->setCurrentIndex((*engine)->findData(static_cast<int>(
                calango::core::CalculatorKind::QuantumEspresso)));
            check(qeGroup->isVisibleTo(&wizard)
                      && !vaspGroup->isVisibleTo(&wizard),
                  "and Quantum ESPRESSO swaps again");
            check(preview
                      && preview->toPlainText().contains(
                          QStringLiteral("epsil = .true.")),
                  "with ph.x asked for the dielectric response");
        }
    }

    // The smearing combo carries core::SmearingMethod as itemData for the same
    // reason the eigensolver combo does — the menu is ordered for the user and
    // no longer mirrors the enum. It also has to show only the parameters the
    // selected method actually takes.
    std::printf("GPAW smearing methods:\n");
    {
        using calango::core::SmearingMethod;
        calango::pybridge::PythonEngine python;
        SinglePointWizard wizard;
        wizard.show(); // isVisibleTo() needs the page realized

        auto* engine = wizard.findChild<QComboBox*>();
        const int gpaw = engine
            ? engine->findData(static_cast<int>(calango::core::CalculatorKind::Gpaw))
            : -1;
        if (gpaw >= 0)
            engine->setCurrentIndex(gpaw);

        const auto combos = wizard.findChildren<QComboBox*>();
        const auto smearing = std::find_if(
            combos.begin(), combos.end(), [](const QComboBox* combo) {
                return combo->itemText(0) == QStringLiteral("Fermi-Dirac");
            });
        check(smearing != combos.end(), "the smearing combo is present");
        if (smearing != combos.end()) {
            // Every method the request named must be reachable, and each must
            // carry its own enum rather than its row number.
            for (const auto& [label, method] :
                 {std::pair{"Fermi-Dirac", SmearingMethod::FermiDirac},
                  std::pair{"Gaussian", SmearingMethod::Gaussian},
                  std::pair{"Methfessel-Paxton", SmearingMethod::MethfesselPaxton},
                  std::pair{"Marzari-Vanderbilt", SmearingMethod::MarzariVanderbilt},
                  std::pair{"Tetrahedron method", SmearingMethod::TetrahedronMethod},
                  // Named for Blöchl, whose curvature correction it is —
                  // which is also how the VASP documentation refers to
                  // ISMEAR = -5, so the label reads the same for both engines.
                  std::pair{"Improved tetrahedron (Blöchl)",
                            SmearingMethod::ImprovedTetrahedronMethod},
                  std::pair{"Orbital-free", SmearingMethod::OrbitalFree},
                  std::pair{"Fixed", SmearingMethod::FixedOccupations}}) {
                // fromUtf8, not QLatin1String: "Blöchl" is two bytes in this
                // source file, and reading it as Latin-1 produces a string
                // that matches nothing.
                const int row = (*smearing)->findText(QString::fromUtf8(label));
                check(row >= 0, "the method is offered");
                if (row >= 0)
                    check((*smearing)->itemData(row).toInt()
                              == static_cast<int>(method),
                          "and carries its enum as item data, not its row");
            }

            // The width spin box: 3 decimals, eV, ≤ 5 — distinct from the SCF
            // tolerance, which shares the suffix but not the decimals.
            const auto spins = wizard.findChildren<QDoubleSpinBox*>();
            const auto width = std::find_if(
                spins.begin(), spins.end(), [](const QDoubleSpinBox* spin) {
                    return spin->decimals() == 3 && spin->maximum() == 5.0
                        && spin->suffix().contains(QStringLiteral("eV"));
                });
            const auto ints = wizard.findChildren<QSpinBox*>();
            const auto order = std::find_if(
                ints.begin(), ints.end(), [](const QSpinBox* spin) {
                    return spin->maximum() == 10 && spin->minimum() == 0;
                });
            check(width != spins.end(), "the width spin box is present");
            check(order != ints.end(), "and the Methfessel-Paxton order box");

            const auto select = [&smearing](SmearingMethod method) {
                (*smearing)->setCurrentIndex(
                    (*smearing)->findData(static_cast<int>(method)));
            };
            if (width != spins.end() && order != ints.end()) {
                select(SmearingMethod::FermiDirac);
                check((*width)->isVisibleTo(&wizard), "Fermi-Dirac shows a width");
                check(!(*order)->isVisibleTo(&wizard), "and no order");

                select(SmearingMethod::MethfesselPaxton);
                check((*width)->isVisibleTo(&wizard), "MP shows a width");
                check((*order)->isVisibleTo(&wizard), "and its expansion order");

                select(SmearingMethod::MarzariVanderbilt);
                check((*width)->isVisibleTo(&wizard), "MV shows a width");
                check(!(*order)->isVisibleTo(&wizard), "and no order");

                // The exact BZ integrators take neither: showing a width there
                // would advertise a parameter GPAW rejects.
                select(SmearingMethod::TetrahedronMethod);
                check(!(*width)->isVisibleTo(&wizard),
                      "the tetrahedron method shows no width");
                check(!(*order)->isVisibleTo(&wizard), "and no order");

                select(SmearingMethod::OrbitalFree);
                check(!(*width)->isVisibleTo(&wizard),
                      "orbital-free shows no width either");
            }

            // "Fixed" swaps in an occupation-number field, and refuses to be
            // a valid configuration until it is filled — there is no default
            // GPAW could fall back on.
            select(SmearingMethod::FixedOccupations);
            const auto edits = wizard.findChildren<QLineEdit*>();
            const auto numbers = std::find_if(
                edits.begin(), edits.end(), [](const QLineEdit* edit) {
                    return edit->placeholderText().contains(
                        QStringLiteral("2 2 2 0 0"));
                });
            check(numbers != edits.end(),
                  "Fixed shows an occupation-number field");
            if (numbers != edits.end()) {
                check((*numbers)->isVisibleTo(&wizard), "and it is visible");
                select(SmearingMethod::FermiDirac);
                check(!(*numbers)->isVisibleTo(&wizard),
                      "and hidden again for a method that does not take one");
            }
        }

        // --- The same page, under VASP ------------------------------------
        // The menu is not a fixed list any more: VASP encodes the occupation
        // scheme in a single integer, ISMEAR, and has no value for three of
        // the schemes GPAW runs. The script generator substitutes a narrow
        // Gaussian for them, which makes offering them a way to ask for one
        // thing and get another.
        std::printf("Smearing menu under VASP:\n");
        const int vasp = engine
            ? engine->findData(static_cast<int>(calango::core::CalculatorKind::Vasp))
            : -1;
        check(vasp >= 0, "VASP is offered as an engine");
        if (vasp >= 0 && smearing != combos.end()) {
            engine->setCurrentIndex(vasp);
            for (const auto& [label, method] :
                 {std::pair{"Fermi-Dirac", SmearingMethod::FermiDirac},
                  std::pair{"Gaussian", SmearingMethod::Gaussian},
                  std::pair{"Methfessel-Paxton", SmearingMethod::MethfesselPaxton},
                  std::pair{"Tetrahedron method", SmearingMethod::TetrahedronMethod},
                  std::pair{"Improved tetrahedron (Blöchl)",
                            SmearingMethod::ImprovedTetrahedronMethod}}) {
                const int row = (*smearing)->findData(static_cast<int>(method));
                check(row >= 0, "a scheme with an ISMEAR survives the filter");
                if (row >= 0)
                    check((*smearing)->itemText(row) == QString::fromUtf8(label),
                          "under the same name it has for GPAW");
            }
            for (const SmearingMethod method :
                 {SmearingMethod::MarzariVanderbilt, SmearingMethod::OrbitalFree,
                  SmearingMethod::FixedOccupations}) {
                check((*smearing)->findData(static_cast<int>(method)) < 0,
                      "and one without is withdrawn rather than approximated");
            }
            // Whatever was selected must still be a real selection. Landing on
            // -1 would make the config read SmearingMethod(0) by accident.
            check((*smearing)->currentIndex() >= 0,
                  "the selection survives the refilter");

            // The engine's own controls take the place of GPAW's on the shared
            // rows, rather than sitting in a VASP-shaped form of their own.
            const auto algo = std::find_if(
                combos.begin(), combos.end(), [](const QComboBox* combo) {
                    return combo->count() > 0
                        && combo->itemText(0).startsWith(
                               QStringLiteral("Normal — blocked Davidson"));
                });
            check(algo != combos.end(), "ALGO stands in as the eigensolver");
            if (algo != combos.end())
                check((*algo)->isVisibleTo(&wizard), "and is shown for VASP");

            // The POTCAR path is no longer editable here at all: it belongs to
            // Preferences → External Files, and a second field for it was a
            // second place for it to be wrong.
            const auto vaspEdits = wizard.findChildren<QLineEdit*>();
            const auto potcarField = std::find_if(
                vaspEdits.begin(), vaspEdits.end(), [](const QLineEdit* edit) {
                    return edit->placeholderText().contains(
                        QStringLiteral("POTCAR"), Qt::CaseInsensitive);
                });
            check(potcarField == vaspEdits.end(),
                  "and no POTCAR directory field is offered");

            // Γ-centering is a plane-wave concern, not a GPAW one — an even
            // mesh misses Γ whoever computes it.
            const auto boxes = wizard.findChildren<QCheckBox*>();
            const auto gamma = std::find_if(
                boxes.begin(), boxes.end(), [](const QCheckBox* box) {
                    return box->text() == QStringLiteral("Gamma-centered Grid");
                });
            check(gamma != boxes.end() && (*gamma)->isVisibleTo(&wizard),
                  "Gamma-centered Grid is offered for VASP too");
        }
        wizard.hide();
    }

    // Every file the generators write must reach the Volumetric Data dock with
    // a readable name. The Hartree potential did not: the dock's table was
    // keyed off a literal that had drifted from the one the script emits, so
    // it arrived labelled "potential_hartree.cube". Both ends now share
    // core::densityFiles, and this asserts the mapping is total over it.
    std::printf("Volumetric display names:\n");
    {
        using namespace calango::core::densityFiles;
        check(volumetricDisplayName(QLatin1String(kHartree))
                  == QStringLiteral("Hartree potential"),
              "the Hartree potential is named, not spelled as its file");
        for (const char* file : {kAllElectron, kPseudo, kSpin, kHartree, kElf,
                                 kKineticEnergy, kDensity,
                                 kChargeDensityDifference}) {
            const QString name = QLatin1String(file);
            check(volumetricDisplayName(name) != name,
                  "every generated density file has a display label");
        }
        for (const char* vasp : {"CHGCAR", "AECCAR0", "AECCAR2", "LOCPOT",
                                 "ELFCAR"}) {
            const QString name = QLatin1String(vasp);
            check(volumetricDisplayName(name) != name,
                  "and so does every VASP grid");
        }
        // A cube dropped in by hand keeps its file name: that is the only
        // honest label available for it, and dropping it would leave the row
        // blank.
        check(volumetricDisplayName(QStringLiteral("my_field.cube"))
                  == QStringLiteral("my_field.cube"),
              "an unknown file falls back to its own name");
    }

    // The fractional window of the "Show neighboring cells" dialog. Whole
    // cells, and the off-by-one at integer bounds is the entire contract:
    // 0 -> 1 must be ONE cell, not two.
    std::printf("Neighboring-cell window:\n");
    {
        using calango::render::NeighborCellRange;
        NeighborCellRange range; // the default
        check(range.homeCellOnly(), "0 -> 1 on every axis is the home cell alone");
        check(range.cellOffsets().size() == 1, "and draws exactly one cell");

        NeighborCellRange plusX;
        plusX.max[0] = 2.0;
        check(!plusX.homeCellOnly(), "0 -> 2 along x is more than the home cell");
        const auto offsets = plusX.cellOffsets();
        check(offsets.size() == 2, "and draws two cells");
        check(offsets[0] == (std::array<int, 3>{0, 0, 0})
                  && offsets[1] == (std::array<int, 3>{1, 0, 0}),
              "the home cell and its neighbour along +x");

        NeighborCellRange negative;
        negative.min[1] = -1.0;
        check(negative.cellOffsets().size() == 2,
              "a negative minimum extends the other way");

        NeighborCellRange partial;
        partial.max[2] = 1.5;
        check(partial.cellOffsets().size() == 2,
              "a window that only reaches into a cell still draws it whole");

        NeighborCellRange block;
        block.max[0] = 2.0;
        block.max[1] = 2.0;
        block.max[2] = 2.0;
        check(block.cellOffsets().size() == 8, "the window is a 3D block");

        NeighborCellRange degenerate;
        degenerate.max[0] = 0.0; // min == max
        check(degenerate.cellOffsets().size() == 1,
              "a degenerate window still draws one cell rather than none");
    }

    // -- MLWF source step: the gate on every Wannier post-process ------------
    //
    // Wannier Interpolation, Fermi Surface and Topological Invariants all
    // diagonalize the localized H(R) an MLWF run produced. The step that picks
    // that run used to be a QInputDialog raised BEFORE the settings dialog;
    // it is now the dialog's own first group, which means the dialog is also
    // where "you picked a run whose wavefunctions are gone" has to surface.
    std::printf("MLWF source selection:\n");
    {
        const QString root =
            QDir::tempPath() + QStringLiteral("/calango-mlwf-source-test");
        const QString good = root + QStringLiteral("/complete");
        const QString stale = root + QStringLiteral("/stale");
        const QString notARun = root + QStringLiteral("/empty");
        for (const QString& dir : {good, stale, notARun})
            QDir().mkpath(dir);

        // A complete run: wannier.json plus the .gpw it recorded.
        {
            QFile gpw(good + QStringLiteral("/wannier.gpw"));
            gpw.open(QIODevice::WriteOnly);
            gpw.write("x");
            QFile json(good + QStringLiteral("/wannier.json"));
            json.open(QIODevice::WriteOnly | QIODevice::Text);
            json.write(QStringLiteral(
                           R"({"nwannier": 4, "projection": "orbitals",)"
                           R"( "total_spread": 12.5, "gpw": "%1"})")
                           .arg(good + QStringLiteral("/wannier.gpw"))
                           .toUtf8());
        }
        // Complete, but the wavefunctions it borrowed from a single-point
        // baseline are gone — the case a bare "did it finish?" check misses.
        {
            QFile json(stale + QStringLiteral("/wannier.json"));
            json.open(QIODevice::WriteOnly | QIODevice::Text);
            json.write(R"({"nwannier": 4, "gpw": "/nowhere/single_point.gpw"})");
        }

        const QList<QPair<QString, QString>> runs{
            {QStringLiteral("#1 — stale"), stale},
            {QStringLiteral("#2 — complete"), good}};

        // Preselects the newest, which is the one being followed up.
        MlwfSourceSelector selector(runs);
        check(selector.directory() == good,
              "the most recent completed run is preselected");
        check(selector.isValid(), "and validates when its .gpw is reachable");
        check(selector.invalidReason().isEmpty(),
              "with no complaint to make about it");

        // Selecting the stale run must fail HERE, not after a job is staged.
        if (auto* combo = selector.findChild<QComboBox*>()) {
            combo->setCurrentIndex(0);
            check(selector.directory() == stale, "the selection follows the combo");
            check(!selector.isValid(),
                  "a run whose wavefunctions are gone is refused up front");
            check(selector.invalidReason().contains(
                      QLatin1String("/nowhere/single_point.gpw")),
                  "and the reason names the file that is missing");
        }

        MlwfSourceSelector none({});
        check(!none.isValid(), "an empty candidate list is not valid");

        // The interpreter these modules run under has to come from the MLWF
        // run's own calculator.json, because every one of their scripts does
        // `from gpaw import GPAW`. Launching them under the embedded
        // interpreter — which carries ASE but no GPAW — is a
        // ModuleNotFoundError a hundred lines into a staged job, and that is
        // exactly what shipped. The provenance file is the record of where
        // GPAW actually is.
        {
            QFile provenance(good + QStringLiteral("/calculator.json"));
            provenance.open(QIODevice::WriteOnly | QIODevice::Text);
            provenance.write(
                QStringLiteral(R"({"engine": "GPAW", "engine_kind": 5,)"
                               R"( "python": "%1", "conda_env": "%2"})")
                    .arg(good + QStringLiteral("/bin/python"),
                         good)
                    .toUtf8());
        }
        const auto inherited =
            SimulationWizardBase::readCalculatorProvenance(good);
        check(inherited.has_value(),
              "a completed MLWF run carries its calculator provenance");
        check(inherited && inherited->pythonExecutable
                               == good + QStringLiteral("/bin/python"),
              "including the interpreter its GPAW ran under — which is what "
              "the post-processes must be launched with, not the embedded one");

        // Each of the three dialogs carries the step and refuses to accept
        // without it. The Fermi Surface and Topology dialogs also fold the
        // choice into config(), so the caller no longer patches mlwfDir in.
        FermiSurfaceDialog fermi(runs);
        check(fermi.mlwfDirectory() == good,
              "Fermi Surface takes its source from the step");
        check(fermi.config().mlwfDir == good.toStdString(),
              "and reports it in config() rather than leaving it blank");

        TopologyDialog topology(runs);
        check(topology.config().mlwfDir == good.toStdString(),
              "Topological Invariants does the same");

        // With nothing to pick, OK must be unavailable in all three.
        // Wannier Interpolation carries the same step wired the same way; it
        // is not constructed here because reaching it means linking the
        // embedded k-path editor and its QOpenGLWidget Brillouin-zone view.
        for (QWidget* dialog : {static_cast<QWidget*>(new FermiSurfaceDialog({})),
                                static_cast<QWidget*>(new TopologyDialog({}))}) {
            bool anyAcceptEnabled = false;
            for (QDialogButtonBox* box : dialog->findChildren<QDialogButtonBox*>())
                for (QAbstractButton* button : box->buttons())
                    if (box->buttonRole(button) == QDialogButtonBox::AcceptRole
                        && button->isEnabled())
                        anyAcceptEnabled = true;
            check(!anyAcceptEnabled,
                  "no MLWF run selected leaves the accept button disabled");
            delete dialog;
        }

        QDir(root).removeRecursively();
    }

    // -- Random Noise Viewer: the JSON schema is a two-file contract ---------
    //
    // random_noise.json is written by a generated Python script and read by
    // this C++ window; neither file mentions the other, so a key renamed on
    // one side fails silently on the other — the window opens showing dashes
    // where the spread should be. The fixture below is a verbatim excerpt of
    // what RandomNoiseScriptGenerator emits, so renaming a key breaks here.
    std::printf("Random Noise viewer:\n");
    {
        const QString dir =
            QDir::tempPath() + QStringLiteral("/calango-noise-viewer-test");
        QDir().mkpath(dir);
        {
            QFile json(dir + QStringLiteral("/random_noise.json"));
            json.open(QIODevice::WriteOnly | QIODevice::Text);
            json.write(R"({
  "summary": {
    "members": 4, "evaluated": 3, "failed": 1,
    "energy_mean": 0.09, "energy_std": 0.0677,
    "energy_min": -0.02, "energy_max": 0.23, "energy_range": 0.25,
    "reference_energy": -0.02, "mean_shift": 0.113,
    "energy_per_atom_mean": 0.0225, "energy_per_atom_std": 0.0169,
    "force_samples": 6,
    "force_component_mean": 0.0, "force_component_std": 0.509,
    "force_magnitude_mean": 0.78, "force_magnitude_std": 0.405,
    "force_magnitude_max": 1.61
  },
  "members": [
    {"frame": 0, "natoms": 4, "energy": -0.02, "energy_per_atom": -0.005},
    {"frame": 1, "natoms": 4, "energy": 0.07, "energy_per_atom": 0.0175},
    {"frame": 2, "natoms": 4, "energy": 0.23, "energy_per_atom": 0.0575},
    {"frame": 3, "natoms": 4, "energy": null, "error": "SCF failed"}
  ],
  "force_magnitudes": [0.11, 0.42, 0.78, 0.91, 1.2, 1.61]
})");
        }
        // No trajectory beside it: Export must degrade to a disabled button
        // rather than offering to copy a file that is not there.
        RandomNoiseViewer viewer(dir);
        check(viewer.hasData(), "reads the schema the generator writes");

        // The summary text is the deliverable — the standard deviations of the
        // energies and of the forces, both read from the file rather than
        // recomputed, so the window and random_noise.json cannot disagree.
        // QStringLiteral, not QLatin1String: "σ" is two UTF-8 bytes, which
        // Latin-1 would read as two separate characters and never match.
        QString summary;
        for (QLabel* label : viewer.findChildren<QLabel*>())
            if (label->text().contains(QStringLiteral("σ")))
                summary = label->text();
        check(summary.contains(QLatin1String("0.0677")),
              "reports the energy standard deviation");
        check(summary.contains(QLatin1String("0.509")),
              "reports the force standard deviation");
        check(summary.contains(QLatin1String("16.9")),
              "and the per-atom energy spread, converted to meV");
        check(summary.contains(QLatin1String("1 failed")),
              "says how many members failed, since they are excluded");

        // Both histograms exist and were fed: the energy panel takes the three
        // finite member energies (the failed one is skipped, not binned as a
        // NaN edge), the force panel the pooled per-atom magnitudes.
        check(viewer.findChildren<HistogramPlotWidget*>().size() == 2,
              "draws an energy and a force histogram");

        exerciseControls(&viewer);
        check(true, "survives re-binning across the whole range");

        // Export copies the evaluated trajectory. With none written — a run
        // where every member failed — the button must be off rather than
        // offering to copy a file that is not there.
        const auto exportButton = [](const QWidget& window) -> QAbstractButton* {
            for (QAbstractButton* button :
                 window.findChildren<QAbstractButton*>())
                if (button->text().startsWith(QLatin1String("Export")))
                    return button;
            return nullptr;
        };
        QAbstractButton* button = exportButton(viewer);
        check(button != nullptr && !button->isEnabled(),
              "Export is disabled when the run wrote no trajectory");

        {
            QFile traj(dir + QStringLiteral("/noise_singlepoint.extxyz"));
            traj.open(QIODevice::WriteOnly | QIODevice::Text);
            traj.write("1\nProperties=species:S:1:pos:R:3 energy=-0.02\n"
                       "Cu 0.0 0.0 0.0\n");
        }
        RandomNoiseViewer withTrajectory(dir);
        QAbstractButton* enabled = exportButton(withTrajectory);
        check(enabled != nullptr && enabled->isEnabled(),
              "and enabled once the trajectory is beside the results");

        QFile::remove(dir + QStringLiteral("/noise_singlepoint.extxyz"));
        QFile::remove(dir + QStringLiteral("/random_noise.json"));
        QDir().rmdir(dir);
    }
    {
        // A directory with no results at all: the caller checks hasData() and
        // deletes the window, so this must not crash on the way to false.
        RandomNoiseViewer empty(QDir::tempPath()
                                + QStringLiteral("/calango-does-not-exist"));
        check(!empty.hasData(), "reports no data for a directory without one");
    }

    // -- MACE Trainer: the config MACE will actually accept ------------------
    //
    // Everything here was verified against mace-torch 0.3.15's own
    // `mace.tools.arg_parser`. The failures it guards against all happen on
    // the training machine, minutes-to-hours after the dialog closed:
    //
    //  * MACE loads the config through configargparse, which ABORTS on a key
    //    it does not recognise — an invented setting is a dead run.
    //  * Its default reference-data keys are REF_energy / REF_forces. ASE —
    //    and so every dataset Calango exports — puts the energy and forces on
    //    a SinglePointCalculator, leaving atoms.info and atoms.arrays empty on
    //    read-back. With the defaults, MACE finds neither key and stops.
    //  * E0s is not optional. Without it, and without config_type=IsolatedAtom
    //    frames in the training file, MACE raises "E0s not found in training
    //    file and not specified in command line" before the first epoch.
    std::printf("MACE trainer configuration:\n");
    {
        MaceTrainerDialog dialog;
        check(true, "constructs without dereferencing a half-built widget");

        const QString yaml = dialog.yaml();
        const auto has = [&yaml](const char* needle) {
            return yaml.contains(QLatin1String(needle));
        };
        check(has("energy_key:"), "names the energy key rather than "
                                  "inheriting MACE's REF_energy default");
        check(has("forces_key:"), "names the forces key too");
        check(has("energy_key: \"energy\"") && has("forces_key: \"forces\""),
              "and defaults them to what ASE writes");
        check(has("E0s:"), "supplies isolated-atom energies");
        check(has("num_interactions:"),
              "spells num_interactions in full (the singular form only works "
              "by argparse prefix matching)");
        check(has("default_dtype:"), "pins the training precision");
        check(has("save_cpu:"),
              "saves a CPU copy, so the model loads without the training GPU");
        // A zero weight under `loss: weighted` fits nothing; emitting it reads
        // as a stress term that exists when none does.
        check(!has("stress_weight: 0") && !has("virials_weight: 0"),
              "omits the loss weights that are switched off");

        // Stage two must leave room for at least one evaluation — that is when
        // MACE writes the stage-two checkpoint it then reloads at the end of
        // the run. Too late a start produces no checkpoint, and MACE dies
        // loading it after training successfully to the last epoch.
        QSpinBox* epochs = nullptr;
        QSpinBox* swaStart = nullptr;
        for (QSpinBox* spin : dialog.findChildren<QSpinBox*>()) {
            if (spin->value() == 200 && spin->maximum() == 100000)
                epochs = spin;
            if (spin->value() == 150)
                swaStart = spin;
        }
        check(epochs != nullptr && swaStart != nullptr,
              "the epoch and stage-two controls are present");
        if (epochs && swaStart) {
            epochs->setValue(20);
            check(swaStart->value() < 20,
                  "shrinking the epoch budget pulls the stage-two start in "
                  "with it");
            epochs->setValue(200);
        }

        exerciseControls(&dialog);
        check(true, "survives every control being toggled");

        // The launcher is Python that has to parse on a machine Calango is not
        // installed on. Pin the invocation shape rather than only the text: it
        // must drive MACE's own entry point, and it must not reach for a
        // Calango helper module.
        const QString runner = dialog.runnerScript();
        check(runner.contains(QLatin1String("from mace.cli.run_train import "
                                            "main as mace_run_train")),
              "drives MACE's documented in-process entry point");
        check(runner.contains(QLatin1String("logging.getLogger().handlers"
                                            ".clear()")),
              "clears our log handlers so MACE does not double every line");
        check(!runner.contains(QLatin1String("calango_log")),
              "the launcher is self-contained too");
    }

    // --- Preview refresh during construction ------------------------------
    //
    // The invariant, pinned in the base class rather than in one wizard: a
    // control created on a settings page may refresh the script preview while
    // that page is still being built, and must not crash doing it. Thirty-odd
    // wizards rely on this; the one that broke it took the application down on
    // the menu click that opened it.
    {
        std::printf("Preview refresh during construction:\n");
        // Like every other wizard block here: the shared calculator page
        // resolves its interpreter through PythonEngine while it is built.
        calango::pybridge::PythonEngine python;
        PreviewDuringConstructionWizard wizard;
        check(wizard.refreshedWhileBuilding(),
              "a settings page can refresh the preview before the review page "
              "exists");
        // And once the whole wizard IS built, the preview works normally.
        wizard.refreshPreviewForTest();
        QPlainTextEdit* preview = nullptr;
        for (QPlainTextEdit* edit : wizard.findChildren<QPlainTextEdit*>())
            if (edit->toPlainText().contains(QStringLiteral("# probe")))
                preview = edit;
        check(preview != nullptr,
              "and the refresh reaches it once the review page is up");
        exerciseControls(&wizard);
        check(true, "survives every control being toggled");
    }

    // --- Electronic Structure wizard --------------------------------------
    //
    // The wizard that actually crashed. Its orbital-projection table seeds one
    // row per element while the settings page is being built, and each row used
    // to refresh the script preview — which does not exist yet, because the
    // review page is built last. The application died on the menu click that
    // opened it, before a single widget was shown.
    //
    // It is the most control-dense settings page in the application: a k-path
    // editor, spin-orbit coupling, PDOS, band symmetry and fatbands, several of
    // which enable and disable each other. Constructing it and toggling
    // everything is the exercise.
    {
        std::printf("Electronic Structure wizard:\n");
        calango::pybridge::PythonEngine python;

        // Graphene: two atoms, a real hexagonal cell (the k-path editor needs
        // one), and two shells per element for the projection defaults.
        auto structure = std::make_shared<calango::core::Structure>();
        calango::core::UnitCell cell;
        cell.setVectors({calango::core::Vec3{2.46, 0.0, 0.0},
                         calango::core::Vec3{-1.23, 2.1304, 0.0},
                         calango::core::Vec3{0.0, 0.0, 15.0}});
        cell.setPbc({true, true, true});
        structure->setCell(cell);
        calango::core::Atom carbon;
        carbon.atomicNumber = 6;
        carbon.position = {0.0, 0.0, 7.5};
        structure->addAtom(carbon);
        carbon.position = {1.23, 0.7101, 7.5};
        structure->addAtom(carbon);

        ElectronicBandsWizard wizard(structure);
        check(true, "constructs without dereferencing the unbuilt preview");
        wizard.setDensityBaselines(
            {{QStringLiteral("proc_1"), QStringLiteral("/jobs/1/sp.gpw")}});

        // The projection table must have seeded itself from the structure —
        // one channel for carbon. An empty table is not a crash but it is the
        // same bug half-fixed: the rows are what the seeding call produces.
        QTableWidget* channels = nullptr;
        for (QTableWidget* table : wizard.findChildren<QTableWidget*>())
            if (table->columnCount() == 3)
                channels = table;
        check(channels != nullptr && channels->rowCount() == 1,
              "the fatband table seeds one channel per element");

        // The setup page must FIT ON A SCREEN. This is a real regression, not
        // a hypothetical: stacked in one column the page measured 587x1027
        // (minimum 940 tall) and ran off the bottom of a laptop display, and
        // it got there one feature at a time — each addition was individually
        // reasonable and none of them could see the total.
        //
        // Arranged in two columns it is 865x820, minimum 772x748. The bounds
        // below are checked TOGETHER on purpose: a single-column layout cannot
        // satisfy both, because narrow is exactly how it got tall.
        wizard.ensurePolished();
        const QSize hint = wizard.minimumSizeHint();
        std::printf("    minimum size %dx%d\n", hint.width(), hint.height());
        check(hint.height() <= 820,
              "the setup page fits the height of a laptop screen");
        check(hint.width() >= 700,
              "and is wide rather than tall — the two columns are side by "
              "side, not stacked");

        exerciseControls(&wizard);
        check(true, "survives every control being toggled");

        // Toggling everything switched spin-orbit coupling on and off again.
        // The two scalar-state post-processes must be usable afterwards —
        // SOC clears and disables them, and leaving them disabled once it is
        // switched back off would be a silent loss of both features.
        QGroupBox* symmetryGroup = nullptr;
        for (QGroupBox* group : wizard.findChildren<QGroupBox*>())
            if (group->title().contains(QStringLiteral("Band symmetry")))
                symmetryGroup = group;
        check(symmetryGroup != nullptr && symmetryGroup->isEnabled(),
              "band symmetry is re-enabled once spin-orbit coupling is off");
    }

    // --- Magnetic Space Group -------------------------------------------
    //
    // Its constructor is the shape this test exists for: it builds the moment
    // table, then immediately runs a determination whose result it writes BACK
    // into that table and into labels created earlier in the same constructor.
    // Every control it owns re-enters that path — the source combo reloads the
    // moments and re-determines, the tolerance spin boxes re-determine — so
    // toggling them all is the exercise.
    {
        std::printf("Magnetic Space Group dialog:\n");
        calango::pybridge::PythonEngine python;

        // The CsCl-shaped cube with antiparallel moments: geometrically bcc
        // (Im-3m), magnetically the type-IV group whose unitary part is the
        // simple-cubic Pm-3m. A case where the answer is not the geometry's.
        constexpr double a = 2.87;
        auto structure = std::make_shared<calango::core::Structure>();
        calango::core::UnitCell cell;
        cell.setVectors({calango::core::Vec3{a, 0.0, 0.0},
                         calango::core::Vec3{0.0, a, 0.0},
                         calango::core::Vec3{0.0, 0.0, a}});
        cell.setPbc({true, true, true});
        structure->setCell(cell);
        calango::core::Atom atom;
        atom.atomicNumber = 26;
        atom.position = {0.0, 0.0, 0.0};
        structure->addAtom(atom);
        atom.position = {0.5 * a, 0.5 * a, 0.5 * a};
        structure->addAtom(atom);
        structure->setScalarField("magmoms", {2.2, -2.2});

        MagneticSpaceGroupDialog dialog(structure);
        check(true, "constructs and runs its first determination");

        // The moments must arrive in the table: they are the input, and a
        // table that silently came up empty would classify a grey group and
        // look perfectly plausible doing it.
        QTableWidget* moments = nullptr;
        for (QTableWidget* table : dialog.findChildren<QTableWidget*>())
            if (table->columnCount() == 9)
                moments = table;
        check(moments != nullptr && moments->rowCount() == 2,
              "the moment table is filled from the structure");
        if (moments && moments->rowCount() == 2 && moments->item(0, 7)) {
            check(std::abs(moments->item(0, 7)->text().toDouble() - 2.2) < 1e-6,
                  "with the collinear moment promoted onto z");
        }

        exerciseControls(&dialog);
        check(true, "survives every control being toggled");
    }

    // The Liquid / Gas Interface wizard. Both of its pages recompute a live
    // summary from the wizard's shared parameter block on every control
    // change, and stage 2 seeds a table row inside its own constructor —
    // exactly the seed-during-construction shape that segfaulted the
    // Electronic Structure wizard. Its pages are also QWizardPages, whose
    // initializePage() runs on a page transition rather than at construction,
    // so both are pushed here.
    {
        std::printf("Liquid / Gas Interface wizard:\n");

        constexpr double a = 4.05;
        auto slab = std::make_shared<calango::core::Structure>();
        calango::core::UnitCell cell;
        cell.setVectors({calango::core::Vec3{a, 0.0, 0.0},
                         calango::core::Vec3{0.0, a, 0.0},
                         calango::core::Vec3{0.0, 0.0, 3.0 * a}});
        cell.setPbc({true, true, true});
        slab->setCell(cell);
        for (int layer = 0; layer < 3; ++layer) {
            calango::core::Atom atom;
            atom.atomicNumber = 13;
            atom.position = {0.0, 0.0, layer * a * 0.5};
            slab->addAtom(atom);
        }

        LiquidInterfaceWizard wizard(slab);
        check(true, "constructs with a slab");
        wizard.restart();
        check(wizard.currentPage() != nullptr, "and opens on its first page");

        // Stage 1 must accept these defaults, or Next is dead and the wizard
        // cannot be used at all.
        check(wizard.currentPage()->isComplete(),
              "whose defaults are a complete request");
        exerciseControls(wizard.currentPage());
        check(true, "stage 1 survives every control being toggled");

        wizard.next();
        check(wizard.currentPage() != nullptr
                  && wizard.currentPage()->isComplete(),
              "stage 2 opens seeded with one solvent row");
        exerciseControls(wizard.currentPage());
        check(true, "and survives every control being toggled");

        // The parameters the pages wrote have to be the ones the builder is
        // handed: a live summary that reads its own widgets while the build
        // reads a stale struct is a wizard that lies about what it produced.
        QString error;
        const bool built = wizard.build(&error);
        check(built, built ? "the collected parameters build a cell"
                           : ("the collected parameters build a cell — "
                              + error.toStdString())
                                 .c_str());
        check(built && wizard.result() && wizard.result()->totalMolecules > 0,
              "with molecules actually packed into the region");
    }

    std::printf("Molecular Dynamics wizard — annealing mode:\n");
    {
        // Same reason as the Single-Point block: SimulationWizardBase reads
        // the embedded interpreter while building its Calculator Settings
        // stage, and PythonEngine::instance() asserts rather than lazily
        // constructing one.
        calango::pybridge::PythonEngine python;
        MolecularDynamicsWizard wizard;
        check(true, "constructs");

        auto* mode = wizard.findChild<QComboBox*>(QStringLiteral("mdModeCombo"));
        auto* schedule =
            wizard.findChild<QComboBox*>(QStringLiteral("mdScheduleCombo"));
        check(mode != nullptr && schedule != nullptr,
              "has a mode selector and a schedule selector");
        check(schedule != nullptr && schedule->count() == 3,
              "offering Linear, Exponential and Logarithmic");

        // The script is the deliverable, and script() returns the review
        // stage's live preview — the same text the run is staged from, not a
        // re-derivation that could agree with the generator while the wizard
        // disagreed with both.
        const auto scriptFor = [&wizard](bool annealing, int scheduleIndex) {
            auto* modeCombo =
                wizard.findChild<QComboBox*>(QStringLiteral("mdModeCombo"));
            auto* scheduleCombo =
                wizard.findChild<QComboBox*>(QStringLiteral("mdScheduleCombo"));
            // Away and back, so the mode combo always emits: setCurrentIndex
            // to the value already held is silent, and the preview would then
            // still be showing whatever the previous call left in it.
            modeCombo->setCurrentIndex(annealing ? 0 : 1);
            scheduleCombo->setCurrentIndex(scheduleIndex);
            modeCombo->setCurrentIndex(annealing ? 1 : 0);
            return wizard.script();
        };

        const QString constant = scriptFor(false, 0);
        check(!constant.contains(QStringLiteral("_anneal_target")),
              "constant-temperature mode generates no schedule");
        check(constant.contains(QStringLiteral("CALANGO_TARGET_TEMP")),
              "and keeps the single thermostat setpoint marker");

        const QString exponential = scriptFor(true, 1);
        check(exponential.contains(QStringLiteral("math.exp(-anneal_k * x)")),
              "picking Exponential writes the exponential law");
        check(exponential.contains(QStringLiteral("_set_target_temperature")),
              "and retargets the thermostat during the run");
        check(!exponential.contains(QStringLiteral("CALANGO_TARGET_TEMP")),
              "with no static reference line, which a ramp does not have");

        const QString logarithmic = scriptFor(true, 2);
        check(logarithmic.contains(QStringLiteral("math.log1p(anneal_k * x)")),
              "and picking Logarithmic writes the logarithmic one");

        // Switching back has to leave nothing behind: a stale `import math`
        // is harmless, a stale retargeting observer is a run that anneals
        // when the user asked it not to.
        const QString backToConstant = scriptFor(false, 0);
        check(!backToConstant.contains(QStringLiteral("_anneal_target")),
              "switching back to constant temperature drops the schedule again");

        // Annealing needs a thermostat, so NVE must not remain selectable.
        auto* modeCombo =
            wizard.findChild<QComboBox*>(QStringLiteral("mdModeCombo"));
        modeCombo->setCurrentIndex(1);
        QComboBox* ensemble = nullptr;
        for (QComboBox* combo : wizard.findChildren<QComboBox*>())
            if (combo->count() == 7
                && combo->itemText(0).contains(QStringLiteral("NVE")))
                ensemble = combo;
        check(ensemble != nullptr, "the ensemble combo is findable");
        if (ensemble) {
            check(ensemble->currentIndex() != 0,
                  "annealing moves off NVE rather than annealing nothing");
            check(!ensemble->model()->index(0, 0).flags().testFlag(
                      Qt::ItemIsEnabled),
                  "and NVE is withdrawn from the list while it is selected");
            modeCombo->setCurrentIndex(0);
            check(ensemble->model()->index(0, 0).flags().testFlag(
                      Qt::ItemIsEnabled),
                  "returning to constant temperature offers NVE again");
        }

        exerciseControls(&wizard);
        check(true, "survives every control being toggled in either mode");
    }

    // A small crystal both defect wizards can chew on: 4x4x4 simple cubic.
    const auto cubicCrystal = [] {
        auto structure = std::make_shared<calango::core::Structure>();
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                for (int k = 0; k < 4; ++k) {
                    calango::core::Atom atom;
                    atom.atomicNumber = 13;
                    atom.position = {i * 3.0, j * 3.0, k * 3.0};
                    structure->addAtom(atom);
                }
        structure->setCell(calango::core::UnitCell({12, 0, 0}, {0, 12, 0},
                                                   {0, 0, 12},
                                                   {true, true, true}));
        return structure;
    };

    std::printf("Dislocation wizard:\n");
    {
        DislocationWizard wizard(cubicCrystal());
        check(true, "constructs with a crystal");
        wizard.restart();
        check(wizard.currentPage() != nullptr
                  && wizard.currentPage()->isComplete(),
              "and opens on a first page whose defaults are buildable");
        exerciseControls(wizard.currentPage());
        check(true, "stage 1 survives every control being toggled");

        // Each of the five types has to reach a buildable state, including the
        // anisotropic one — whose sextic solver is the part most likely to
        // refuse a tensor the UI happily offered.
        auto* type =
            wizard.findChild<QComboBox*>(QStringLiteral("dislocationTypeCombo"));
        check(type != nullptr && type->count() == 5,
              "all five dislocation types are offered");
        if (type) {
            int buildable = 0;
            for (int i = 0; i < type->count(); ++i) {
                type->setCurrentIndex(i);
                wizard.restart();
                wizard.next();
                exerciseControls(wizard.currentPage());
                // exerciseControls walks every combo to its last entry, so put
                // the type back before building.
                type->setCurrentIndex(i);
                QString error;
                if (wizard.build(&error) && wizard.result()
                    && !wizard.result()->structure.empty())
                    ++buildable;
                else
                    std::printf("      (%s: %s)\n",
                                qPrintable(type->itemText(i)),
                                qPrintable(error));
            }
            check(buildable == type->count(),
                  "and every one of them produces a structure");
        }
    }

    std::printf("Solid Interface wizard:\n");
    {
        std::vector<PhaseSource> phases;
        phases.emplace_back(QStringLiteral("Al"), cubicCrystal());
        phases.emplace_back(QStringLiteral("Al (second phase)"), cubicCrystal());
        SolidInterfaceWizard wizard(phases);
        check(true, "constructs with two candidate phases");
        wizard.restart();
        check(wizard.currentPage() != nullptr
                  && wizard.currentPage()->isComplete(),
              "and opens on a complete first page");
        exerciseControls(wizard.currentPage());
        check(true, "stage 1 survives every control being toggled");

        auto* kind =
            wizard.findChild<QComboBox*>(QStringLiteral("interfaceKindCombo"));
        check(kind != nullptr && kind->count() == 5,
              "all five interface kinds are offered");
        if (kind) {
            int buildable = 0;
            for (int i = 0; i < kind->count(); ++i) {
                kind->setCurrentIndex(i);
                wizard.restart();
                wizard.next();
                // Keep the boxes small: this is a construction smoke test, not
                // a benchmark, and a 4x4x4 repeat of a 64-atom cell is already
                // four thousand atoms per grain.
                for (QSpinBox* spin : wizard.currentPage()
                                          ->findChildren<QSpinBox*>())
                    if (spin->maximum() >= 200)
                        spin->setValue(2);
                kind->setCurrentIndex(i);
                QString error;
                if (wizard.build(&error) && wizard.result()
                    && !wizard.result()->structure.empty())
                    ++buildable;
                else
                    std::printf("      (%s: %s)\n",
                                qPrintable(kind->itemText(i)),
                                qPrintable(error));
            }
            check(buildable == kind->count(),
                  "and every one of them produces a structure");
        }
    }

    // The database importer for Structure Containers. Constructing it must not
    // touch the network — it is opened long before anyone types a query, and
    // an importer that blocked on construction would be indistinguishable from
    // the freeze it was written to fix.
    {
        std::printf("DatabaseImportDialog:\n");
        DatabaseImportDialog dialog;
        check(dialog.entries().isEmpty(), "starts with nothing collected");
        // Searching with no query is a no-op rather than a request: the
        // Return key in an empty box must not become an API call.
        QMetaObject::invokeMethod(&dialog, "search");
        check(dialog.entries().isEmpty(), "an empty query searches nothing");
        exerciseControls(&dialog);
        check(dialog.entries().isEmpty(),
              "and survives every control being toggled without a key");
    }

    std::printf(failures == 0 ? "\nAll dialog construction checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
