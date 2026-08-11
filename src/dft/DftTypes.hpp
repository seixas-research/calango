#pragma once

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
/// STATUS: this module is a scaffold. The pieces that are self-contained and
/// cheap to verify are implemented and tested — the radial mesh and its
/// quadrature, the radial Poisson solve, the density mixing. The pieces that
/// need the full machinery (basis generation, matrix assembly, the SCF loop)
/// declare their interfaces and report `NotImplemented` rather than returning
/// numbers nobody computed. An engine that silently produces a plausible
/// energy is worse than one that says it cannot yet.
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
};

const char* toString(XcFunctional functional);

/// What the caller asks the engine to do.
struct Parameters {
    XcFunctional xc = XcFunctional::LdaPz;

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
    /// Radial shells per atom in the integration grid.
    int radialShells = 60;
    /// Angular (Lebedev) points per radial shell.
    int angularPoints = 194;

    // -- Basis --------------------------------------------------------------
    /// Radius (Å) beyond which every basis function is exactly zero.
    ///
    /// This is what makes the matrices sparse, and it is an approximation with
    /// a knob: too small and the tails that bind the system are cut off, too
    /// large and the sparsity — the whole reason for a local basis — is lost.
    double confinementRadiusA = 6.0;
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
