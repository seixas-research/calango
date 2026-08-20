#pragma once

#include "core/CalculatorConfig.hpp"
#include "core/Structure.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Parameters for an N-component Dilute Solution Interpolation (DSI) run —
/// see `core/Dsim.hpp` for the model itself and `docs/sphinx/source/
/// simulations/dsim.md` for the working equations. N=2 (the module's
/// original scope) is the size = 2 special case of everything here, not a
/// separate code path.
///
/// All N + N(N-1) structures are built ahead of time in C++ (DsimWizard,
/// from the user's own N input structures — one per pristine species,
/// picked from open documents or imported files — via the existing
/// supercell builder plus a single-atom relabel for each impurity) and
/// baked into the generated script as literal `ase.Atoms(...)` data,
/// exactly the way `ElasticScriptGenerator` bakes its precomputed strain
/// matrices: the script needs no separate staged trajectory file, and
/// running it stays a one-file, standalone operation.
struct DsimConfig {
    /// Engine + relaxation knobs from the wizard's shared Calculator
    /// Settings stage. `relaxCell` is forced true by the wizard regardless
    /// of what the stage defaults to — the paper's protocol always relaxes
    /// both ions AND the cell (Methods: "All geometries were fully
    /// optimized... including unit cell relaxation").
    CalculatorConfig calculator;

    /// N chemical symbols, species[i] naming pristine[i] and every
    /// impurity[i][*]/impurity[*][i] row/column.
    std::vector<std::string> species;

    /// N pristine supercells, one per species, each built from that
    /// species' OWN input structure (not a shared template relabeled —
    /// every element keeps its own native lattice/starting geometry).
    std::vector<Structure> pristine;

    /// N x N; `impurity[i][j]` (i != j) is host j's pristine supercell
    /// with one atom substituted to species i (species i diluted in host
    /// j — Eq. 9-10). Diagonal entries are unused (default-constructed,
    /// empty `Structure`s) — the pristine supercell already covers x=0/x=1
    /// for that species pair.
    std::vector<std::vector<Structure>> impurity;
};

/// Turns a DsimConfig into a standalone ASE script that relaxes all
/// N + N(N-1) supercells, computes the full N x N differential mixing
/// enthalpy matrix (Eq. 9-10), and — depending on N — the interpolated
/// binary DeltaH_mix(x) curve (N=2), the ternary composition-triangle grid
/// (N=3), and always the N(N-1)/2 pairwise binary sub-curves (every N,
/// the fallback view for N>3 and a useful cross-section for any N — see
/// docs/sphinx/source/simulations/dsim.md's "Extensibility" section).
/// Every formula is reimplemented here rather than imported, per the
/// project's "generated scripts never import Calango" convention, and
/// writes `dsim.json` (schema `calango.dsim/2`) with the
/// `CALANGO_RESULT dsim=dsim.json` marker the job controller watches for.
std::string generateDsimScript(const DsimConfig& config);

} // namespace calango::core
