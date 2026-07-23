#pragma once

#include <QString>

namespace calango::gui {

/// Helpers for putting a generated ASE script somewhere it can actually run.
///
/// Every script Calango generates begins with
/// `from calango_log import CalangoLog` (see
/// AseScriptGenerator::jsonLoggerPreamble), so the script alone is not
/// self-contained: `calango_log.py` has to sit beside it. Python puts a
/// script's own directory first on sys.path, so "beside it" is all that is
/// needed — no PYTHONPATH juggling, and a remote submission that uploads the
/// job directory carries the module along automatically.
///
/// Both functions report failure by return value; callers decide whether that
/// is fatal (job staging) or a warning (user-facing export).

/// Write `calango_log.py` into `directory`, overwriting any existing copy so
/// a stale module from an older Calango version never shadows the current
/// one. Returns false if the file could not be written.
bool writeLoggerModule(const QString& directory);

/// Write `text` to `scriptPath` and stage the logger module next to it.
/// `error` receives a human-readable reason on failure.
bool writeScriptWithLogger(const QString& scriptPath, const QString& text,
                           QString* error = nullptr);

} // namespace calango::gui
