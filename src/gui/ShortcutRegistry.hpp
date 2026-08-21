#pragma once

#include <QKeySequence>
#include <QString>
#include <QVector>

namespace calango::gui {

/// One remappable action: a stable identifier, what the Hotkeys tab shows
/// for it, and its factory-default binding.
struct ShortcutAction {
    /// Stable across releases — this IS the persistence key (see
    /// SettingsManager::kHotkeys), so renaming one silently orphans every
    /// user's remap of it. Dotted, grouped by where the action lives:
    /// "viewport.mode.rotate", "edit.undo", ...
    QString id;
    QString label;      ///< "Rotation mode", shown in the Hotkeys table
    QString category;   ///< "Viewport — Mouse Mode", the table's grouping column
    QKeySequence defaultKey;
};

/// Central action -> key-binding table for every REMAPPABLE shortcut in the
/// app: the six mouse-mode keys (previously QKeySequence literals scattered
/// through MainWindow's addModeAction() calls), the camera/view resets, the
/// three edit actions, and [Tab]/[Shift+Tab] viewport-tab cycling. A caller
/// looks a binding up BY ID rather than comparing a literal key, so a remap
/// in Preferences → Hotkeys takes effect everywhere the action is triggered
/// or displayed without any of those call sites needing to know a remap
/// happened.
///
/// Static, like ThemeManager/SettingsManager — there is exactly one set of
/// bindings for the whole app, never per-window or per-document. Every
/// method reads/writes QSettings (SettingsManager::kHotkeys) directly rather
/// than caching, so a change from one place (Preferences) is visible to
/// every other reader on its very next call — the same "no cache to
/// invalidate" tradeoff SettingsManager itself makes.
class ShortcutRegistry {
public:
    /// The full inventory, in a fixed, stable order — the Hotkeys table's
    /// display order.
    static const QVector<ShortcutAction>& actions();
    /// nullptr for an unknown id.
    static const ShortcutAction* find(const QString& id);

    /// The binding currently in effect for `id` — the persisted override if
    /// one exists, else the action's own default. An unknown id returns an
    /// empty QKeySequence rather than asserting: a caller holding a stale id
    /// (an action removed in a later release) degrades to "unbound", not a
    /// crash.
    static QKeySequence binding(const QString& id);

    /// Persist a new binding for `id`. Does NOT check for conflicts itself —
    /// call conflictFor() first (the Hotkeys tab always does, before ever
    /// calling this) — so a caller that has already decided to allow a
    /// clash (there is none in this app today, but a future one should not
    /// have to fight this API) is not second-guessed.
    static void setBinding(const QString& id, const QKeySequence& key);
    static void resetToDefault(const QString& id);
    static void resetAllToDefaults();

    /// Non-remappable shortcuts the app also uses (File/Edit menu standards,
    /// Preferences itself, the Simulation menu's Ctrl+R, ...) — exposed ONLY
    /// so conflictFor() can warn about a clash with one of these, never for
    /// editing. Not exhaustive of every QAction shortcut in the app, only
    /// the ones dense enough in the letter/Ctrl+letter space to plausibly
    /// collide with a remap.
    struct FixedShortcut {
        QString label;
        QKeySequence key;
    };
    static const QVector<FixedShortcut>& fixedShortcuts();

    /// What `key` would collide with among every remappable binding (other
    /// than `excludeId`, when given, so an action does not "conflict with
    /// itself" while re-typing its own current key) and every fixed
    /// shortcut — the label of the clash, or empty when `key` is free (or
    /// itself empty: "no shortcut" never conflicts with anything). Used by
    /// both the Hotkeys tab's live capture warning and by anyone about to
    /// call setBinding().
    static QString conflictFor(const QKeySequence& key,
                               const QString& excludeId = QString());
};

} // namespace calango::gui
