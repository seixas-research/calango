#pragma once

#include "core/ChemicalOrder.hpp"
#include "core/GrapheneOxideBuilder.hpp"
#include "core/Structure.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Warren-Cowley short-range-order analysis of graphene oxide's functional-
/// group DECORATION, mapped onto the standard multicomponent alloy
/// formalism: each framework carbon's "species" is its functionalization
/// state — pristine, epoxide-C, hydroxyl-C, carboxyl-C, carbonyl-C — rather
/// than a real chemical element (every site here is physically carbon; the
/// species axis is a relabeling of GO chemistry onto the alloy math, not a
/// composition). α_ij(shell) < 0 reads as i-j attraction/ordering
/// (unlike-species pairs preferred at that shell), > 0 as clustering/
/// repulsion-of-unlike, 0 as the random-decoration expectation — the
/// ordinary Warren-Cowley reading, unchanged by the relabeling.
///
/// Reuses core::computeWarrenCowley() UNCHANGED — the shell math, the
/// α = 1 - p_ij/c_j formula and the periodic-image handling are that
/// module's own, tested code. This module's only new work is building a
/// CARBON-ONLY structure with fake, internal-use-only atomic numbers
/// standing in for the five states (never shown to the user — see
/// GrapheneOxidePairCorrelationResult::speciesNames), the same
/// "sublattice only, spectators dropped" trick core::SqsGenerator already
/// uses to feed a non-trivial structure subset through the identical
/// function.
struct GrapheneOxidePairCorrelationResult {
    /// The raw multicomponent result. wc.species holds this module's
    /// internal fake atomic numbers (see the .cpp), in the SAME order as
    /// speciesNames below — index them together, never wc.species directly
    /// for display.
    WarrenCowleyResult wc;
    /// Human-readable name per entry of wc.species: "Pristine", "Epoxide",
    /// "Hydroxyl", "Carboxyl", "Carbonyl" — whichever states are actually
    /// present in the analyzed structure. Same length/order as wc.species
    /// and wc.concentrations.
    std::vector<std::string> speciesNames;
};

/// Compute the analysis for one structure (one frame of a GO-MDMC
/// trajectory, or a single Graphene Oxide Build). `shellCutoffs` are RADIUS
/// cutoffs in the exact sense WarrenCowleyOptions::shellCutoffs uses —
/// honeycombShellCutoffs() below returns the honeycomb-appropriate defaults
/// for a requested shell count.
GrapheneOxidePairCorrelationResult analyzeGrapheneOxidePairCorrelation(
    const Structure& structure, const std::vector<double>& shellCutoffs);

/// Radius cutoffs isolating the first `shellCount` coordination shells of a
/// graphene honeycomb lattice, discovered EMPIRICALLY from a real pristine
/// sheet's own bonding (core::GrapheneOxideBuilder::pristine()) rather than
/// hardcoded from a lattice-sum formula — see the shell-enumeration test,
/// which checks the result against the lattice rather than trusting these
/// numbers by construction. Each cutoff sits at the midpoint between one
/// shell's radius and the next, same convention
/// WarrenCowleyOptions::shellCutoffs documents. Returns fewer than
/// `shellCount` cutoffs only if the internal search radius (generous, but
/// finite) could not resolve that many distinct shells — callers should
/// treat a short return as "ask for fewer shells", not silently pad it.
std::vector<double> honeycombShellCutoffs(int shellCount);

} // namespace calango::core
