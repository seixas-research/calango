#pragma once

#include "core/CalculatorConfig.hpp"

#include <cstdint>
#include <string>

namespace calango::core {

/// Hybrid molecular-dynamics / Monte Carlo refinement of a graphene oxide
/// decoration.
///
/// WHAT PROBLEM THIS SOLVES. GrapheneOxideBuilder places functional groups at
/// random free sites subject to the chemistry (one group per carbon, epoxides
/// on bonded pairs, edge groups on the rim). That gives a structure at the
/// requested COMPOSITION, but a random arrangement is not a low-energy one:
/// real graphene oxide is strongly correlated, with hydroxyls clustering and
/// epoxides lining up along the ridges they create. Relaxing the geometry does
/// not fix that, because a local optimizer moves atoms and never moves a group
/// from one carbon to another — the arrangement is a discrete degree of
/// freedom, and it needs a discrete sampler.
///
/// WHY THIS IS A GENERATED SCRIPT AND NOT A C++ LOOP. The acceptance criterion
/// needs a real energy, which means a real calculator — MACE, xTB, GPAW — and
/// in this application every calculator lives behind ASE in Python. A C++ loop
/// would have to drive embedded Python for every one of its thousands of energy
/// evaluations, on the GUI thread, for hours. Emitting a script instead puts the
/// run in the job system where every other calculation lives: it can be queued,
/// exported, sent to a cluster, and read before it is trusted.
///
/// THE ALGORITHM, AND THE ONE PART THAT IS NOT TEXTBOOK.
///
///   1. Pick an existing functional group at random and remove it.
///   2. Draw a new site of the kind that group needs — a single carbon for a
///      hydroxyl or an edge group, a bonded pair for an epoxide — from the
///      sites currently free, and pick a face at random for basal groups.
///   3. Run a short burst of molecular dynamics at the target temperature so
///      the new arrangement relaxes into its own local basin rather than being
///      judged on an unrelaxed geometry. The burst is the ONLY relaxation
///      mechanism in the run — there is deliberately no geometry optimizer
///      anywhere in it.
///   4. **Validate the topology.** This is the step a generic Metropolis code
///      does not have and this system cannot do without. Molecular dynamics on
///      a reactive surface will happily abstract a hydroxyl and a neighbouring
///      hydrogen and release water — the energy drops, the move looks
///      excellent, and the structure is no longer the material anyone asked
///      for. A move whose chemistry did not survive the dynamics is rejected
///      regardless of its energy.
///   5. Metropolis: accept with min(1, exp(-ΔE / kB T)), else revert exactly —
///      positions, group inventory and site pools all restored.
///
/// WHAT IT TOOK TO MAKE THAT PROTOCOL ACTUALLY RUN. The builder places every
/// group analytically on a FLAT sheet, so the as-built structure carries
/// ~10 eV/Å on its host carbons and tens of eV of strain. The first version
/// of this script released that in a single 20-step burst — a thermal shock
/// of thousands of kelvin for a few femtoseconds — and whichever group the
/// builder had placed closest to a neighbour came apart, after which the
/// run exited. Three things fix that without changing the protocol:
///
///   * an **initial equilibration** stage of its own (`equilibrationSteps`,
///     `equilibrationFrictionPerFs`): long enough for the sheet to pucker and
///     thermalize, thermostatted strongly enough to drain the strain as it is
///     released, checked for intact chemistry every few steps — and a group
///     that still comes apart is RELOCATED to a fresh free site (the
///     sampler's own move, inventory preserved) rather than the run
///     abandoned. The run refuses only when relocating stops helping;
///   * a **steric clearance** on every proposal (2.0 Å heavy–heavy, 1.6 Å
///     to a hydrogen — wider than the builder's 1.55 Å placement rule, which
///     defines the motif space rather than what a burst keeps): a bridging
///     oxygen 1.9 Å from a neighbouring hydroxyl oxygen is not a
///     configuration a burst relaxes, it is one it resolves by breaking a
///     bond, so it is refused before any energy is evaluated;
///   * the **cell is part of the reverted state** under NPT, and only the
///     in-plane vectors are ever scaled (ase's isotropic barostat scaled the
///     vacuum axis too, and a rejected move used to keep the scaled cell).
///
/// The site bookkeeping in the emitted script is a port of
/// core::ReactiveSiteGraph, whose C++ tests are the reference for the coupling
/// rules between single and pair sites. The two exist separately because the
/// builder runs in process and the sampler runs in the job; they are small,
/// and the invariant they share is asserted on both sides.
///
/// HYDROXYL ANTIPOSITION. GrapheneOxideBuilder::Config::hydroxylAntiposition
/// places hydroxyls as bonded, opposite-face PAIRS rather than independent
/// singles — see `hydroxylAntiposition` below. Moving the two halves of a
/// pair independently, the way a plain hydroxyl move set would, breaks that
/// arrangement the first time either half hops: nothing then re-forms the
/// pair, and the run silently drifts away from the material it was asked to
/// refine. When the flag is set, the emitted script instead recovers each
/// pair from the STARTING geometry once (the only point pairing is ever
/// derived rather than tracked) and carries it from then on as a single
/// compound "hydroxyl_pair" entry in the move list — occupying and drawing a
/// bonded PAIR site exactly like an epoxide, so it inherits the same
/// proposal-symmetry argument the existing epoxide pair-move already relies
/// on: a move always releases exactly what it goes on to occupy, so the
/// count of free sites of that region is the same before and after, and the
/// reverse move is exactly as likely to be proposed as the forward one. No
/// separate Hastings correction is introduced or needed.
/// How a proposed move is relaxed before it is judged — which is the whole
/// difference between the two modules this generator serves.
///
/// GO/MCMD (MolecularDynamics) runs a short thermostatted BURST after each
/// move: cheap, finite-temperature, and it samples the thermal ensemble. Its
/// weakness is measurable and was measured: a freshly rebuilt group carries
/// several eV of placement strain, and a burst too short to drain it hands
/// Metropolis a trial energy biased upward by that much, so the acceptance
/// ratio collapses whatever the temperature says (one 200-cycle run accepted
/// 3 moves, with a median trial ΔE of +3.9 eV = 150 kT).
///
/// GO/MC-Opt (Optimization) relaxes each proposal to a LOCAL MINIMUM instead,
/// with a force criterion. Both sides of the Metropolis test are then at a
/// minimum, so the comparison is between two arrangements rather than between
/// an arrangement and a placement artifact — the structural fix for the bias
/// above. It costs more per cycle (an optimization is many force evaluations,
/// not a fixed handful) and it samples the ATHERMAL landscape: the walk is
/// over local minima, not over a canonical ensemble, so the temperature is a
/// Metropolis parameter and nothing else. That is the right tool for "find me
/// the best decoration" and the wrong one for "sample the 300 K ensemble".
enum class GoMcRelaxation {
    MolecularDynamics, ///< GO/MCMD — a thermostatted burst per cycle
    Optimization,      ///< GO/MC-Opt — relax to a local minimum per cycle
};

/// Local-optimizer choice for GoMcRelaxation::Optimization. Append-only: the
/// value is written into the generated script as an ASE class name.
enum class GoMcOptimizer {
    Bfgs,   ///< ase.optimize.BFGS — the default, quasi-Newton, robust
    Lbfgs,  ///< ase.optimize.LBFGS — limited memory, better for large cells
    Fire,   ///< ase.optimize.FIRE — no Hessian, tolerant of bad starts
    Mdmin,  ///< ase.optimize.MDMin — cheapest per step, slowest to converge
};

/// The ASE class name `optimizer` selects, for the generated script.
const char* goMcOptimizerName(GoMcOptimizer optimizer);

struct GrapheneOxideMcmdConfig {
    /// Structure staged next to run.py — the builder's output.
    std::string inputStructure = "structure.extxyz";
    /// Lowest-energy arrangement found, which is what the run is for. Note
    /// this is NOT the last frame: a finite-temperature walk ends wherever it
    /// happens to be, and reporting that as the answer throws away the best
    /// configuration the sampler visited.
    std::string outputStructure = "mcmd_optimized.extxyz";
    /// Accepted configurations, in order — every one of them, appended as
    /// the walk accepts it, each frame carrying its own 0-based
    /// acceptance ordinal and the per-frame provenance of the
    /// all-structures log.
    ///
    /// This replaced `mcmd_trajectory.extxyz`, which held the same
    /// quantity worse: written once at the end from an in-memory list,
    /// throttled by `snapshotInterval`, and seeded at index 0 with the
    /// post-equilibration structure — a frame that was never an accepted
    /// configuration at all.
    std::string trajectory = "accepted_structures.extxyz";

    CalculatorConfig calculator;

    /// Temperature of both the dynamics and the acceptance test, in kelvin.
    ///
    /// One temperature, used twice, on purpose: a Metropolis test at a
    /// different temperature from the dynamics that generated the state does
    /// not sample any ensemble at all. Higher values explore more arrangements
    /// and accept more bad ones; 300-600 K is the useful range for annealing a
    /// decoration.
    double temperatureK = 300.0;

    /// Monte Carlo cycles — one attempted group move each.
    int cycles = 200;

    /// Molecular-dynamics steps per cycle.
    ///
    /// The burst is the only relaxation the protocol has, so it must be long
    /// enough for the strain of a freshly placed group to drain before the
    /// energy is judged — otherwise every Metropolis test sees an uphill
    /// move whatever the new site is worth. Measured on an equilibrated
    /// 7×4 sheet under MACE-MP-0 (epoxide moves, 300 K, 0.5 fs, friction
    /// 0.02/fs): ΔE is +1.8 eV after 10-20 steps, +0.4 at 40, within the
    /// thermal noise (±0.4 eV) around 80-120 steps, and NEGATIVE (-0.8 to
    /// -1.8 eV) by 200 — the burst is then long enough for the dynamics to
    /// carry the sheet's own slow relaxation past the placement strain, so
    /// the energy keeps dropping and Metropolis keeps accepting, which is
    /// what this protocol is for. The default stays at the protocol's own
    /// regime — many cheap cycles, a short burst each; the figures above
    /// are what to expect when lengthening it. The cost of the run is
    /// cycles × mdSteps energy evaluations.
    int mdStepsPerCycle = 5;

    /// Integration step, in femtoseconds. Graphene oxide carries O–H and
    /// C–H stretches whose periods are ~10 fs, so half a femtosecond is
    /// twenty points per period — a step at or above 1 fs integrates them
    /// badly and heats the system artificially, which this run then reads
    /// as broken chemistry.
    double timestepFs = 0.5;
    /// Thermostat coupling for the per-cycle burst, in inverse femtoseconds:
    /// the Langevin friction under NVT, and 1/(Berendsen temperature time)
    /// under NPT, so the knob means the same thing in both ensembles (ase's
    /// own Berendsen default of 500 fs would leave a burst of tens of
    /// femtoseconds effectively unthermostatted). Larger than a production-MD
    /// value on purpose, though a burst this short is mostly a thermal
    /// perturbation either way — the real equilibration is the stage below.
    double frictionPerFs = 0.02;

    /// Initial equilibration: molecular-dynamics steps run ONCE, before the
    /// first Monte Carlo cycle, to bring the as-built (flat, strained)
    /// structure to the target temperature. Checked for intact chemistry
    /// every few steps; a group that comes apart is relocated to a fresh
    /// free site and the dynamics resumes from the last intact state. Zero
    /// skips the stage entirely and starts the walk from the as-built
    /// geometry's own energy.
    ///
    /// The default is deliberately SHORT. The stage is checked for intact
    /// chemistry every EQUILIBRATION_CHECK_EVERY steps and relocates
    /// whatever came apart, so it costs one energy evaluation per step
    /// before the sampling has started at all; the protocol this module is
    /// for is many cheap cycles, and the cost of the run is dominated by
    /// what happens after this stage. Raise it for an as-built structure
    /// that carries more strain than a burst can drain.
    int equilibrationSteps = 10;
    /// Thermostat coupling during the equilibration, in inverse
    /// femtoseconds — stronger than the per-cycle value because this stage
    /// has tens of eV of strain to drain as it is released, and a thermostat
    /// with a 50 fs time constant lets that become a thermal shock first.
    double equilibrationFrictionPerFs = 0.1;

    /// Constant pressure instead of constant volume. Periodic sheets only —
    /// a flake has no cell to relax, and asking for NPT on one is meaningless
    /// rather than merely wasteful.
    bool constantPressure = false;
    double pressureGpa = 0.0;

    /// Attach basal groups to either face. Off restricts every move to the
    /// face the group already sat on, which is what a single-sided model wants.
    bool bothFaces = true;

    /// Was the input structure built with GrapheneOxideBuilder::Config::
    /// hydroxylAntiposition? When true, the sampler recovers each bonded,
    /// opposite-face hydroxyl pair from the STARTING geometry once, up
    /// front, and moves every such pair as one compound unit for the rest
    /// of the run — drawing a fresh bonded-pair site, the same pool an
    /// epoxide draws from, and rebuilding both -OH groups with a freshly
    /// drawn opposite-face split — so a swap can never separate a pair
    /// onto two independently sited carbons.
    ///
    /// ON by default, and exposed as a real checkbox on the wizard's MCMD
    /// settings page. It used to be inherited state, read off the input
    /// Graphene Oxide Build's own "go_pair_id" scalar field and never
    /// offered as a control, on the argument that a toggle could disagree
    /// with the geometry actually being refined. That hazard is real but it
    /// is not fatal, because pairing is recovered FROM GEOMETRY: on a build
    /// that has no bonded, opposite-face hydroxyl pairs the bootstrap finds
    /// none and every hydroxyl stays an ordinary single, so the flag set on
    /// an unpaired build is a no-op rather than a corruption. The wizard
    /// therefore keeps showing what the input build actually contains, in
    /// prose, beside the checkbox. Off leaves every hydroxyl moved
    /// individually, exactly as before this option existed.
    bool hydroxylAntiposition = true;

    /// Deterministic seed for the move sequence and the thermostat.
    std::uint32_t seed = 0;

    /// NO LONGER READ by this generator, and no longer emitted into the
    /// script. It throttled `mcmd_trajectory.extxyz`, which no longer
    /// exists: `trajectory` above now receives EVERY accepted
    /// configuration, because an acceptance ordinal that skips is not an
    /// ordinal. Kept as a field so the wizard that sets it still compiles;
    /// its control has no effect until that wizard is revisited.
    int snapshotInterval = 1;

    // -- Live viewport -----------------------------------------------------

    /// Stream a geometry to the viewport every this many MC cycles. Zero
    /// streams nothing.
    ///
    /// A throttle, and it needs one. Every streamed frame is a block of text
    /// written, parsed, turned into a Structure and pushed through the
    /// renderer, and a run doing thousands of cycles can produce them faster
    /// than a viewport can draw them - at which point the pipe backs up and the
    /// application spends its time watching a calculation instead of running
    /// one.
    ///
    /// This is the ONE knob the live view has. Every cycle by default,
    /// because the protocol this module is for is many short cycles and a
    /// view that skips four of every five of them is not showing the run;
    /// zero is the headless setting a cluster wants.
    int viewportEveryCycles = 1;

    /// Also stream the geometry the DYNAMICS produced, before the move is
    /// judged.
    ///
    /// Two different things are worth watching and they answer different
    /// questions. The relaxed geometry after the MD burst shows the atoms
    /// MOVING - whether the structure is holding together. The accepted
    /// configuration after the Metropolis test shows the groups HOPPING, which
    /// is the discrete process the run exists to sample.
    ///
    /// BOTH are now streamed unconditionally, subject to the one throttle
    /// above: the dynamics between MC steps is always-on behavior, and
    /// `viewportEveryCycles` is its configurable interval. The wizard no
    /// longer offers a separate on/off for it - a run set to show the
    /// groups hopping and not the atoms moving cannot show the structure
    /// coming apart, which is the failure this module actually has. The
    /// field is kept, and still emitted, so a caller that constructs the
    /// config directly can turn the extra frames off; nothing in the
    /// application does.
    bool streamMdFrames = true;

    /// NOT read by this generator, and not emitted into the script — MCMD's
    /// classification of which carbon belongs to which functional group is
    /// already the C++ classifier (core::GrapheneOxideBuilder::
    /// functionalGroupLabels()), and re-deriving it from the streamed
    /// geometry client-side, once per frame, is exactly as cheap as deriving
    /// it once. Kept on this config purely because GrapheneOxideMcmdWizard
    /// already collects every other Output-page toggle here, and having
    /// MainWindow read `wizard.castPerFrame()` (which forwards to this
    /// field) match the shape of `wizard.streamMdFrames()`-style access
    /// beat inventing a second, parallel place for one bool to live. See
    /// MainWindow::openGoMcmd() and
    /// MainWindow::redefineFunctionalGroupCastForFrame().
    bool castPerFrame = true;

    // -- GO/MC-Opt ----------------------------------------------------------

    /// Which module this configuration is for — see GoMcRelaxation.
    GoMcRelaxation relaxation = GoMcRelaxation::MolecularDynamics;

    // ---- Grand canonical (GO Grand Canonical MC) ------------------------
    //
    // OFF for GO/MCMD and GO/MC-Opt, which conserve the group inventory and
    // only relocate it. ON adds insertion and deletion moves governed by
    // chemical potentials, so the COMPOSITION becomes a sampled quantity
    // rather than an input.
    bool grandCanonical = false;

    /// Δμ_H and Δμ_O, in eV, relative to the reference potentials the run
    /// computes for itself:
    ///
    ///     μ_H⁰ = ½ E_tot(H₂)
    ///     μ_O⁰ = E_tot(H₂O) − E_tot(H₂)
    ///
    /// (hydrogen from the H₂ molecule; oxygen from water in equilibrium with
    /// H₂ — the standard humid-environment reference). The absolute
    /// potentials the acceptance criterion uses are μ_s = μ_s⁰ + Δμ_s.
    ///
    /// Both references MUST come from the same calculator and settings as
    /// the sheet energies: the criterion subtracts one from the other, and a
    /// reference computed at a different cutoff or functional leaves a
    /// residue in every acceptance decision that has nothing to do with the
    /// chemistry. The run computes them itself for exactly that reason.
    double deltaMuHEv = 0.0;
    double deltaMuOEv = 0.0;

    /// Relative proposal weights for the three move classes. Normalized in
    /// the script, so these are ratios rather than probabilities. Swap is
    /// kept because a pure insert/delete walk equilibrates the composition
    /// long before it equilibrates the arrangement at that composition.
    double swapWeight = 1.0;
    double insertWeight = 1.0;
    double deleteWeight = 1.0;

    /// Where the two reference molecules' energies are cached, relative to
    /// the run directory's parent. Keyed inside by a calculator+settings
    /// signature, so a second run with the same settings reuses them and a
    /// run with different ones recomputes rather than silently inheriting.
    std::string referenceCache = "go_gcmc_references.json";
    /// Optimization only: the local optimizer.
    GoMcOptimizer optimizer = GoMcOptimizer::Bfgs;
    /// Optimization only: the force convergence criterion, in eV/Å. The value
    /// every relaxation in this application defaults to, and the one a reader
    /// expects to see quoted beside a relaxed energy.
    double fmax = 0.05;
    /// Optimization only: the step ceiling per cycle. A cap, not a target —
    /// convergence to `fmax` is what ends a relaxation, and this is what stops
    /// one cycle of a 200-cycle run from spending an afternoon on a proposal
    /// that was never going to converge. A cycle that hits it is recorded as
    /// unconverged and still judged: refusing to judge it would silently drop
    /// the hardest proposals out of the statistics.
    int optimizerMaxSteps = 200;
    /// Optimization only: cap on the distance one optimizer step may move an
    /// atom, in Å (ase's own `maxstep`). The default is ase's; raising it
    /// speeds up a badly strained start and risks stepping over the minimum.
    double optimizerMaxStep = 0.2;
    // -- Variable-cell relaxation -------------------------------------------
    //
    // The SAME four questions Geometry Optimization asks, and deliberately not
    // a simplified version of them: an MC-Opt run whose cell was relaxed
    // isotropically is not comparable with one relaxed anisotropically, so a
    // module that offered only an on/off switch would be producing energies a
    // user cannot line up against the relaxations they already ran. Collected
    // by the shared gui::CellRelaxationControls, which is the one
    // implementation of the rules between them.

    /// Relax the CELL as well as the atoms. Off by default — a graphene oxide
    /// sheet in vacuum has no meaningful cell degree of freedom along the
    /// vacuum axis, and letting one move is how a slab collapses onto its own
    /// image.
    bool relaxCell = false;
    /// Which ASE cell filter wraps the atoms. FrechetCellFilter is the
    /// better-behaved default; UnitCellFilter is the classic one.
    CellFilter cellFilter = CellFilter::FrechetCell;
    /// Constrain the strain to hydrostatic (isotropic) rather than the full
    /// anisotropic stress relaxation.
    bool cellHydrostatic = false;
    /// Use the per-component Voigt mask below instead of the isotropic /
    /// anisotropic presets — how a 2D sheet releases its two in-plane axes and
    /// pins the vacuum one.
    bool cellCustomMask = false;
    /// Voigt-order mask [xx, yy, zz, yz, xz, xy]: true = relax that component.
    bool cellMask[6] = {true, true, true, true, true, true};
};

class GrapheneOxideMcmdScriptGenerator {
public:
    static std::string generate(const GrapheneOxideMcmdConfig& config);
};

} // namespace calango::core
