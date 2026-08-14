#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include <string>

namespace calango::core {

/// Pull the one line worth showing out of a failed job's log.
///
/// Every generated script is Python, so a failure ends in a traceback whose
/// LAST line carries the exception type and message and whose middle is
/// interpreter frames the user cannot act on. Until now that line stayed in
/// log.txt: a failed run turned the process row red and said nothing, so the
/// reason had to be hunted for by opening a file the UI never mentioned.
///
/// Returns an empty string when nothing recognisable is present — the caller
/// then falls back to a generic message rather than inventing one.
///
/// Qt-free so it can be tested against real captured logs without a GUI.
std::string extractFailureReason(const std::string& log);

} // namespace calango::core
