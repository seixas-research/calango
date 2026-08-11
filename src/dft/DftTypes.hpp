#pragma once

#include <array>
#include <string>
#include <vector>

/// Calango's own density-functional-theory engine.
///
/// An ALL-ELECTRON solver on NUMERICAL ATOMIC ORBITALS: every electron is
/// treated explicitly (no pseudopotential, no frozen core) and the basis
/// functions are numerically tabulated radial functions times real spherical
/// harmonics, centred on the nuclei.
///
/// Why that pair, stated once here because it explains nearly every design
/// choice further down:
///
///   * All-electron removes the pseudopotential from the list of things a
///     result depends on. It is what makes core-level spectroscopy, hyperfine
///     parameters and heavy-element chemistry computable at all, and it means
///     a number is wrong only because the functional or the basis is, not
///     because of a transferability assumption made by whoever generated the
///     potential.
///   * Numerical atomic orbitals are tabulated on a radial grid rather than
///     expanded in analytic primitives, so a basis function can be the exact
///     numerical solution of the atomic problem. That makes the free atom
///     exact at the first basis level and gives every subsequent level a
///     physical meaning, and it makes the basis strictly local — a function
///     is exactly zero beyond its confinement radius, which is what turns the
///     Hamiltonian and overlap matrices sparse and the cost near-linear.
///
/// The price is paid in the integrals: nothing is analytic. Every matrix
/// element is a numerical quadrature over a real-space grid, so the grid IS
/// the accuracy, and most of the engineering lives in building grids that are
/// dense where the wavefunction has structure (at the nucleus) and sparse
/// where it does not.
///
/// STATUS: the ground-state pipeline is implemented end to end — the radial
/// atomic solver that generates the basis, the multicentre grid, the
/// Hamiltonian assembly, the generalised eigenproblem and the
/// self-consistency loop. What each piece is validated against is listed on
/// CalangoDFTEngine, and what is still missing is listed by
/// `CalangoDFTEngine::unimplementedSteps()` rather than in a comment, so a
/// caller can show it. Nothing here returns a number it did not compute: a
/// step that cannot run reports `NotImplemented` and leaves its output
/// untouched.
namespace calango::dft {

/// How far a call got. Every entry point returns one of these rather than
/// throwing or returning a default-constructed result, so "not implemented"
/// and "diverged" are distinguishable by a caller and by a test.
enum class Status {
    Ok,
    /// The requested step exists as an interface but has no implementation
    /// yet. Never returned alongside usable numbers.
    NotImplemented,
    /// The input was rejected: an unsupported element, a malformed grid, a
    /// basis that does not span the electrons requested.
    InvalidInput,
    /// The SCF ran but did not reach the requested tolerance.
    NotConverged,
    /// A numerical failure inside a step that was expected to succeed.
    NumericalFailure,
};

const char* toString(Status status);

/// A result plus why it is (or is not) a result.
struct Outcome {
    Status status = Status::NotImplemented;
    /// Human-readable detail. Always set when `status != Ok`, and written for
    /// somebody reading a log rather than for a switch statement.
    std::string message;

    bool ok() const { return status == Status::Ok; }
    static Outcome success() { return {Status::Ok, {}}; }
    static Outcome notImplemented(std::string what)
    {
        return {Status::NotImplemented, std::move(what)};
    }
    static Outcome invalid(std::string why)
    {
        return {Status::InvalidInput, std::move(why)};
    }
};

/// Exchange-correlation treatment. Deliberately a small set: each one added
/// has to be validated against reference numbers, and an unvalidated
/// functional in a menu is a wrong answer waiting to be selected.
enum class XcFunctional {
    /// Local density approximation, Perdew-Zunger parameterisation of the
    /// Ceperley-Alder electron gas.
    LdaPz,
    /// Perdew-Burke-Ernzerhof generalised gradient approximation.
    GgaPbe,
    /// Local density approximation, Vosko-Wilk-Nusair (formula V) fit to the
    /// same electron-gas data. Analytic everywhere in r_s, where PZ81
    /// switches parameterisation at r_s = 1, and what "LDA" means in most of
    /// the older reference tables.
    LdaVwn,
    /// Local density approximation, Perdew-Wang 1992 correlation. THE DEFAULT,
    /// and the one to reach for when comparing against another code: it is
    /// what GPAW, VASP and Quantum ESPRESSO all mean by plain "LDA", so a
    /// number computed with it can be checked against theirs to every digit
    /// the grids support rather than to the ~10 meV/atom the different
    /// electron-gas fits differ by.
    LdaPw,
};

const char* toString(XcFunctional functional);

/// What the caller asks the engine to do.
struct Parameters {
    XcFunctional xc = XcFunctional::LdaPw;

    // -- Self-consistency ---------------------------------------------------
    /// Convergence threshold on the total energy between iterations (eV).
    double energyToleranceEv = 1.0e-6;
    /// Convergence threshold on the integrated absolute density change
    /// (electrons). Both must be met: energy alone can look converged while
    /// the density is still moving, because the energy is variational and
    /// therefore second-order in the density error.
    double densityToleranceElectrons = 1.0e-5;
    int maxIterations = 100;

    /// Linear-mixing fraction of the new density. Also the fallback when the
    /// Pulay history is too short to extrapolate.
    double mixingFraction = 0.3;
    /// How many previous iterations the Pulay/DIIS extrapolation keeps. 0
    /// disables it, leaving plain linear mixing.
    int mixingHistory = 8;

    // -- Integration grids --------------------------------------------------
    /// Radial shells per atom in the integration grid, BEFORE the ones beyond
    /// the outer radius are dropped. An all-electron density needs many of
    /// them: the 1s shell of a third-row element lives inside 0.1 bohr and the
    /// valence tail runs to ten.
    int radialShells = 120;
    /// The spherical-harmonic degree the angular rule must integrate exactly.
    /// The smallest tabulated Lebedev rule that reaches it is used, or a
    /// generated Gauss-Legendre product rule above them.
    int angularPoints = 11;

    /// Fermi-Dirac occupation width (eV).
    ///
    /// Not a convergence aid bolted on for metals — without it this loop does
    /// not converge at all for an open shell. Silicon's 3p² puts two electrons
    /// into a THREEFOLD DEGENERATE level, and filling states one at a time
    /// hands both to whichever of the three the eigensolver happened to return
    /// first. The eigenvectors of a degenerate subspace are arbitrary, so that
    /// choice rotates every iteration: the energy sits still to eight digits
    /// while the density swings by more than an electron, forever. A Fermi
    /// distribution gives equal occupation to equal eigenvalues by
    /// construction, which is also what "the spherical atom" means.
    ///
    /// Small on purpose: it has to separate genuinely different levels, and
    /// the entropy it introduces is not subtracted from the reported energy.
    double smearingWidthEv = 0.02;

    // -- Brillouin zone -----------------------------------------------------
    /// Gamma-centred Monkhorst-Pack divisions. Ignored for a finite system,
    /// where the only k-point is Gamma and it is not a choice.
    std::array<int, 3> kGrid{{4, 4, 4}};

    /// How far to fold the k-mesh with symmetry. 0 none, 1 time reversal,
    /// 2 the full crystal point group. See KPointGrid for why these are
    /// separate rather than a single on/off.
    int kSymmetry = 1;

    // -- Energy derivatives -------------------------------------------------
    /// Compute the analytic force decomposition alongside the energy.
    ///
    /// Off by default: it costs an extra pass over the grid and the tabulation
    /// of ∇φ, and the number a caller should act on is the finite-difference
    /// force, which is exact. The analytic terms exist to be MEASURED against
    /// that one — see ForceCalculator.
    bool computeForces = false;

    // -- Basis --------------------------------------------------------------
    /// Radius (Å) beyond which every basis function is exactly zero.
    ///
    /// This is what makes the matrices sparse, and it is an approximation with
    /// a knob: too small and the tails that bind the system are cut off, too
    /// large and the sparsity — the whole reason for a local basis — is lost.
    /// 3 Å is 5.7 bohr, which reaches past the second-neighbour shell of a
    /// typical solid.
    double confinementRadiusA = 3.0;

    /// Width (Å) of the smooth barrier that damps each orbital to zero, ending
    /// at `confinementRadiusA`. Confinement begins at the difference of the
    /// two.
    ///
    /// NOT a refinement of a hard wall — a correction to one. Solving the atom
    /// in a box gives u(r_cut) = 0 with u′(r_cut) ≠ 0, so φ is continuous at
    /// the cutoff sphere while ∇φ jumps, and ∇²φ therefore carries a DELTA
    /// FUNCTION on that sphere. The engine stores −½∇²φ per basis function as
    /// (ε − v_at)·u, which is built from the smooth part of u″ alone and
    /// misses that delta entirely.
    ///
    /// What that costs is invisible in the obvious places and severe in one:
    /// the diagonal is untouched, because the delta is weighted by
    /// u(r_cut) = 0, so every self-consistency check and every
    /// kinetic-energy identity still passes. Every INTER-ATOMIC element is
    /// short by a surface term, because a neighbour's orbital is not zero on
    /// this atom's cutoff sphere. Hence: isolated atoms exact, two atoms 14 Å
    /// apart exact, and the error growing with coordination and with the
    /// number of cutoff spheres in the basis.
    ///
    /// A finite barrier folded into the potential the orbital is SOLVED IN
    /// removes the delta rather than approximating it: v_at stays finite
    /// everywhere, so −½∇²φ = (ε − v_at)φ holds at every radius, and u reaches
    /// the cutoff with both it and its derivative already damped to nothing.
    ///
    /// The barrier is the published form of Junquera, Paz, Sánchez-Portal and
    /// Artacho, Phys. Rev. B 64, 235111 (2001).
    double confinementWidthA = 0.8;

    /// How many basis tiers to generate: 1 minimal, 2 double-zeta plus
    /// polarisation, 3 triple-zeta plus double polarisation.
    ///
    /// Tier 2 is the default: 1 is not a defensible production setting.
    ///
    /// This knob was added to reduce silicon's cohesive-energy overbinding and
    /// at first appeared to do the opposite, which turned out to be a bug in
    /// the CONFINEMENT rather than anything about the basis — see
    /// `confinementWidthA`. With a hard wall each added function brought its
    /// own cutoff sphere and its own missing surface term, so enlarging the
    /// basis made the crystal worse and worse. With the barrier, measured on
    /// silicon at a fixed 2x2x2 mesh:
    ///
    ///                   hard wall      smooth barrier
    ///     tier 1          7.11              5.68     eV/atom
    ///     tier 2         10.77              6.26
    ///     tier 3         66.56              6.41
    ///
    /// against roughly 5.3 for converged LDA. The barrier column is a proper
    /// convergence sequence — increments of +0.57 then +0.15 — where the wall
    /// column diverges. What remains above the reference is accounted for by
    /// the spherical non-spin-polarised atomic reference (worth about 0.6 eV
    /// for silicon), residual confinement, and a k-mesh that is still not
    /// converged.
    ///
    /// The cost is the square of the basis size, so tier 2 is about four times
    /// tier 1. Tests that care more about runtime than about basis quality set
    /// this explicitly.
    int basisTiers = 2;
};

/// One chemical species the engine must be able to treat.
struct Species {
    int atomicNumber = 0;
    /// Free-atom electron count. Equal to `atomicNumber` for a neutral atom;
    /// carried separately because the basis is generated from a possibly
    /// charged reference configuration.
    double referenceElectrons = 0.0;
};

/// What a finished calculation produced.
struct EnergyBreakdown {
    double total = 0.0;        ///< eV
    double kinetic = 0.0;      ///< eV
    double electrostatic = 0.0;///< eV, electron-nuclear + Hartree + nuclear
    double exchangeCorrelation = 0.0; ///< eV
    /// Sum of the occupied eigenvalues (eV). Not an energy contribution — it
    /// is reported because the double-counting corrections are checked
    /// against it, and a mismatch there is the usual first sign of a broken
    /// Hamiltonian.
    double bandStructure = 0.0;
};

} // namespace calango::dft
