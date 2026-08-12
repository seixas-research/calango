#pragma once

#include "core/ElectronPhononAnalysis.hpp"

#include <string>

namespace calango::core {

/// Load the raw output of a `gpaw.elph` run into `ElectronPhononInput`.
///
/// `manifestPath` is the small text file the generated script writes
/// (`elph_raw.txt`); the bulk arrays it names are `.npy` files beside it. The
/// split is because of one array: |g|^2 is
/// (spins, q, k, modes, bands, bands) complex, 1.3 MB for a test case and tens
/// of gigabytes for a production mesh. GPAW already writes exactly that array,
/// in exactly that index order, as `gsqklnn.npy`, so the manifest points
/// straight at it and nothing recopies or reformats it.
///
/// The manifest is plain `key value...` lines rather than JSON so that this
/// module — and the closed-form test that pins it — stay free of Qt.
///
/// Returns false with `error` set if the manifest is missing or malformed, if
/// an array is absent, or if any array's shape disagrees with the declared
/// mesh. Shape disagreement is fatal on purpose: a g array read at the wrong
/// dimensions still produces a plausible lambda.
bool loadElectronPhononInput(const std::string& manifestPath,
                             ElectronPhononInput& out, std::string* error);

/// Write `result` as `epc.json`, the file the results viewer reads.
///
/// Written here rather than by the script because the analysis now happens
/// here — the script's job ends at the raw arrays.
bool writeElectronPhononResult(const std::string& path,
                               const ElectronPhononResult& result,
                               std::string* error);

/// Read raw arrays, analyse them, and write `epc.json` next to the manifest.
///
/// The whole post-processing step, so the caller only needs the run directory.
bool postProcessElectronPhonon(const std::string& directory,
                               ElectronPhononResult& result,
                               std::string* error);

/// Name of the manifest a run directory is expected to contain. A directory
/// without one has not reached the post-processing stage.
const char* electronPhononManifestName();

} // namespace calango::core
