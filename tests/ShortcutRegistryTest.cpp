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
#include <QSet>
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

    ShortcutRegistry::resetAllToDefaults(); // leave no trace for other tests

    if (failures == 0) {
        std::printf("\nAll shortcut registry checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d shortcut registry check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
