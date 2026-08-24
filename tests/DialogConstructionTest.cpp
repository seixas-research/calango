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

#include "gui/TiIntegrandPlot.hpp"
#include "core/CalculatorConfig.hpp"
#include "core/Structure.hpp"
#include "gui/CvmComparisonWindow.hpp"
#include "gui/DsimResultsWindow.hpp"
#include "gui/DsimTernaryPlotWidget.hpp"
#include "gui/DsimWizard.hpp"
#include "gui/EgqcaPlotWidget.hpp"
#include "gui/FilmTimelineWidget.hpp"
#include "gui/DatabaseImportDialog.hpp"
#include "gui/GeometryConstraintsDialog.hpp"
#include "gui/GoMdmcLiveTabs.hpp"
#include "gui/GrapheneOxideMdmcWizard.hpp"
#include "gui/MdmcSummaryDialog.hpp"
#include "gui/WorkflowReportDialog.hpp"
#include "gui/GrapheneOxideWizard.hpp"
#include "gui/HubbardParametersDialog.hpp"
#include "gui/OpticsPlotStyleDialog.hpp"
#include "gui/OverlayEditDialog.hpp"
#include "gui/CddWizard.hpp"
#include "gui/XasResultsWindow.hpp"
#include "gui/NonlinearOpticsResultsWindow.hpp"
#include "gui/EnergyDiagramViewer.hpp"
#include "gui/WavefunctionsWizard.hpp"
#include "gui/WavefunctionsResultsViewer.hpp"
#include "gui/XasWizard.hpp"
#include "gui/EditVolumetricRenderDialog.hpp"
#include "gui/IsovalueHistogramWidget.hpp"
#include "gui/GuiUtils.hpp"
#include "gui/CutoffConvergenceWizard.hpp"
#include "gui/FermiSurfaceDialog.hpp"
#include "gui/KpointsConvergenceWizard.hpp"
#include "gui/MlwfSourceSelector.hpp"
#include "gui/TopologyDialog.hpp"
#include "gui/MaceTrainerDialog.hpp"
#include "gui/MolecularDesignDialog.hpp"
#include "gui/MoleculeCanvas.hpp"
#include "gui/ElectronicBandsWizard.hpp"
#include "gui/DislocationWizard.hpp"
#include "gui/LiquidInterfaceWizard.hpp"
#include "gui/SolidInterfaceWizard.hpp"
#include "gui/StructureEditorDialog.hpp"
#include "gui/MagneticSpaceGroupDialog.hpp"
#include "gui/RandomNoiseViewer.hpp"
#include "gui/RandomNoiseWizard.hpp"
#include "gui/SimulationWizardBase.hpp"
#include "gui/GeometryOptimizationWizard.hpp"
#include "gui/MolecularDynamicsWizard.hpp"
#include "gui/ThermodynamicIntegrationWizard.hpp"
#include "gui/ThermodynamicIntegrationResults.hpp"
#include "gui/SinglePointWizard.hpp"
#include "gui/NonlinearOpticsWizard.hpp"
#include "core/CalphadModel.hpp"
#include "core/LocaleSafeNumber.hpp"
#include "core/PdbxFile.hpp"
#include "core/TdbExpression.hpp"
#include "core/PhononThermodynamics.hpp"
#include "core/TdbDatabase.hpp"
#include "gui/CalphadDialog.hpp"
#include "gui/PhaseDiagramWindow.hpp"
#include "gui/TdbGeneratorDialog.hpp"
#include "gui/HpcPanel.hpp"
#include "gui/PreferencesDialog.hpp"
#include "gui/SettingsManager.hpp"
#include "gui/ShortcutRegistry.hpp"
#include "gui/ViewportWidget.hpp"
#include "gui/OpticsWizard.hpp"
#include "gui/WannierWizard.hpp"
#include "gui/WannierRunLoader.hpp"
#include "gui/BoltzmannTransportDialog.hpp"
#include "gui/CrpaDialog.hpp"
#include "gui/ProcessManagerPanel.hpp"
#include "gui/RamanIrWizard.hpp"
#include "gui/Defect2dWizard.hpp"
#include "gui/TwoDBandsWizard.hpp"
#include "gui/VibrationalAnalysisDialog.hpp"
#include "python_bridge/PythonEngine.hpp"
#include "render/StructureRenderer.hpp"

#include <QPainter>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDir>
#include <QSettings>
#include <QFile>
#include <QLabel>
#include <QComboBox>
#include <QDockWidget>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QKeySequenceEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTabWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <QTemporaryDir>

#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

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

/// A synthetic "Graphene Oxide Build" for GrapheneOxideMdmcWizard::
/// setInputBuild() — geometry is irrelevant here (every atom sits at the
/// origin), only the persisted classification fields
/// GrapheneOxideMdmcWizard reads matter: `basal` + `edge` carbon atoms (the
/// "edge" field), of which the first `min(groups, basal + edge)` carry a
/// distinct "go_group_id" (so the wizard counts exactly `groups` functional
/// groups), and — when `antiposition` — the first two of those share a
/// "go_pair_id".
calango::core::Structure makeGoBuildStructure(int groups, int basal, int edge,
                                              bool antiposition)
{
    using calango::core::Atom;
    using calango::core::Structure;
    using calango::core::Vec3;

    Structure s;
    const int total = basal + edge;
    for (int i = 0; i < total; ++i) {
        Atom atom;
        atom.atomicNumber = 6; // carbon
        atom.position = Vec3{0.0, 0.0, 0.0};
        s.addAtom(atom);
    }

    std::vector<double> edgeField(static_cast<std::size_t>(total), 0.0);
    for (int i = basal; i < total; ++i)
        edgeField[static_cast<std::size_t>(i)] = 1.0;

    std::vector<double> groupField(static_cast<std::size_t>(total), -1.0);
    std::vector<double> groupIdField(static_cast<std::size_t>(total), -1.0);
    std::vector<double> pairIdField(static_cast<std::size_t>(total), -1.0);
    const int marked = std::min(groups, total);
    for (int i = 0; i < marked; ++i) {
        groupField[static_cast<std::size_t>(i)] = 1.0; // Group::Hydroxyl
        groupIdField[static_cast<std::size_t>(i)] = static_cast<double>(i);
    }
    if (antiposition && marked >= 2) {
        pairIdField[0] = 0.0;
        pairIdField[1] = 0.0;
    }

    s.setScalarField("edge", std::move(edgeField));
    s.setScalarField("go_group", std::move(groupField));
    s.setScalarField("go_group_id", std::move(groupIdField));
    s.setScalarField("go_pair_id", std::move(pairIdField));
    return s;
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
              "periodic sheet: the H/O slider's 1:2 default gave both basal groups");

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

        // The wiring risk this checkbox actually carries: that ticking it in
        // the wizard reaches config.hydroxylAntiposition at all. The
        // placement invariants themselves (pairing, opposite faces) are the
        // core builder's own test's job; this only has to show the wizard's
        // checkbox and the build it produces agree.
        GrapheneOxideWizard antiposWizard;
        // Found by its label text rather than an objectName -- this
        // checkbox, like the other stage-2 toggles, is not named for
        // findChild lookup; only the four ratio controls are.
        auto* antiposCheck = [&]() -> QCheckBox* {
            for (QCheckBox* box : antiposWizard.findChildren<QCheckBox*>())
                if (box->text().contains(QStringLiteral("antiposition"),
                                          Qt::CaseInsensitive))
                    return box;
            return nullptr;
        }();
        check(antiposCheck != nullptr,
              "the antiposition checkbox is present in the wizard");
        if (antiposCheck) {
            antiposCheck->setChecked(true);
            if (auto* basalOxidationBox = antiposWizard.findChild<QDoubleSpinBox*>(
                    QStringLiteral("basalOxidationBox")))
                basalOxidationBox->setValue(0.3); // enough basal oxygen to pair
            for (int stage = 0; stage < kGrapheneOxideStages; ++stage)
                QMetaObject::invokeMethod(&antiposWizard, "goNext");
            const auto antiposReport = antiposWizard.report();
            check(antiposReport.placedFor(Group::Hydroxyl) > 0,
                  "wizard-driven antiposition build placed hydroxyls");
            check(antiposReport.placedFor(Group::Hydroxyl) % 2 == 0,
                  "wizard-driven antiposition build placed them in pairs "
                  "(even count) -- config.hydroxylAntiposition reached the "
                  "builder");
        }
    }

    std::printf("Graphene Oxide MDMC wizard:\n");
    {
        // A SimulationWizardBase resolves its interpreter through
        // PythonEngine, which asserts rather than lazily constructing. Scoped
        // so the runtime is finalized before the app exits.
        calango::pybridge::PythonEngine python;
        GrapheneOxideMdmcWizard wizard;
        check(true, "constructs");
        // setInputBuild runs BEFORE the pages it writes into may exist in a
        // future edit, and it is called by the host immediately after
        // construction — exactly the ordering that has crashed wizards here.
        wizard.setInputBuild(makeGoBuildStructure(24, 60, 18, false));
        check(true, "accepts a build without dereferencing a missing page");
        // A flake has no cell, so constant pressure must not be selectable.
        // Offering NPT on a molecule is meaningless, not merely wasteful.
        const auto* ensemble = wizard.findChildren<QComboBox*>().isEmpty()
            ? nullptr
            : wizard.findChildren<QComboBox*>().first();
        (void)ensemble;

        // The defaults this page seeds must be the SAME defaults
        // core::GrapheneOxideMdmcConfig carries — the two are written out
        // separately (a setValue() here, an initializer there) and nothing
        // in the compiler makes them agree. Read from the generated script,
        // which is the only place both of them meet, and read BEFORE
        // exerciseControls() below nudges every spin box off its default.
        {
            const QString fresh = wizard.script();
            const auto pins = {
                std::make_pair("md_steps = 5", "MD steps per cycle"),
                std::make_pair("timestep_fs = 0.5", "MD timestep, in fs"),
                std::make_pair("equilibration_steps = 10",
                               "initial equilibration steps"),
                std::make_pair("viewport_every = 1",
                               "live-view frame interval, in cycles"),
                std::make_pair("stream_md_frames = True",
                               "the dynamics between MC steps is always-on"),
                std::make_pair("hydroxyl_antiposition = True",
                               "hydroxyls antiposition"),
            };
            for (const auto& [needle, what] : pins)
                check(fresh.contains(QLatin1String(needle)),
                      std::string("the page's default for ") + what
                          + " is the config struct's ('" + needle + "')");
        }
        exerciseControls(&wizard);
        check(true, "survives every control being exercised");
        wizard.setInputBuild(makeGoBuildStructure(0, 60, 18, false));
        check(true, "handles a build with no groups to move");

        // Hydroxyls antiposition is now a real CONTROL on this page, checked
        // by default — it used to be inherited state read off the input
        // build's "go_pair_id" field and shown only as prose. Both halves
        // are checked here: the control exists, is checked, and reaches the
        // script; and the prose that says what the input build actually
        // CONTAINS is still there beside it, so the two can be compared.
        //
        // `exerciseControls()` toggles every checkbox exactly twice, so it
        // leaves each one as it found it — a default checked here is still
        // checked when the script is generated below.
        const auto antipositionBox = [](const GrapheneOxideMdmcWizard& w) {
            QCheckBox* found = nullptr;
            for (QCheckBox* box : w.findChildren<QCheckBox*>())
                if (box->text().contains(QStringLiteral("antiposition"),
                                         Qt::CaseInsensitive))
                    found = box;
            return found;
        };

        GrapheneOxideMdmcWizard antiposWizard;
        antiposWizard.setInputBuild(
            makeGoBuildStructure(10, 40, 0, /*antiposition=*/true));
        bool labelMentionsAntiposition = false;
        for (const QLabel* label : antiposWizard.findChildren<QLabel*>())
            if (label->text().contains(QStringLiteral("antiposition"),
                                       Qt::CaseInsensitive))
                labelMentionsAntiposition = true;
        check(labelMentionsAntiposition,
              "the substrate summary still says what the input build "
              "contains");
        QCheckBox* antiposOn = antipositionBox(antiposWizard);
        check(antiposOn != nullptr,
              "hydroxyls antiposition is a checkbox on the settings page");
        check(antiposOn && antiposOn->isChecked(), "checked by default");
        exerciseControls(&antiposWizard);
        check(antiposWizard.script().contains(
                  QStringLiteral("hydroxyl_antiposition = True")),
              "and the flag reaches the generated script");

        // The control is the user's, not the input build's: a build with no
        // antiposition pairs still gets the checkbox checked, and the flag
        // still reaches the script. That is safe because the emitted
        // pairing bootstrap recovers pairs from the GEOMETRY - it finds
        // none here and every hydroxyl stays an ordinary single.
        GrapheneOxideMdmcWizard plainWizard;
        plainWizard.setInputBuild(
            makeGoBuildStructure(10, 40, 0, /*antiposition=*/false));
        QCheckBox* plainBox = antipositionBox(plainWizard);
        check(plainBox && plainBox->isChecked(),
              "checked by default on a build that has no pairs too - the "
              "control is not derived from the input any more");
        check(plainWizard.script().contains(
                  QStringLiteral("hydroxyl_antiposition = True")),
              "and that default reaches the script");

        GrapheneOxideMdmcWizard offWizard;
        offWizard.setInputBuild(
            makeGoBuildStructure(10, 40, 0, /*antiposition=*/false));
        QCheckBox* offBox = antipositionBox(offWizard);
        check(offBox != nullptr, "the checkbox is there to be turned off");
        if (offBox)
            offBox->setChecked(false);
        exerciseControls(&offWizard);
        check(offWizard.script().contains(
                  QStringLiteral("hydroxyl_antiposition = False")),
              "unchecking it reaches the script, exactly as before this "
              "option existed");

        // Retired with the always-on decision: the dynamics between MC
        // steps is streamed unconditionally now, throttled by the one
        // viewport interval, so a second on/off for it must not come back.
        bool hasStreamMdCheckbox = false;
        for (const QCheckBox* box : offWizard.findChildren<QCheckBox*>())
            if (box->text().contains(QStringLiteral("dynamics"),
                                     Qt::CaseInsensitive))
                hasStreamMdCheckbox = true;
        check(!hasStreamMdCheckbox,
              "no separate \"also show the dynamics\" checkbox - the "
              "interval is the only knob");
    }

    // GO-MDMC used to open FOUR viewport tabs per local run: the two live
    // tabs it is supposed to, plus its working input copy (opened only so
    // stageJob() -- which stages whatever document is current -- would find
    // it) and the generic stdout-streamed trajectory tab every frame-
    // producing run gets. Both extras spent the opening of a run showing the
    // unrelaxed input geometry, and the streamed one was a strictly worse
    // duplicate of "All Structures" besides.
    //
    // MainWindow itself is not built into any test binary, so what is pinned
    // here is the CONTRACT the three tab-opening sites were rewritten
    // against -- the list of views a run creates, and the predicate that
    // suppresses the streamed tab. gui/GoMdmcLiveTabs.hpp is deliberately the
    // only place either answer is written down, so a regression has to go
    // through this.
    std::printf("GO-MDMC live viewport tabs:\n");
    {
        const auto views = calango::gui::goMdmcLiveViews();
        check(views.size() == 2,
              "a GO-MDMC run creates exactly two viewport tabs (it was four)");
        if (views.size() == 2) {
            check(views[0].fileName
                      == QStringLiteral("mdmc_all_structures.extxyz"),
                  "the first follows mdmc_all_structures.extxyz");
            check(views[0].title
                      == QStringLiteral("GO-MDMC / All Structures"),
                  "titled \"GO-MDMC / All Structures\"");
            check(views[1].fileName
                      == QStringLiteral("accepted_structures.extxyz"),
                  "the second follows accepted_structures.extxyz");
            check(views[1].title == QStringLiteral("GO-MDMC / Accepted"),
                  "titled \"GO-MDMC / Accepted\"");
        }
        // Same file names the generated script writes. Read off the config
        // rather than retyped, so renaming one and not the other fails here.
        calango::core::GrapheneOxideMdmcConfig mdmcConfig;
        check(QString::fromStdString(mdmcConfig.trajectory)
                  == views[1].fileName,
              "and the accepted-tab file name IS the script's own "
              "config.trajectory, not a second copy of the literal");

        check(!calango::gui::opensStreamedTrajectoryTab(
                  calango::gui::goMdmcTaskLabel()),
              "GO-MDMC takes no stdout-streamed trajectory tab");
        for (const char* other : {"Molecular Dynamics", "Geometry Optimization",
                                  "NEB", "Local calculation"}) {
            check(calango::gui::opensStreamedTrajectoryTab(
                      QString::fromLatin1(other)),
                  std::string("every other frame-producing run still does (")
                      + other + ")");
        }
    }

    std::printf("MDMC Summary dialog:\n");
    {
        // The counters moved out of the Results dock into a window of their
        // own, opened when a run finishes and by double-clicking its row in
        // the Processes panel.
        calango::gui::MdmcSummaryDialog summary;
        check(summary.processId() == -1,
              "starts bound to no process");
        check(summary.runSummaryTable() != nullptr
                  && summary.moveBreakdownTable() != nullptr,
              "carries both tables MainWindow paints into");
        check(summary.runSummaryTable()->columnCount() == 2,
              "the run block is two columns (quantity, value)");
        check(summary.moveBreakdownTable()->columnCount() == 4,
              "the per-move-kind block keeps its four");
        check(!summary.isModal(),
              "modeless -- the numbers are meant to sit beside the viewport");

        // Binding clears whatever the previous run left behind, so a window
        // re-pointed at another process can never show a mix of the two.
        summary.moveBreakdownTable()->setRowCount(3);
        summary.runSummaryTable()->setRowCount(5);
        summary.bindProcess(7, QStringLiteral("GO-MDMC"), true);
        check(summary.processId() == 7, "binds to a process by id");
        check(summary.runSummaryTable()->rowCount() == 0
                  && summary.moveBreakdownTable()->rowCount() == 0,
              "and clears both tables, so no other run's numbers survive it");
        summary.setRunning(false);
        check(summary.processId() == 7,
              "a finished run keeps its binding -- only the subtitle changes");
        exerciseControls(&summary);
        check(true, "survives every control being toggled");
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
            // unmodified run uses.
            check(engine->currentIndex() == 0,
                  "the engine combo opens on its first entry");
            check(engine->findData(static_cast<int>(
                      calango::core::CalculatorKind::Gpaw)) == 0,
                  "GPAW is first, and therefore the default engine");
            check(engine->itemData(engine->currentIndex()).toInt()
                      == static_cast<int>(calango::core::CalculatorKind::Gpaw),
                  "so an untouched wizard runs GPAW");

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

            // DFTB+: its "DFTB settings" group must appear when it is
            // selected (see SimulationWizardBase::buildDftbGroup()).
            const int dftb = engine->findData(static_cast<int>(
                calango::core::CalculatorKind::DftbPlus));
            check(dftb >= 0, "DFTB+ is offered as an engine");
            if (dftb >= 0) {
                engine->setCurrentIndex(dftb);
                const auto boxes = wizard.findChildren<QGroupBox*>();
                const auto named = std::find_if(
                    boxes.begin(), boxes.end(), [](const QGroupBox* box) {
                        return box->title().contains(QStringLiteral("DFTB"));
                    });
                check(named != boxes.end(), "a DFTB settings group exists");
                if (named != boxes.end())
                    check((*named)->isVisibleTo(&wizard),
                          "and is shown when DFTB+ is the selected engine");
            }
        }
    }

    // The Random Noise dialog is a pure in-process generator (no script, no
    // job) that publishes its ensemble from a button rather than on OK — the
    // construction check is that the button works and the ensemble it
    // publishes has the shape callers rely on: frame 0 untouched, later
    // frames displaced, and — with the linear ramp on — displacement growing
    // monotonically from frame to frame.
    std::printf("Random Noise dialog:\n");
    {
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

        // Linear ramp: find the schedule combo, switch it to "Linear ramp",
        // regenerate, and check the black-box shape the wizard-level feature
        // promises: frame 0 exactly unperturbed, and the last (full-
        // amplitude) frame displaced well past the first (smallest-
        // amplitude) perturbed frame. Comparing only the two endpoints —
        // rather than every adjacent pair — is deliberate: each frame draws
        // its own INDEPENDENT random displacement, so with 20 ramped frames
        // (a ~5% amplitude step apart) a strict pairwise comparison would be
        // a coin flip at points and flake; the exact per-frame FORMULA is
        // pinned deterministically in NoiseTest.cpp instead, where no
        // randomness is involved. The ~20x amplitude gap between the first
        // and last ramped frame here is not.
        const auto combos = wizard.findChildren<QComboBox*>();
        const auto rampCombo = std::find_if(
            combos.begin(), combos.end(), [](const QComboBox* combo) {
                return combo->count() == 2
                    && combo->itemText(1).contains(QStringLiteral("ramp"));
            });
        check(rampCombo != combos.end(), "offers an amplitude-schedule combo");
        if (rampCombo != combos.end()) {
            (*rampCombo)->setCurrentIndex(1);
            generated = 0;
            (*generate)->click();
            check(generated > 2, "the ramped ensemble still generates");

            const auto rms = [&](std::size_t index) {
                const auto& frame = wizard.frames()[index]->atoms();
                const auto& source = reference->atoms();
                double sum = 0.0;
                for (std::size_t i = 0; i < frame.size(); ++i)
                    sum += (frame[i].position - source[i].position).norm();
                return sum;
            };
            if (wizard.frames().size() >= 3) {
                check(rms(0) < 1e-12,
                      "ramped frame 0 is still exactly the unperturbed "
                      "reference — the ramp's zero-noise endpoint");
                check(rms(wizard.frames().size() - 1) > 3.0 * rms(1),
                      "and the full-amplitude last frame is displaced well "
                      "past the smallest-amplitude first perturbed frame");
            }
        }
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

    // Task 1 (session N+1): "Geometry Optimization Setup" with VASP
    // selected had grown too tall for the screen once the internal-
    // optimizer parameters (IBRION/NSW/EDIFFG/ISIF/SAXIS/LMAXMIX/baseline)
    // joined the shared VASP settings group. The fix is structural
    // (SimulationWizardBase::wrapInScrollArea(), applied to every stage
    // page in buildUi(), not a VASP-only patch) — pinned here on the
    // wizard that actually reported the bug, with the worst-case VASP
    // configuration (internal relaxation driver, which is what surfaces
    // the extra IBRION/ISIF/EDIFFG/NSW row on top of the always-shown
    // fields) selected.
    std::printf("Geometry Optimization Setup: VASP page fits the screen "
               "(Task 1):\n");
    {
        calango::pybridge::PythonEngine python;
        GeometryOptimizationWizard wizard;
        check(true, "constructs");
        // Unlike the Cutoff/K-points Convergence wizards above (one combo
        // box, full stop), this one has several by Stage 2 (optimizer
        // algorithm, cell filter, …) — found() by TYPE alone would as
        // likely hit one of those as the actual engine combo, so identify
        // it by what only it can offer: VASP as an item.
        QComboBox* engine = nullptr;
        for (QComboBox* combo : wizard.findChildren<QComboBox*>())
            if (combo->findData(static_cast<int>(
                    calango::core::CalculatorKind::Vasp)) >= 0) {
                engine = combo;
                break;
            }
        check(engine != nullptr, "has an engine combo that offers VASP");
        if (engine)
            engine->setCurrentIndex(engine->findData(
                static_cast<int>(calango::core::CalculatorKind::Vasp)));
        // The internal-relaxation driver is the worst case for height: it
        // is what surfaces the IBRION/ISIF/EDIFFG/NSW row on top of every
        // other VASP field (see SimulationWizardBase::updateVaspRows()).
        QComboBox* driverCombo = nullptr;
        for (QComboBox* combo : wizard.findChildren<QComboBox*>())
            for (int i = 0; i < combo->count(); ++i)
                if (combo->itemText(i).contains(
                        QStringLiteral("internal relaxation"))) {
                    driverCombo = combo;
                    break;
                }
        check(driverCombo != nullptr, "has the relaxation-driver combo");
        if (driverCombo)
            driverCombo->setCurrentIndex(
                static_cast<int>(calango::core::VaspRelaxDriver::Vasp));

        wizard.show(); // realize the layout — sizes are 0 before this
        // 900x640 is the wizard's own base size (SimulationWizardBase::
        // buildUi()); the bound here is generous on top of that only for
        // window-chrome slack, not for the VASP group's own content —
        // which is exactly what must NOT reach this height any more.
        check(wizard.height() <= 700,
              "the dialog itself stays a fixed, screen-safe height instead "
              "of growing to fit the VASP group's content");

        // Identified by containing the ENGINE combo specifically (found
        // above), not just "any" combo box — Stage 2 (Relaxation Settings)
        // is wrapped too and has combo boxes of its own (the optimizer
        // algorithm), so "the first scroll area with a combo box in it"
        // would as likely match the wrong stage.
        QScrollArea* calculatorScroll = nullptr;
        for (QScrollArea* scroll : wizard.findChildren<QScrollArea*>()) {
            if (!scroll->widget() || !engine)
                continue;
            for (QWidget* w = engine; w; w = w->parentWidget())
                if (w == scroll->widget()) {
                    calculatorScroll = scroll;
                    break;
                }
            if (calculatorScroll)
                break;
        }
        check(calculatorScroll != nullptr,
              "the Calculator Settings page (holding the engine combo) is "
              "wrapped in a scroll area");
        // Concrete evidence, not just an inference: the page's OWN natural
        // height (were it not scrolling) is what the dialog would have had
        // to grow to before this fix — printed so a regression that erodes
        // the margin again is visible here even if the 700px bound above
        // still happens to hold.
        if (calculatorScroll && calculatorScroll->widget())
            std::printf(
                "    VASP Calculator Settings page's own natural size: "
                "%dx%d (wizard itself stays <= 700 tall regardless)\n",
                calculatorScroll->widget()->minimumSizeHint().width(),
                calculatorScroll->widget()->minimumSizeHint().height());

        // The LAST row of the VASP group ("Extra INCAR tags") is the field
        // most likely to have gone unreachable before this fix — find it by
        // its distinctive placeholder text and confirm it is actually
        // reachable through the scroll area, not merely constructed.
        QWidget* extraIncarEdit = nullptr;
        for (QPlainTextEdit* edit : wizard.findChildren<QPlainTextEdit*>())
            if (edit->placeholderText().contains(QStringLiteral("NBANDS"))) {
                extraIncarEdit = edit;
                break;
            }
        check(extraIncarEdit != nullptr, "the Extra INCAR tags field exists");
        if (extraIncarEdit && calculatorScroll) {
            bool insideScrollArea = false;
            for (QWidget* w = extraIncarEdit; w; w = w->parentWidget())
                if (w == calculatorScroll->widget()) {
                    insideScrollArea = true;
                    break;
                }
            check(insideScrollArea,
                  "and it is a descendant of the scrolled widget, so it "
                  "stays reachable regardless of how tall the VASP group "
                  "grows");
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
            // "Direct LCAO", not "Direct". It is GPAW's DirectLCAO and it is
            // the only solver that runs in LCAO mode; the bare label read as a
            // general-purpose exact diagonalization and invited pairing it
            // with a plane-wave run, which aborts inside GPAW.
            check((*solver)->itemText(1) == QStringLiteral("RMM-DIIS")
                      && (*solver)->itemText(2) == QStringLiteral("CG")
                      && (*solver)->itemText(3) == QStringLiteral("Direct LCAO"),
                  "capitalized, in the listed order");
            check((*solver)->toolTip().contains(QStringLiteral("LCAO mode")),
                  "and the tooltip states that the choice is tied to the "
                  "basis rather than free");
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

        // Task 2's investigation: a core-hole calculation needs a PAW
        // dataset that no ordinary ground state ever used, so it cannot
        // restart from one — the wizard takes the raw structure directly
        // (like Single Point / Geometry Optimization) rather than inheriting
        // a baseline .gpw (like Optics / GW / Wannier). Pinned two ways: the
        // script itself reads the staged structure file directly and stages
        // no baseline, and its OWN ground state is genuinely converged with
        // the settings the wizard's Calculator & Convergence page collected
        // rather than a baseline's.
        const QString script = wizard.script();
        check(script.contains(QStringLiteral("atoms = read(")),
              "the script reads the staged structure directly, not a "
              "baseline");
        check(!script.contains(QStringLiteral("baseline_1"))
                  && !script.contains(QStringLiteral("GPAW(restart="))
                  && !script.contains(QStringLiteral(".gpw', mode='all')\n"
                                                     "atoms = ")),
              "and stages no parent .gpw to restart from");
        check(script.contains(QStringLiteral("convergence={"))
                  && script.contains(QStringLiteral("occupations={")),
              "its own ground state is converged with the wizard's own SCF "
              "and smearing settings, not left on GPAW's silent defaults");
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

        // Task 1: brought up to the Optics viewer's own standard — the same
        // "Customize Appearance…" dialog, and Export CSV…/Export Image…
        // buttons in the same place and words Optics uses for them.
        const auto buttons = window.findChildren<QPushButton*>();
        const auto hasButton = [&buttons](const QString& text) {
            return std::any_of(buttons.begin(), buttons.end(),
                               [&text](const QPushButton* b) {
                                   return b->text() == text;
                               });
        };
        check(hasButton(QStringLiteral("Customize Appearance…")),
              "offers the same appearance dialog Optics does");
        check(hasButton(QStringLiteral("Export CSV…")),
              "offers Export CSV…, worded exactly like Optics's own button");
        check(hasButton(QStringLiteral("Export Image…")),
              "and Export Image…, likewise");

        // The energy window, mirroring Optics's Range: row.
        const auto spins = window.findChildren<QDoubleSpinBox*>();
        check(std::any_of(spins.begin(), spins.end(),
                          [](const QDoubleSpinBox* s) {
                              return s->specialValueText()
                                  == QStringLiteral("auto");
                          }),
              "the energy window defaults to \"auto\" like Optics's Range:");

        // Live broadening: enabled here because the staged file above
        // carries stick_energy_eV/stick_isotropic; opens at the run's own
        // fwhm_eV (0.5) exactly like Optics's η control opens at its own
        // eta_eV.
        auto* broadeningSpin =
            window.findChild<QDoubleSpinBox*>(QStringLiteral("xasBroadening"));
        check(broadeningSpin != nullptr, "the broadening control is present");
        if (broadeningSpin) {
            check(broadeningSpin->isEnabled(),
                  "enabled: this run recorded its own transitions");
            check(std::abs(broadeningSpin->value() - 0.5) < 1e-9,
                  "and opens at the run's own fwhm_eV");
        }
    }

    std::printf("Nonlinear Optics results window:\n");
    {
        // Task: brought up to Optics's own standard — the same
        // "Customize Appearance…" dialog and Export Image… button, worded
        // exactly like Optics/XAS use them, on top of the CSV export this
        // window already had.
        const QString dir =
            QDir::temp().filePath(QStringLiteral("calango_nlopt_appearance"));
        QDir().mkpath(dir);
        QFile file(dir + QStringLiteral("/nlopt.json"));
        check(file.open(QIODevice::WriteOnly), "a results file can be staged");
        file.write(R"({"formula":"GaAs","eta_eV":0.05,"gauge":"lg",
                       "eshift_eV":0.0,"centrosymmetric":false,
                       "energy_eV":[0.0,1.0,2.0],
                       "shg":{"xyz":{"energy_eV":[0.0,1.0,2.0],
                                     "chi2_re_pm_V":[0.0,1.0,2.0],
                                     "chi2_im_pm_V":[0.0,0.5,1.0]}}})");
        file.close();

        NonlinearOpticsResultsWindow window;
        check(window.loadResults(dir), "parses a well-formed nlopt.json");

        const auto buttons = window.findChildren<QPushButton*>();
        const auto hasButton = [&buttons](const QString& text) {
            return std::any_of(buttons.begin(), buttons.end(),
                               [&text](const QPushButton* b) {
                                   return b->text() == text;
                               });
        };
        check(hasButton(QStringLiteral("Customize Appearance…")),
              "offers the same appearance dialog Optics does");
        check(hasButton(QStringLiteral("Export Image…")),
              "and Export Image…, worded exactly like Optics/XAS");
        check(hasButton(QStringLiteral("Export CSV…")),
              "still offers the CSV export this window already had");

        // Clicking Customize Appearance… must open a real
        // OpticsPlotStyleDialog — the same reused struct/dialog, not a
        // stand-in — and it must not crash on a window whose plot has no
        // reference-slider quirks to trip over (unlike the isovalue
        // histogram, this is a plain SpectrumPlotWidget).
        QPushButton* appearanceButton = nullptr;
        for (QPushButton* b : buttons)
            if (b->text() == QStringLiteral("Customize Appearance…"))
                appearanceButton = b;
        check(appearanceButton != nullptr,
              "the appearance button was found for the click below");
        if (appearanceButton) {
            appearanceButton->click();
            auto* dialog = window.findChild<OpticsPlotStyleDialog*>();
            check(dialog != nullptr,
                  "clicking it opens an OpticsPlotStyleDialog — the same "
                  "styling code Optics and XAS already use, not a parallel "
                  "one built for this window");
            if (dialog)
                dialog->close();
        }

        check(!window.loadResults(QStringLiteral("/nonexistent/nlopt.json")),
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

        // REGRESSION: the isovalue histogram bars were QPalette::Mid — a
        // low-contrast border/disabled tone that read as barely-there
        // against QPalette::Base. Bars are now QPalette::Text at partial
        // alpha; this pins the actual rendered contrast rather than which
        // QPalette role the code happens to name, so a future edit back
        // toward a washed-out fill is caught even if it still compiles.
        auto* histogram = dialog.findChild<IsovalueHistogramWidget*>();
        check(histogram != nullptr, "the isosurface page has a histogram");
        if (histogram) {
            // A deterministic triangular spread, not random data — occupies
            // most of the range so bars are guaranteed at several bins
            // without depending on an RNG's seed.
            std::vector<double> values;
            for (int i = 1; i <= 300; ++i)
                for (int j = 0; j < i; ++j)
                    values.push_back(static_cast<double>(i) / 300.0);
            dialog.setFieldHistogram(values, 0.0, 1.0);
            histogram->setCurrentValue(0.02); // marker off in a low-density
                                              // corner, away from the bars
                                              // sampled below

            histogram->resize(400, 56);
            QImage image(400, 56, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            {
                QPainter painter(&image);
                histogram->render(&painter, QPoint(0, 0));
            }
            const QColor background = image.pixelColor(2, 2);

            // Grayish pixels only, so the blue marker (a distinct hue) can't
            // masquerade as bar contrast — isolates exactly what the bug
            // report was about.
            int maxDelta = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QColor c = image.pixelColor(x, y);
                    if (std::abs(c.red() - c.green()) >= 20
                        || std::abs(c.green() - c.blue()) >= 20
                        || std::abs(c.red() - c.blue()) >= 20)
                        continue; // not grayish -> the marker, skip it
                    const int delta = std::abs(c.red() - background.red())
                        + std::abs(c.green() - background.green())
                        + std::abs(c.blue() - background.blue());
                    maxDelta = std::max(maxDelta, delta);
                }
            }
            check(maxDelta > 300,
                  "the histogram bars render with real contrast against the "
                  "background (max grayish-pixel delta " + std::to_string(maxDelta)
                      + "/765) — QPalette::Mid measured well under this");
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

        // Task 3: VASP, Quantum ESPRESSO and SIESTA, reached through this
        // wizard's OWN engine picker (the standard one is hidden, same as
        // Electronic Structure) — switching it must swap which group
        // (baseline restart vs. self-contained SCF) is visible AND change
        // what the previewed script actually emits, not just which controls
        // show. A wizard whose extras page ignores the engine change would
        // pass the "has a script preview" check above and still generate
        // the same GPAW script no matter what the user picked.
        const auto combos = wizard.findChildren<QComboBox*>();
        const auto engineCombo = std::find_if(
            combos.begin(), combos.end(), [](const QComboBox* combo) {
                return combo->count() == 4
                    && combo->itemText(0) == QStringLiteral("GPAW");
            });
        check(engineCombo != combos.end(), "has its own engine picker");
        if (engineCombo != combos.end()) {
            auto* preview = wizard.findChild<QPlainTextEdit*>();

            const auto selectEngine = [&](const QString& label) {
                (*engineCombo)->setCurrentIndex((*engineCombo)->findText(label));
            };
            const auto groupVisible = [&](const QString& title) {
                for (const QGroupBox* g : wizard.findChildren<QGroupBox*>())
                    if (g->title() == title)
                        return g->isVisibleTo(&wizard);
                return false;
            };

            wizard.show(); // isVisibleTo() needs the page realized

            selectEngine(QStringLiteral("Quantum ESPRESSO"));
            check(groupVisible(QStringLiteral("Self-contained SCF"))
                      && !groupVisible(QStringLiteral("Baseline SCF Density")),
                  "Quantum ESPRESSO shows the self-contained SCF group, not "
                  "the baseline one");
            if (preview)
                check(preview->toPlainText().contains(
                          QStringLiteral("\"calculation\": \"scf\"")),
                      "and the preview switches to the self-contained chain");

            selectEngine(QStringLiteral("SIESTA"));
            check(groupVisible(QStringLiteral("Self-contained SCF"))
                      && !groupVisible(QStringLiteral("Baseline SCF Density")),
                  "SIESTA shows the same self-contained group");
            if (preview)
                check(preview->toPlainText().contains(
                          QStringLiteral("bandpath=_grid_path")),
                      "and the preview reflects SIESTA's own chaining");

            selectEngine(QStringLiteral("VASP"));
            check(!groupVisible(QStringLiteral("Self-contained SCF"))
                      && groupVisible(QStringLiteral("Baseline SCF Density")),
                  "VASP switches back to the baseline group");
            if (preview)
                check(preview->toPlainText().contains(
                          QStringLiteral("icharg=11")),
                      "and the preview reflects VASP's ICHARG=11 restart");

            selectEngine(QStringLiteral("GPAW"));
            check(groupVisible(QStringLiteral("Baseline SCF Density"))
                      && !groupVisible(QStringLiteral("Self-contained SCF")),
                  "and back to GPAW restores the baseline group");
        }
        exerciseControls(&wizard);
        check(true, "survives every control being exercised, on every engine");
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

    // The CALPHAD dialog. Its controls do not exist until a database is
    // loaded — they are built FROM the file — so construction alone proves
    // very little and the test loads one.
    std::printf("CALPHAD dialog:\n");
    {
        CalphadDialog dialog;
        dialog.show();
        check(dialog.selectedElements().isEmpty(),
              "nothing is selectable before a database is loaded");

        // Refusing a non-database must not be a crash, and must leave the
        // dialog usable.
        check(!dialog.loadDatabaseText(QStringLiteral("POSCAR\n1.0\n"),
                                       QStringLiteral("bad.tdb")),
              "a file that is not a database is refused");

        const QString feCr = QStringLiteral(
            "$ test database\n"
            " ELEMENT VA  VACUUM   0 0 0 !\n"
            " ELEMENT FE  BCC_A2   55.847 4489 27.28 !\n"
            " ELEMENT CR  BCC_A2   51.996 4050 23.54 !\n"
            " PHASE LIQUID % 1 1.0 !\n"
            " CONSTITUENT LIQUID : FE,CR : !\n"
            " PHASE BCC_A2 % 2 1 3 !\n"
            " CONSTITUENT BCC_A2 : CR,FE : VA : !\n"
            " PHASE SIGMA % 3 8 4 18 !\n"
            " CONSTITUENT SIGMA : FE : CR : CR,FE : !\n");
        check(dialog.loadDatabaseText(feCr, QStringLiteral("fecr.tdb")),
              "a real database loads");

        const auto boxes = dialog.findChildren<QCheckBox*>();
        const auto boxNamed = [&boxes](const QString& text) {
            return std::find_if(boxes.begin(), boxes.end(),
                                [&text](const QCheckBox* b) {
                                    return b->text() == text;
                                });
        };
        // Element checkboxes are built from the file, and the vacancy is not
        // one of them — a user cannot build a system out of vacancies.
        check(boxNamed(QStringLiteral("FE")) != boxes.end()
                  && boxNamed(QStringLiteral("CR")) != boxes.end(),
              "element checkboxes appear for Fe and Cr");
        check(boxNamed(QStringLiteral("VA")) == boxes.end(),
              "but not for the vacancy");
        check(boxNamed(QStringLiteral("SIGMA")) != boxes.end(),
              "and a checkbox per phase");

        // -- Availability follows the element selection -------------------
        const auto fe = boxNamed(QStringLiteral("FE"));
        const auto cr = boxNamed(QStringLiteral("CR"));
        const auto sigma = boxNamed(QStringLiteral("SIGMA"));
        const auto liquid = boxNamed(QStringLiteral("LIQUID"));
        if (fe != boxes.end() && cr != boxes.end() && sigma != boxes.end()
            && liquid != boxes.end()) {
            (*fe)->setChecked(true);
            check((*liquid)->isEnabled(),
                  "Fe alone supports LIQUID — a sublattice needs ANY of its "
                  "constituents");
            // SIGMA is (Fe)(Cr)(Cr,Fe); its second sublattice is Cr only.
            check(!(*sigma)->isEnabled(),
                  "but not SIGMA, whose second sublattice is Cr only");
            check(!dialog.selectedPhases().contains(QStringLiteral("SIGMA")),
                  "and an unavailable phase is excluded from the selection "
                  "even though its box is still ticked");

            (*cr)->setChecked(true);
            check((*sigma)->isEnabled(), "adding Cr makes SIGMA available");
            check(dialog.selectedPhases().contains(QStringLiteral("SIGMA")),
                  "and it joins the selection");

            // Suspending is the deliberate act; phases start active.
            (*sigma)->setChecked(false);
            check(!dialog.selectedPhases().contains(QStringLiteral("SIGMA")),
                  "unticking suspends a phase without removing it from the "
                  "database");
            check((*sigma)->isEnabled(),
                  "and it stays visible and re-selectable, rather than "
                  "vanishing from the list");

            check(dialog.selectedElements().size() == 2,
                  "the element selection reads back");
        }
    }

    // Decimal separators in FILE FORMATS, checked here rather than in a core
    // test for a specific reason: this is the only test binary that builds a
    // QApplication, and building one is what sets LC_NUMERIC from the
    // environment. On a decimal-comma locale (pt_BR, de_DE, fr_FR, …)
    // std::stod("12.345") stops at the '.' and returns 12 — so every atomic
    // coordinate, occupancy and B-factor in an imported mmCIF was silently
    // truncated to its integer part. Nothing errored; the file loaded and the
    // molecule was wrong. A core test cannot see it, because no QApplication
    // means the C locale stays put.
    std::printf("Decimal separators under the user's locale:\n");
    {
        std::printf("    (LC_NUMERIC = %s)\n",
                    std::setlocale(LC_NUMERIC, nullptr));
        QTemporaryDir scratch;
        if (scratch.isValid()) {
            const QString path = scratch.path() + QStringLiteral("/tiny.cif");
            QFile file(path);
            file.open(QIODevice::WriteOnly | QIODevice::Text);
            file.write(
                "data_TEST\n"
                "loop_\n"
                "_atom_site.group_PDB\n"
                "_atom_site.id\n"
                "_atom_site.type_symbol\n"
                "_atom_site.Cartn_x\n"
                "_atom_site.Cartn_y\n"
                "_atom_site.Cartn_z\n"
                "ATOM 1 C 12.345 -6.750 0.125\n"
                "ATOM 2 O 3.500 4.250 -1.875\n");
            file.close();

            bool parsed = true;
            calango::core::Structure molecule;
            try {
                molecule = calango::core::PdbxFile::read(path.toStdString());
            } catch (const std::exception&) {
                parsed = false;
            }
            check(parsed && molecule.size() == 2,
                  "a two-atom mmCIF parses");
            if (parsed && molecule.size() == 2) {
                // The fractional part is the whole point: 12.345 truncating to
                // 12 is a 0.345 A error on every atom, which is a different
                // molecule, not a rounding difference.
                check(std::fabs(molecule.atoms()[0].position.x - 12.345) < 1e-9,
                      "and its coordinates keep their fractional part — a "
                      "decimal point in a file is a decimal point, whatever "
                      "the user's locale");
                check(std::fabs(molecule.atoms()[0].position.y + 6.75) < 1e-9,
                      "including negative ones");
                check(std::fabs(molecule.atoms()[1].position.z + 1.875) < 1e-9,
                      "on every atom, not just the first");
            }
        }
    }

    // The phase-diagram window. Constructed, not just declared: it computes a
    // diagram in its constructor, and that constructor runs recompute(), which
    // writes into two plot widgets and a status label. A wizard in this
    // project has already crashed by refreshing a preview from a constructor
    // before the page it drew into existed, which is the whole reason this
    // file exists.
    std::printf("Phase diagram window:\n");
    {
        // A Fe-Cr database with real Gibbs expressions, so the window has
        // something to evaluate rather than exercising only its empty state.
        const QString feCr = QStringLiteral(
            "$ Fe-Cr with expressions\n"
            " ELEMENT VA  VACUUM   0 0 0 !\n"
            " ELEMENT FE  BCC_A2   55.847 4489 27.28 !\n"
            " ELEMENT CR  BCC_A2   51.996 4050 23.54 !\n"
            " PHASE BCC_A2 % 1 1 !\n"
            " CONSTITUENT BCC_A2 : CR,FE : !\n"
            " PHASE LIQUID % 1 1.0 !\n"
            " CONSTITUENT LIQUID : CR,FE : !\n"
            " PARAMETER G(BCC_A2,FE;0) 298.15 0; 6000 N !\n"
            " PARAMETER G(BCC_A2,CR;0) 298.15 0; 6000 N !\n"
            " PARAMETER L(BCC_A2,CR,FE;0) 298.15 +20500-9.68*T; 6000 N !\n"
            " PARAMETER G(LIQUID,FE;0) 298.15 +13807-7.63*T; 6000 N !\n"
            " PARAMETER G(LIQUID,CR;0) 298.15 +21000-9.66*T; 6000 N !\n"
            " PARAMETER L(LIQUID,CR,FE;0) 298.15 -17737+7.997*T; 6000 N !\n");
        calango::core::TdbDatabase database;
        std::string error;
        check(database.parse(feCr.toStdString(), &error),
              "the fixture database parses");

        PhaseDiagramWindow window(database, {QStringLiteral("FE"),
                                             QStringLiteral("CR")},
                                  {QStringLiteral("BCC_A2"),
                                   QStringLiteral("LIQUID")});
        window.show();
        check(window.statusText().contains(QStringLiteral("2 phase")),
              "both phases are modelled as substitutional solutions");
        window.recompute();
        check(!window.statusText().isEmpty(),
              "and recomputing on demand leaves a status behind");

        // WHICH SIDE OF THE DIAGRAM IS WHICH. The x axis is the mole fraction
        // of the user's SECOND selected element, which follows the database's
        // declaration order (FE then CR here, so x is x_CR). A phase's own
        // constituents are sorted ALPHABETICALLY, because that is the order
        // TDB writes interaction parameters in — so for Fe-Cr the two orders
        // DISAGREE, and a translation that assumes they agree mirrors every
        // Gibbs curve about x = 1/2. The result still looks like a phase
        // diagram; it is just the wrong way round.
        //
        // Pinned with a fixture whose answer is forced: each phase is 50 kJ/mol
        // above the other at one end, so the CR-rich end can only be LIQUID and
        // the FE-rich end can only be BCC_A2.
        const QString lopsided = QStringLiteral(
            " ELEMENT VA  VACUUM   0 0 0 !\n"
            " ELEMENT FE  BCC_A2   55.847 4489 27.28 !\n"
            " ELEMENT CR  BCC_A2   51.996 4050 23.54 !\n"
            " PHASE BCC_A2 % 1 1 !\n"
            " CONSTITUENT BCC_A2 : CR,FE : !\n"
            " PHASE LIQUID % 1 1.0 !\n"
            " CONSTITUENT LIQUID : CR,FE : !\n"
            " PARAMETER G(BCC_A2,FE;0) 298.15 0; 6000 N !\n"
            " PARAMETER G(BCC_A2,CR;0) 298.15 50000; 6000 N !\n"
            " PARAMETER G(LIQUID,FE;0) 298.15 50000; 6000 N !\n"
            " PARAMETER G(LIQUID,CR;0) 298.15 0; 6000 N !\n");
        calango::core::TdbDatabase forced;
        check(forced.parse(lopsided.toStdString(), &error),
              "the lopsided fixture parses");
        PhaseDiagramWindow sided(forced,
                                 {QStringLiteral("FE"), QStringLiteral("CR")},
                                 {QStringLiteral("BCC_A2"),
                                  QStringLiteral("LIQUID")});
        sided.show();
        const auto& computed = sided.binaryDiagram();
        // A 50 kJ/mol offset each way makes the two-phase field span almost
        // the whole axis, so the ENDS of its tie-line are what carries the
        // answer: the Fe-rich end must be BCC_A2 and the Cr-rich end LIQUID.
        // (Probing a single composition would land inside the field and report
        // both phases, which says nothing about the direction.)
        QString feRich;
        QString crRich;
        if (!computed.sections.empty()) {
            const auto& mid = computed.sections[computed.sections.size() / 2];
            if (!mid.tieLines.empty()) {
                const auto name = [&computed](int index) {
                    return index < 0 ? QString()
                                     : QString::fromStdString(
                                           computed.phaseNames[static_cast<
                                               std::size_t>(index)]);
                };
                feRich = name(mid.tieLines.front().leftPhase);
                crRich = name(mid.tieLines.back().rightPhase);
            }
        }
        check(feRich == QStringLiteral("BCC_A2"),
              "the Fe-rich end of the two-phase field is the phase whose Fe "
              "endmember is the low one");
        check(crRich == QStringLiteral("LIQUID"),
              "and the Cr-rich end the other — the composition axis is not "
              "mirrored by the alphabetical constituent order");

        // Three elements switches it to the ternary tab. The database has no
        // NI, so every phase is refused — the point is that it REFUSES with a
        // reason rather than crashing or drawing an empty triangle silently.
        PhaseDiagramWindow ternary(database,
                                   {QStringLiteral("FE"), QStringLiteral("CR"),
                                    QStringLiteral("NI")},
                                   {QStringLiteral("BCC_A2")});
        ternary.show();
        check(!ternary.statusText().isEmpty(),
              "a ternary with an element the database cannot dissolve says so");
    }

    // A REAL, PUBLISHED database: the Nb-Re assessment of Liu, Hargather and
    // Liu (CALPHAD 41 (2013) 119-127), as distributed by NIMS. It exercises
    // everything the hand-written fixtures cannot:
    //
    //   - interactions written with the symbol G rather than L,
    //   - a phase name with a space after the parenthesis, "G( BCC_RENB,RE;0)",
    //   - a lowercase species in one parameter,
    //   - SIGMARENB, a genuine (Re)10(Nb)4(Re,Nb)16 sublattice phase whose
    //     endmembers are identified by the WHOLE occupation tuple, and
    //   - CHI_RENB, which mixes on two sublattices and must be refused.
    std::printf("Real database — Nb-Re (Liu 2013):\n");
    {
        const QString path =
            QStringLiteral("/Users/leseixas/Downloads/nbre_liu.tdb");
        QFile source(path);
        if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
            std::printf("    (not present here — skipped)\n");
        } else {
            calango::core::TdbDatabase database;
            std::string problem;
            check(database.parse(QString::fromUtf8(source.readAll()).toStdString(),
                                 &problem),
                  "the published Nb-Re database parses");
            check(database.phases.size() == 6, "with its six phases");

            // SIGMA is the one that used to come back "fine" with zero
            // energies. Zero is not a conservative default for a Gibbs energy:
            // it undercuts every real phase, so sigma appeared stable across
            // the whole diagram.
            const auto sigma = calango::core::tdbSubstitutionalPhase(
                database, "SIGMARENB", {"NB", "RE"}, 2500.0);
            check(sigma.ok, "the sigma phase is modelled");
            check(std::fabs(sigma.atomsPerFormulaUnit - 30.0) < 1e-9,
                  "with 30 atoms per formula unit (10+4+16)");
            check(std::fabs(sigma.mixingSites - 16.0) < 1e-9,
                  "and 16 mixing sites");
            check(sigma.endmemberJPerMol.size() == 2
                      && sigma.endmemberJPerMol[0] != 0.0
                      && sigma.endmemberJPerMol[1] != 0.0,
                  "and REAL endmember energies — a Gibbs energy of exactly "
                  "zero is a phase that undercuts every other one");
            // (Re)10(Nb)4(Re,Nb)16: the site fraction runs over [0,1] but the
            // MOLE fraction of Nb only over [4/30, 20/30]. Confusing the two
            // gives sigma the whole axis and invents solubility.
            check(std::fabs(sigma.minMoleFraction - 10.0 / 30.0) < 1e-9
                      && std::fabs(sigma.maxMoleFraction - 26.0 / 30.0) < 1e-9,
                  "spanning only x_Re in [1/3, 13/15], not the whole axis");
            check(!std::isfinite(sigma.gibbsAtMoleFraction(0.1)),
                  "and refusing a composition it cannot reach");
            check(std::isfinite(sigma.gibbsAtMoleFraction(0.5)),
                  "while answering inside its range");

            const auto chi = calango::core::tdbSubstitutionalPhase(
                database, "CHI_RENB", {"NB", "RE"}, 2500.0);
            check(!chi.ok,
                  "chi, which mixes on two sublattices, is refused rather than "
                  "approximated");

            // The diagram itself. Nb melts at 2750 K and Re at 3459 K, so a
            // window to 3600 K contains both melting points and the liquid
            // must be the stable phase above them.
            QStringList phaseNames;
            for (const calango::core::TdbPhase& phase : database.phases)
                phaseNames << QString::fromStdString(phase.name);
            PhaseDiagramWindow window(database,
                                      {QStringLiteral("NB"), QStringLiteral("RE")},
                                      phaseNames);
            window.show();
            // Nb melts at 2750 K and Re at 3459 K, so the 300-2000 K default
            // window shows nothing but solids for this system.
            window.setTemperatureRange(800.0, 3600.0);
            check(window.statusText().contains(QStringLiteral("5 phase")),
                  "five of the six phases are modelled; chi is reported, not "
                  "silently dropped");

            const auto& computed = window.binaryDiagram();
            check(!computed.sections.empty(), "the T-x diagram is computed");
            const auto phaseAt = [&computed](double temperatureK, double x) {
                const calango::core::BinarySection* best = nullptr;
                double bestDistance = 1e30;
                for (const calango::core::BinarySection& section :
                     computed.sections) {
                    const double distance =
                        std::fabs(section.temperatureK - temperatureK);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        best = &section;
                    }
                }
                if (!best)
                    return QString();
                const std::vector<int> assemblage =
                    calango::core::binaryAssemblageAt(*best, x);
                if (assemblage.empty())
                    return QString();
                return QString::fromStdString(
                    computed.phaseNames[static_cast<std::size_t>(
                        assemblage.front())]);
            };
            // Where the whole chain — expression evaluator, sublattice model,
            // convex hull — has to land on a real assessment. Every one of
            // these is a fact about Nb-Re, not about this implementation:
            // Nb melts at 2750 K and is bcc, Re melts at 3459 K and is hcp,
            // and the two form a sigma phase in between.
            check(phaseAt(3600.0, 0.5) == QStringLiteral("LIQUID_RENB"),
                  "above both melting points the system is liquid");
            check(phaseAt(1000.0, 0.02) == QStringLiteral("BCC_RENB"),
                  "cold and Nb-rich it is bcc — niobium's own structure");
            check(phaseAt(2600.0, 0.99) == QStringLiteral("HCP_RENB"),
                  "Re-rich below rhenium's melting point it is hcp — "
                  "rhenium's own structure");
            // The intermetallic actually appears. A sigma phase read with zero
            // endmember energies would have swallowed the whole diagram; one
            // whose parameters were skipped would be absent from it entirely.
            const auto assemblageAt = [&computed](double temperatureK,
                                                  double x) {
                const calango::core::BinarySection* best = nullptr;
                double bestDistance = 1e30;
                for (const calango::core::BinarySection& section :
                     computed.sections) {
                    const double distance =
                        std::fabs(section.temperatureK - temperatureK);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        best = &section;
                    }
                }
                QStringList names;
                if (best) {
                    for (const int index :
                         calango::core::binaryAssemblageAt(*best, x))
                        names << QString::fromStdString(
                            computed.phaseNames[static_cast<std::size_t>(index)]);
                }
                return names;
            };
            check(assemblageAt(1000.0, 0.8)
                      .contains(QStringLiteral("SIGMARENB")),
                  "and the sigma phase is on the diagram where it belongs");
            check(!assemblageAt(3600.0, 0.5)
                       .contains(QStringLiteral("SIGMARENB")),
                  "but gone above the liquidus, as an intermetallic must be");

            // --- Export ---------------------------------------------------
            QTemporaryDir out;
            if (out.isValid()) {
                const QString csv = out.path() + QStringLiteral("/nbre.csv");
                check(window.exportCsv(csv), "the tie-lines export as CSV");
                QFile written(csv);
                written.open(QIODevice::ReadOnly | QIODevice::Text);
                const QString text = QString::fromUtf8(written.readAll());
                check(text.contains(QStringLiteral(
                          "temperature_K,x_left,x_right,phase_left,phase_right")),
                      "with a header naming its columns");
                check(text.contains(QStringLiteral("LIQUID_RENB")),
                      "and the phase names spelled out, not indices");
                // The locale rule again, on the other writer: a CSV whose
                // decimals are commas has twice as many columns as its header.
                const QStringList rows =
                    text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                bool commaDecimals = false;
                int dataRows = 0;
                for (const QString& row : rows) {
                    if (row.startsWith(QLatin1Char('#'))
                        || row.startsWith(QStringLiteral("temperature")))
                        continue;
                    ++dataRows;
                    if (row.count(QLatin1Char(',')) != 4)
                        commaDecimals = true;
                }
                check(dataRows > 50,
                      "carrying the whole sweep, not a summary");
                check(!commaDecimals,
                      "every row having exactly five fields — decimal points, "
                      "not decimal commas");

                const QString png = out.path() + QStringLiteral("/nbre.png");
                check(window.exportImage(png, 3.0), "the plot exports as PNG");

                // NO HOLES IN THE SHADING. Filled regions used to be built as
                // one polygon per traced field, and a boundary that moves
                // faster than the temperature sampling then belongs to neither
                // the field below nor the one above: the Nb-Re melting line
                // jumps from x = 0.465 to x = 0.318 between two adjacent
                // isotherms, and it left a white gash straight across the
                // diagram that read as "no phase is stable here".
                //
                // Checked by scanning a column of the exported image: between
                // the first and last shaded pixel there must be no unshaded
                // run. Self-locating, so it does not depend on the plot's
                // margins.
                QImage rendered(png);
                check(!rendered.isNull(), "the exported PNG loads back");
                if (!rendered.isNull()) {
                    const int column = rendered.width() / 4; // x_Re about 0.2
                    const QRgb canvas = window.style().canvas.rgb();
                    // The plot interior is bounded by the axis frame, which is
                    // the only thing drawn in the spine colour across the full
                    // width. Locating it this way keeps the test independent
                    // of the widget's margins — and stops it mistaking the
                    // blank strip above the x-axis tick labels for a hole.
                    const QRgb spine = window.style().spine.rgb();
                    // Tight tolerance on purpose: the tick-label ink is only
                    // ~35 per channel away from the frame colour, and a loose
                    // match picks up the labels BELOW the plot as though they
                    // were its bottom edge — which then reports the blank
                    // margin above them as a hole in the shading.
                    const auto isSpine = [spine](QRgb pixel) {
                        return std::abs(qRed(pixel) - qRed(spine)) < 12
                            && std::abs(qGreen(pixel) - qGreen(spine)) < 12
                            && std::abs(qBlue(pixel) - qBlue(spine)) < 12;
                    };
                    int top = -1;
                    int bottom = -1;
                    for (int y = 0; y < rendered.height(); ++y) {
                        const QRgb pixel = rendered.pixel(column, y) | 0xff000000u;
                        if (!isSpine(pixel))
                            continue;
                        if (top < 0)
                            top = y;
                        bottom = y;
                    }
                    check(top >= 0 && bottom > top + 10,
                          "the plot frame is found in the exported image");
                    int longestGap = 0;
                    int gap = 0;
                    for (int y = top + 2; y < bottom - 1; ++y) {
                        const QRgb pixel = rendered.pixel(column, y) | 0xff000000u;
                        if (pixel == canvas) {
                            ++gap;
                            longestGap = std::max(longestGap, gap);
                        } else {
                            gap = 0;
                        }
                    }
                    // A couple of pixels of tolerance for antialiasing; the
                    // bug this pins produced a gash tens of pixels deep.
                    check(longestGap <= 2,
                          "and no unshaded gap inside it — the phase regions "
                          "tile the diagram, whatever the boundary does "
                          "between two sampled isotherms");

                    // The same scan with the shading turned off must find a
                    // large gap. Without this the check above would pass on a
                    // plot that happened to be dark for any other reason, and
                    // a coverage test that cannot fail is not a test.
                    PhaseDiagramStyle bare = window.style();
                    bare.showShading = false;
                    bare.showBoundaryCurves = false;
                    bare.showTieLines = false;
                    bare.showGrid = false;
                    window.setStyle(bare);
                    const QString unshaded =
                        out.path() + QStringLiteral("/bare.png");
                    check(window.exportImage(unshaded, 3.0),
                          "the unshaded plot exports too");
                    QImage empty(unshaded);
                    int bareGap = 0;
                    int run = 0;
                    if (!empty.isNull()) {
                        for (int y = top + 2; y < bottom - 1; ++y) {
                            const QRgb pixel =
                                empty.pixel(column, y) | 0xff000000u;
                            run = pixel == canvas ? run + 1 : 0;
                            bareGap = std::max(bareGap, run);
                        }
                    }
                    check(bareGap > 20,
                          "and with the shading off the very same scan finds "
                          "the plot empty — so the coverage check has teeth");
                    window.setStyle(PhaseDiagramStyle{});
                }
                check(QFileInfo(png).size() > 5000,
                      "at a size that says it drew something");
                const QString svg = out.path() + QStringLiteral("/nbre.svg");
                check(window.exportImage(svg), "and as SVG vector art");
                QFile vector(svg);
                vector.open(QIODevice::ReadOnly | QIODevice::Text);
                check(QString::fromUtf8(vector.read(400))
                          .contains(QStringLiteral("<svg")),
                      "which is a real SVG document");
            }

            // --- Appearance ------------------------------------------------
            PhaseDiagramStyle style = window.style();
            style.canvas = QColor(0, 0, 0);
            style.showGrid = false;
            style.showLegend = false;
            style.phaseColors[0] = QColor(255, 0, 255);
            window.setStyle(style);
            check(window.style().canvas == QColor(0, 0, 0)
                      && !window.style().showGrid,
                  "a customized appearance is applied");
            if (out.isValid()) {
                // Re-exporting after a restyle must still produce a file: the
                // export renders through the SAME code path as the screen, so
                // a style that broke drawing would break both.
                const QString restyled = out.path() + QStringLiteral("/dark.png");
                check(window.exportImage(restyled, 2.0),
                      "and the restyled plot still exports");
            }
        }
    }

    // The database generator. Its table, its fit and its .tdb preview are all
    // built from a file, so construction alone proves little; the test loads
    // an ensemble whose formation energies are a KNOWN regular solution and
    // checks that the coefficient comes back.
    std::printf("TDB generator dialog:\n");
    {
        TdbGeneratorDialog dialog;
        dialog.show();
        check(dialog.databaseText().isEmpty(),
              "nothing is written before an ensemble is loaded");
        check(!dialog.loadEnsembleJson(QStringLiteral("{}"),
                                       QStringLiteral("empty.json")),
              "an empty results file is refused");

        constexpr double kOmega = 12000.0;
        constexpr double kEvPerAtom = 96485.33212331001;
        QJsonArray configurations;
        for (int i = 0; i <= 8; ++i) {
            const double x = i / 8.0;
            QJsonObject entry;
            entry.insert(QStringLiteral("concentration"), x);
            entry.insert(QStringLiteral("formation_energy"),
                         kOmega * x * (1.0 - x) / kEvPerAtom);
            entry.insert(QStringLiteral("energy_per_atom"),
                         kOmega * x * (1.0 - x) / kEvPerAtom - 3.0);
            entry.insert(QStringLiteral("formula"), QStringLiteral("AgAu"));
            configurations.append(entry);
        }
        QJsonObject root;
        root.insert(QStringLiteral("configurations"), configurations);
        root.insert(QStringLiteral("concentration_element"),
                    QStringLiteral("Au"));
        check(dialog.loadEnsembleJson(
                  QString::fromUtf8(QJsonDocument(root).toJson()),
                  QStringLiteral("cluster_expansion.json")),
              "a real ensemble loads");
        check(dialog.assessment().ok, "and is assessed");
        check(!dialog.assessment().vibrational,
              "statically, because a cluster expansion carries no phonons");
        check(dialog.databaseText().contains(QStringLiteral("PARAMETER L(")),
              "producing a database with an interaction parameter");
        // The default order is 2, so L0 is not simply Omega — but the excess
        // energy the model reproduces is, at the composition where the two
        // agree by construction. Checked through the value rather than the
        // coefficient so the assertion survives a change of default order.
        const double excess = calango::core::redlichKisterExcess(
            dialog.assessment().fit.terms, 0.5, 800.0);
        check(std::fabs(excess - kOmega * 0.25) < 1.0,
              "whose excess Gibbs energy at x = 1/2 is the Omega/4 the "
              "ensemble was built from");

        // --- Attaching a phonon DOS ---------------------------------------
        // The file the phonon script writes is a HISTOGRAM — mode weight per
        // bin — while the harmonic integrals want a DENSITY g(ω). Getting that
        // conversion wrong scales F_vib, and with it every excess entropy
        // fitted from it, by the bin width: a factor of about five that looks
        // nothing like a unit error and would never be noticed in a plot.
        //
        // Checked against an independent evaluation of the same integrals,
        // rather than against a remembered number.
        QTemporaryDir scratch;
        if (scratch.isValid()) {
            constexpr double kBin = 0.5; // cm^-1
            QJsonArray omega;
            QJsonArray counts;
            std::vector<double> omegaVec;
            std::vector<double> densityVec;
            for (int i = 0; i <= 600; ++i) {
                const double w = i * kBin;
                const double weight = w * w * 1e-4; // any positive spectrum
                omega.append(w);
                counts.append(weight);
                omegaVec.push_back(w);
                densityVec.push_back(weight / kBin); // histogram -> density
            }
            QJsonObject dos;
            dos.insert(QStringLiteral("frequencies"), omega);
            dos.insert(QStringLiteral("dos"), counts);
            dos.insert(QStringLiteral("broadened"), false);
            dos.insert(QStringLiteral("bin_width"), kBin);
            const QString path =
                scratch.path() + QStringLiteral("/phonon_dos.json");
            QFile file(path);
            file.open(QIODevice::WriteOnly);
            file.write(QJsonDocument(dos).toJson());
            file.close();

            check(dialog.loadPhononDos(0, path),
                  "a phonon DOS attaches to the x = 0 endpoint");
            const auto& vib = dialog.input().referenceVibAEvPerAtom;
            check(!vib.empty(), "and lands on the temperature grid");
            if (!vib.empty()) {
                // The dialog's default grid starts at 300 K.
                const calango::core::PhononThermoResult reference =
                    calango::core::computePhononThermodynamics(
                        omegaVec, densityVec, 300.0, 300.0, 1);
                const double atoms = reference.totalModes / 3.0;
                check(std::fabs(vib.front()
                                - reference.points.front().freeEnergyEv / atoms)
                          < 1e-9,
                      "carrying F_vib per ATOM from the density — not the raw "
                      "histogram, which would be wrong by the bin width");
            }
            check(!dialog.assessment().vibrational,
                  "one endpoint's phonons are not enough: the assessment stays "
                  "static until every configuration has them");
            check(!dialog.loadPhononDos(0, scratch.path()
                                               + QStringLiteral("/nope.json")),
                  "and a missing file is refused rather than silently ignored");
        }
    }

    // The HPC dock (formerly "Remote Access"). The preset system is the part
    // worth driving: it is only useful if what Save writes is what selecting
    // the entry puts back, and that round trip runs through the widgets
    // rather than through ClusterPreset alone.
    std::printf("HPC panel:\n");
    {
        HpcPanel panel(QString{});
        panel.show();

        const auto combos = panel.findChildren<QComboBox*>();
        const auto spins = panel.findChildren<QSpinBox*>();
        const auto edits = panel.findChildren<QLineEdit*>();
        const auto buttons = panel.findChildren<QPushButton*>();
        // Save/Delete are icon-only buttons (Task 4): they carry no visible
        // text, only a tooltip and an accessibleName ("Save connection
        // profile" / "Delete connection profile") — matched here by
        // `contains` so the short names below still find them, alongside
        // every other button in the panel that IS still found by its text.
        const auto buttonNamed = [&buttons](const QString& text) {
            return std::find_if(buttons.begin(), buttons.end(),
                                [&text](const QPushButton* b) {
                                    return b->text() == text
                                        || b->accessibleName().contains(text);
                                });
        };
        check(buttonNamed(QStringLiteral("Save")) != buttons.end(),
              "has a preset Save button");
        check(buttonNamed(QStringLiteral("Delete")) != buttons.end(),
              "and a Delete button");
        // The preset combo is the only editable one — the name you type is
        // the name Save uses.
        const auto presetCombo = std::find_if(
            combos.begin(), combos.end(),
            [](const QComboBox* c) { return c->isEditable(); });
        check(presetCombo != combos.end(), "and an editable cluster combo");

        // The Slurm resource fields part 4 asked for.
        const auto spinWithSuffix = [&spins](const QString& suffix) {
            return std::find_if(spins.begin(), spins.end(),
                                [&suffix](const QSpinBox* s) {
                                    return s->suffix().contains(suffix);
                                });
        };
        // GB, not MB (Task 3) — see the dedicated GB-suffix/48:00:00-default
        // checks further down, which also re-derive the memory spin box
        // independently (by specialValueText(), not suffix) so the two
        // checks do not share a false-positive failure mode.
        check(spinWithSuffix(QStringLiteral("GB")) != spins.end(),
              "exposes a memory field");
        check(spins.size() >= 4,
              "alongside port, nodes and tasks-per-node spin boxes");
        // Memory 0 must read as "cluster default", not as a literal zero —
        // SLURM reads --mem=0 as "all the memory on the node".
        if (spinWithSuffix(QStringLiteral("GB")) != spins.end())
            check(!(*spinWithSuffix(QStringLiteral("GB")))
                       ->specialValueText().isEmpty(),
                  "whose zero reads as the cluster default rather than none");

        // The verbose paragraph is gone; its content moved to a tooltip.
        bool verboseProse = false;
        for (const QLabel* label : panel.findChildren<QLabel*>())
            if (label->text().contains(QStringLiteral("Stage 4"))
                || label->text().length() > 160)
                verboseProse = true;
        check(!verboseProse,
              "and carries no multi-line helper paragraph, which cost "
              "vertical space in a bottom-row dock");

        // -- The round trip -------------------------------------------------
        if (presetCombo != combos.end()
            && buttonNamed(QStringLiteral("Save")) != buttons.end()) {
            const auto hostEdit = std::find_if(
                edits.begin(), edits.end(), [](const QLineEdit* e) {
                    return e->placeholderText().contains(
                        QStringLiteral("cluster.university.edu"));
                });
            const auto passwordEdit = std::find_if(
                edits.begin(), edits.end(), [](const QLineEdit* e) {
                    return e->echoMode() == QLineEdit::Password;
                });
            check(hostEdit != edits.end() && passwordEdit != edits.end(),
                  "the host and password fields are identifiable");
            if (hostEdit != edits.end() && passwordEdit != edits.end()) {
                (*hostEdit)->setText(QStringLiteral("alpha.cluster.test"));
                (*passwordEdit)->setText(QStringLiteral("hunter2"));
                (*presetCombo)->setCurrentText(QStringLiteral("Alpha"));
                (*buttonNamed(QStringLiteral("Save")))->click();
                check((*presetCombo)->findText(QStringLiteral("Alpha")) >= 0,
                      "saving adds the cluster to the combo");

                // Move to a different cluster, then come back.
                (*hostEdit)->setText(QStringLiteral("beta.cluster.test"));
                (*presetCombo)->setCurrentText(QStringLiteral("Beta"));
                (*buttonNamed(QStringLiteral("Save")))->click();
                const int alpha =
                    (*presetCombo)->findText(QStringLiteral("Alpha"));
                (*presetCombo)->setCurrentIndex(alpha);
                Q_EMIT(*presetCombo)->activated(alpha);
                check((*hostEdit)->text()
                          == QStringLiteral("alpha.cluster.test"),
                      "and selecting it restores what was saved");
                // Switching cluster must not carry the old password across:
                // silently authenticating elsewhere with it is how an account
                // gets locked out for reasons nobody can reconstruct.
                check((*passwordEdit)->text().isEmpty(),
                      "while the password is cleared, never carried between "
                      "clusters");

                // Saving the same name again edits rather than duplicating.
                const int before = (*presetCombo)->count();
                (*buttonNamed(QStringLiteral("Save")))->click();
                check((*presetCombo)->count() == before,
                      "and re-saving a name edits it instead of adding a "
                      "second entry");
            }
        }

        // -- Per-cluster VASP POTCAR override (Task 1) ----------------------
        const auto potcarEdit = std::find_if(
            edits.begin(), edits.end(), [](const QLineEdit* e) {
                return e->placeholderText().contains(
                    QStringLiteral("POTCAR directory"));
            });
        check(potcarEdit != edits.end(),
              "exposes a per-cluster VASP POTCAR directory field");
        if (potcarEdit != edits.end()) {
            // Empty by default: specFromUi() must not export anything.
            (*potcarEdit)->clear();
            const auto specEmpty = panel.specFromUi(
                QStringLiteral("job"), calango::core::CalculatorKind::Vasp);
            check(specEmpty.setupLines.find("CALANGO_VASP_PP_PATH")
                      == std::string::npos,
                  "an empty POTCAR field exports nothing into setupLines");

            // Set: exported ahead of the user's own setup lines, quoted.
            for (QPlainTextEdit* edit : panel.findChildren<QPlainTextEdit*>())
                if (edit->placeholderText().contains(QStringLiteral("module load")))
                    edit->setPlainText(QStringLiteral("module load vasp"));
            (*potcarEdit)->setText(QStringLiteral("/cluster/pseudo/potcars"));
            const auto specSet = panel.specFromUi(
                QStringLiteral("job"), calango::core::CalculatorKind::Vasp);
            const QString setupSet = QString::fromStdString(specSet.setupLines);
            check(setupSet.contains(
                      QStringLiteral(
                          "export CALANGO_VASP_PP_PATH=\"/cluster/pseudo/potcars\"")),
                  "a configured POTCAR field exports CALANGO_VASP_PP_PATH");
            check(setupSet.indexOf(QStringLiteral("CALANGO_VASP_PP_PATH"))
                      < setupSet.indexOf(QStringLiteral("module load vasp")),
                  "and it comes BEFORE the user's own setup lines");

            // Persists with the preset, like the other scheduler fields.
            if (presetCombo != combos.end()
                && buttonNamed(QStringLiteral("Save")) != buttons.end()) {
                (*presetCombo)->setCurrentText(QStringLiteral("Alpha"));
                (*buttonNamed(QStringLiteral("Save")))->click();
                (*potcarEdit)->clear();
                (*presetCombo)->setCurrentText(QStringLiteral("Beta"));
                const int alpha = (*presetCombo)->findText(QStringLiteral("Alpha"));
                (*presetCombo)->setCurrentIndex(alpha);
                Q_EMIT(*presetCombo)->activated(alpha);
                check((*potcarEdit)->text()
                          == QStringLiteral("/cluster/pseudo/potcars"),
                      "the POTCAR override round-trips with its cluster preset");
            }
        }

        // -- SLURM extensions (Task 4) ---------------------------------
        // Account/QOS fields removed (Task 3) -- verify they are actually
        // GONE, not just untested, and that Node list (still SLURM-only)
        // is unaffected by their removal.
        const auto accountEdit = std::find_if(
            edits.begin(), edits.end(), [](const QLineEdit* e) {
                return e->placeholderText().contains(QStringLiteral("billing account"));
            });
        check(accountEdit == edits.end(),
              "the Account field no longer exists on the Scheduler tab "
              "(Task 3)");
        const auto hpcLabels = panel.findChildren<QLabel*>();
        const auto qosLabel = std::find_if(
            hpcLabels.begin(), hpcLabels.end(), [](const QLabel* l) {
                return l->text() == QStringLiteral("QOS:");
            });
        check(qosLabel == hpcLabels.end(),
              "neither does its \"QOS:\" row label");
        const auto nodeListEdit = std::find_if(
            edits.begin(), edits.end(), [](const QLineEdit* e) {
                return e->placeholderText().contains(QStringLiteral("scheduler picks"));
            });
        check(nodeListEdit != edits.end(), "the Node list field is present");

        const auto plainEdits = panel.findChildren<QPlainTextEdit*>();
        const auto commandEdit = std::find_if(
            plainEdits.begin(), plainEdits.end(), [](const QPlainTextEdit* e) {
                return e->placeholderText() == QStringLiteral("python3 run.py");
            });
        check(commandEdit != plainEdits.end(),
              "the Command field is left BLANK by default, with "
              "\"python3 run.py\" only as placeholder text -- a literal "
              "default here (rather than blank) would defeat specFromUi()'s "
              "\"blank = use the calculator-aware default\" resolution "
              "below, which is exactly the bug this replaced (Task 2)");
        if (commandEdit != plainEdits.end())
            check((*commandEdit)->toPlainText().isEmpty(),
                  "and its actual text is empty, not the placeholder string "
                  "itself");

        // -- GPAW cores reaching the REMOTE launch command (Task 2) --------
        // This is the actual regression: RemoteJobSpec::command used to
        // default to a hardcoded, calculator-blind "python3 run.py" --
        // meaning a GPAW job submitted to a cluster with Nodes x
        // Tasks/node > 1 silently ran as ONE serial process, exactly the
        // "runs on 1 core despite cores=4" bug, just on the remote path
        // (RunCommands.cpp already had the local "Run" tab right).
        const auto shapeNodesSpin = std::find_if(
            spins.begin(), spins.end(), [](const QSpinBox* s) {
                return s->toolTip().contains(QStringLiteral("--nodes"));
            });
        const auto shapeTasksSpin = std::find_if(
            spins.begin(), spins.end(), [](const QSpinBox* s) {
                return s->toolTip().startsWith(
                    QStringLiteral("Ranks (or cores) on each node"));
            });
        if (shapeNodesSpin != spins.end() && shapeTasksSpin != spins.end()
            && commandEdit != plainEdits.end()) {
            (*commandEdit)->clear(); // no per-cluster override
            (*shapeNodesSpin)->setValue(2);
            (*shapeTasksSpin)->setValue(2);
            const auto gpawSpec = panel.specFromUi(
                QStringLiteral("job"), calango::core::CalculatorKind::Gpaw);
            const QString gpawCommand = QString::fromStdString(gpawSpec.command);
            check(gpawCommand.contains(QStringLiteral("mpirun -n 4"))
                      && gpawCommand.contains(QStringLiteral("gpaw python")),
                  "a blank Command field resolves, for GPAW, to an "
                  "mpirun-wrapped launch line whose rank count is Nodes x "
                  "Tasks/node (2x2=4), not a bare serial \"python3 run.py\"");

            // A solver-command engine (VASP) takes the OTHER branch of the
            // same fix: the rank count belongs in the exported
            // ASE_VASP_COMMAND, not on the job's own command line, which
            // stays a plain interpreter invocation.
            const auto vaspSpec = panel.specFromUi(
                QStringLiteral("job"), calango::core::CalculatorKind::Vasp);
            const QString vaspCommand = QString::fromStdString(vaspSpec.command);
            const QString vaspSetup = QString::fromStdString(vaspSpec.setupLines);
            check(!vaspCommand.contains(QStringLiteral("mpirun")),
                  "...while for VASP (a solver-command engine) the job's "
                  "own command line stays a plain interpreter invocation");
            check(vaspSetup.contains(QStringLiteral(
                      "export ASE_VASP_COMMAND=\"mpirun -np 4 vasp_std\"")),
                  "and the rank count instead reaches ASE_VASP_COMMAND, "
                  "exported ahead of the job's setup lines");
        }

        // Hidden for PBS/SGE, shown for SLURM -- re-derived from the combo
        // rather than assumed, since "SLURM first" is a UI convention, not
        // a guarantee.
        const auto schedulerCombo = std::find_if(
            combos.begin(), combos.end(), [](const QComboBox* c) {
                return c->findText(QStringLiteral("SLURM")) >= 0
                    && c->findText(QStringLiteral("PBS")) >= 0;
            });
        check(schedulerCombo != combos.end(), "the scheduler combo is findable");
        // isVisible() reflects the whole ancestor chain, including whichever
        // tab page currently owns it -- Connection, not Scheduler, is the
        // panel's default tab, so every widget checked below is invisible
        // for that reason alone unless the Scheduler tab is made current
        // first.
        if (auto* tabWidget = panel.findChild<QTabWidget*>()) {
            for (int i = 0; i < tabWidget->count(); ++i)
                if (tabWidget->tabText(i) == QStringLiteral("Scheduler"))
                    tabWidget->setCurrentIndex(i);
        }
        if (schedulerCombo != combos.end() && nodeListEdit != edits.end()) {
            (*schedulerCombo)->setCurrentText(QStringLiteral("SLURM"));
            check((*nodeListEdit)->isVisible(), "Node list is offered for SLURM");
            (*schedulerCombo)->setCurrentText(QStringLiteral("PBS"));
            check(!(*nodeListEdit)->isVisible(),
                  "and hidden for PBS, whose script has no use for it");
            (*schedulerCombo)->setCurrentText(QStringLiteral("SLURM"));
        }

        // specFromUi() actually carries this through to RemoteJobSpec.
        if (nodeListEdit != edits.end()) {
            (*nodeListEdit)->setText(QStringLiteral("work1"));
            const auto spec = panel.specFromUi(
                QStringLiteral("job"), calango::core::CalculatorKind::Gpaw);
            check(spec.nodeList == "work1",
                  "a typed Node list value reaches RemoteJobSpec via "
                  "specFromUi()");
        }

        // The Account/QOS escape hatch (Task 3): a hand-typed #SBATCH
        // --account= line in "Extra #SBATCH lines" reaches RemoteJobSpec
        // exactly like any other extraDirectives content -- the free-form
        // route the tab's own tooltip now points a cluster that requires
        // a billing account or QOS at, now that neither has its own field.
        const auto extraDirectivesEdit = std::find_if(
            plainEdits.begin(), plainEdits.end(), [](const QPlainTextEdit* e) {
                return e->placeholderText().contains(QStringLiteral("#SBATCH"));
            });
        check(extraDirectivesEdit != plainEdits.end(),
              "the \"Extra #SBATCH lines\" escape hatch is present");
        if (extraDirectivesEdit != plainEdits.end()) {
            (*extraDirectivesEdit)
                ->setPlainText(QStringLiteral("#SBATCH --account=phys-2026\n"
                                              "#SBATCH --qos=high"));
            const auto spec = panel.specFromUi(
                QStringLiteral("job"), calango::core::CalculatorKind::Gpaw);
            const QString extra = QString::fromStdString(spec.extraDirectives);
            check(extra.contains(QStringLiteral("--account=phys-2026"))
                      && extra.contains(QStringLiteral("--qos=high")),
                  "a hand-typed account/QOS directive reaches RemoteJobSpec "
                  "via specFromUi(), unchanged, the same way any other "
                  "extraDirectives content does");
        }

        // Memory / node is GB now (Task 3), not MB.
        const auto memorySpin = std::find_if(
            spins.begin(), spins.end(), [](const QSpinBox* s) {
                return s->specialValueText() == QStringLiteral("cluster default");
            });
        check(memorySpin != spins.end()
                  && (*memorySpin)->suffix() == QStringLiteral(" GB"),
              "the memory field's suffix reads GB, not MB");

        // Walltime defaults to 48:00:00 for a brand-new panel (Task 3) --
        // this instance was never given a saved preset/settings value, so
        // it is still showing the freshly-constructed default.
        const auto walltimeEdit = std::find_if(
            edits.begin(), edits.end(), [](const QLineEdit* e) {
                return e->toolTip() == QStringLiteral("HH:MM:SS.");
            });
        check(walltimeEdit != edits.end()
                  && (*walltimeEdit)->text() == QStringLiteral("48:00:00"),
              "a new Scheduler tab defaults its walltime to 48:00:00");
    }

    // Preferences -> Simulation Files (Task 3): the default simulations
    // folder and how "leave it configured" vs. "leave it blank" resolve.
    std::printf("Preferences -> Simulation Files (Task 3):\n");
    {
        // defaultSimulationsDirectory() is a pure string computation --
        // exercised directly, with NO key set, so this block never touches
        // the real $HOME (simulationsDirectory() below is only ever
        // exercised against an explicitly-configured scratch directory, for
        // exactly that reason).
        const QString expected =
            QDir::homePath() + QStringLiteral("/calango_simulations");
        check(SettingsManager::defaultSimulationsDirectory() == expected,
              "the default is ~/calango_simulations, resolved through "
              "QDir::homePath() -- the proper per-platform home-directory "
              "API, never a literal \"~\" (which QDir/QFile never expand)");

        QTemporaryDir sandbox;
        check(sandbox.isValid(), "scratch directory created");
        const QString customPath =
            QDir(sandbox.path()).filePath(QStringLiteral("my_custom_runs"));
        QSettings().setValue(
            QLatin1String(SettingsManager::kSimulationsDir), customPath);
        check(SettingsManager::simulationsDirectory() == customPath,
              "an explicitly-configured path is used verbatim -- "
              "new-default-only semantics: a user who already set their "
              "own path is never redirected to the new default");
        check(QDir(customPath).exists(),
              "and it is created on first use, since it did not exist yet");

        // Reset (Preferences' own "Reset" button just clears the key) falls
        // back to the NEW default -- the one place this matters: an
        // install that never touched this setting starts using
        // ~/calango_simulations from here on, same as any other
        // never-configured default. Not exercised via simulationsDirectory()
        // itself (that would mkpath the real $HOME), but this is exactly
        // what PreferencesDialog's placeholder/tooltip/status label and
        // Reset button all already read live off
        // defaultSimulationsDirectory() (PreferencesDialog.cpp) -- checked
        // above.
        QSettings().setValue(QLatin1String(SettingsManager::kSimulationsDir),
                             QString());
        PreferencesDialog dialog;
        QLineEdit* simulationsEdit = nullptr;
        for (QLineEdit* edit : dialog.findChildren<QLineEdit*>())
            if (edit->placeholderText() == expected)
                simulationsEdit = edit;
        check(simulationsEdit != nullptr,
              "the Simulation Files field's placeholder reads the new "
              "default when nothing is configured");
        check(simulationsEdit && simulationsEdit->text().isEmpty(),
              "and the field itself is left blank -- not pre-filled with "
              "the default text, so it round-trips as \"unconfigured\" "
              "rather than freezing today's default the way commandEdit_ "
              "used to (Task 2's HPC panel fix)");
    }

    // Preferences -> Hotkeys (Task 2): the table PreferencesDialog builds
    // from ShortcutRegistry::actions(), and the registry itself is what
    // MainWindow/ViewportWidget actually read at runtime -- driving the
    // WIDGETS here and asserting on the REGISTRY is what proves the wiring
    // between them, not just that each half works in isolation.
    std::printf("Preferences -> Hotkeys:\n");
    {
        ShortcutRegistry::resetAllToDefaults(); // a clean slate for this block
        PreferencesDialog dialog;
        dialog.show();

        const auto edits = dialog.findChildren<QKeySequenceEdit*>();
        const auto& actions = ShortcutRegistry::actions();
        check(edits.size() == actions.size(),
              "one QKeySequenceEdit per ShortcutRegistry action");

        // Row order matches ShortcutRegistry::actions() (PreferencesDialog
        // builds the table by iterating it directly) -- relied on below
        // rather than searched for, the same way the table is built.
        if (edits.size() == actions.size()) {
            const int rotateRow = 0; // "viewport.mode.rotate", R by default
            const int panRow = 1;    // "viewport.mode.pan", T by default
            check(actions.at(rotateRow).id
                      == QStringLiteral("viewport.mode.rotate")
                  && actions.at(panRow).id == QStringLiteral("viewport.mode.pan"),
              "row order assumption holds (fix the row indices above if "
              "this ever fails)");

            // A conflict-free remap: typing X for rotation.
            edits.at(rotateRow)->setKeySequence(QKeySequence(Qt::Key_X));
            Q_EMIT edits.at(rotateRow)->editingFinished();
            check(ShortcutRegistry::binding(actions.at(rotateRow).id)
                      == QKeySequence(Qt::Key_X),
                  "a conflict-free remap reaches the registry");

            // A COLLIDING remap: pointing rotation at whatever pan already
            // has. Must be refused -- the registry keeps rotation's PREVIOUS
            // binding (X, from just above), not pan's key.
            //
            // Refusal goes through a real, blocking QMessageBox::warning()
            // (PreferencesDialog.cpp) -- nothing in a headless ctest run
            // would ever click it, so its OWN nested event loop would hang
            // this test forever. Armed here rather than skipped: closing
            // whatever modal shows up shortly after triggering the conflict
            // is the standard Qt pattern for exercising a code path that
            // pops one, and it is what actually proves the warning fired at
            // all (a hung process is not silent success).
            const QKeySequence panKey = ShortcutRegistry::binding(actions.at(panRow).id);
            edits.at(rotateRow)->setKeySequence(panKey);
            QTimer::singleShot(0, &dialog, [] {
                if (QWidget* modal = QApplication::activeModalWidget())
                    modal->close();
            });
            Q_EMIT edits.at(rotateRow)->editingFinished();
            check(ShortcutRegistry::binding(actions.at(rotateRow).id)
                      == QKeySequence(Qt::Key_X),
                  "a colliding remap is refused -- rotation keeps its prior "
                  "binding, not pan's");
            check(edits.at(rotateRow)->keySequence() == QKeySequence(Qt::Key_X),
                  "and the widget itself is reverted to match, not left "
                  "showing the refused key");
            check(ShortcutRegistry::binding(actions.at(panRow).id) == panKey,
                  "pan itself is completely unaffected by the refused "
                  "attempt on rotation");

            // Per-row Reset.
            const auto resetButtons = dialog.findChildren<QPushButton*>(
                QString(), Qt::FindChildrenRecursively);
            QPushButton* rotateReset = nullptr;
            for (QPushButton* button : resetButtons)
                if (button->text() == QStringLiteral("Reset")
                    && button->toolTip().contains(actions.at(rotateRow).label)) {
                    rotateReset = button;
                    break;
                }
            check(rotateReset != nullptr, "rotation's per-row Reset button is findable");
            if (rotateReset) {
                rotateReset->click();
                check(ShortcutRegistry::binding(actions.at(rotateRow).id)
                          == actions.at(rotateRow).defaultKey,
                      "clicking it restores the factory default (R)");
                check(edits.at(rotateRow)->keySequence()
                          == actions.at(rotateRow).defaultKey,
                      "and the widget reflects it immediately");
            }

            // Global "Reset All to Defaults": remap two, then clear both.
            edits.at(rotateRow)->setKeySequence(QKeySequence(Qt::Key_X));
            Q_EMIT edits.at(rotateRow)->editingFinished();
            edits.at(panRow)->setKeySequence(QKeySequence(Qt::Key_Y));
            Q_EMIT edits.at(panRow)->editingFinished();
            QPushButton* resetAll = nullptr;
            for (QPushButton* button : resetButtons)
                if (button->text() == QStringLiteral("Reset All to Defaults")) {
                    resetAll = button;
                    break;
                }
            check(resetAll != nullptr, "the global Reset All button is findable");
            if (resetAll) {
                resetAll->click();
                check(ShortcutRegistry::binding(actions.at(rotateRow).id)
                              == actions.at(rotateRow).defaultKey
                          && ShortcutRegistry::binding(actions.at(panRow).id)
                              == actions.at(panRow).defaultKey,
                      "clears every remap made through this dialog at once");
            }
        }

        exerciseControls(&dialog);
        check(true, "survives every control being exercised");
        ShortcutRegistry::resetAllToDefaults(); // leave no trace for later tests
    }

    // Viewport [Tab]/[Shift+Tab] cycling (Task 1 bugfix). The previous
    // implementation had ONLY a keyPressEvent() branch, which silently did
    // nothing: QWidget::event() special-cases a literal Tab/Backtab
    // keypress BEFORE keyPressEvent() ever runs -- it calls
    // focusNextPrevChild() first, and only falls through to keyPressEvent()
    // if that returns false. Sending real QKeyEvents through
    // QApplication::sendEvent() (rather than calling focusNextPrevChild()
    // directly) is what actually exercises that dispatch chain -- calling
    // the override directly would have passed even on the ORIGINAL broken
    // build, since the bug was entirely about which method Qt calls first.
    std::printf("Viewport Tab/Backtab cycling (Task 1 bugfix):\n");
    {
        ShortcutRegistry::resetAllToDefaults();
        ViewportWidget viewport;
        QSignalSpy spy(&viewport, &ViewportWidget::cycleTabRequested);

        QKeyEvent tabPress(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(&viewport, &tabPress);
        check(spy.count() == 1 && spy.takeFirst().at(0).toInt() == 1,
              "a literal Tab keypress on the viewport cycles forward -- "
              "via focusNextPrevChild(), the actual interception point");

        // Shift+Tab commonly arrives as the distinct Key_Backtab code, not
        // Key_Tab with a Shift modifier (X11 among other platforms) -- both
        // spellings must cycle backward.
        QKeyEvent backtabPress(QEvent::KeyPress, Qt::Key_Backtab, Qt::NoModifier);
        QApplication::sendEvent(&viewport, &backtabPress);
        check(spy.count() == 1 && spy.takeFirst().at(0).toInt() == -1,
              "a literal Key_Backtab keypress cycles backward");

        QKeyEvent shiftTabPress(QEvent::KeyPress, Qt::Key_Tab, Qt::ShiftModifier);
        QApplication::sendEvent(&viewport, &shiftTabPress);
        check(spy.count() == 1 && spy.takeFirst().at(0).toInt() == -1,
              "Key_Tab plus a Shift modifier ALSO cycles backward (some "
              "platforms deliver Shift+Tab this way instead of Key_Backtab)");

        // Scoping: the same keypress on an ordinary line edit elsewhere in
        // the window must NOT cycle tabs -- ordinary Tab focus traversal is
        // completely untouched, because ViewportWidget's own
        // focusNextPrevChild() override is only ever invoked when
        // ViewportWidget itself is the widget Qt delivers the key press to.
        QLineEdit sideField;
        QKeyEvent tabOnLineEdit(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(&sideField, &tabOnLineEdit);
        check(spy.count() == 0,
              "the same Tab keypress delivered to a line edit elsewhere in "
              "the window does not cycle viewport tabs");

        // Remap deferral: once "viewport.tab.next" is bound to something
        // other than Tab, a literal Tab keypress on the viewport must stop
        // cycling (ordinary focus traversal resumes instead) -- the
        // interception has to read the registry's CURRENT binding, not
        // hard-code Key_Tab.
        ShortcutRegistry::setBinding(
            QStringLiteral("viewport.tab.next"),
            QKeySequence(Qt::CTRL | Qt::Key_BracketRight));
        QKeyEvent tabAfterRemap(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(&viewport, &tabAfterRemap);
        check(spy.count() == 0,
              "Tab no longer cycles once \"viewport.tab.next\" is remapped "
              "away from it");
        QKeyEvent remappedPress(QEvent::KeyPress, Qt::Key_BracketRight,
                                Qt::ControlModifier);
        QApplication::sendEvent(&viewport, &remappedPress);
        check(spy.count() == 1 && spy.takeFirst().at(0).toInt() == 1,
              "and the NEW binding cycles forward instead, through the "
              "ordinary keyPressEvent() path (Ctrl+] is not a key Qt ever "
              "routes through focusNextPrevChild())");

        ShortcutRegistry::resetAllToDefaults(); // leave no trace for later tests
    }

    // Panel show/hide hotkeys (Task 2): Ctrl/Cmd+0..9, one per dock, ten
    // registry entries in a "Panels" category.
    std::printf("Panel show/hide hotkeys (Ctrl/Cmd+0..9):\n");
    {
        ShortcutRegistry::resetAllToDefaults();
        static const std::pair<QString, int> kExpected[] = {
            {QStringLiteral("panel.toggle.structure"), 0},
            {QStringLiteral("panel.toggle.volumetricData"), 1},
            {QStringLiteral("panel.toggle.additionalOverlays"), 2},
            {QStringLiteral("panel.toggle.processes"), 3},
            {QStringLiteral("panel.toggle.representation"), 4},
            {QStringLiteral("panel.toggle.spatialReferences"), 5},
            {QStringLiteral("panel.toggle.visualEffects"), 6},
            {QStringLiteral("panel.toggle.orchestration"), 7},
            {QStringLiteral("panel.toggle.hpc"), 8},
            {QStringLiteral("panel.toggle.results"), 9},
        };
        bool allPresent = true;
        bool allCorrectDigit = true;
        bool allPanelsCategory = true;
        for (const auto& [id, digit] : kExpected) {
            const ShortcutAction* action = ShortcutRegistry::find(id);
            if (!action) {
                allPresent = false;
                continue;
            }
            // Qt::CTRL is the portable modifier — Cmd on macOS, Ctrl
            // elsewhere — with no per-platform branching in the source; on
            // THIS build platform it compares equal to whichever native
            // QKeySequence text the platform actually uses.
            const QKeySequence expectedKey(
                Qt::CTRL | static_cast<Qt::Key>(Qt::Key_0 + digit));
            if (action->defaultKey != expectedKey)
                allCorrectDigit = false;
            if (action->category != QStringLiteral("Panels"))
                allPanelsCategory = false;
        }
        check(allPresent, "all ten panel.toggle.* ids are registered");
        check(allCorrectDigit,
              "each is bound to Ctrl+<its digit> by default (Qt::CTRL, the "
              "portable modifier — Cmd on macOS, Ctrl on Linux/Windows)");
        check(allPanelsCategory, "all ten share the \"Panels\" category");

        // No conflict among the ten themselves, or against anything else
        // already registered — ShortcutRegistry::conflictFor() is the same
        // check Preferences -> Hotkeys runs on every remap attempt.
        bool noConflicts = true;
        for (const auto& [id, digit] : kExpected) {
            (void)digit;
            if (!ShortcutRegistry::conflictFor(ShortcutRegistry::binding(id), id)
                     .isEmpty())
                noConflicts = false;
        }
        check(noConflicts,
              "none of the ten collides with any other registered shortcut "
              "(a repo-wide grep also found zero pre-existing Ctrl/Cmd+digit "
              "bindings to conflict with in the first place)");

        // The actual toggle mechanism MainWindow::createMenusAndDocks()
        // wires per dock — findChild() by objectName, setShortcut() from
        // the registry, raise() whenever the action becomes checked —
        // reproduced here against a standalone QDockWidget rather than a
        // full MainWindow (which this test binary does not build), since
        // the pattern's correctness does not depend on which window hosts
        // it.
        QMainWindow host;
        auto* dock = new QDockWidget(QStringLiteral("Structure"), &host);
        dock->setObjectName(QStringLiteral("structureDock"));
        host.addDockWidget(Qt::LeftDockWidgetArea, dock);
        // A child's isVisible() reflects whether it is ACTUALLY realized on
        // screen, which requires the whole ancestor chain (including this
        // never-shown-by-default QMainWindow) to be shown too -- without
        // this, every isVisible() check below reads false regardless of
        // what setVisible()/trigger() just did.
        host.show();
        QAction* toggle = dock->toggleViewAction();
        toggle->setShortcut(ShortcutRegistry::binding(kExpected[0].first));
        int raiseCount = 0;
        QObject::connect(toggle, &QAction::toggled, dock, [dock, &raiseCount](bool visible) {
            if (visible) {
                ++raiseCount;
                dock->raise();
            }
        });

        check(toggle->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_0),
              "the dock's toggleViewAction() carries the Ctrl+0 shortcut, "
              "so the View menu displays it next to the entry automatically");

        // Start from a KNOWN state -- addDockWidget() alone leaves it
        // visible by default, which would make the first trigger() below a
        // hide rather than a show if left unchecked.
        dock->hide();
        check(!dock->isVisible() && !toggle->isChecked(),
              "starts hidden, with the action's own checked state agreeing");

        toggle->trigger(); // exactly what Qt does when the shortcut fires
        check(dock->isVisible() && raiseCount == 1,
              "trigger()ing the action while hidden shows AND raises the "
              "panel");
        toggle->trigger();
        check(!dock->isVisible(),
              "trigger()ing it again while visible hides it (plain "
              "show/hide toggle -- see the wiring comment in MainWindow.cpp "
              "for why this stays simple rather than a raise-then-hide "
              "three-state cycle for a manually tabified dock)");
        toggle->trigger();
        check(dock->isVisible() && raiseCount == 2,
              "and a third trigger() shows it again, raising once more");

        ShortcutRegistry::resetAllToDefaults(); // leave no trace for later tests
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
            // Waits for the CONDITION, not for a duration. A fixed sleep
            // tests the machine's load: under `ctest -j` the event loop can
            // be starved past the tick, and it can equally overshoot several
            // seconds, so neither "reads 0:01" nor "changed after 1.2 s" is
            // stable. Spinning until it moves, with a generous ceiling, tests
            // exactly the claim — the timer advances on its own.
            QString ticked = row->text(3);
            QElapsedTimer deadline;
            deadline.start();
            while (ticked == QStringLiteral("0:00")
                   && deadline.elapsed() < 15000) {
                spin(200);
                ticked = row->text(3);
            }
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

            // taskStatus() (Task 3, 2026-08-22): the by-ID counterpart to
            // rowStatus(), added so a baseline picker (MainWindow's
            // gpawBaselines()/gpawDensityFiles()/vaspChargeDensityFiles()/
            // espressoBaselines()) can filter OUT a crashed or still-
            // running parent instead of only checking whether it left a
            // plausible-looking file behind -- the real gap behind treating
            // "a CHGCAR exists" as "the SCF that wrote it converged",
            // found investigating proc_4's own diagnosis.
            check(panel.taskStatus(id) == ProcessManagerPanel::Status::Completed,
                  "taskStatus() reports the completed job's real status");
            check(panel.taskStatus(aborted) == ProcessManagerPanel::Status::Failed,
                  "and the failed one's, by the same ID it was registered "
                  "under");
            check(panel.taskStatus(9999) == ProcessManagerPanel::Status::Queued,
                  "an unregistered ID reports Queued rather than crashing "
                  "or fabricating a status");
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
    // The path from a completed Wannier run into the three modules that
    // consume its Hamiltonian.
    //
    // WHY THIS EXISTS. Boltzmann Transport, Berry Phase and cRPA used to offer
    // exactly two inputs: a wannier90 `_hr.dat` from somewhere else, or a
    // built-in toy. Calango's own Wannier run wrote no H(R), so the only route
    // to real data ran through the code the native solvers were written to
    // replace. The run emits `wannier_hr.dat` now; this is the reader, and the
    // cases below are the three ways a directory can fail to be one.
    std::printf("Wannier run -> Hamiltonian, for the modules that consume it:\n");
    {
        const auto writeRun = [](const QTemporaryDir& dir, const char* json,
                                 const char* hr) {
            QFile j(dir.filePath(QStringLiteral("wannier.json")));
            j.open(QIODevice::WriteOnly);
            j.write(json);
            j.close();
            if (hr) {
                QFile f(dir.filePath(QStringLiteral("wannier_hr.dat")));
                f.open(QIODevice::WriteOnly);
                f.write(hr);
            }
        };
        // Two orbitals, three R vectors along a: on-site plus one hop either
        // way. Small enough to read, and Hermitian, which is what the
        // consumers assume.
        const char* kHr =
            " test\n"
            "           2\n"
            "           3\n"
            "    1    1    1\n"
            "   -1    0    0    1    1          0.500000000000          0.000000000000\n"
            "   -1    0    0    2    1          0.000000000000          0.000000000000\n"
            "   -1    0    0    1    2          0.000000000000          0.000000000000\n"
            "   -1    0    0    2    2          0.500000000000          0.000000000000\n"
            "    0    0    0    1    1          1.000000000000          0.000000000000\n"
            "    0    0    0    2    1          0.250000000000          0.000000000000\n"
            "    0    0    0    1    2          0.250000000000          0.000000000000\n"
            "    0    0    0    2    2         -1.000000000000          0.000000000000\n"
            "    1    0    0    1    1          0.500000000000          0.000000000000\n"
            "    1    0    0    2    1          0.000000000000          0.000000000000\n"
            "    1    0    0    1    2          0.000000000000          0.000000000000\n"
            "    1    0    0    2    2          0.500000000000          0.000000000000\n";
        const char* kJson =
            R"({"total_spread":1.5,"nwannier":2,"hr":"wannier_hr.dat",)"
            R"("cell":[[3.0,0.0,0.0],[0.0,3.5,0.0],[0.0,0.0,4.0]],)"
            R"("centers":[[0.1,0.2,0.3],[1.5,0.0,0.0]],)"
            R"("spreads":[0.75,0.75],"cubes":[]})";

        QTemporaryDir good, legacy, missing;
        writeRun(good, kJson, kHr);
        // A run from before H(R) was written: no `hr` key at all.
        writeRun(legacy,
                 R"({"total_spread":1.5,"nwannier":2,"centers":[],)"
                 R"("spreads":[],"cubes":[]})",
                 nullptr);
        // Recorded, but the file is gone.
        writeRun(missing, kJson, nullptr);

        WannierRunData data;
        QString error;
        check(loadWannierRun(good.path(), &data, &error),
              "a completed run loads");
        check(error.isEmpty(), "with no error");
        check(data.nWannier == 2, "the Wannier count comes from the run");
        check(data.hamiltonian.orbitals() == 2,
              "and the Hamiltonian carries that many orbitals");
        check(data.hamiltonian.hoppings().size() == 3,
              "with all three H(R) blocks");
        // The cell is not decoration: the integer R vectors mean nothing
        // without it, and a default cubic cell would silently give every
        // hopping the wrong distance.
        check(std::abs(data.cell[0][0] - 3.0) < 1e-12
                  && std::abs(data.cell[1][1] - 3.5) < 1e-12
                  && std::abs(data.cell[2][2] - 4.0) < 1e-12,
              "the run's own cell is adopted, not a placeholder");
        check(data.centres.size() == 2 && data.spreads.size() == 2,
              "centres and spreads come across for the cRPA table");
        check(std::abs(data.spreads.at(0) - 0.75) < 1e-12,
              "with their values intact");

        // A one-band cosine: H(k) = 1 + 2*0.5*cos(2 pi k) along a, so the
        // band edges are exactly 2.0 and 0.0. Closed form, and it proves the
        // hoppings were read with the right sign and not halved by the
        // degeneracy division.
        const auto atGamma = data.hamiltonian.bands({0.0, 0.0, 0.0}, false);
        const auto atEdge = data.hamiltonian.bands({0.5, 0.0, 0.0}, false);
        check(atGamma.energies.size() == 2, "two bands at Gamma");
        // Gamma: diag(1+1, -1+1) = (2, 0) mixed by the 0.25 off-diagonal.
        double gammaMax = atGamma.energies.front();
        for (const double e : atGamma.energies)
            gammaMax = std::max(gammaMax, e);
        double edgeMin = atEdge.energies.front();
        for (const double e : atEdge.energies)
            edgeMin = std::min(edgeMin, e);
        check(std::abs(gammaMax - 2.0307764064) < 1e-6,
              "and the top of the band at Gamma is the closed-form value");
        check(std::abs(edgeMin - (-2.0307764064)) < 1e-6,
              "as is the bottom at the zone boundary");

        // The two ways a directory fails, told apart — because the remedies
        // differ: one says re-run, the other says the file moved.
        WannierRunData other;
        QString legacyError;
        check(!loadWannierRun(legacy.path(), &other, &legacyError),
              "a run with no recorded H(R) is refused");
        check(legacyError.contains(QStringLiteral("Re-run")),
              "and is told to re-run the Wannierization");
        QString missingError;
        check(!loadWannierRun(missing.path(), &other, &missingError),
              "a recorded-but-absent file is refused too");
        check(missingError.contains(QStringLiteral("moved")),
              "with a different remedy — the file went away");
        QString emptyError;
        QTemporaryDir nothing;
        check(!loadWannierRun(nothing.path(), &other, &emptyError)
                  && emptyError.contains(QStringLiteral("wannier.json")),
              "and a directory that is not a Wannier run at all says so");

        // Both consumers must offer the run and load it when picked.
        const QList<QPair<QString, QString>> runs{
            {QStringLiteral("#7 — Wannierization"), good.path()}};
        // These panels carry several combos, so the run selector is found by
        // what it HOLDS rather than by being first: a positional lookup would
        // pass or fail on unrelated layout changes.
        const auto runComboIn = [&good](const QWidget& widget) -> QComboBox* {
            for (QComboBox* combo : widget.findChildren<QComboBox*>())
                for (int i = 0; i < combo->count(); ++i)
                    if (combo->itemData(i).toString() == good.path())
                        return combo;
            return nullptr;
        };
        {
            BoltzmannTransportDialog dialog;
            dialog.setWannierRuns(runs);
            dialog.show();
            auto* combo = runComboIn(dialog);
            check(combo != nullptr && combo->count() == 2,
                  "Boltzmann Transport lists the completed run");
            if (combo && combo->count() == 2) {
                combo->setCurrentIndex(1);
                const auto labels = dialog.findChildren<QLabel*>();
                bool adopted = false;
                for (const QLabel* label : labels)
                    adopted = adopted
                        || label->text().contains(QStringLiteral("wannier_hr.dat"));
                check(adopted,
                      "and adopts its Hamiltonian when the run is picked");
            }
        }
        {
            CrpaDialog dialog;
            dialog.setWannierRuns(runs);
            dialog.show();
            auto* combo = runComboIn(dialog);
            check(combo != nullptr && combo->count() == 2,
                  "cRPA lists it too");
            if (combo && combo->count() == 2) {
                combo->setCurrentIndex(1);
                // The spreads decide which orbitals the correlated subspace
                // should hold, so a table still showing the placeholder 1.0
                // would have the user choosing on invented numbers.
                auto* table = dialog.findChild<QTableWidget*>();
                check(table != nullptr && table->rowCount() == 2,
                      "and rebuilds its orbital table from the run");
                if (table && table->rowCount() == 2 && table->item(0, 2))
                    check(table->item(0, 2)->text().startsWith(
                              QStringLiteral("0.75")),
                          "with the run's measured spreads, not placeholders");
            }
        }
    }

    // The Wannier setup's baseline pre-condition.
    //
    // ASE's Wannier needs the FULL Brillouin zone; a single point that folded
    // its k-set into the irreducible wedge cannot feed it, and until this
    // warning existed the only way to find that out was to run the job and
    // read the traceback. Three states, and all three are pinned here —
    // including "unknown", which must warn WITHOUT claiming symmetry was on.
    std::printf("Wannier setup: baseline bands / k-points / symmetry:\n");
    {
        // Three baselines, differing only in what they recorded.
        QTemporaryDir folded, full, legacy;
        const auto writeRun = [](const QTemporaryDir& dir, const char* json,
                                 const char* log) {
            if (json) {
                QFile f(dir.filePath(QStringLiteral("calculator.json")));
                f.open(QIODevice::WriteOnly);
                f.write(json);
            }
            if (log) {
                QFile f(dir.filePath(QStringLiteral("gpaw.out")));
                f.open(QIODevice::WriteOnly);
                f.write(log);
            }
        };
        writeRun(folded,
                 R"({"engine":"GPAW","engine_kind":5,"kpts":[16,16,16],)"
                 R"("symmetry_off":false,"xc":"PBE"})",
                 "Number of symmetries: 6\n"
                 "  Number of BZ points: 4096\n"
                 "  Number of IBZ points: 417\n"
                 "  Monkhorst-Pack size: [16, 16, 16]\n"
                 "Bands:           19\n");
        writeRun(full,
                 R"({"engine":"GPAW","engine_kind":5,"kpts":[8,8,8],)"
                 R"("symmetry_off":true,"xc":"PBE"})",
                 "Number of symmetries: 1\n"
                 "  Number of BZ points: 512\n"
                 "  Number of IBZ points: 512\n"
                 "  Monkhorst-Pack size: [8, 8, 8]\n"
                 "Bands:           26\n");
        // An older Calango's sidecar: no symmetry flag at all.
        writeRun(legacy,
                 R"({"engine":"GPAW","engine_kind":5,"kpts":[4,4,4],)"
                 R"("xc":"PBE"})",
                 nullptr);

        calango::pybridge::PythonEngine python;
        auto structure = std::make_shared<calango::core::Structure>();
        WannierWizard wizard(structure);
        wizard.setDensityBaselines({
            {QStringLiteral("#1 folded"), folded.path()},
            {QStringLiteral("#2 full"), full.path()},
            {QStringLiteral("#3 legacy"), legacy.path()},
        });
        wizard.show();
        check(true, "constructs with baselines");

        // The two labels, found by content rather than by object name: the
        // summary is the one that states the check, the warning the one that
        // carries the marker.
        const auto labels = wizard.findChildren<QLabel*>();
        const auto findLabel = [&labels](const QString& fragment) -> QLabel* {
            for (QLabel* label : labels)
                if (label->text().contains(fragment))
                    return label;
            return nullptr;
        };
        const auto visibleWarning = [&labels]() -> QLabel* {
            for (QLabel* label : labels)
                if (label->text().contains(QString::fromUtf8("⚠"))
                    && !label->isHidden())
                    return label;
            return nullptr;
        };

        // Index 1 = the folded baseline (index 0 is the "(none)" entry).
        wizard.findChild<QComboBox*>()->setCurrentIndex(1);
        {
            QLabel* summary = findLabel(QStringLiteral("Symmetry:"));
            check(summary != nullptr, "folded: a symmetry line is shown");
            if (summary) {
                check(summary->text().contains(QStringLiteral(">on<")),
                      "folded: it reads \"Symmetry: on\"");
                check(summary->text().contains(QStringLiteral(">19<")),
                      "folded: the band count is shown");
                check(summary->text().contains(QStringLiteral("16×16×16")),
                      "folded: and the k-mesh");
                check(summary->text().contains(QStringLiteral("417"))
                          && summary->text().contains(QStringLiteral("4096")),
                      "folded: with how many of the zone's points survived");
            }
            QLabel* warning = visibleWarning();
            check(warning != nullptr, "folded: the warning is visible");
            if (warning) {
                check(warning->text().contains(QStringLiteral("#d9534f")),
                      "folded: in the application's red");
                check(warning->text().contains(QStringLiteral("required")),
                      "folded: stating that \"Symmetry: off\" is required");
                check(warning->text().contains(QStringLiteral("full")),
                      "folded: and why — the full Brillouin-zone k-set");
            }
        }

        // Changing the selection must restate all of it immediately: a summary
        // that lags one selection behind is worse than none, because it
        // describes a calculation the user is no longer looking at.
        wizard.findChild<QComboBox*>()->setCurrentIndex(2);
        {
            QLabel* summary = findLabel(QStringLiteral("Symmetry:"));
            check(summary && summary->text().contains(QStringLiteral(">off<")),
                  "full zone: the line updates to \"Symmetry: off\"");
            check(summary && summary->text().contains(QStringLiteral(">26<")),
                  "full zone: with that baseline's own band count");
            check(visibleWarning() == nullptr,
                  "full zone: and no warning is shown at all");
        }

        wizard.findChild<QComboBox*>()->setCurrentIndex(3);
        {
            QLabel* summary = findLabel(QStringLiteral("Symmetry:"));
            check(summary && summary->text().contains(QStringLiteral("unknown")),
                  "legacy: an undetermined setting says so");
            QLabel* warning = visibleWarning();
            check(warning != nullptr, "legacy: and still warns");
            if (warning) {
                // Amber, not red, and phrased as not knowing. Reporting this
                // as "symmetry is on" would put a re-run demand on a baseline
                // that may be perfectly good.
                check(warning->text().contains(QStringLiteral("#d08a4a")),
                      "legacy: in the cautionary amber, not the error red");
                check(!warning->text().contains(QStringLiteral("was run <i>with</i>")),
                      "legacy: without claiming symmetry was on");
            }
        }

        // "(none)" runs a fresh SCF, and that script sets symmetry="off"
        // itself — so there is nothing left to warn about.
        wizard.findChild<QComboBox*>()->setCurrentIndex(0);
        check(visibleWarning() == nullptr,
              "no baseline: no warning (the fresh SCF forces symmetry off)");
    }

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
        // "IBZ", not "irreducible BZ": the label was shortened to "Use IBZ
        // symmetry" when the wrapped explanatory paragraph under it was
        // dropped for clipping the page.
        const auto ibz = boxNamed(QStringLiteral("IBZ"));
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

    std::printf("Thermodynamic Integration wizard:\n");
    {
        calango::pybridge::PythonEngine python;
        calango::gui::ThermodynamicIntegrationWizard wizard;
        check(true, "constructs");

        auto* reference =
            wizard.findChild<QComboBox*>(QStringLiteral("tiReferenceCombo"));
        auto* schedule =
            wizard.findChild<QComboBox*>(QStringLiteral("tiScheduleCombo"));
        auto* quadrature =
            wizard.findChild<QComboBox*>(QStringLiteral("tiQuadratureCombo"));
        auto* windows =
            wizard.findChild<QSpinBox*>(QStringLiteral("tiWindowsSpin"));
        auto* jobs = wizard.findChild<QSpinBox*>(QStringLiteral("tiJobsSpin"));
        auto* hysteresis =
            wizard.findChild<QCheckBox*>(QStringLiteral("tiHysteresisCheck"));
        auto* equilibration = wizard.findChild<QSpinBox*>(
            QStringLiteral("tiEquilibrationSpin"));
        check(reference && schedule && quadrature && windows && jobs
                  && hysteresis && equilibration,
              "exposes the path controls");

        // Gauss-Legendre weights are only valid on Gauss-Legendre nodes, so
        // the two controls are not independent: the schedule wins and the
        // quadrature follows. Letting the pair disagree would have the core
        // silently fall back to a trapezoid on a grid chosen for Gauss.
        if (schedule && quadrature) {
            schedule->setCurrentIndex(0); // Gauss-Legendre nodes
            check(!quadrature->isEnabled()
                      && quadrature->currentData().toInt()
                          == static_cast<int>(
                              calango::core::TiQuadrature::GaussLegendre),
                  "Gauss-Legendre nodes lock the quadrature to Gauss-Legendre");
            schedule->setCurrentIndex(1); // Uniform
            check(quadrature->isEnabled()
                      && quadrature->currentData().toInt()
                          != static_cast<int>(
                              calango::core::TiQuadrature::GaussLegendre),
                  "and a uniform grid frees it, off Gauss-Legendre");
        }

        // Splitting the run withdraws hysteresis: a reversed sweep needs one
        // sequential chain of windows, and a job owning a slice has none.
        if (jobs && hysteresis && windows) {
            windows->setValue(8);
            hysteresis->setChecked(true);
            jobs->setValue(4);
            check(!hysteresis->isEnabled() && !hysteresis->isChecked(),
                  "splitting the run withdraws the hysteresis sweep");
            check(wizard.jobCount() == 4 && wizard.scripts().size() == 4,
                  "and produces one script per job");
            // Every script must declare the WHOLE path, or a slice that
            // renumbered it would write window 000 and look complete.
            const QStringList slices = wizard.scripts();
            bool sameLambdas = true;
            for (const QString& text : slices)
                sameLambdas = sameLambdas
                    && text.section(QStringLiteral("LAMBDAS = "), 1, 1)
                            .section(QLatin1Char('\n'), 0, 0)
                        == slices.front()
                               .section(QStringLiteral("LAMBDAS = "), 1, 1)
                               .section(QLatin1Char('\n'), 0, 0);
            check(sameLambdas, "each declaring the same full lambda path");
            jobs->setValue(1);
            check(hysteresis->isEnabled(),
                  "and collapsing back to one job offers it again");
        }

        // generateScript() must ALWAYS be the whole path: the orchestration
        // canvas runs one node as one process and never asks for scripts().
        if (jobs && windows) {
            jobs->setValue(3);
            const QString single = wizard.script();
            check(single.contains(QStringLiteral("WINDOW_INDICES = [0, 1, 2, 3, "
                                                 "4, 5, 6, 7]")),
                  "the previewed script covers every window, not one slice");
            jobs->setValue(1);
        }

        // The endpoint singularity has to be visible before the run, not
        // discovered afterwards: an ideal-gas reference on a grid that
        // includes lambda = 0 is the combination that under-converges
        // silently.
        if (reference && schedule) {
            reference->setCurrentIndex(0); // ideal gas
            schedule->setCurrentIndex(1);  // uniform, includes lambda = 0
            bool warned = false;
            for (QLabel* label : wizard.findChildren<QLabel*>())
                warned = warned
                    || label->text().contains(QStringLiteral("λ = 0 against an "
                                                             "ideal gas"));
            check(warned, "warns about lambda = 0 against an ideal gas");
            schedule->setCurrentIndex(0); // back to Gauss-Legendre
        }

        // The module's cold entry point. The Simulation menu carries ONE TI
        // action now — the wizard — so a finished run from an earlier session
        // or from a cluster is reachable only through this button, the same
        // button on the results window, and the Processes panel. If the button
        // stops emitting, the only symptom is that old runs quietly become
        // unopenable, which no other assertion in this file would notice.
        {
            auto* loadResults = wizard.findChild<QPushButton*>(
                QStringLiteral("tiLoadResultsButton"));
            check(loadResults != nullptr,
                  "the wizard carries a Load Results button");
            if (loadResults) {
                int emitted = 0;
                QObject::connect(
                    &wizard,
                    &ThermodynamicIntegrationWizard::loadResultsRequested,
                    &wizard, [&emitted] { ++emitted; });
                loadResults->click();
                check(emitted == 1,
                      "which asks the host to open a run rather than doing it");
                // Return on this page belongs to the wizard's Next. A default
                // button here would open a file dialog instead.
                check(!loadResults->isDefault()
                          && !loadResults->autoDefault(),
                      "and never steals the Return key");
            }
        }

        exerciseControls(&wizard);
        check(true, "survives every control being toggled");
    }

    std::printf("Thermodynamic Integration results reader:\n");
    {
        // An INCOMPLETE run must produce no free energy at all. This is the
        // behaviour the whole module is built around: quadrature weights are a
        // property of the node set, so an integral over the surviving windows
        // is a different integral, not a noisier one.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("ti.json"));
        QFile file(path);
        file.open(QIODevice::WriteOnly);
        file.write(R"JSON({
          "schema": "calango.thermodynamic_integration/1",
          "reference": "ideal_gas", "schedule": "uniform",
          "quadrature": "trapezoid", "windows_expected": 3,
          "natoms": 2, "volume_A3": 1000.0, "temperature_K": 300.0,
          "pressure_GPa": 0.0, "masses_amu": [39.948, 39.948],
          "complete": false,
          "paths": {"forward": {"complete": false, "missing": [2],
            "failed": [], "windows": [
              {"index": 0, "lambda": 0.0, "status": "ok", "samples": 4,
               "mean_dudl_eV": 1.0, "variance_dudl_eV2": 0.0,
               "series_eV": [1.0, 1.0, 1.0, 1.0], "mean_volume_A3": 1000.0},
              {"index": 1, "lambda": 0.5, "status": "ok", "samples": 4,
               "mean_dudl_eV": 1.0, "variance_dudl_eV2": 0.0,
               "series_eV": [1.0, 1.0, 1.0, 1.0], "mean_volume_A3": 1000.0}]}}
        })JSON");
        file.close();

        const auto report =
            calango::gui::readThermodynamicIntegrationRun(path);
        check(report.parsed, "the summary parses");
        check(!report.complete, "a missing window leaves the path incomplete");
        check(report.assembly.helmholtzEv == 0.0
                  && report.assembly.gibbsEv == 0.0,
              "and no free energy is produced");
        check(report.text.contains(QStringLiteral("INCOMPLETE")),
              "the report says so in as many words");
    }

    std::printf("Thermodynamic Integration CSV export:\n");
    {
        // What "Export Results…" writes, on a path with one window that RAN
        // and failed. Two things the file must not do, both of which produce a
        // CSV that opens fine and says something false.
        //
        // (1) Drop the failed window. Three windows were run; two reported. A
        //     file with two rows and no sign of the third describes a shorter
        //     path that converged.
        // (2) Write decimal commas. This binary is the only one that builds a
        //     QApplication, which is what sets LC_NUMERIC from the environment
        //     — on pt_BR / de_DE / fr_FR a printf-formatted "0.5" becomes
        //     "0,5" and every row silently gains a column. QString::number
        //     formats via QLocale::c(), which is why it is used here; this
        //     checks that it stays used, under a comma locale engaged on
        //     purpose.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("ti.json"));
        QFile file(path);
        file.open(QIODevice::WriteOnly);
        file.write(R"JSON({
          "schema": "calango.thermodynamic_integration/1",
          "reference": "ideal_gas", "schedule": "uniform",
          "quadrature": "trapezoid", "windows_expected": 3,
          "natoms": 2, "volume_A3": 1000.0, "temperature_K": 300.0,
          "pressure_GPa": 0.0, "masses_amu": [39.948, 39.948],
          "complete": false,
          "paths": {"forward": {"complete": false, "missing": [],
            "failed": [1], "windows": [
              {"index": 0, "lambda": 0.25, "status": "ok", "samples": 4,
               "mean_dudl_eV": 1.5, "variance_dudl_eV2": 0.0,
               "series_eV": [1.5, 1.5, 1.5, 1.5], "mean_volume_A3": 1000.0},
              {"index": 1, "lambda": 0.5, "status": "failed",
               "error": "the MD run diverged"},
              {"index": 2, "lambda": 0.75, "status": "ok", "samples": 4,
               "mean_dudl_eV": 2.5, "variance_dudl_eV2": 0.0,
               "series_eV": [2.5, 2.5, 2.5, 2.5], "mean_volume_A3": 1000.0}]}}
        })JSON");
        file.close();

        const auto report =
            calango::gui::readThermodynamicIntegrationRun(path);

        const char* savedLocale = std::setlocale(LC_NUMERIC, nullptr);
        const std::string savedName = savedLocale ? savedLocale : "C";
        bool commaLocale = false;
        for (const char* name : {"pt_BR.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8"})
            if (std::setlocale(LC_NUMERIC, name)) {
                commaLocale = true;
                break;
            }

        const QString csv = calango::gui::thermodynamicIntegrationCsv(report);
        std::setlocale(LC_NUMERIC, savedName.c_str());

        int dataRows = 0;
        int failedRows = 0;
        bool everyFieldParses = true;
        for (const QString& row : csv.split(QLatin1Char('\n'))) {
            if (row.isEmpty() || row.startsWith(QLatin1Char('#'))
                || row.startsWith(QLatin1String("index,")))
                continue;
            ++dataRows;
            // Seven columns, always: index, lambda, dudl, error, tau, samples,
            // status. A comma written into a number is exactly what breaks it,
            // and the failed row's quoted reason must not split either.
            const QStringList fields = row.split(QLatin1Char(','));
            if (fields.size() != 7) {
                everyFieldParses = false;
                continue;
            }
            if (fields.at(6) != QLatin1String("ok")) {
                ++failedRows;
                continue;
            }
            double value = 0.0;
            for (int column = 1; column <= 4; ++column)
                if (!calango::core::localeSafeParse(
                        fields.at(column).toStdString(), &value))
                    everyFieldParses = false;
        }
        check(dataRows == 3,
              "the CSV keeps a row for the window that failed");
        check(failedRows == 1, "and labels it rather than blanking it");
        check(everyFieldParses,
              commaLocale
                  ? "every number reads back with a dot under a comma locale"
                  : "every number reads back with a dot");
        check(csv.contains(QLatin1String("# complete,no")),
              "the header says the run is incomplete");
        check(!csv.contains(QLatin1String("# F_eV,")),
              "and carries no free energy, matching the report");
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

    // The Vibrational Mode Analysis module. It loads a phonon run — and with
    // it repopulates two combo boxes, whose currentIndexChanged slots read
    // widgets built earlier — from INSIDE its own constructor, which is the
    // exact shape this test exists for.
    //
    // What is pinned beyond "it constructs": the refusals. A directory that is
    // not a phonon run, and a run whose eigenvectors cover a different number
    // of atoms than the structure they would be drawn on, must both leave the
    // animation dead rather than producing a convincing wrong picture.
    {
        std::printf("VibrationalAnalysisDialog:\n");
        QTemporaryDir scratch;
        check(scratch.isValid(), "a scratch run directory");
        if (scratch.isValid()) {
            const auto writeJson = [](const QString& path,
                                      const QJsonObject& object) {
                QFile file(path);
                file.open(QIODevice::WriteOnly);
                file.write(QJsonDocument(object).toJson());
            };

            // A two-atom Gamma-only run: one rigid translation (the acoustic
            // sum rule must find it) and one stretch, written in the ASE
            // driver's bare-triple encoding.
            QJsonObject band;
            QJsonArray gammaRow;
            gammaRow.append(0.0);
            gammaRow.append(1800.0);
            QJsonArray frequencies;
            frequencies.append(gammaRow);
            band.insert(QStringLiteral("frequencies"), frequencies);
            writeJson(scratch.path() + QStringLiteral("/phonon_band.json"), band);

            const auto triple = [](double x, double y, double z) {
                QJsonArray v;
                v.append(x);
                v.append(y);
                v.append(z);
                return v;
            };
            QJsonArray branches;
            QJsonArray acoustic;
            acoustic.append(triple(0.7071067811865476, 0.0, 0.0));
            acoustic.append(triple(0.7071067811865476, 0.0, 0.0));
            branches.append(acoustic);
            QJsonArray stretch;
            stretch.append(triple(0.7071067811865476, 0.0, 0.0));
            stretch.append(triple(-0.7071067811865476, 0.0, 0.0));
            branches.append(stretch);
            QJsonObject qpoint;
            qpoint.insert(QStringLiteral("label"), QStringLiteral("G"));
            qpoint.insert(QStringLiteral("q"), triple(0.0, 0.0, 0.0));
            qpoint.insert(QStringLiteral("frequencies"), gammaRow);
            qpoint.insert(QStringLiteral("eigenvectors"), branches);
            QJsonArray qpoints;
            qpoints.append(qpoint);
            QJsonObject modes;
            modes.insert(QStringLiteral("eigenvector_convention"),
                         QStringLiteral("displacement"));
            modes.insert(QStringLiteral("qpoints"), qpoints);
            writeJson(scratch.path() + QStringLiteral("/phonon_modes.json"),
                      modes);

            // Two identical atoms, so the mode's displacement pattern and its
            // mass-weighted form coincide and the acoustic branch is exactly a
            // rigid translation.
            auto reference = std::make_shared<calango::core::Structure>();
            for (int i = 0; i < 2; ++i) {
                calango::core::Atom atom;
                atom.atomicNumber = 14;
                atom.position = {2.35 * i, 0.0, 0.0};
                reference->addAtom(atom);
            }

            VibrationalAnalysisDialog dialog({}, scratch.path(), reference);
            check(true, "constructs on a completed phonon run");

            auto* qCombo = dialog.findChildren<QComboBox*>().value(1);
            auto* modeCombo = dialog.findChildren<QComboBox*>().value(2);
            check(qCombo != nullptr && qCombo->count() == 1,
                  "the q-point combo is filled from the run");
            check(modeCombo != nullptr && modeCombo->count() == 2,
                  "and the mode combo lists both branches");
            // Read off the acoustic sum rule, not off the branch index.
            check(modeCombo != nullptr
                      && modeCombo->itemText(0).contains(
                          QStringLiteral("acoustic")),
                  "branch 1 is identified as acoustic");
            check(modeCombo != nullptr
                      && !modeCombo->itemText(1).contains(
                          QStringLiteral("acoustic")),
                  "and the stretch is not");

            // The trajectory request is the module's only output; it must
            // carry one full period rather than a single frame.
            std::size_t frameCount = 0;
            QObject::connect(
                &dialog, &VibrationalAnalysisDialog::modeTrajectoryRequested,
                &dialog,
                [&frameCount](
                    const std::vector<
                        std::shared_ptr<calango::core::Structure>>& frames,
                    const QString&) { frameCount = frames.size(); });
            if (modeCombo)
                modeCombo->setCurrentIndex(1);
            QMetaObject::invokeMethod(&dialog, "createModeTrajectory");
            check(frameCount == 32, "Create Mode Trajectory emits one period");

            exerciseControls(&dialog);
            check(true, "survives every control being toggled");
        }

        // A directory with no phonon data at all: the animation controls must
        // be dead, not merely empty.
        QTemporaryDir empty;
        if (empty.isValid()) {
            VibrationalAnalysisDialog dialog({}, empty.path(), nullptr);
            bool anyEnabled = false;
            for (QPushButton* button : dialog.findChildren<QPushButton*>())
                if (button->text().contains(QStringLiteral("Trajectory")))
                    anyEnabled = anyEnabled || button->isEnabled();
            check(!anyEnabled,
                  "a directory with no phonon_band.json refuses to animate");
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

    // The CVM comparison viewer. Three curves on one canvas is the whole
    // deliverable, so what is checked is that all three are actually there
    // and that the ideal one is drawn as a BASELINE against temperature but
    // as a CURVE against composition — where it is -x ln x - (1-x) ln(1-x)
    // and drawing it flat would be simply wrong.
    {
        std::printf("CvmComparisonWindow:\n");
        CvmComparisonWindow window;
        // Rendering headless is the point: the plot holds no viewport and no
        // GL context, so it can be exercised in this test at all.
        QImage image(720, 460, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            window.findChild<CvmComparisonPlot*>()->render(
                painter, QRectF(0, 0, 720, 460));
        }
        // The canvas must not come back blank: count pixels that are neither
        // the white canvas nor the pale grid.
        int ink = 0;
        for (int y = 0; y < image.height(); y += 2)
            for (int x = 0; x < image.width(); x += 2) {
                const QColor c = image.pixelColor(x, y);
                if (c.red() < 200 || c.green() < 200 || c.blue() < 200)
                    ++ink;
            }
        check(ink > 200,
              "the comparison canvas renders curves rather than an empty "
              "frame (" + std::to_string(ink) + " ink samples)");
        exerciseControls(&window);
        check(true, "and survives every control being toggled without a key");

        // Long-range order must actually REACH the canvas. The four-sublattice
        // solver existed in core for a while with no way to run it from the
        // interface, which is the failure this check exists to prevent: the
        // curve count and the T_c marker both have to change when the box is
        // ticked.
        auto* lro = window.findChild<QCheckBox*>();
        check(lro != nullptr, "the long-range-order control exists");
        if (lro) {
            // exerciseControls() above cycled every spin box, including the
            // species count — and the stoichiometry snap only fires for a
            // BINARY. Without resetting it this block silently measured a
            // multi-component system that cannot form L1_2, and reported a
            // near-empty canvas as a pass.
            if (auto* species = window.findChild<QSpinBox*>())
                species->setValue(2);
            // Cu3Au: x = 1/4, ordering interaction, a range that brackets T_c.
            lro->setChecked(true);
            QMetaObject::invokeMethod(&window, "recompute");
            QImage ordered(720, 460, QImage::Format_ARGB32_Premultiplied);
            ordered.fill(Qt::white);
            {
                QPainter painter(&ordered);
                window.findChild<CvmComparisonPlot*>()->render(
                    painter, QRectF(0, 0, 720, 460));
            }
            int ink = 0;
            for (int y = 0; y < ordered.height(); y += 2)
                for (int x = 0; x < ordered.width(); x += 2) {
                    const QColor c = ordered.pixelColor(x, y);
                    if (c.red() < 200 || c.green() < 200 || c.blue() < 200)
                        ++ink;
                }
            // A real comparison, not merely "something drew": three curves
            // plus the shaded gap and the T_c rule should ink comparably to
            // the homogeneous view. The earlier threshold of 200 passed on an
            // essentially blank plot.
            check(ink > 1500,
                  "with long-range order enabled the canvas renders the "
                  "ordered/disordered comparison ("
                      + std::to_string(ink) + " ink samples)");
            lro->setChecked(false);
            QMetaObject::invokeMethod(&window, "recompute");
            check(true, "and toggling back restores the homogeneous view");
        }
    }

    // The TI integrand plot. Delta_F is a number; every characteristic TI
    // failure is a shape, so what is checked is that the shape actually
    // renders — and, with a negative control, that the check could fail.
    {
        std::printf("TiIntegrandPlot:\n");
        TiIntegrandPlot plot;
        std::vector<calango::core::TiWindowSample> windows;
        for (int i = 0; i < 6; ++i) {
            calango::core::TiWindowSample w;
            w.index = i;
            w.lambda = (i + 0.5) / 6.0;
            w.dudlEv = -50.0 - 60.0 * w.lambda;
            w.dudlErrorEv = 0.5 + 3.0 * (1.0 - w.lambda);
            w.samples = 300;
            w.ok = true;
            windows.push_back(w);
        }
        plot.setWindows(windows, {});
        plot.setEndpointWarning(true, QStringLiteral("test"));
        const auto ink = [](const QImage& image) {
            int n = 0;
            for (int y = 0; y < image.height(); y += 2)
                for (int x = 0; x < image.width(); x += 2) {
                    const QColor c = image.pixelColor(x, y);
                    if (c.red() < 200 || c.green() < 200 || c.blue() < 200)
                        ++n;
                }
            return n;
        };
        QImage drawn(640, 380, QImage::Format_ARGB32_Premultiplied);
        drawn.fill(Qt::white);
        {
            QPainter painter(&drawn);
            plot.render(painter, QRectF(0, 0, 640, 380));
        }
        const int withData = ink(drawn);
        // Negative control: no windows must give an essentially empty canvas,
        // so the count above is measuring the curve and not the frame.
        TiIntegrandPlot empty;
        QImage blank(640, 380, QImage::Format_ARGB32_Premultiplied);
        blank.fill(Qt::white);
        {
            QPainter painter(&blank);
            empty.render(painter, QRectF(0, 0, 640, 380));
        }
        const int withoutData = ink(blank);
        check(withData > 4 * std::max(1, withoutData),
              "the integrand, its error bars and the shaded integral render ("
                  + std::to_string(withData) + " vs "
                  + std::to_string(withoutData) + " ink samples empty)");
        check(plot.exportImage(QDir::tempPath()
                                   + QStringLiteral("/calango_ti_plot.png"),
                               2.0),
              "and the plot exports through the same render()");
    }

    std::printf("Wavefunctions results viewer:\n");
    {
        // Mirrors XasResultsWindow's own staged-fixture pattern: a minimal
        // but schema-complete wavefunctions.json, matching exactly what
        // WavefunctionScriptGenerator writes.
        const QString path =
            QDir::temp().filePath(QStringLiteral("calango_wavefunctions.json"));
        QFile file(path);
        check(file.open(QIODevice::WriteOnly), "a results file can be staged");
        file.write(R"({"baseline_dir":"/jobs/proc_1",
                       "gpw":"/jobs/proc_1/single_point.gpw",
                       "quantity":"density","all_electron":false,
                       "periodic":true,
                       "states":[
                         {"spin":0,"kpt":0,"band":4,"energy_eV":-0.72,
                          "occupation":2.0,"quantity":"density",
                          "all_electron":false,"periodic":true,
                          "complex_valued":false,
                          "cube":"psi_n4_k0_spin-up_density.cube"},
                         {"spin":1,"kpt":1,"band":5,"energy_eV":1.02,
                          "occupation":0.0,"quantity":"density",
                          "all_electron":true,"periodic":true,
                          "complex_valued":true,
                          "cube":"psi_n5_k1_spin-down_density.cube"}
                       ]})");
        file.close();

        WavefunctionsResultsViewer viewer;
        check(viewer.loadResults(path), "and loaded");
        check(viewer.hasData(), "with data present");
        check(qobject_cast<QDialog*>(&viewer) != nullptr,
              "it is a QDialog, like every other results viewer");

        const auto tables = viewer.findChildren<QTableWidget*>();
        check(!tables.empty(), "the summary table exists");
        if (!tables.empty()) {
            QTableWidget* table = tables.front();
            check(table->rowCount() == 2,
                  "one row per state — both from this run, not just one");
        }
        check(!viewer.loadResults(QStringLiteral("/nonexistent/wf.json")),
              "a missing file is reported rather than shown empty");
    }

    std::printf("Wavefunctions wizard — zero-selection warning:\n");
    {
        // REGRESSION: the warning label's own itemChanged connection used
        // Qt::UniqueConnection with a lambda target, which Qt logs as
        // invalid at runtime ("unique connections require a pointer to
        // member function") and then — on this Qt version — makes NO
        // connection at all rather than falling back to a plain one. The
        // label was permanently stuck showing whatever its first update
        // computed, never reacting to a checkbox click again. Verified
        // directly on the table rather than through a real baseline peek
        // (which needs GPAW) — the connection this guards is between the
        // table and the label, independent of where the rows came from.
        // SimulationWizardBase reads the embedded interpreter while
        // building its Calculator Settings stage, and PythonEngine::
        // instance() asserts rather than lazily constructing one.
        calango::pybridge::PythonEngine python;
        auto structure = std::make_shared<calango::core::Structure>();
        WavefunctionsWizard wizard(structure);

        auto* table = wizard.findChild<QTableWidget*>();
        check(table != nullptr, "the state table exists");
        QLabel* warning = nullptr;
        for (QLabel* label : wizard.findChildren<QLabel*>())
            if (label->text().contains(QStringLiteral("No states are ticked")))
                warning = label;
        check(warning != nullptr,
              "the zero-selection warning label exists and is populated "
              "before any baseline is even selected (an empty table is "
              "zero states too)");
        // isHidden() reflects the label's OWN explicit setVisible() call;
        // isVisible() would additionally require the whole wizard to be
        // shown, which it deliberately is not here.
        check(warning && !warning->isHidden(),
              "and starts visible — nothing is selected yet");

        if (table) {
            table->setRowCount(1);
            auto* check_item = new QTableWidgetItem();
            check_item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            check_item->setCheckState(Qt::Unchecked);
            table->setItem(0, 0, check_item);

            check_item->setCheckState(Qt::Checked);
            check(warning && warning->isHidden(),
                  "ticking a row hides the warning — the itemChanged "
                  "connection actually fires");

            check_item->setCheckState(Qt::Unchecked);
            check(warning && !warning->isHidden(),
                  "and unticking it again brings the warning back — this "
                  "is the same connection firing a second time, not a "
                  "one-shot Qt::UniqueConnection accident");
        }
    }

    std::printf("Energy Diagram viewer window type:\n");
    {
        // REGRESSION: EnergyDiagramViewer was a plain QWidget. Shown via
        // show() with a parent (MainWindow::openEnergyDiagramResults() does
        // exactly that — new EnergyDiagramViewer(this)), a plain QWidget
        // with a parent renders as a frameless CHILD embedded inside that
        // parent's own client area rather than an independent top-level
        // window — no title bar, no close button, not movable, and its
        // content bleeds into whatever the parent draws underneath it
        // (this is what the user actually saw: a garbled overlap with
        // MainWindow's own tab bar, not a level-diagram problem). Every
        // other results viewer in this codebase (XasResultsWindow,
        // BandPdosWindow, MlwfViewer, OpticsResultsWindow,
        // GwResultsWindow, ...) is a QDialog, which is a real top-level
        // window even when constructed with a parent — EnergyDiagramViewer
        // must be one too, and this checks BOTH the class itself and the
        // actual runtime behavior with a parent, since a *parentless*
        // widget can look like a window regardless of its base class and
        // would have hidden this exact bug (verified against a real
        // offscreen render during the fix: the parentless case reported
        // isWindow()==true even on the OLD, buggy QWidget-based class).
        check(qobject_cast<QDialog*>(static_cast<QWidget*>(nullptr)) == nullptr,
              "sanity: qobject_cast on null is null (guards the real check "
              "below against a false pass)");

        QWidget standInMainWindow;
        auto* viewer = new EnergyDiagramViewer(&standInMainWindow);
        check(qobject_cast<QDialog*>(viewer) != nullptr,
              "EnergyDiagramViewer is a QDialog, like every other results "
              "viewer in this app — not a plain QWidget");
        check(viewer->isWindow(),
              "and behaves as an independent top-level window even when "
              "constructed with a parent (closeable, movable) rather than "
              "an embedded child of it");
        check(viewer->parentWidget() == &standInMainWindow,
              "while still recording the parent MainWindow passed in, for "
              "ownership/stacking — being a real window does not mean "
              "losing the parent relationship");
        delete viewer;
    }

    std::printf("Energy Diagram appearance (white/blue/red) and export "
               "buttons:\n");
    {
        // REGRESSION: the level diagram used to follow the THEMED palette
        // (QPalette::Base/Text/Mid), so in Dark theme it drew a dark canvas
        // with low-contrast level bars — the user asked for a fixed white/
        // blue/red scheme, like every other data plot in the app
        // (PlotPalette), independent of the active Qt theme.
        const EnergyDiagramStyle defaults;
        check(defaults.canvasBackground == QColor(255, 255, 255),
              "default canvas is pure white, not a themed base color");
        check(defaults.occupiedColor == QColor(0x1f, 0x77, 0xb4),
              "default occupied-level color is PlotPalette's tab10 blue");
        check(defaults.unoccupiedColor == QColor(0xd6, 0x27, 0x28),
              "default unoccupied-level color is PlotPalette's tab10 red");

        EnergyLevelDiagramWidget widget;
        widget.resize(300, 300);

        const auto countNear = [](const QImage& image, const QColor& target) {
            int n = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const QColor c = image.pixelColor(x, y);
                    if (std::abs(c.red() - target.red()) <= 12
                        && std::abs(c.green() - target.green()) <= 12
                        && std::abs(c.blue() - target.blue()) <= 12)
                        ++n;
                }
            return n;
        };

        QImage emptyImage(300, 300, QImage::Format_ARGB32_Premultiplied);
        {
            QPainter painter(&emptyImage);
            widget.renderTo(painter, QSize(300, 300));
        }
        const int emptyOccupied = countNear(emptyImage, defaults.occupiedColor);
        const int emptyUnoccupied =
            countNear(emptyImage, defaults.unoccupiedColor);
        check(countNear(emptyImage, defaults.canvasBackground) > 300 * 300 / 2,
              "with no levels set, the canvas itself renders white (not the "
              "themed QPalette::Base)");

        std::vector<EnergyLevelDiagramEntry> levels;
        EnergyLevelDiagramEntry occ;
        occ.spin = 0;
        occ.bands = {0};
        occ.energyEv = -5.0;
        occ.occupation = 2.0; // occupied() is occupation > 0.5
        levels.push_back(occ);
        EnergyLevelDiagramEntry virt;
        virt.spin = 0;
        virt.bands = {1};
        virt.energyEv = 2.0;
        virt.occupation = 0.0; // unoccupied
        levels.push_back(virt);
        widget.setLevels(levels, 1);

        QImage drawnImage(300, 300, QImage::Format_ARGB32_Premultiplied);
        {
            QPainter painter(&drawnImage);
            widget.renderTo(painter, QSize(300, 300));
        }
        const int drawnOccupied = countNear(drawnImage, defaults.occupiedColor);
        const int drawnUnoccupied =
            countNear(drawnImage, defaults.unoccupiedColor);
        check(drawnOccupied > emptyOccupied,
              "the occupied level draws in blue (" + std::to_string(drawnOccupied)
                  + " vs " + std::to_string(emptyOccupied)
                  + " matching pixels with nothing drawn)");
        check(drawnUnoccupied > emptyUnoccupied,
              "the unoccupied level draws in red ("
                  + std::to_string(drawnUnoccupied) + " vs "
                  + std::to_string(emptyUnoccupied)
                  + " matching pixels with nothing drawn)");

        // setStyle() actually reaches renderTo() — pick colors nothing else
        // on the canvas would produce (default text/placeholder/gap colors
        // are all far from pure green), so a false pass from some unrelated
        // element is not possible.
        EnergyDiagramStyle custom;
        custom.occupiedColor = QColor(0, 255, 0);
        widget.setStyle(custom);
        QImage restyled(300, 300, QImage::Format_ARGB32_Premultiplied);
        {
            QPainter painter(&restyled);
            widget.renderTo(painter, QSize(300, 300));
        }
        check(countNear(restyled, QColor(0, 255, 0)) > 0,
              "setStyle() changes what renderTo() actually draws, not just "
              "a stored struct nothing reads");

        QWidget standInMainWindow2;
        auto* viewer2 = new EnergyDiagramViewer(&standInMainWindow2);
        auto hasButtonLabelled = [&](const QString& text) {
            for (QPushButton* b : viewer2->findChildren<QPushButton*>())
                if (b->text() == text)
                    return true;
            return false;
        };
        check(hasButtonLabelled(QObject::tr("Customize Appearance…")),
              "the viewer offers a Customize Appearance… button");
        check(hasButtonLabelled(QObject::tr("Export Image…")),
              "and an Export Image… button");
        check(hasButtonLabelled(QObject::tr("Export Levels…")),
              "and an Export Levels… CSV button, distinct from the existing "
              "Export Transitions… one — the diagram and the table show "
              "different data");
        check(hasButtonLabelled(QObject::tr("Export Transitions…")),
              "the pre-existing transitions CSV export is still there");
        delete viewer2;
    }

    std::printf("DsimWizard / DsimResultsWindow:\n");
    {
        // goNext() (not exercised by construction alone here) would call
        // AseBridge::makeSupercell, which needs a live embedded interpreter
        // — matching the CDD wizard test's own guard above.
        calango::pybridge::PythonEngine python;

        auto makeSingleSpecies = [](int z) {
            auto structure = std::make_shared<calango::core::Structure>();
            structure->setCell(calango::core::UnitCell({3.6, 0, 0}, {0, 3.6, 0},
                                                        {0, 0, 3.6}));
            calango::core::Atom atom;
            atom.atomicNumber = z;
            atom.position = {0.0, 0.0, 0.0};
            structure->addAtom(atom);
            return structure;
        };
        auto makeAlloy = [](int z1, int z2) {
            auto structure = std::make_shared<calango::core::Structure>();
            structure->setCell(calango::core::UnitCell({3.6, 0, 0}, {0, 3.6, 0},
                                                        {0, 0, 3.6}));
            calango::core::Atom a1;
            a1.atomicNumber = z1;
            a1.position = {0.0, 0.0, 0.0};
            structure->addAtom(a1);
            calango::core::Atom a2;
            a2.atomicNumber = z2;
            a2.position = {1.8, 1.8, 1.8};
            structure->addAtom(a2);
            return structure;
        };
        const auto cu = makeSingleSpecies(29);
        const auto pd = makeSingleSpecies(46);

        const auto hasLabelContaining = [](QWidget& w, const QString& needle) {
            for (QLabel* label : w.findChildren<QLabel*>())
                if (label->text().contains(needle))
                    return true;
            return false;
        };

        DsimWizard wizard(DsimWizard::MaterialList{});
        check(hasLabelContaining(wizard, QStringLiteral("at least 2")),
              "starts with fewer than 2 structures and says so");

        wizard.addStructures({{QStringLiteral("Cu (tab)"), cu}});
        check(hasLabelContaining(wizard, QStringLiteral("at least 2")),
              "still refuses with only one valid structure");
        check(wizard.validStructures().size() == 1, "one valid structure so far");

        wizard.addStructures({{QStringLiteral("Pd (tab)"), pd}});
        check(wizard.validStructures().size() == 2,
              "two single-species structures are both accepted");
        // Default 3x3x3 on a 1-atom cell: 27 atoms, matching the paper's
        // own supercell.
        check(hasLabelContaining(wizard, QStringLiteral("27 atoms")),
              "the summary reports the per-species atom count/dilution");

        // A multi-species entry (not single-species) is refused, not
        // silently accepted as a third component.
        wizard.addStructures({{QStringLiteral("CuPd (tab)"), makeAlloy(29, 46)}});
        check(wizard.validStructures().size() == 2,
              "a multi-species entry does not count as a valid component");
        bool sawRefusalNote = false;
        for (QListWidget* list : wizard.findChildren<QListWidget*>())
            for (int row = 0; row < list->count(); ++row)
                if (list->item(row)->text().contains(QStringLiteral("not single-species")))
                    sawRefusalNote = true;
        check(sawRefusalNote, "and the list shows why, not just a silently-dropped row");

        // A DUPLICATE species (a second Cu entry) is refused too — DSIM
        // needs N distinct components, not two references to the same one.
        wizard.addStructures({{QStringLiteral("Cu again (file)"), makeSingleSpecies(29)}});
        check(wizard.validStructures().size() == 2,
              "a duplicate species does not count as a third component either");

        // Geometry Optimization Settings: its own stage, not silently
        // hardcoded — the paper's fmax default (0.02 eV/A) has to be the
        // spin box's actual value, not just a comment.
        auto* fmaxSpin = wizard.findChild<QDoubleSpinBox*>();
        check(fmaxSpin != nullptr && std::abs(fmaxSpin->value() - 0.02) < 1e-9,
              "the force-convergence stage exists and defaults to the paper's own fmax (0.02 eV/A)");
        auto* optimizerCombo = wizard.findChild<QComboBox*>();
        check(optimizerCombo != nullptr && optimizerCombo->count() == 5,
              "and an optimizer combo (5 entries, matching core::Optimizer) is offered");
        check(optimizerCombo != nullptr && optimizerCombo->currentIndex() == 2,
              "defaulting to FIRE (index 2), a robust default for a supercell relaxation");
        bool sawMaxSteps300 = false;
        for (QSpinBox* spin : wizard.findChildren<QSpinBox*>())
            if (spin->maximum() > 6 && spin->value() == 300)
                sawMaxSteps300 = true;
        check(sawMaxSteps300, "and max steps defaults to 300 (distinct from the nx/ny/nz repeat spin boxes)");
        bool relaxCellChecked = false;
        for (QCheckBox* box : wizard.findChildren<QCheckBox*>())
            if (box->text().contains(QStringLiteral("Relax the unit cell")))
                relaxCellChecked = box->isChecked();
        check(relaxCellChecked,
              "and cell relaxation defaults to ON (unlike Geometry Optimization's own "
              "default-off convention) — DSIM's protocol has no fixed-cell mode");

        // Multi-phase alloys (Fe(bcc)-Co(hcp) and similar): eligible only
        // for exactly 2 valid components — already true here (cu, pd) —
        // and its hidden phase-label fields become visible once checked.
        // goNext()'s own structure-building for this mode (relabeling one
        // element onto the other's template, substituting the impurities,
        // dispatching generateScript() to
        // core::generateDsimMultiPhaseScript) needs a live embedded
        // interpreter for AseBridge::makeSupercell and is exercised
        // instead by the script-generator's own lint fixture
        // (tests/ScriptGenerationTest.cpp's dsim_multiphase.py,
        // byte-compiled by the generated_script_lint ctest) plus a real
        // Fe-Co run — see docs/sphinx/source/simulations/dsim.md.
        wizard.show(); // isVisibleTo() needs the page realized
        QCheckBox* multiPhaseCheck = nullptr;
        for (QCheckBox* box : wizard.findChildren<QCheckBox*>())
            if (box->text().contains(QStringLiteral("Different crystal structures")))
                multiPhaseCheck = box;
        check(multiPhaseCheck != nullptr && multiPhaseCheck->isEnabled(),
              "the multi-phase checkbox exists and is enabled for exactly 2 valid components");

        QLineEdit* phaseAEdit = nullptr;
        QLineEdit* phaseBEdit = nullptr;
        for (QLineEdit* edit : wizard.findChildren<QLineEdit*>()) {
            if (edit->placeholderText() == QStringLiteral("e.g. bcc"))
                phaseAEdit = edit;
            if (edit->placeholderText() == QStringLiteral("e.g. hcp"))
                phaseBEdit = edit;
        }
        check(phaseAEdit != nullptr && phaseBEdit != nullptr, "and both phase-label fields exist");
        check(phaseAEdit != nullptr && !phaseAEdit->isVisibleTo(&wizard),
              "hidden while the checkbox is unchecked");

        if (multiPhaseCheck)
            multiPhaseCheck->setChecked(true);
        check(phaseAEdit && phaseBEdit && phaseAEdit->isVisibleTo(&wizard)
                  && phaseBEdit->isVisibleTo(&wizard),
              "and shown once multi-phase mode is checked");

        // A 3rd valid structure makes the mode ineligible again — the
        // checkbox is disabled AND force-unchecked, not left checked for
        // a run shape it can no longer build (N=2 only).
        wizard.addStructures({{QStringLiteral("Ag (tab)"), makeSingleSpecies(47)}});
        check(multiPhaseCheck && !multiPhaseCheck->isEnabled() && !multiPhaseCheck->isChecked(),
              "adding a 3rd valid component disables and unchecks it again");
    }
    {
        QTemporaryDir dir;
        const auto writeJson = [&dir](const char* name, const char* body) {
            const QString path = dir.filePath(QString::fromLatin1(name));
            QFile json(path);
            json.open(QIODevice::WriteOnly);
            json.write(body);
            return path;
        };

        // N=2: the binary curve view.
        const QString binaryPath = writeJson("dsim_binary.json", R"({
            "schema": "calango.dsim/2", "species": ["Cu", "Pd"],
            "supercell_atom_count": 27, "dilution": 0.037037037037037035,
            "records": {
                "pristine_Cu": {"energy": -0.19, "energy_per_atom": -0.007,
                                "natoms": 27, "formula": "Cu27", "converged": true, "steps": 5},
                "pristine_Pd": {"energy": -0.007, "energy_per_atom": -0.0003,
                                "natoms": 27, "formula": "Pd27", "converged": true, "steps": 5},
                "Pd_in_Cu": {"energy": -0.265, "energy_per_atom": -0.0098,
                           "natoms": 27, "formula": "Cu26Pd", "converged": true, "steps": 6},
                "Cu_in_Pd": {"energy": -0.080, "energy_per_atom": -0.0030,
                           "natoms": 27, "formula": "CuPd26", "converged": true, "steps": 6}
            },
            "failures": {},
            "analysis": {
                "m_matrix": [[0.0, -0.002451], [-0.003043, 0.0]],
                "binary": {
                    "m_b_in_a_eV": -0.003043, "m_a_in_b_eV": -0.002451,
                    "dHdx_at_0_eV": -0.003043, "dHdx_at_1_eV": 0.002451,
                    "x_grid": [0.0, 0.5, 1.0],
                    "enthalpy_eV_per_atom": [0.0, -0.000687, 0.0],
                    "enthalpy_kJ_per_mol": [0.0, -0.0663, 0.0]
                },
                "ternary": null,
                "pairwise": [{"species_i": "Cu", "species_j": "Pd",
                             "x_grid": [0.0, 0.5, 1.0],
                             "enthalpy_eV_per_atom": [0.0, -0.000687, 0.0],
                             "enthalpy_kJ_per_mol": [0.0, -0.0663, 0.0]}]
            }
        })");

        DsimResultsWindow window;
        check(window.loadResults(binaryPath), "loads a well-formed N=2 dsim.json");
        auto* table = window.findChild<QTableWidget*>();
        check(table != nullptr && table->rowCount() == 4,
              "and populates the four-supercell table (N=2: N + N(N-1) = 4)");
        auto* tangentsCheck = window.findChild<QCheckBox*>();
        check(tangentsCheck != nullptr && tangentsCheck->isEnabled(),
              "the tangent-lines checkbox is enabled for a binary result");

        DsimResultsWindow missing;
        check(!missing.loadResults(dir.filePath(QStringLiteral("nope.json"))),
              "a missing result file is reported, not silently accepted");

        // N=3: the ternary composition-triangle view.
        const QString ternaryPath = writeJson("dsim_ternary.json", R"({
            "schema": "calango.dsim/2", "species": ["Ag", "Au", "Cu"],
            "supercell_atom_count": 27, "dilution": 0.037037037037037035,
            "records": {
                "pristine_Ag": {"energy": -0.08, "energy_per_atom": -0.003,
                                "natoms": 27, "formula": "Ag27", "converged": true},
                "pristine_Au": {"energy": -0.07, "energy_per_atom": -0.0026,
                                "natoms": 27, "formula": "Au27", "converged": true},
                "pristine_Cu": {"energy": -0.05, "energy_per_atom": -0.0019,
                                "natoms": 27, "formula": "Cu27", "converged": true},
                "Ag_in_Au": {"energy": -0.033, "energy_per_atom": -0.0012,
                            "natoms": 27, "formula": "Au26Ag", "converged": true},
                "Ag_in_Cu": {"energy": -0.054, "energy_per_atom": -0.002,
                            "natoms": 27, "formula": "Cu26Ag", "converged": true},
                "Au_in_Ag": {"energy": -0.033, "energy_per_atom": -0.0012,
                            "natoms": 27, "formula": "Ag26Au", "converged": true},
                "Au_in_Cu": {"energy": -0.011, "energy_per_atom": -0.0004,
                            "natoms": 27, "formula": "Cu26Au", "converged": true},
                "Cu_in_Ag": {"energy": -0.054, "energy_per_atom": -0.002,
                            "natoms": 27, "formula": "Ag26Cu", "converged": true},
                "Cu_in_Au": {"energy": -0.011, "energy_per_atom": -0.0004,
                            "natoms": 27, "formula": "Au26Cu", "converged": true}
            },
            "failures": {},
            "analysis": {
                "m_matrix": [[0.0, -0.04, -0.01], [-0.04, 0.0, -0.07], [-0.01, -0.07, 0.0]],
                "binary": null,
                "ternary": {
                    "resolution": 2,
                    "xB": [0.0, 0.5, 0.0, 1.0, 0.5, 0.0],
                    "xC": [0.0, 0.0, 0.5, 0.0, 0.5, 1.0],
                    "enthalpy_eV_per_atom": [0.0, -0.01, -0.0025, 0.0, -0.0175, 0.0],
                    "enthalpy_kJ_per_mol": [0.0, -0.965, -0.241, 0.0, -1.688, 0.0]
                },
                "pairwise": [
                    {"species_i": "Ag", "species_j": "Au", "x_grid": [0.0, 1.0],
                     "enthalpy_eV_per_atom": [0.0, 0.0], "enthalpy_kJ_per_mol": [0.0, 0.0]},
                    {"species_i": "Ag", "species_j": "Cu", "x_grid": [0.0, 1.0],
                     "enthalpy_eV_per_atom": [0.0, 0.0], "enthalpy_kJ_per_mol": [0.0, 0.0]},
                    {"species_i": "Au", "species_j": "Cu", "x_grid": [0.0, 1.0],
                     "enthalpy_eV_per_atom": [0.0, 0.0], "enthalpy_kJ_per_mol": [0.0, 0.0]}
                ]
            }
        })");
        DsimResultsWindow ternaryWindow;
        check(ternaryWindow.loadResults(ternaryPath), "loads a well-formed N=3 dsim.json");
        auto* ternaryTable = ternaryWindow.findChild<QTableWidget*>();
        check(ternaryTable != nullptr && ternaryTable->rowCount() == 9,
              "and populates the nine-supercell table (N=3: N + N(N-1) = 9)");
        auto* ternaryPlot = ternaryWindow.findChild<DsimTernaryPlotWidget*>();
        check(ternaryPlot != nullptr && ternaryPlot->hasData(),
              "and the ternary plot widget receives the composition-triangle grid");
        auto* ternaryTangentsCheck = ternaryWindow.findChild<QCheckBox*>();
        check(ternaryTangentsCheck != nullptr && !ternaryTangentsCheck->isEnabled(),
              "the tangent-lines checkbox is disabled for a ternary result "
              "(no dilute-limit-pair concept for N=3)");

        // Multi-phase (schema calango.dsim/3, Fe(bcc)-Co(hcp)): the same
        // 8-supercell fixture DsimTest.cpp's solveDsimMultiPhase check
        // uses (10-atom supercells, bccEnergies/hcpEnergies), with its
        // CORRECTED curve values at x={0, 0.5, 1} filled in from that same
        // closed-form derivation — so this test cross-checks the JSON
        // PARSING/plotting path against physics already verified there,
        // not a second, independent guess at the numbers.
        const QString multiPhasePath = writeJson("dsim_multiphase.json", R"({
            "schema": "calango.dsim/3", "species": ["Fe", "Co"],
            "species_a": "Fe", "species_b": "Co",
            "phase_a_label": "bcc", "phase_b_label": "hcp",
            "supercell_atom_count": 10, "dilution": 0.1,
            "records": {
                "pristine_Fe_bcc": {"energy": -100.0, "energy_per_atom": -10.0,
                                    "natoms": 10, "formula": "Fe10", "converged": true},
                "pristine_Co_bcc": {"energy": -97.0, "energy_per_atom": -9.7,
                                    "natoms": 10, "formula": "Co10", "converged": true},
                "Co_in_Fe_bcc": {"energy": -100.5, "energy_per_atom": -10.05,
                                "natoms": 10, "formula": "Fe9Co", "converged": true},
                "Fe_in_Co_bcc": {"energy": -97.4, "energy_per_atom": -9.74,
                                "natoms": 10, "formula": "Co9Fe", "converged": true},
                "pristine_Fe_hcp": {"energy": -96.0, "energy_per_atom": -9.6,
                                    "natoms": 10, "formula": "Fe10", "converged": true},
                "pristine_Co_hcp": {"energy": -98.0, "energy_per_atom": -9.8,
                                    "natoms": 10, "formula": "Co10", "converged": true},
                "Co_in_Fe_hcp": {"energy": -96.3, "energy_per_atom": -9.63,
                                "natoms": 10, "formula": "Fe9Co", "converged": true},
                "Fe_in_Co_hcp": {"energy": -98.2, "energy_per_atom": -9.82,
                                "natoms": 10, "formula": "Co9Fe", "converged": true}
            },
            "failures": {},
            "multi_phase": {
                "phase_a": {
                    "label": "bcc",
                    "energy_matrix": [[-100.0, -97.4], [-100.5, -97.0]],
                    "m_matrix": [[0.0, -0.1], [-0.8, 0.0]],
                    "raw": {"x_grid": [0.0, 0.5, 1.0],
                            "enthalpy_eV_per_atom": [0.0, -0.1125, 0.0],
                            "enthalpy_kJ_per_mol": [0.0, -10.85459985, 0.0],
                            "dHdx_at_0_eV": -0.8, "dHdx_at_1_eV": 0.1},
                    "shift_at_x0_eV": 0.0, "shift_at_x1_eV": 0.1,
                    "corrected": {"x_grid": [0.0, 0.5, 1.0],
                                  "enthalpy_eV_per_atom": [0.0, -0.0625, 0.1],
                                  "enthalpy_kJ_per_mol": [0.0, -6.03033325, 9.6485332],
                                  "dHdx_at_0_eV": -0.8, "dHdx_at_1_eV": 0.1}
                },
                "phase_b": {
                    "label": "hcp",
                    "energy_matrix": [[-96.0, -98.2], [-96.3, -98.0]],
                    "m_matrix": [[0.0, -0.4], [-0.1, 0.0]],
                    "raw": {"x_grid": [0.0, 0.5, 1.0],
                            "enthalpy_eV_per_atom": [0.0, -0.0625, 0.0],
                            "enthalpy_kJ_per_mol": [0.0, -6.03033325, 0.0],
                            "dHdx_at_0_eV": -0.1, "dHdx_at_1_eV": 0.4},
                    "shift_at_x0_eV": 0.4, "shift_at_x1_eV": 0.0,
                    "corrected": {"x_grid": [0.0, 0.5, 1.0],
                                  "enthalpy_eV_per_atom": [0.4, 0.1375, 0.0],
                                  "enthalpy_kJ_per_mol": [38.5941328, 13.26673265, 0.0],
                                  "dHdx_at_0_eV": -0.1, "dHdx_at_1_eV": 0.4}
                }
            }
        })");
        DsimResultsWindow multiPhaseWindow;
        check(multiPhaseWindow.loadResults(multiPhasePath), "loads a well-formed schema calango.dsim/3 file");
        auto* multiPhaseTable = multiPhaseWindow.findChild<QTableWidget*>();
        check(multiPhaseTable != nullptr && multiPhaseTable->rowCount() == 8,
              "and populates the eight-supercell table (2 phases x (2 pristine + 2 impurity))");
        auto* multiPhaseTangentsCheck = multiPhaseWindow.findChild<QCheckBox*>();
        check(multiPhaseTangentsCheck != nullptr && !multiPhaseTangentsCheck->isEnabled(),
              "the tangent-lines checkbox is disabled for a multi-phase result "
              "(two branches, not one dilute-limit pair)");

        auto* multiPhasePlot = multiPhaseWindow.findChild<EgqcaPlotWidget*>();
        check(multiPhasePlot != nullptr && multiPhasePlot->hasData(),
              "the two-branch curve is plotted on the ordinary (non-ternary) plot widget");
        // DsimPlotStyle defaults to kJ/mol (style_.useKilojoulesPerMole),
        // so the plotted series carries the JSON's "enthalpy_kJ_per_mol"
        // column, not the eV/atom one — 0.1/0.4 eV/atom * kEvToKjPerMol
        // (96.485332) = 9.6485332/38.5941328 kJ/mol, the same fixture
        // values above. Tolerance covers EgqcaPlotWidget::toCsv()'s own
        // 6-significant-figure QTextStream rounding, not fixture error.
        const QString csv = multiPhasePlot ? multiPhasePlot->toCsv() : QString();
        const auto valueFor = [&csv](const QString& label, double x) {
            for (const QString& line : csv.split(QLatin1Char('\n'))) {
                const QStringList parts = line.split(QLatin1Char(','));
                if (parts.size() == 3 && parts[0] == label
                    && std::abs(parts[1].toDouble() - x) < 1e-9)
                    return parts[2].toDouble();
            }
            return std::numeric_limits<double>::quiet_NaN();
        };
        check(std::abs(valueFor(QStringLiteral("bcc"), 0.0) - 0.0) < 1e-9,
              "the bcc curve is plotted at exactly 0 at Fe (x=0)");
        check(std::abs(valueFor(QStringLiteral("bcc"), 1.0) - 9.6485332) < 1e-3,
              "and at Co's own bcc-hcp lattice stability (0.1 eV/atom = 9.65 kJ/mol) at "
              "x=1 -- the CORRECTED curve, not the raw (zero-at-both-ends) one");
        check(std::abs(valueFor(QStringLiteral("hcp"), 1.0) - 0.0) < 1e-9,
              "the hcp curve is plotted at exactly 0 at Co (x=1)");
        check(std::abs(valueFor(QStringLiteral("hcp"), 0.0) - 38.5941328) < 1e-3,
              "and at Fe's own hcp-bcc lattice stability (0.4 eV/atom = 38.59 kJ/mol) at x=0");
    }

    std::printf("Molecular Design dialog:\n");
    {
        // The construction-order hazard here is real and specific: the
        // constructor builds twelve exclusive tool buttons, then installs one
        // QShortcut per tool that calls selectTool() — which reaches back into
        // the button map — and then calls selectTool() itself before the two
        // sidebars have finished laying out. A dialog that built its shortcuts
        // before its buttons would dereference an empty map here.
        MolecularDesignDialog dialog;
        check(true, "constructs");

        auto* canvas = dialog.canvas();
        check(canvas != nullptr, "and owns a canvas");
        check(dialog.findChild<QWidget*>(QStringLiteral("moleculeToolSidebar"))
                  != nullptr,
              "with the left tool sidebar");
        check(dialog.findChild<QWidget*>(QStringLiteral("moleculeOutputSidebar"))
                  != nullptr,
              "and the right output sidebar");
        check(dialog.findChild<QPushButton*>(QStringLiteral("sendToViewportButton"))
                  != nullptr,
              "and a Send to 3D Viewport button");

        // Every ring the template palette offers reaches the combo, named, and
        // carries its enum value as item data — which is what the tool reads
        // when it stamps. A missing entry would be a template the user simply
        // cannot select.
        //
        // NOT checked here: whether each item's ICON resolves. This binary
        // bundles no assets/icons/ resource at all (only the application does),
        // so every QIcon in it is legitimately null and an assertion on that
        // could never pass. The icon-registry test owns that half, source-scan
        // against the CMake resource list, and covers every ring glyph.
        auto* rings = dialog.findChild<QComboBox*>(QStringLiteral("ringTemplateCombo"));
        check(rings != nullptr, "the ring palette exists");
        if (rings) {
            const auto& templates = calango::core::ringTemplates();
            check(rings->count() == static_cast<int>(templates.size()),
                  "listing every ring template");
            bool named = true;
            bool addressable = true;
            for (int i = 0; i < rings->count() && i < static_cast<int>(templates.size());
                 ++i) {
                named = named
                    && rings->itemText(i)
                        == QLatin1String(calango::core::ringTemplateName(templates[i]));
                addressable = addressable
                    && rings->itemData(i).toInt() == static_cast<int>(templates[i]);
            }
            check(named, "each named as the core table names it");
            check(addressable,
                  "and each carrying its template value, which is what the "
                  "ring tool stamps from");
        }

        // SMILES in: a valid string draws, an invalid one changes nothing.
        check(dialog.loadSmiles(QStringLiteral("c1ccccc1")),
              "a valid SMILES string imports");
        check(canvas && canvas->graph().atomCount() == 6,
              "drawing six carbons");
        check(!dialog.loadSmiles(QStringLiteral("c1cc")),
              "an invalid one is refused");
        check(canvas && canvas->graph().atomCount() == 6,
              "and leaves the canvas exactly as it was");

        // The output seam: the dialog hands MainWindow a finished Structure.
        // MainWindow's own slot then calls addDocument() — the standard import
        // machinery — which this binary does not build, so what is pinned here
        // is the CONTRACT: one signal, carrying a real C6H6 and the tab name.
        std::shared_ptr<calango::core::Structure> sent;
        QString sentName;
        QObject::connect(&dialog, &MolecularDesignDialog::structureReady,
                         [&sent, &sentName](
                             std::shared_ptr<calango::core::Structure> structure,
                             const QString& name) {
                             sent = std::move(structure);
                             sentName = name;
                         });
        check(dialog.sendToViewport(), "Send to 3D Viewport succeeds");
        check(sent != nullptr, "emitting a structure");
        check(sent && sent->size() == 12,
              "of twelve atoms — the six drawn carbons plus six hydrogens");
        check(sent && sent->chemicalFormula() == "C6H6", "which is C6H6");
        check(sentName == QStringLiteral("C6H6"),
              "and the new tab is named after the formula");

        // Undo takes the SMILES import back out.
        check(canvas && canvas->canUndo(), "the import is undoable");
        if (canvas) {
            canvas->undo();
            check(canvas->graph().atomCount() == 0,
                  "and undoing it empties the canvas");
            check(canvas->canRedo(), "with a redo available");
            canvas->redo();
            check(canvas->graph().atomCount() == 6, "which brings it back");
        }

        // Every tool is selectable, including from a sketch-modifying state.
        if (canvas) {
            for (int tool = 0; tool <= static_cast<int>(MoleculeCanvas::Tool::Ring);
                 ++tool) {
                canvas->setTool(static_cast<MoleculeCanvas::Tool>(tool));
            }
            check(canvas->tool() == MoleculeCanvas::Tool::Ring,
                  "every tool can be selected in turn");
        }

        // -- Highlights, and Clear ------------------------------------------
        //
        // Both are annotations on the drawing rather than changes to it, and
        // both have to be one undo away — a wiped canvas that could not come
        // back would be the one unrecoverable action in a dialog whose whole
        // editing model is snapshot undo.
        check(dialog.findChild<QCheckBox*>(
                  QStringLiteral("aromaticHighlightBox")) != nullptr,
              "the appearance sidebar offers the aromatic-ring highlight");
        check(dialog.findChild<QPushButton*>(
                  QStringLiteral("aromaticColorButton")) != nullptr,
              "with a colour of its own");
        {
            int swatches = 0;
            for (int i = 0; i < MoleculeCanvas::highlightPaletteSize(); ++i) {
                if (dialog.findChild<QPushButton*>(
                        QStringLiteral("highlightSwatch%1").arg(i)))
                    ++swatches;
            }
            check(swatches == MoleculeCanvas::highlightPaletteSize()
                      && swatches > 1,
                  "and the region palette puts every colour on screen");
        }
        check(dialog.findChild<QPushButton*>(
                  QStringLiteral("clearHighlightButton")) != nullptr,
              "plus a way to take a highlight back off");

        if (canvas) {
            check(!canvas->aromaticHighlight(),
                  "the aromatic fill is OFF by default — a Kekule drawing is "
                  "what a chemist expects to see");
            canvas->setAromaticHighlight(true);
            check(canvas->aromaticHighlight(), "and can be turned on");
            canvas->setAromaticHighlight(false);

            // Region highlights ride on the atoms and survive undo with them.
            check(!canvas->hasHighlights(), "a fresh sketch has no highlights");
            canvas->selectAll();
            canvas->highlightSelection(1);
            check(canvas->hasHighlights(), "colouring a selection marks it");
            canvas->undo();
            check(!canvas->hasHighlights(),
                  "and undo takes the colour back off — one step, like every "
                  "other edit");
            canvas->redo();
            check(canvas->hasHighlights(), "redo puts it back");
            canvas->selectAll();
            canvas->highlightSelection(-1);
            check(!canvas->hasHighlights(),
                  "and \"remove highlight\" clears it without touching the "
                  "atoms");
            check(canvas->graph().atomCount() == 6,
                  "which are all still there");

            // Clear wipes everything, in ONE undo step.
            const int before = canvas->graph().atomCount();
            check(before > 0, "there is something on the canvas to clear");
            canvas->clearCanvas();
            check(canvas->graph().atomCount() == 0, "Clear empties the canvas");
            check(canvas->canUndo(), "and is undoable");
            canvas->undo();
            check(canvas->graph().atomCount() == before,
                  "one undo brings the whole drawing back");

            // A highlight is an ANNOTATION: it must not reach the 3D export.
            canvas->selectAll();
            canvas->highlightSelection(0);
            std::shared_ptr<calango::core::Structure> exported;
            QObject::connect(
                &dialog, &MolecularDesignDialog::structureReady, &dialog,
                [&exported](std::shared_ptr<calango::core::Structure> s,
                            const QString&) { exported = std::move(s); });
            check(dialog.sendToViewport(),
                  "a highlighted sketch still exports");
            check(exported && exported->chemicalFormula() == "C6H6",
                  "as the same C6H6 — the highlight is a drawing annotation, "
                  "not chemistry");

            // ...but it MUST reach the image export, which is the other half
            // of "an annotation". renderTo() is the one path both the screen
            // and the exported PNG/SVG go through, so rendering it here is
            // rendering what the file would contain.
            //
            // Counted as SATURATED pixels: this sketch is all carbon, drawn
            // in ink on white, so every pixel of it is grey. Anything with a
            // channel spread came from a highlight and from nothing else.
            const auto tintedPixels = [&canvas]() {
                QImage image(320, 260, QImage::Format_ARGB32);
                image.fill(Qt::white);
                QPainter painter(&image);
                painter.setRenderHint(QPainter::Antialiasing, true);
                canvas->renderTo(painter, image.size(), Qt::white);
                painter.end();
                int tinted = 0;
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        const QColor c = image.pixelColor(x, y);
                        const int spread =
                            std::max({c.red(), c.green(), c.blue()})
                            - std::min({c.red(), c.green(), c.blue()});
                        if (spread > 25)
                            ++tinted;
                    }
                }
                return tinted;
            };
            const int withRegion = tintedPixels();
            check(withRegion > 200,
                  "a region highlight is rendered into the exported image");

            canvas->selectAll();
            canvas->highlightSelection(-1);
            check(tintedPixels() == 0,
                  "and the same drawing without it exports pure ink on white "
                  "— nothing else in a carbon-only sketch is coloured");

            canvas->setAromaticHighlight(true);
            const int withAromatic = tintedPixels();
            check(withAromatic > 200,
                  "the aromatic fill is rendered into the export too — "
                  "benzene is perceived aromatic and filled");
            canvas->setAromaticHighlight(false);
            check(tintedPixels() == 0, "and vanishes when it is switched off");
        }

        exerciseControls(&dialog);
        check(true, "survives every control being toggled");
    }

    std::printf(failures == 0 ? "\nAll dialog construction checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
