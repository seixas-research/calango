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
///      judged on an unrelaxed geometry.
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
/// The site bookkeeping in the emitted script is a port of
/// core::ReactiveSiteGraph, whose C++ tests are the reference for the coupling
/// rules between single and pair sites. The two exist separately because the
/// builder runs in process and the sampler runs in the job; they are small,
/// and the invariant they share is asserted on both sides.
struct GrapheneOxideMdmcConfig {
    /// Structure staged next to run.py — the builder's output.
    std::string inputStructure = "structure.extxyz";
    /// Lowest-energy arrangement found, which is what the run is for. Note
    /// this is NOT the last frame: a finite-temperature walk ends wherever it
    /// happens to be, and reporting that as the answer throws away the best
    /// configuration the sampler visited.
    std::string outputStructure = "mdmc_optimized.extxyz";
    /// Accepted configurations, in order, for the trajectory viewer.
    std::string trajectory = "mdmc_trajectory.extxyz";

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
    /// Few (5-20) is the intended regime. This is not a relaxation: it is
    /// enough dynamics to let the neighbours accommodate the moved group, and
    /// the cost of the whole run is cycles × mdSteps energy evaluations, so
    /// this multiplies directly into the wall clock.
    int mdStepsPerCycle = 20;

    double timestepFs = 1.0;
    /// Langevin friction, in inverse femtoseconds. The thermostat has to be
    /// strong enough to equilibrate within the short burst above, which is why
    /// this is larger than a production-MD value.
    double frictionPerFs = 0.02;

    /// Constant pressure instead of constant volume. Periodic sheets only —
    /// a flake has no cell to relax, and asking for NPT on one is meaningless
    /// rather than merely wasteful.
    bool constantPressure = false;
    double pressureGpa = 0.0;

    /// Attach basal groups to either face. Off restricts every move to the
    /// face the group already sat on, which is what a single-sided model wants.
    bool bothFaces = true;

    /// Deterministic seed for the move sequence and the thermostat.
    std::uint32_t seed = 0;

    /// Write an accepted configuration to the trajectory every this many
    /// ACCEPTED moves. Zero writes none.
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
    int viewportEveryCycles = 5;

    /// Also stream the geometry the DYNAMICS produced, before the move is
    /// judged.
    ///
    /// Two different things are worth watching and they answer different
    /// questions. The relaxed geometry after the MD burst shows the atoms
    /// MOVING - whether the structure is holding together. The accepted
    /// configuration after the Metropolis test shows the groups HOPPING, which
    /// is the discrete process the run exists to sample. The second is always
    /// streamed (subject to the throttle); the first is opt-in because it is
    /// much the more expensive, and because a rejected move's geometry is not
    /// a state of the ensemble at all.
    bool streamMdFrames = false;
};

class GrapheneOxideMdmcScriptGenerator {
public:
    static std::string generate(const GrapheneOxideMdmcConfig& config);
};

} // namespace calango::core
