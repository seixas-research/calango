#pragma once

#include "core/ChemicalOrder.hpp"
#include "core/Structure.hpp"

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace calango::core {

/// Special Quasirandom Structure generation for substitutional alloys —
/// natively in C++, with no Python, icet or subprocess involved.
///
/// A random solid solution has, in the infinite-size limit, cluster correlation
/// functions Π_α that are exactly the products of the species concentrations.
/// A finite supercell cannot reproduce that for every cluster at once, so an
/// SQS is the decoration whose correlations best match the random-alloy
/// targets over the clusters that matter most:
///
///     ΔΠ = Σ_α w_α · | Π_α^SQS − Π_α^random |
///
/// where α runs over the PAIR clusters of each coordination shell and — when
/// asked for — over the TRIPLET and QUADRUPLET clusters within their own
/// cutoffs. w_α weights them (nearer shells and lower orders dominate the
/// physics, so they carry more weight). This is minimized by Metropolis Monte
/// Carlo with a geometric annealing schedule over species swaps on the
/// substitutional sublattice, which preserves the composition exactly by
/// construction.
///
/// Multi-body terms are OFF by default (both cutoffs are 0), so a Params with
/// only the pair shells set reproduces the pair-only objective bit for bit.
/// They matter when three-body terms carry real energy — high-entropy alloys
/// above all, where a decoration can reproduce every pair correlation exactly
/// while its triplet statistics are badly wrong, because pairs simply cannot
/// see the difference between the two.
///
/// Correlations are evaluated for every species tuple, so the scheme handles
/// binary, ternary and quaternary (and higher) solid solutions uniformly — a
/// binary is not a special case in the code.
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

        // -- Multi-body clusters, OFF by default -----------------------------
        // A cutoff of 0 disables the order entirely and costs nothing: no
        // neighbour list is built and no term enters ΔΠ. That is what keeps a
        // pair-only run identical to what this class did before triplets
        // existed.
        //
        // Unlike pairs these are NOT split into shells. A triplet has three
        // distances rather than one, so "the shell it belongs to" is not a
        // number; the standard alternative is a symmetry orbit decomposition,
        // which needs the space group and buys little here because the
        // annealer only ever sees the summed deviation. So every triplet
        // inside `tripletCutoff` forms one orbit with one weight, and likewise
        // for quadruplets. A cluster is "inside" the cutoff when ALL of its
        // pairwise distances are — the usual compact-cluster criterion, and the
        // same one core::ClusterExpansion uses.

        /// Å; every pairwise distance in the triangle must be ≤ this.
        double tripletCutoff = 0.0;
        /// Å; every pairwise distance in the tetrahedron must be ≤ this.
        double quadrupletCutoff = 0.0;
        /// Weights of the two multi-body terms in ΔΠ. Below the first shell's
        /// weight by default: the pair correlations are what the energy is
        /// most sensitive to, and letting a large triplet population outvote
        /// them produces a structure with beautiful triangles and a wrong
        /// nearest-neighbour count.
        double tripletWeight = 0.5;
        double quadrupletWeight = 0.25;

        /// Safety cap on the clusters enumerated for ONE multi-body order.
        /// Exceeding it throws rather than truncating: a truncated cluster list
        /// is an arbitrary subset of the geometry, and optimizing against it
        /// would silently target a different random alloy.
        int maxClusters = 4000000;

        int steps = 20000;   ///< Monte Carlo swap attempts
        unsigned seed = 42;
        /// Annealing temperature schedule (dimensionless, in units of ΔΠ).
        double startTemperature = 0.5;
        double endTemperature = 1e-4;
    };

    /// ΔΠ split by cluster order. The split is what says WHY a run stalled:
    /// a total that will not fall while the pair term is already at 1e-5 means
    /// the cell is too small to satisfy the triplets, not that the annealing
    /// needs more steps.
    struct Deviation {
        double total = 0.0;
        double pair = 0.0;
        double triplet = 0.0;
        double quadruplet = 0.0;
    };

    struct Result {
        Structure structure;
        /// Final objective ΔΠ — 0 means the supercell reproduces the
        /// random-alloy correlations exactly over the sampled clusters.
        double objective = 0.0;
        /// Objective of the initial random decoration, for context: the
        /// improvement factor is what tells the user whether the annealing
        /// actually did anything on this cell.
        double initialObjective = 0.0;
        /// `objective`, decomposed by cluster order.
        Deviation deviation;
        int shells = 0;      ///< coordination shells actually populated
        int pairs = 0;       ///< pair clusters enumerated (all shells)
        int triplets = 0;    ///< triplet clusters enumerated (0 when disabled)
        int quadruplets = 0; ///< quadruplet clusters enumerated
        int sublatticeSites = 0;
        int steps = 0;       ///< swap attempts performed
        int accepted = 0;    ///< swaps accepted
        std::string method;  ///< human-readable backend description

        /// Warren-Cowley short-range order of the decoration that was
        /// produced, over the pair shell cutoffs — α = 0 is the ideal random
        /// alloy, so this is the objective restated in the units alloy people
        /// actually read.
        ///
        /// Computed on the SUBLATTICE ALONE (same cell, spectator species
        /// removed). Running it on the whole decorated cell would count the
        /// untouched species as an alloy component and dilute every α with
        /// neighbours the SQS never had a say over.
        WarrenCowleyResult shortRangeOrder;
    };

    /// Throws std::invalid_argument when the request cannot be satisfied
    /// (fewer than two species, no matching sites, degenerate cell, …).
    static Result generate(const Structure& base, const Params& params);

    /// Build the `nx × ny × nz` supercell of `base`. Exposed because the SQS
    /// sublattice indices refer to it, and callers that want to inspect the
    /// undecorated supercell should not have to re-derive it.
    static Structure makeSupercell(const Structure& base, int nx, int ny, int nz);

    /// ΔΠ of a structure that is ALREADY decorated, against the random-alloy
    /// targets implied by its own composition.
    ///
    /// The sublattice is every site carrying one of `params.composition`'s
    /// species (`replaceElement`, `nx/ny/nz`, `steps` and `seed` are ignored —
    /// nothing is built and nothing is annealed), and the concentrations come
    /// from the counts actually present rather than from the requested
    /// fractions.
    ///
    /// Exists so that a hand-built decoration with a KNOWN answer can be
    /// checked against the same code the annealer minimizes: the ordered
    /// structures whose correlations are exact integers are the only reference
    /// an SQS objective can be validated against, and re-deriving the cluster
    /// enumeration in a test would validate the test instead.
    static Deviation evaluate(const Structure& decorated, const Params& params);
};

} // namespace calango::core
