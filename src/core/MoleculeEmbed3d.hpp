#pragma once

#include "core/MoleculeGraph.hpp"
#include "core/Structure.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Knobs on the 2D -> 3D conversion. The defaults are what the dialog's
/// "Send to 3D Viewport" uses.
struct EmbedOptions {
    /// Turn each atom's implicit hydrogen count into real atoms. Off, the
    /// exported structure is the heavy-atom skeleton alone — useful when the
    /// hydrogens are about to be placed by something else, and wrong for
    /// anything that will be handed to a calculator.
    bool addHydrogens = true;
    /// Relaxation steps of the internal force field. 0 skips relaxation and
    /// exports the flat 2D drawing verbatim, which is how the tests separate a
    /// layout problem from an optimizer one.
    int steps = 1200;
    /// Å of vacuum padded around the molecule when a cell is written. 0 leaves
    /// the structure with NO cell, which is what a molecule should have.
    double vacuum = 0.0;
};

/// What embed() did, for the status line and for an honest failure message.
struct EmbedResult {
    bool ok = false;
    int heavyAtoms = 0;
    int hydrogensAdded = 0;
    /// Largest residual force at the end of the relaxation (arbitrary internal
    /// units — a convergence indicator, not a physical gradient).
    double residual = 0.0;
    /// Populated only on failure.
    std::string error;
};

/// Turn a 2D sketch into a real 3D structure.
///
/// THE FORCE FIELD IS A NATIVE, INTERNAL ONE — a small MM cleanup written for
/// this module, not a calculator from Calango's simulation stack. That choice
/// is deliberate and the alternatives were each disqualified for a concrete
/// reason:
///
///   * **EMT**, the only calculator that always runs in-process, is
///     parameterized for FCC metals only. It has no carbon, no oxygen and no
///     nitrogen; a benzene handed to it is not a poor result, it is an
///     exception.
///   * **xTB** (GFN2/GFN-FF) IS wired into Calango, but only as a generated
///     ASE script run in a subprocess against an optional `xtb` Python
///     package. Making the sketcher's export depend on that would put a
///     several-second subprocess and an "install xtb" dialog between a drawn
///     ring and the viewport, and would make the sketcher unusable on a
///     machine with no Python environment — which the other native builders
///     (polymer, ice, graphene oxide) deliberately are not.
///   * **A tabulated fragment library** cannot cover an arbitrary sketch.
///
/// So: bond stretching to a length taken from the covalent radii and the bond
/// order, angle bending to the angle the coordination implies, a planarity
/// restraint on every sp2 centre, a torsion term that keeps a double bond and
/// its four substituents coplanar, and soft non-bonded repulsion beyond 1-3.
/// Minimized by damped steepest descent. Perceived aromatic rings get ONE bond
/// length for the whole ring rather than the alternating long/short pair a
/// Kekulé structure would otherwise relax to — without that, benzene comes out
/// as a genuine 1.34/1.52 Å alternating hexagon, which is a correct answer to
/// the wrong question.
///
/// ACCURACY, stated plainly: this reproduces bond lengths to about 0.02 Å and
/// angles to a couple of degrees for ordinary organic connectivity, and it
/// gets planarity and ring geometry right. It is a CLEANUP, not a
/// conformational search — it will not find the global minimum of a flexible
/// chain, it has no electrostatics, no hydrogen bonding and no dispersion, and
/// its ring puckering is whatever the restraints happen to produce. Anything
/// that needs a real geometry should be run through the Geometry Optimization
/// wizard afterwards, which is exactly what the new tab is set up for.
EmbedResult embed(const MoleculeGraph& graph, Structure& out,
                  const EmbedOptions& options = {});

/// The same, restricted to `atomIndices` (a selection). An empty list means
/// the whole canvas.
EmbedResult embed(const MoleculeGraph& graph,
                  const std::vector<int>& atomIndices, Structure& out,
                  const EmbedOptions& options = {});

} // namespace calango::core
