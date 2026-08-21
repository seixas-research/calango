#include "gui/ShortcutRegistry.hpp"

#include "gui/SettingsManager.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace calango::gui {

namespace {

/// The inventory, in the Hotkeys table's display order. Building this fresh
/// on every call (rather than a function-local static) is deliberate and
/// cheap — ~13 entries, no heap churn worth caching — and sidesteps any
/// static-initialization-order question with the QKeySequence::StandardKey
/// constructors below, which query the platform.
QVector<ShortcutAction> defaultActions()
{
    return {
        {QStringLiteral("viewport.mode.rotate"), QObject::tr("Rotation mode"),
         QObject::tr("Viewport — Mouse Mode"), QKeySequence(Qt::Key_R)},
        {QStringLiteral("viewport.mode.pan"), QObject::tr("Translation mode"),
         QObject::tr("Viewport — Mouse Mode"), QKeySequence(Qt::Key_T)},
        {QStringLiteral("viewport.mode.select"), QObject::tr("Selection mode"),
         QObject::tr("Viewport — Mouse Mode"), QKeySequence(Qt::Key_S)},
        {QStringLiteral("viewport.mode.insert"), QObject::tr("Insertion mode"),
         QObject::tr("Viewport — Mouse Mode"), QKeySequence(Qt::Key_I)},
        {QStringLiteral("viewport.mode.distance"),
         QObject::tr("Distance measurement mode"),
         QObject::tr("Viewport — Mouse Mode"), QKeySequence(Qt::Key_D)},
        {QStringLiteral("viewport.mode.angle"),
         QObject::tr("Angle measurement mode"),
         QObject::tr("Viewport — Mouse Mode"), QKeySequence(Qt::Key_A)},
        {QStringLiteral("viewport.resetCamera"), QObject::tr("Reset camera"),
         QObject::tr("Viewport — View"), QKeySequence(Qt::Key_F)},
        {QStringLiteral("viewport.toggleProjection"),
         QObject::tr("Toggle perspective / orthographic"),
         QObject::tr("Viewport — View"), QKeySequence(Qt::Key_O)},
        {QStringLiteral("edit.undo"), QObject::tr("Undo"), QObject::tr("Edit"),
         QKeySequence(QKeySequence::Undo)},
        {QStringLiteral("edit.redo"), QObject::tr("Redo"), QObject::tr("Edit"),
         QKeySequence(QKeySequence::Redo)},
        {QStringLiteral("edit.deleteSelection"),
         QObject::tr("Delete selected atoms"), QObject::tr("Edit"),
         QKeySequence(QKeySequence::Delete)},
        {QStringLiteral("viewport.tab.next"), QObject::tr("Next tab"),
         QObject::tr("Viewport — Navigation"), QKeySequence(Qt::Key_Tab)},
        {QStringLiteral("viewport.tab.previous"), QObject::tr("Previous tab"),
         QObject::tr("Viewport — Navigation"),
         QKeySequence(Qt::SHIFT | Qt::Key_Tab)},
        // Panel show/hide, Ctrl+0..9 — Qt::CTRL is the portable modifier: it
        // maps to Cmd on macOS and Ctrl everywhere else with no per-platform
        // branching. Digit order matches the existing View menu's own dock
        // order (MainWindow::createMenusAndDocks()), so the mapping reads
        // the same way in both places.
        {QStringLiteral("panel.toggle.structure"), QObject::tr("Toggle Structure panel"),
         QObject::tr("Panels"), QKeySequence(Qt::CTRL | Qt::Key_0)},
        {QStringLiteral("panel.toggle.volumetricData"),
         QObject::tr("Toggle Volumetric Data panel"), QObject::tr("Panels"),
         QKeySequence(Qt::CTRL | Qt::Key_1)},
        {QStringLiteral("panel.toggle.additionalOverlays"),
         QObject::tr("Toggle Additional Overlays panel"), QObject::tr("Panels"),
         QKeySequence(Qt::CTRL | Qt::Key_2)},
        {QStringLiteral("panel.toggle.processes"), QObject::tr("Toggle Processes panel"),
         QObject::tr("Panels"), QKeySequence(Qt::CTRL | Qt::Key_3)},
        {QStringLiteral("panel.toggle.representation"),
         QObject::tr("Toggle Representation panel"), QObject::tr("Panels"),
         QKeySequence(Qt::CTRL | Qt::Key_4)},
        {QStringLiteral("panel.toggle.spatialReferences"),
         QObject::tr("Toggle Spatial References panel"), QObject::tr("Panels"),
         QKeySequence(Qt::CTRL | Qt::Key_5)},
        {QStringLiteral("panel.toggle.visualEffects"),
         QObject::tr("Toggle Visual Effects panel"), QObject::tr("Panels"),
         QKeySequence(Qt::CTRL | Qt::Key_6)},
        {QStringLiteral("panel.toggle.orchestration"),
         QObject::tr("Toggle Orchestration panel"), QObject::tr("Panels"),
         QKeySequence(Qt::CTRL | Qt::Key_7)},
        {QStringLiteral("panel.toggle.hpc"), QObject::tr("Toggle HPC panel"),
         QObject::tr("Panels"), QKeySequence(Qt::CTRL | Qt::Key_8)},
        {QStringLiteral("panel.toggle.results"), QObject::tr("Toggle Results panel"),
         QObject::tr("Panels"), QKeySequence(Qt::CTRL | Qt::Key_9)},
    };
}

/// Read the persisted override map. Empty when nothing has ever been
/// remapped — the common case, and the one Preferences ships with.
QJsonObject loadOverrides()
{
    const QString text =
        QSettings().value(QLatin1String(SettingsManager::kHotkeys)).toString();
    if (text.isEmpty())
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    return document.isObject() ? document.object() : QJsonObject{};
}

void saveOverrides(const QJsonObject& overrides)
{
    QSettings().setValue(
        QLatin1String(SettingsManager::kHotkeys),
        QString::fromUtf8(QJsonDocument(overrides).toJson(QJsonDocument::Compact)));
}

} // namespace

const QVector<ShortcutAction>& ShortcutRegistry::actions()
{
    static const QVector<ShortcutAction> table = defaultActions();
    return table;
}

const ShortcutAction* ShortcutRegistry::find(const QString& id)
{
    for (const ShortcutAction& action : actions())
        if (action.id == id)
            return &action;
    return nullptr;
}

QKeySequence ShortcutRegistry::binding(const QString& id)
{
    const ShortcutAction* action = find(id);
    if (!action)
        return QKeySequence();
    const QJsonValue override_ = loadOverrides().value(id);
    if (override_.isString())
        return QKeySequence(override_.toString(), QKeySequence::PortableText);
    return action->defaultKey;
}

void ShortcutRegistry::setBinding(const QString& id, const QKeySequence& key)
{
    if (!find(id))
        return; // an id nothing owns cannot be persisted meaningfully
    QJsonObject overrides = loadOverrides();
    overrides[id] = key.toString(QKeySequence::PortableText);
    saveOverrides(overrides);
}

void ShortcutRegistry::resetToDefault(const QString& id)
{
    QJsonObject overrides = loadOverrides();
    if (!overrides.contains(id))
        return; // nothing overridden — nothing to write back
    overrides.remove(id);
    saveOverrides(overrides);
}

void ShortcutRegistry::resetAllToDefaults()
{
    saveOverrides({});
}

const QVector<ShortcutRegistry::FixedShortcut>& ShortcutRegistry::fixedShortcuts()
{
    // Every entry resolved live against QKeySequence::StandardKey where one
    // exists, rather than a hardcoded literal, so this stays correct on
    // whichever platform actually runs — Cmd+N vs Ctrl+N, etc. Not
    // exhaustive of every QAction in the app (see the header); this is the
    // subset dense enough in the letter/Ctrl+letter space to plausibly
    // collide with a remap of one of the actions() above.
    static const QVector<FixedShortcut> table = {
        {QObject::tr("New Workspace"), QKeySequence(QKeySequence::New)},
        {QObject::tr("Open Structure…"), QKeySequence(QKeySequence::Open)},
        {QObject::tr("Open Trajectory…"), QKeySequence(QObject::tr("Ctrl+T"))},
        {QObject::tr("Save Structure As…"), QKeySequence(QKeySequence::SaveAs)},
        {QObject::tr("Save Trajectory As…"),
         QKeySequence(QObject::tr("Ctrl+Shift+T"))},
        {QObject::tr("Save Project"), QKeySequence(QKeySequence::Save)},
        {QObject::tr("Save/Open Project"),
         QKeySequence(QObject::tr("Ctrl+Shift+O"))},
        {QObject::tr("Export Image…"), QKeySequence(QObject::tr("Ctrl+E"))},
        {QObject::tr("Close Tab"), QKeySequence(QKeySequence::Close)},
        {QObject::tr("Quit"), QKeySequence(QKeySequence::Quit)},
        {QObject::tr("Add Atom…"), QKeySequence(QObject::tr("Ctrl+Shift+A"))},
        {QObject::tr("Bond Editor…"), QKeySequence(QObject::tr("Ctrl+B"))},
        {QObject::tr("Preferences…"), QKeySequence(QObject::tr("Ctrl+P"))},
        {QObject::tr("Single-point Calculation…"),
         QKeySequence(QObject::tr("Ctrl+R"))},
    };
    return table;
}

QString ShortcutRegistry::conflictFor(const QKeySequence& key,
                                      const QString& excludeId)
{
    if (key.isEmpty())
        return QString(); // "no shortcut" never conflicts with anything

    for (const ShortcutAction& action : actions()) {
        if (action.id == excludeId)
            continue;
        if (binding(action.id) == key)
            return action.label;
    }
    for (const FixedShortcut& fixed : fixedShortcuts())
        if (fixed.key == key)
            return fixed.label;
    return QString();
}

} // namespace calango::gui
