#pragma once

#include "core/Structure.hpp"

#include <string>
#include <utility>
#include <vector>

namespace calango::pybridge {

/// Special Quasirandom Structure generation for substitutional alloys.
///
/// The base structure is repeated into a supercell; the sites of one
/// chosen element form the substitutional sublattice, which is decorated
/// with the target composition so that the Warren-Cowley short-range
/// order parameters of the chosen pair shells are driven toward zero
/// (the ideal random alloy) — the defining property of an SQS.
///
/// Backend: icet's generate_sqs when the package is importable in the
/// embedded environment, otherwise a self-contained simulated-annealing
/// Monte Carlo over pair correlations (numpy + ase.neighborlist only).
/// Must be called on the GUI thread with PythonEngine alive; throws
/// std::runtime_error with the Python traceback on failure.
class SqsBuilder {
public:
    struct Params {
        int nx = 2, ny = 2, nz = 2;   ///< supercell repetitions
        std::string replaceElement;   ///< symbol whose sites get decorated
        /// Target composition on that sublattice, e.g. {{"Cu",0.75},{"Au",0.25}}.
        /// Fractions are normalized; counts are rounded to whole atoms.
        std::vector<std::pair<std::string, double>> composition;
        double shell1 = 3.2;          ///< first-shell cutoff (Å)
        double shell2 = 4.8;          ///< second-shell cutoff (Å; <= shell1 disables)
        int steps = 5000;             ///< Monte Carlo steps (fallback backend)
        unsigned seed = 42;
    };

    struct Result {
        core::Structure structure;
        std::string method;   ///< "icet" or "Monte Carlo (internal)"
        double objective = 0.0; ///< final Σ α² of the internal backend (0 for icet)
    };

    static Result generate(const core::Structure& base, const Params& params);
};

} // namespace calango::pybridge
