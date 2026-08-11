#pragma once

#include "dft/DftTypes.hpp"
#include "dft/RadialGrid.hpp"

#include <string>
#include <vector>

namespace calango::dft {

/// One occupied (or partly occupied) shell of a spherical free atom.
struct AtomicShell {
    int principal = 1; ///< n
    int l = 0;
    /// Electrons in the shell, spherically averaged. Silicon's 3p² is carried
    /// as 2.0 in a shell of capacity 6, not as two of the three p orbitals:
    /// the atom solved here is the SPHERICAL one, whose density has no angular
    /// dependence by construction. That is the reference configuration a
    /// numerical basis is generated from, and it is deliberately not the
    /// symmetry-broken ³P ground state — a basis built from a particular
    /// m-occupation would be biased towards one orientation of a bond.
    double occupation = 0.0;
    double eigenvalue = 0.0;    ///< hartree
    std::vector<double> u;      ///< u(r) = r·R(r), ∫u² dr = 1
    std::string label;          ///< "3p"
};

/// The self-consistent spherical free atom.
struct AtomicResult {
    Outcome outcome;
    std::vector<AtomicShell> shells;
    /// ρ(r) in electrons per bohr³ (NOT 4πr²ρ), on the solver's grid.
    std::vector<double> density;
    /// The confining barrier that was added to the potential, in hartree, or
    /// all zeros for a free atom. Returned separately because a caller needs
    /// BOTH: the electronic potential alone is what the next SCF iteration
    /// mixes, and the sum is what the kinetic-energy identity −½∇²φ =
    /// (ε − v)φ requires.
    std::vector<double> confiningPotential;
    /// The ELECTRONIC part of the effective potential, v_H + v_xc, in hartree.
    /// The nuclear −Z/r is kept out of it on purpose: it is analytic, it is
    /// the only divergent piece, and adding it to an array means the array
    /// has an infinity in element zero.
    std::vector<double> electronicPotential;
    double totalEnergy = 0.0;       ///< hartree
    double kinetic = 0.0;
    double externalEnergy = 0.0;    ///< ∫ρ(−Z/r)
    double hartreeEnergy = 0.0;     ///< ½∫ρ v_H
    double xcEnergy = 0.0;
    double bandStructure = 0.0;     ///< Σ f_nl ε_nl
    int iterations = 0;
    double finalResidual = 0.0;
};

/// All-electron LDA solver for a single spherical atom.
///
/// This is the foundation the rest of the engine stands on, for two reasons
/// that are worth separating:
///
///   1. It GENERATES THE BASIS. A numerical atomic orbital is, by definition,
///      a solution of this problem — so the quality of every later number is
///      bounded by this solver's accuracy, and nothing else in the engine can
///      compensate for an error here.
///   2. It is the only part of an all-electron code that can be checked
///      against an external reference to many digits. A free atom has no
///      geometry, no k-points and no basis-set error: two correct codes must
///      agree on its total energy to the accuracy of their radial meshes.
///      `dft_atomic_solver` does exactly that against GPAW's own all-electron
///      atomic solver.
///
/// Method. The radial Kohn-Sham equation in u(r) = r·R(r),
///
///     u'' = [ l(l+1)/r² + 2(v_eff(r) − ε) ] u,   v_eff = −Z/r + v_H + v_xc
///
/// is integrated as a first-order pair in the MESH INDEX rather than in r,
/// where the steps are uniform and classical Runge-Kutta keeps its order. The
/// eigenvalue is found by bisection on the NODE COUNT of the outward solution:
/// the number of nodes is a non-decreasing step function of ε that jumps by
/// one at each eigenvalue, so bracketing the jump brackets the eigenvalue with
/// no derivative, no matching condition and no way to converge to the wrong
/// state. The final wavefunction is then rebuilt by joining an outward
/// solution to an inward one at the classical turning point, because outward
/// integration alone is dominated by the growing solution beyond it.
///
/// Hartree atomic units throughout.
class AtomicSolver {
public:
    AtomicSolver(RadialGrid grid, Parameters parameters);

    /// Solve the neutral atom of atomic number `z` in its aufbau
    /// configuration.
    AtomicResult solve(int z) const;

    /// Solve an explicitly given configuration — used for charged or excited
    /// reference states, and by the basis generator when a confining
    /// potential is applied.
    ///
    /// `confinementRadiusBohr` > 0 is where every orbital reaches exactly
    /// zero; `confinementWidthBohr` is how far back from it the smooth barrier
    /// begins. A zero width degenerates to a hard wall, which is retained only
    /// so the difference can be measured — see Parameters::confinementWidthA
    /// for why a wall is wrong.
    AtomicResult solveConfiguration(int z, std::vector<AtomicShell> occupations,
                                    double confinementRadiusBohr = 0.0,
                                    double confinementWidthBohr = 0.0) const;

    /// The confining barrier on this solver's mesh: zero inside
    /// `radiusBohr − widthBohr`, rising smoothly, capped where the radial
    /// integrator would otherwise go stiff.
    std::vector<double> confiningPotential(double radiusBohr,
                                           double widthBohr) const;

    /// Solve the radial equation for ONE (n, l) in a given electronic
    /// potential, without any self-consistency.
    ///
    /// Public because it is the only piece of this class with an exact answer
    /// to check against: pass an electronic potential of zero and the
    /// eigenvalues are the hydrogenic −Z²/2n², to whatever accuracy the mesh
    /// and the integrator have. Every other test of an atomic solver compares
    /// it against another program; this one compares it against arithmetic.
    ///
    /// `shell.principal` and `shell.l` select the state; `shell.eigenvalue`
    /// and `shell.u` are filled in.
    Outcome solveOrbital(double z,
                         const std::vector<double>& electronicPotential,
                         double confinementRadiusBohr,
                         AtomicShell& shell) const;

    /// The aufbau ground-state configuration of a neutral atom, spherically
    /// averaged. Madelung (n+l, then n) order, which reproduces the periodic
    /// table for the elements this engine claims to support.
    static std::vector<AtomicShell> groundStateConfiguration(int z);

    /// "1s", "3p", … for a shell.
    static std::string shellLabel(int principal, int l);

    const RadialGrid& grid() const { return grid_; }

private:
    RadialGrid grid_;
    Parameters parameters_;
};

} // namespace calango::dft
