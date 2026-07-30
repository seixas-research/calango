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
#include "gui/GeometryConstraintsDialog.hpp"
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
#include "gui/KpointsConvergenceWizard.hpp"
#include "gui/MaceTrainerDialog.hpp"
#include "gui/RandomNoiseViewer.hpp"
#include "gui/RandomNoiseWizard.hpp"
#include "gui/SinglePointWizard.hpp"
#include "gui/TwoDBandsWizard.hpp"
#include "python_bridge/PythonEngine.hpp"
#include "render/StructureRenderer.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
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
#include <QSpinBox>

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
            for (int i = 0; i < count; ++i)
                engine->setCurrentIndex(i);
            check(true, "switching through every engine does not crash");

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
                  std::pair{"Improved tetrahedron method",
                            SmearingMethod::ImprovedTetrahedronMethod},
                  std::pair{"Orbital-free", SmearingMethod::OrbitalFree},
                  std::pair{"Fixed", SmearingMethod::FixedOccupations}}) {
                const int row = (*smearing)->findText(QLatin1String(label));
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

    std::printf(failures == 0 ? "\nAll dialog construction checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
