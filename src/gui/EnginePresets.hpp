#pragma once

#include "core/CalculatorConfig.hpp"

#include <QString>
#include <QVector>

class QJsonObject;

namespace calango::gui {

/// Single source of truth for the per-calculator Conda environment presets.
///
/// The mapping (engine -> env folder / python executable) is persisted as a
/// compact JSON object string under the QSettings key
/// `SettingsManager::kEnvironmentPresets` and mirrored to
/// `~/.calango/settings.json`. It is configured centrally in the Preferences
/// "Python & Environments" tab and consumed silently by the simulation wizards
/// (which no longer prompt for an environment). An empty / missing entry means
/// "fall back to the active $PATH / embedded interpreter" — see
/// `CondaEnvs::resolvePython("")`.
namespace EnginePresets {

/// Stable, untranslated key used to store an engine's env preset. Kept stable
/// across releases so existing settings.json files keep resolving.
QString presetName(core::CalculatorKind kind);

/// Human-readable label for the engine (used in the Preferences table).
QString displayName(core::CalculatorKind kind);

/// The engines exposed as configurable rows in Preferences, in display order.
/// Only the "real" external backends that benefit from a dedicated env appear;
/// the toy potentials (EMT/ASAP/LJ) run in the embedded interpreter.
const QVector<core::CalculatorKind>& configurableEngines();

/// The full preset map, parsed from QSettings / settings.json.
QJsonObject readMap();

/// Persist the full preset map back to QSettings (flushed to settings.json on
/// SettingsManager::save()).
void writeMap(const QJsonObject& map);

/// Env preset for a single engine (empty string when unset).
QString envFor(core::CalculatorKind kind);

/// Set (or clear, when `env` is empty) the env preset for a single engine.
void setEnvFor(core::CalculatorKind kind, const QString& env);

} // namespace EnginePresets

} // namespace calango::gui
