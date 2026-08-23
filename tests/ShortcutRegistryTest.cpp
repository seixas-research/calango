// ShortcutRegistry: the action -> key-binding table Preferences -> Hotkeys
// edits and MainWindow/ViewportWidget read from, instead of the literal
// QKeySequence comparisons scattered through event handlers this replaced.
//
// Isolated from a real installation's shortcuts by construction, not by
// CALANGO_CONFIG_DIR: ShortcutRegistry stores overrides directly through
// QSettings (the native per-app backend), and the org/app name set below
// ("CalangoTest"/"ShortcutRegistryTest", distinct from main.cpp's "Seixas
// Research"/"Calango") gives this test its own separate settings store —
// the same reason CALANGO_CONFIG_DIR itself exists, applied at the QSettings
// layer instead of SettingsManager's JSON-mirror layer, which this test
// never touches.

#include "gui/ShortcutRegistry.hpp"

#include <QGuiApplication>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QSettings>

#include <cstdio>
#include <cstdlib>

using namespace calango::gui;

namespace {

int failures = 0;

void check(bool ok, const char* label)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", label);
    if (!ok)
        ++failures;
}

} // namespace

int main(int argc, char** argv)
{
    // QGuiApplication, not QCoreApplication: QKeySequence(StandardKey) —
    // Undo/Redo/Delete/New/Open/SaveAs/Save/Close/Quit below all resolve
    // through it — reads the platform's key-binding theme via
    // QGuiApplicationPrivate, which is only initialized once a GUI
    // application (Gui or full Widgets) exists. Under a bare
    // QCoreApplication it dereferences a null platform theme and segfaults;
    // caught here by hand with lldb before this comment existed.
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("CalangoTest"));
    QGuiApplication::setApplicationName(QStringLiteral("ShortcutRegistryTest"));
    // A previous run of THIS test crashing or being killed mid-way is the
    // only way this store could carry stale overrides in — nothing else
    // writes to this org/app pair. Cheap enough to just always start clean.
    ShortcutRegistry::resetAllToDefaults();

    std::printf("Inventory:\n");
    {
        const auto& actions = ShortcutRegistry::actions();
        check(actions.size() >= 13,
              "at least the 13 actions Task 2 scoped are present");
        bool allHaveIds = true, allHaveLabels = true;
        QSet<QString> ids;
        for (const ShortcutAction& action : actions) {
            allHaveIds = allHaveIds && !action.id.isEmpty();
            allHaveLabels = allHaveLabels && !action.label.isEmpty();
            ids.insert(action.id);
        }
        check(allHaveIds, "every action has a non-empty id");
        check(allHaveLabels, "and a non-empty label");
        check(ids.size() == actions.size(), "every id is unique");

        check(ShortcutRegistry::find(QStringLiteral("viewport.mode.rotate")) != nullptr,
              "the rotation-mode action is findable by id");
        check(ShortcutRegistry::find(QStringLiteral("no.such.action")) == nullptr,
              "an unknown id is not");
    }

    std::printf("Defaults match what MainWindow used to hard-code:\n");
    {
        check(ShortcutRegistry::binding(QStringLiteral("viewport.mode.rotate"))
                  == QKeySequence(Qt::Key_R),
              "R for rotation mode");
        check(ShortcutRegistry::binding(QStringLiteral("viewport.mode.pan"))
                  == QKeySequence(Qt::Key_T),
              "T for translation mode");
        check(ShortcutRegistry::binding(QStringLiteral("viewport.mode.select"))
                  == QKeySequence(Qt::Key_S),
              "S for selection mode");
        check(ShortcutRegistry::binding(QStringLiteral("viewport.mode.insert"))
                  == QKeySequence(Qt::Key_I),
              "I for insertion mode");
        check(ShortcutRegistry::binding(QStringLiteral("viewport.mode.distance"))
                  == QKeySequence(Qt::Key_D),
              "D for distance measurement");
        check(ShortcutRegistry::binding(QStringLiteral("viewport.mode.angle"))
                  == QKeySequence(Qt::Key_A),
              "A for angle measurement");
        check(ShortcutRegistry::binding(QStringLiteral("viewport.resetCamera"))
                  == QKeySequence(Qt::Key_F),
              "F for reset camera");
        check(ShortcutRegistry::binding(QStringLiteral("viewport.toggleProjection"))
                  == QKeySequence(Qt::Key_O),
              "O for the projection toggle");
        check(ShortcutRegistry::binding(QStringLiteral("viewport.tab.next"))
                  == QKeySequence(Qt::Key_Tab),
              "Tab for next tab (Task 3)");
        check(ShortcutRegistry::binding(QStringLiteral("viewport.tab.previous"))
                  == QKeySequence(Qt::SHIFT | Qt::Key_Tab),
              "Shift+Tab for previous tab (Task 3)");
    }

    std::printf("An unknown id degrades rather than asserts:\n");
    {
        check(ShortcutRegistry::binding(QStringLiteral("no.such.action")).isEmpty(),
              "binding() of an unknown id is an empty sequence, not a crash");
    }

    std::printf("Remap, persist, and reset:\n");
    {
        const QString id = QStringLiteral("viewport.mode.rotate");
        const QKeySequence original = ShortcutRegistry::binding(id);
        check(original == QKeySequence(Qt::Key_R), "starts at the default");

        ShortcutRegistry::setBinding(id, QKeySequence(Qt::Key_X));
        check(ShortcutRegistry::binding(id) == QKeySequence(Qt::Key_X),
              "setBinding() takes effect immediately");

        // Persistence: a SECOND read (simulating a later call, or another
        // window reading the same registry) sees the same override — this
        // is the whole point of routing through SettingsManager::kHotkeys
        // rather than an in-memory-only cache.
        check(QSettings()
                      .value(QLatin1String("hotkeys/bindings"))
                      .toString()
                      .contains(QStringLiteral("viewport.mode.rotate")),
              "the override is actually written to QSettings, not just held "
              "in memory");

        ShortcutRegistry::resetToDefault(id);
        check(ShortcutRegistry::binding(id) == QKeySequence(Qt::Key_R),
              "resetToDefault() restores the factory binding");

        // Other actions are untouched by remapping one.
        check(ShortcutRegistry::binding(QStringLiteral("viewport.mode.pan"))
                  == QKeySequence(Qt::Key_T),
              "remapping rotation left translation exactly where it was");
    }

    std::printf("Reset all:\n");
    {
        ShortcutRegistry::setBinding(QStringLiteral("viewport.mode.rotate"),
                                     QKeySequence(Qt::Key_X));
        ShortcutRegistry::setBinding(QStringLiteral("viewport.mode.pan"),
                                     QKeySequence(Qt::Key_Y));
        ShortcutRegistry::resetAllToDefaults();
        check(ShortcutRegistry::binding(QStringLiteral("viewport.mode.rotate"))
                      == QKeySequence(Qt::Key_R)
                  && ShortcutRegistry::binding(QStringLiteral("viewport.mode.pan"))
                      == QKeySequence(Qt::Key_T),
              "every remap is cleared at once");
    }

    std::printf("Conflict detection:\n");
    {
        // Two remappable actions.
        const QString rotate = QStringLiteral("viewport.mode.rotate");
        const QString pan = QStringLiteral("viewport.mode.pan");
        check(ShortcutRegistry::conflictFor(QKeySequence(Qt::Key_T), rotate)
                  == ShortcutRegistry::find(pan)->label,
              "assigning rotation the SAME key pan already has reports pan's "
              "label as the clash");
        check(ShortcutRegistry::conflictFor(QKeySequence(Qt::Key_R), rotate).isEmpty(),
              "but rotation's OWN current key does not conflict with itself "
              "(excludeId)");
        check(ShortcutRegistry::conflictFor(QKeySequence(Qt::Key_Z)).isEmpty(),
              "an unused key conflicts with nothing");
        check(ShortcutRegistry::conflictFor(QKeySequence()).isEmpty(),
              "\"no shortcut\" never conflicts with anything");

        // Fixed, non-remappable shortcuts must also be caught.
        const QString clash =
            ShortcutRegistry::conflictFor(QKeySequence(QObject::tr("Ctrl+P")));
        check(!clash.isEmpty() && clash.contains(QStringLiteral("Preferences")),
              "colliding with a FIXED shortcut (Ctrl+P, Preferences) is "
              "reported too, not just remappable-vs-remappable");

        bool anyFixed = !ShortcutRegistry::fixedShortcuts().isEmpty();
        check(anyFixed, "the fixed-shortcut inventory is non-empty");
    }

    std::printf("Tab/Backtab default is exactly what ViewportWidget "
                "normalizes Key_Backtab to:\n");
    {
        // ViewportWidget::keyPressEvent folds Key_Backtab into
        // Key_Tab+ShiftModifier before comparing against this binding — this
        // pins the value that normalization has to land on.
        const QKeySequence previous =
            ShortcutRegistry::binding(QStringLiteral("viewport.tab.previous"));
        const QKeySequence normalizedBacktab(
            QKeyCombination(Qt::ShiftModifier, Qt::Key_Tab));
        check(previous == normalizedBacktab,
              "Shift+Tab's stored default equals the normalized form of "
              "Key_Backtab");
    }

    std::printf("No two default bindings collide:\n");
    {
        // conflictFor() is GLOBAL — it has no notion of a shortcut being
        // scoped to one window — so every default in the table has to be
        // distinct even when two of them could never fire at the same time.
        // Molecular Design's twelve single-key tool shortcuts are live only in
        // that dialog and the viewport's R/T/S/I/D/A only in the main window,
        // but a factory default that clashes would still light up the Hotkeys
        // tab's conflict warning on a fresh install, which is a bug report.
        // Uniqueness is PER SCOPE. Two actions in different scopes may hold
        // the same key — that is the whole point of the scope field — so what
        // has to be unique is (scope, key), not key alone. Molecular Design's
        // X and Y deliberately duplicate keys the viewport's own remap tests
        // use, and both are correct.
        QSet<QString> seen;
        QStringList clashes;
        for (const ShortcutAction& action : ShortcutRegistry::actions()) {
            const QString key =
                ShortcutRegistry::binding(action.id).toString(QKeySequence::PortableText);
            if (key.isEmpty())
                continue;
            const QString slot = action.scope + QStringLiteral("\x1f") + key;
            if (seen.contains(slot))
                clashes << (key + QStringLiteral(" (") + action.label
                            + QStringLiteral(")"));
            seen.insert(slot);
        }
        check(clashes.isEmpty(),
              clashes.isEmpty()
                  ? "every default binding is unique"
                  : qPrintable(QStringLiteral("duplicate defaults: ")
                               + clashes.join(QStringLiteral(", "))));

        // And none of them collides with a fixed, non-remappable shortcut.
        QStringList fixedClashes;
        for (const ShortcutAction& action : ShortcutRegistry::actions()) {
            const QKeySequence key = ShortcutRegistry::binding(action.id);
            if (key.isEmpty())
                continue;
            for (const ShortcutRegistry::FixedShortcut& fixed :
                 ShortcutRegistry::fixedShortcuts()) {
                if (fixed.key == key)
                    fixedClashes << (action.label + QStringLiteral(" vs ")
                                     + fixed.label);
            }
        }
        check(fixedClashes.isEmpty(),
              fixedClashes.isEmpty()
                  ? "and none collides with a fixed shortcut"
                  : qPrintable(QStringLiteral("clashes: ")
                               + fixedClashes.join(QStringLiteral(", "))));
    }

    std::printf("Scope keeps a dialog key from blocking an application one:\n");
    {
        // The concrete case: the Caption tool defaults to X and Tidy to Y,
        // both of which an application-wide action must still be free to
        // take. conflictFor() compares within a scope, so it does — and
        // without that, remapping a viewport mode to X pops a modal warning
        // naming a tool in a window that is not open.
        const QString viewportOnX = ShortcutRegistry::conflictFor(
            QKeySequence(Qt::Key_X), QStringLiteral("viewport.mode.rotate"));
        check(viewportOnX.isEmpty(),
              "X is free for a viewport mode even though the Caption tool "
              "holds it");
        const QString viewportOnY = ShortcutRegistry::conflictFor(
            QKeySequence(Qt::Key_Y), QStringLiteral("viewport.mode.pan"));
        check(viewportOnY.isEmpty(), "and so is Y, which Tidy holds");

        // The other direction, and the case that must STILL be refused:
        // within one scope a duplicate is a duplicate.
        const QString sketcherOnY = ShortcutRegistry::conflictFor(
            QKeySequence(Qt::Key_Y),
            QStringLiteral("moleculardesign.tool.caption"));
        check(!sketcherOnY.isEmpty(),
              "but Y IS refused for another Molecular Design tool — same "
              "scope, real collision");
        const QString sketcherOnV = ShortcutRegistry::conflictFor(
            QKeySequence(Qt::Key_V), QStringLiteral("moleculardesign.tidy"));
        check(sketcherOnV == QObject::tr("Selection tool"),
              "and the clash is named: the Selection tool");

        // A fixed shortcut is checked in EVERY scope: the main window's menu
        // accelerators stay reachable under a modeless dialog.
        const QString sketcherOnCtrlP = ShortcutRegistry::conflictFor(
            QKeySequence(QObject::tr("Ctrl+P")),
            QStringLiteral("moleculardesign.tidy"));
        check(!sketcherOnCtrlP.isEmpty(),
              "Ctrl+P is refused inside the sketcher too — Preferences is a "
              "fixed accelerator and is live underneath it");
    }

    std::printf("Molecular Design's tool shortcuts are registered:\n");
    {
        // The sketcher LOOKS ITS KEYS UP by these ids at construction and
        // silently installs no shortcut for one it cannot find, so a renamed
        // or dropped id is invisible at run time — the tool just stops having
        // a key. Pinning the ids here is what makes that a build failure.
        const char* ids[] = {"moleculardesign.tool.select",
                             "moleculardesign.tool.singleBond",
                             "moleculardesign.tool.doubleBond",
                             "moleculardesign.tool.tripleBond",
                             "moleculardesign.tool.wedgeBond",
                             "moleculardesign.tool.hashBond",
                             "moleculardesign.tool.chain",
                             "moleculardesign.tool.atomLabel",
                             "moleculardesign.tool.caption",
                             "moleculardesign.tool.charge",
                             "moleculardesign.tool.eraser",
                             "moleculardesign.tool.ring",
                             "moleculardesign.tidy",
                             "moleculardesign.sendToViewport"};
        bool allPresent = true;
        bool allInGroup = true;
        for (const char* id : ids) {
            const ShortcutAction* action =
                ShortcutRegistry::find(QLatin1String(id));
            allPresent = allPresent && action != nullptr;
            if (action)
                allInGroup = allInGroup
                    && action->category == QStringLiteral("Molecular Design");
        }
        check(allPresent, "all fourteen ids are in the registry");
        check(allInGroup,
              "and every one is filed under the \"Molecular Design\" category, "
              "so the Hotkeys tab groups them together");

        check(ShortcutRegistry::binding(
                  QStringLiteral("moleculardesign.tool.singleBond"))
                  == QKeySequence(Qt::Key_1),
              "the single-bond tool defaults to 1");
        check(ShortcutRegistry::binding(QStringLiteral("moleculardesign.tidy"))
                  == QKeySequence(Qt::Key_Y),
              "Tidy defaults to Y");
    }

    ShortcutRegistry::resetAllToDefaults(); // leave no trace for other tests

    if (failures == 0) {
        std::printf("\nAll shortcut registry checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d shortcut registry check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
