#pragma once

#include "core/CalculatorConfig.hpp"

#include <sstream>
#include <string>
#include <vector>

/// The thin Python wrapper CalculatorKind::CalangoDftb runs through — see
/// src/dftb/DftbTaskConfig.hpp for the manifest format and
/// tools/calango_dftb_run.cpp for what actually consumes it.
///
/// Every feature that wires this engine into a viewer (Single-Point today;
/// Bands/PDOS/Effective-Bands/Optics as their own wizards take it on) shares
/// ONE wrapper-emission function rather than each hand-writing the same
/// "export structure, write manifest, exec the binary, relay stdout"
/// boilerplate — the self-contained-script convention
/// (AseScriptGenerator::jsonLoggerPreamble's own sibling) applies here too:
/// the emitted Python never imports a Calango module, it just writes a
/// small manifest and execs a separate native binary, exactly as a
/// generated script might shell out to `dftb+` or `pw.x` itself.
namespace calango::core {

/// `task` is the manifest's own "task" value ("singlepoint", "bands",
/// "pdos", "unfolding", "optics"). `extraManifestLines` are additional
/// already-formatted "key value" lines specific to that task (e.g.
/// "kpathfile kpath.txt" for bands) — appended verbatim after the shared
/// SCC/mixing/k-mesh block this function itself writes from `c`.
/// `outputFileName` is the result JSON the manifest's own "output" key
/// names (e.g. "bands.json").
void emitDftbNativeWrapper(std::ostringstream& out, const CalculatorConfig& c,
                            const std::string& task,
                            const std::vector<std::string>& extraManifestLines,
                            const std::string& outputFileName);

} // namespace calango::core
