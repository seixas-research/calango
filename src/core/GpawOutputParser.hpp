#pragma once

#include <optional>
#include <string>

namespace calango::core {

/// GPAW's own reported MPI rank count, read from the "cores:  N" line its
/// output header prints once at the very top of every run (gpaw/output.py) —
/// the one ground truth for how many ranks a run actually had, independent
/// of whatever command line Calango believes it launched.
///
/// This is the post-launch half of Task 1's (2026-08-22) "make silent-serial
/// impossible": the pre-flight check (GpawMpiPreflight.hpp) can only verify
/// that parallelism is POSSIBLE before a run starts — it cannot see what
/// actually happened. A launch-command bug discovered after the fact (this
/// session's own RunCommands::resolve() misclassification is the concrete
/// example: cores=4 requested, preflight passed, and the run still silently
/// executed as `cores:  1`) needs a check against the run's OWN account of
/// itself, not against what was intended.
///
/// Returns std::nullopt when no such line is found — an unrecognised or
/// truncated header, or a future GPAW version that changes the format.
/// Callers MUST treat that as "unknown, cannot verify", never as "1": a
/// missing signal is not evidence of a serial run.
std::optional<int> parseGpawWorldSize(const std::string& gpawOutText);

} // namespace calango::core
