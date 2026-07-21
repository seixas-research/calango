#pragma once

#include "core/Structure.hpp"

#include <string>
#include <vector>

namespace calango::pybridge {

/// Γ-point factor-group (nuclear-site) analysis of the vibrational
/// modes: which optical phonons are Raman-active, IR-active or silent.
///
/// Method: spglib provides the space-group operations of the primitive
/// cell; the character table of the isogonal point group is computed
/// numerically from the class-sum algebra (Burnside), the mechanical
/// representation χ(R) = (±1 + 2cosθ) · N_unmoved(R) is reduced into
/// irreps, translations are subtracted as the acoustic branch, and
/// activities follow from the vector (IR) and symmetric-quadratic
/// polarizability (Raman) representations. Mulliken labels are assigned
/// heuristically — subscript conventions in low-symmetry orthorhombic
/// groups can be permuted relative to textbook axis choices.
///
/// GUI-thread only (embedded interpreter); requires spglib + numpy.
class RamanAnalysis {
public:
    struct Mode {
        std::string label;  ///< Mulliken symbol (e.g. "T2g", "E1u", "A1g")
        int degeneracy = 1; ///< irrep dimension
        int totalCount = 0;    ///< copies in the full mechanical rep
        int acousticCount = 0; ///< copies belonging to the acoustic branch
        int opticalCount = 0;  ///< totalCount − acousticCount
        bool ramanActive = false;
        bool irActive = false;
    };

    struct Result {
        std::string spaceGroupSymbol;
        int spaceGroupNumber = 0;
        std::string pointGroup;
        int atomsPrimitive = 0; ///< atoms in the primitive cell (3N modes)
        std::vector<Mode> modes;
        std::string error; ///< empty on success
    };

    static Result analyze(const core::Structure& structure,
                          double symprec = 1e-3);
};

} // namespace calango::pybridge
