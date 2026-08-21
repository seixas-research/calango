#pragma once

#include "dft/DftTypes.hpp"
#include "dftb/DftbTaskConfig.hpp"

/// Top-level driver: read a DftbTaskConfig, run the appropriate pipeline,
/// write the result JSON `config.outputPath` names. The single entry point
/// `tools/calango_dftb_run.cpp` calls.
///
/// Every task shares: load the Slater-Koster parameter set (pre-flight
/// element-pair coverage, same failure mode `SlaterKosterTable::load`
/// already reports), read the structure, build the basis and real-space
/// Hamiltonian, run SCC (or one-shot non-SCC) on the SCF k-mesh. What
/// differs per task is what gets evaluated and written afterward — see
/// runDftbTask()'s own switch.
namespace calango::dftb {

/// Emits progress to stdout as `CALANGO_PROGRESS <step> <total>` and
/// `CALANGO_INFO ...` lines, matching JobRunner's marker convention exactly
/// (see src/jobs/JobRunner.hpp) — the SAME parser that reads a generated
/// Python script's stdout reads this native binary's stdout unmodified.
dft::Outcome runDftbTask(const DftbTaskConfig& config);

} // namespace calango::dftb
