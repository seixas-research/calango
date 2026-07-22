#pragma once

#include "core/Structure.hpp"

#include <string>
#include <vector>

namespace calango::pybridge {

/// Nudged-elastic-band path construction via ASE's NEB modules. GUI-thread
/// only; throws std::runtime_error with the Python traceback on failure.
class NebBuilder {
public:
    /// Interpolate an initial reaction path between `initial` (reactant) and
    /// `final` (product). Returns `intermediateImages + 2` frames (the two
    /// endpoints plus the intermediates). `method` is "linear" or "idpp"
    /// (Improved Dimer / image-dependent pair potential). The two endpoints
    /// must have the same atom count and ordering.
    static std::vector<core::Structure> interpolate(
        const core::Structure& initial, const core::Structure& final,
        int intermediateImages, const std::string& method);
};

} // namespace calango::pybridge
