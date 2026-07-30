#pragma once

#include <QString>

namespace calango::gui {

/// Helper for putting a generated ASE script somewhere it can actually run.
///
/// There is nothing to stage beside it any more: since the structured-logging
/// preamble became an embedded block rather than
/// `from calango_log import CalangoLog` (see
/// AseScriptGenerator::jsonLoggerPreamble), a generated script is one
/// self-contained file. It runs wherever ASE and the configured calculator
/// exist, with no Calango installation and no helper module — which is what
/// makes "export the script and run it on the cluster" actually work.
///
/// The function reports failure by return value; callers decide whether that
/// is fatal (job staging) or a warning (user-facing export).

/// Write `text` to `scriptPath`, reporting short-write / flush failures too —
/// a truncated Python file fails at run time with a confusing SyntaxError
/// rather than an I/O message. `error` receives a human-readable reason.
bool writeScript(const QString& scriptPath, const QString& text,
                 QString* error = nullptr);

} // namespace calango::gui
