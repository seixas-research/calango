#pragma once

#include "core/Structure.hpp"

#include <string>
#include <utility>
#include <vector>

namespace calango::core {

/// Special Quasirandom Structure generation for substitutional alloys —
/// natively in C++, with no Python, icet or subprocess involved.
///
/// A random solid solution has, in the infinite-size limit, pair correlation
/// functions Π_α that are exactly the products of the species concentrations.
/// A finite supercell cannot reproduce that for every cluster at once, so an
/// SQS is the decoration whose correlations best match the random-alloy
/// targets over the shells that matter most:
///
///     ΔΠ = Σ_α w_α · | Π_α^SQS − Π_α^random |
///
/// where α runs over the pair clusters of each coordination shell and w_α
/// weights the shells (nearer shells dominate the physics, so they carry more
/// weight). This is minimized by Metropolis Monte Carlo with a geometric
/// annealing schedule over species swaps on the substitutional sublattice,
/// which preserves the composition exactly by construction.
///
/// Correlations are evaluated for every ORDERED species pair, so the scheme
/// handles binary, ternary and quaternary (and higher) solid solutions
/// uniformly — a binary is not a special case in the code.
class SqsGenerator {
public:
    struct Params {
        int nx = 2, ny = 2, nz = 2; ///< supercell repetitions
        /// Symbol whose sites form the substitutional sublattice. Every other
        /// site keeps its species.
        std::string replaceElement;
        /// Target sublattice composition, e.g. {{"Cu",0.75},{"Au",0.25}}.
        /// Fractions are normalized; site counts use largest-remainder
        /// rounding so they sum to the sublattice size exactly.
        std::vector<std::pair<std::string, double>> composition;
        /// Coordination-shell cutoffs (Å). Pairs are binned by distance into
        /// the shells these bound: (0, shell1], (shell1, shell2], … A cutoff
        /// of 0 (or one not greater than its predecessor) ends the list.
        double shell1 = 3.2;
        double shell2 = 4.8;
        /// Per-shell weights w_α. Shells beyond the supplied weights fall back
        /// to 1/(shell index + 1), so nearer shells always dominate.
        std::vector<double> shellWeights;
        int steps = 20000;   ///< Monte Carlo swap attempts
        unsigned seed = 42;
        /// Annealing temperature schedule (dimensionless, in units of ΔΠ).
        double startTemperature = 0.5;
        double endTemperature = 1e-4;
    };

    struct Result {
        Structure structure;
        /// Final objective ΔΠ — 0 means the supercell reproduces the
        /// random-alloy correlations exactly over the sampled shells.
        double objective = 0.0;
        /// Objective of the initial random decoration, for context: the
        /// improvement factor is what tells the user whether the annealing
        /// actually did anything on this cell.
        double initialObjective = 0.0;
        int shells = 0;      ///< coordination shells actually populated
        int sublatticeSites = 0;
        int steps = 0;       ///< swap attempts performed
        int accepted = 0;    ///< swaps accepted
        std::string method;  ///< human-readable backend description
    };

    /// Throws std::invalid_argument when the request cannot be satisfied
    /// (fewer than two species, no matching sites, degenerate cell, …).
    static Result generate(const Structure& base, const Params& params);

    /// Build the `nx × ny × nz` supercell of `base`. Exposed because the SQS
    /// sublattice indices refer to it, and callers that want to inspect the
    /// undecorated supercell should not have to re-derive it.
    static Structure makeSupercell(const Structure& base, int nx, int ny, int nz);
};

} // namespace calango::core
