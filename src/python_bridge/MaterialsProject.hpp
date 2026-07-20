#pragma once

#include "core/Structure.hpp"

#include <string>

namespace calango::pybridge {

/// Materials Project database access via the embedded Python interpreter.
///
/// Uses the documented REST endpoint (api.materialsproject.org) directly
/// through urllib — no extra Python packages required. The user supplies
/// their personal API key (materialsproject.org/api). GUI-thread only,
/// blocking with a network timeout; throws std::runtime_error with an
/// actionable message on HTTP/auth/parse failures.
class MaterialsProject {
public:
    /// Fetch the structure for a Materials Project ID (e.g. "mp-149").
    static core::Structure fetchStructure(const std::string& materialId,
                                          const std::string& apiKey);
};

} // namespace calango::pybridge
