#pragma once

#include "core/CalculatorConfig.hpp"

#include <QString>
#include <QStringList>

#include <array>
#include <optional>

namespace calango::gui::CalculatorParameters {

/// User-editable per-element calculator defaults, read from
/// ~/.calango/calculator_parameters.json (or $CALANGO_CONFIG_DIR — the same
/// override SettingsManager honours, so tests never touch the real file).
///
/// Shape:
///   {
///     "GPAW": {
///       "default":  { "pw": 500, "kpts": [7, 7, 7] },
///       "elements": {
///         "Fe": { "pw": 700, "kpts": [11, 11, 11] },
///         "O":  { "pw": 800 }
///       }
///     },
///     "VASP": { ... }
///   }
///
/// Engines are keyed by EnginePresets::presetName(). Both "pw" (plane-wave
/// cutoff, eV) and "kpts" ([k1,k2,k3]) are optional at every level: an entry
/// says only what it knows, and anything unsaid falls through — first to the
/// engine's "default" object, then to the wizard's hardcoded values. A
/// missing, empty or malformed file therefore changes nothing.
struct Suggestion {
    std::optional<double> planeWaveCutoffEv;
    std::optional<std::array<int, 3>> kpts;
};

/// The file's absolute path (inside SettingsManager::directory()).
QString filePath();

/// The suggestion for `kind` given the chemical elements present in the
/// structure. Where several elements carry values, the strictest wins —
/// the highest cutoff and the densest mesh per axis — because a cell
/// containing iron needs iron's cutoff no matter how much hydrogen
/// surrounds it. Elements without an entry contribute nothing; if none
/// contributes, the engine's "default" entry applies; if that too is
/// absent, the optionals come back empty and the caller keeps its own
/// defaults.
Suggestion suggestionFor(core::CalculatorKind kind,
                         const QStringList& elements);

/// Write a commented skeleton if (and only if) the file does not exist, so
/// the feature is discoverable by opening ~/.calango rather than by reading
/// source code. Never touches an existing file.
void ensureFileExists();

} // namespace calango::gui::CalculatorParameters
