#pragma once

#include "core/MoleculeGraph.hpp"

#include <string>

namespace calango::core {

/// SMILES in and out for the sketcher, implemented natively.
///
/// WHAT IS SUPPORTED, precisely — this is the ORGANIC SUBSET plus bracket
/// atoms, which is what a sketcher needs and what a chemist types:
///
///   * organic-subset atoms without brackets: `B C N O P S F Cl Br I`, and
///     their aromatic forms `b c n o p s`;
///   * bracket atoms `[...]`: element symbol, hydrogen count (`[nH]`,
///     `[CH3]`), and formal charge (`[NH4+]`, `[O-]`, `[Fe+2]`, `[N++]`);
///   * bond symbols `-` `=` `#` `:` and the disconnection `.`;
///   * branches `(...)`, nested to any depth;
///   * ring-closure digits `1`-`9` and `%nn`, including a bond order written
///     on the closure (`C=1CCCCC=1`);
///   * aromatic rings, KEKULIZED on import — the graph stores alternating
///     single/double bonds, so `c1ccccc1` and `C1=CC=CC=C1` produce exactly
///     the same drawing;
///   * 2D coordinates: ring systems are laid out as regular polygons, fused
///     rings share their edge, and chains come out as the standard zig-zag.
///
/// WHAT IS NOT — each one PARSES without error and is then DROPPED, because a
/// sketch that silently loses a stereocentre is better than one that refuses
/// the paste entirely, and the drawing shows the user exactly what survived:
///
///   * stereochemistry: `@` / `@@` tetrahedral chirality, and the `/` `\`
///     double-bond configuration marks (read as plain single bonds);
///   * isotope labels (`[13C]`);
///   * atom maps and reaction arrows (`[CH3:1]`, `>>`);
///   * wildcards `*` and any SMARTS query syntax.
///
/// Native rather than through a Python library on purpose: nothing in
/// Calango's dependency set ships a SMILES parser (ASE has none, and RDKit is
/// a heavy new dependency for one text field), and the subset above is a few
/// hundred lines that run with no Python environment at all — the same rule
/// the polymer, ice and graphene-oxide builders follow.
namespace smiles {

/// Parse `text` into a graph WITH 2D coordinates, ready to draw.
///
/// Returns false and leaves `graph` untouched on a syntax error, writing a
/// user-facing explanation into `error` (which may be null) — "unclosed
/// branch at position 7", not "parse failed".
bool parse(const std::string& text, MoleculeGraph& graph,
           std::string* error = nullptr);

/// Parse WITHOUT laying anything out — every atom lands at the origin. For a
/// caller that only wants the topology (the round-trip test, a formula
/// lookup); the drawing path always wants parse().
bool parseTopology(const std::string& text, MoleculeGraph& graph,
                   std::string* error = nullptr);

/// Write `graph` as SMILES. Disconnected fragments are joined with `.`, in
/// the order fragments() reports them. Perceived aromatic rings are written
/// lowercase; everything else is written in its Kekulé form. An empty graph
/// gives an empty string.
std::string write(const MoleculeGraph& graph);

/// Give every atom of `graph` a 2D position: ring systems as regular polygons
/// sharing their fused edges, acyclic branches as zig-zag chains, then a
/// spring clean-up. Fragments are placed side by side, left to right.
///
/// Exposed separately from parse() because it is also what the dialog's
/// "Tidy" runs on a drawing whose atoms have no meaningful coordinates yet
/// (a pasted structure, a template stamped on an empty canvas).
void layout2d(MoleculeGraph& graph);

} // namespace smiles
} // namespace calango::core
