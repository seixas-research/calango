#pragma once

#include "core/Structure.hpp"

#include <string>
#include <vector>

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
    /// One row of a summary search — everything the results table shows,
    /// plus the id needed to fetch the structure itself.
    struct SearchHit {
        std::string materialId;    ///< "mp-149"
        std::string formula;       ///< reduced formula, e.g. "Si"
        std::string spaceGroup;    ///< international symbol, e.g. "Fd-3m"
        int spaceGroupNumber = 0;
        double bandGap = 0.0;      ///< eV
        bool hasBandGap = false;   ///< false when the field is null upstream
        double energyAboveHull = 0.0; ///< eV/atom
        bool hasEnergyAboveHull = false;
        int nSites = 0;
        bool isStable = false;
    };

    /// Fetch the structure for a Materials Project ID (e.g. "mp-149").
    static core::Structure fetchStructure(const std::string& materialId,
                                          const std::string& apiKey);

    /// Multi-element / formula search against the summary endpoint.
    ///
    /// `query` accepts either
    ///   - a chemical *system*: "Li-Fe-O" or a bare element list
    ///     ("Li Fe O", "Li,Fe,O") — matches phases containing exactly those
    ///     elements, or containing *at least* them when `exactSystem` is
    ///     false; or
    ///   - a formula: "LiFePO4", "Fe2O3" — matches that stoichiometry.
    /// The distinction is made by `asFormula`.
    ///
    /// Results are ordered by energy above hull (most stable first) and
    /// truncated to `limit` rows; `limit` maps to the API's own paging cap,
    /// so a truncated result set is exactly the most stable `limit` phases.
    static std::vector<SearchHit> search(const std::string& query,
                                         const std::string& apiKey,
                                         bool asFormula,
                                         bool exactSystem,
                                         int limit);
};

} // namespace calango::pybridge
