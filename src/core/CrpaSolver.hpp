#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include <array>
#include <complex>
#include <string>
#include <vector>

namespace calango::core {

/// Constrained RPA in a localized Wannier basis.
///
/// Computes the effective on-site interaction of a correlated subspace by
/// screening the bare Coulomb interaction with every RPA process EXCEPT those
/// that live entirely inside that subspace — the screening the subspace would
/// otherwise do to itself, which a subsequent many-body solver is going to
/// treat explicitly and must not be handed twice.
///
///     V              bare Coulomb in the Wannier basis
///     P_rest(ω)      RPA polarizability with d→d transitions removed
///     W(ω)           [1 − V P_rest(ω)]⁻¹ V
///     U, J           from W(ω = 0)
///
/// CALCULATOR-AGNOSTIC BY CONSTRUCTION. The only input is a Wannier
/// description: H(R) in the Wannier basis, the orbital centres and spreads,
/// and which orbitals span the correlated subspace. No plane-wave
/// coefficients, no pseudopotentials, no code-specific file formats. Anything
/// that can emit a wannier90 `_hr.dat` and a `.wout` can drive this.
///
/// THE PRICE OF THAT CHOICE, STATED PLAINLY. Centres and spreads do not
/// determine a Wannier function, so they cannot determine the exact Coulomb
/// matrix element ⟨w_i w_j|1/r|w_k w_l⟩ either — that needs the functions
/// themselves on a real-space grid. This solver therefore models each Wannier
/// orbital as an isotropic Gaussian whose second moment matches the reported
/// spread, which makes the bare interaction a closed form (see bareCoulomb())
/// rather than a quadrature over data it does not have. The screening,
/// constraint bookkeeping and matrix inversion downstream are exact; the bare
/// V is a model, and it is the leading approximation in the whole chain.
///
/// UNITS. Energies in eV, lengths in Å throughout the public interface.
class CrpaSolver {
public:
    /// One Wannier orbital, as wannier90 reports it.
    struct Orbital {
        std::string label;
        /// Wannier centre in Å, Cartesian.
        std::array<double, 3> centre{0.0, 0.0, 0.0};
        /// Wannier spread Ω = ⟨r²⟩ − ⟨r⟩² in Å². Converted internally to the
        /// per-dimension Gaussian width s = sqrt(Ω/3).
        double spread = 1.0;
        /// True when this orbital spans the correlated subspace whose
        /// interaction is being computed (the "d" orbitals of cRPA).
        bool correlated = false;
        /// Angular momentum of the shell: 0 = s, 1 = p, 2 = d, 3 = f.
        ///
        /// Optional, and it buys exactly one thing: Hund's J. The
        /// density-density interaction alone cannot produce J for a degenerate
        /// shell (see Interaction::j), because that is a statement about the
        /// ANGULAR structure of the orbitals and a spherical Gaussian has
        /// none. Supplying l lets the solver apply the exact Slater-Condon
        /// angular algebra on top of the Gaussian RADIAL density, which is the
        /// same decomposition atomic-structure codes use.
        ///
        /// Left at 0 the solver reports J from the Kanamori route instead.
        int angularL = 0;
    };

    /// A real-space Hamiltonian block H(R), as in a wannier90 `_hr.dat`.
    struct HoppingBlock {
        /// Lattice vector R in fractional (integer) units.
        std::array<int, 3> lattice{0, 0, 0};
        /// Row-major n×n block of H(R) in eV.
        std::vector<double> matrix;
    };

    struct Model {
        std::vector<Orbital> orbitals;
        std::vector<HoppingBlock> hoppings;
        /// Cartesian lattice vectors in Å, rows a1, a2, a3.
        std::array<std::array<double, 3>, 3> cell{
            {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
        /// Electron count per cell that fixes the Fermi level.
        double electrons = 0.0;
    };

    struct Options {
        /// Monkhorst-Pack mesh the polarizability is summed over.
        std::array<int, 3> kmesh{4, 4, 4};
        /// Adiabatic broadening δ in eV in the energy denominators.
        double broadening = 0.05;
        /// Transitions whose energy exceeds this (eV) are dropped from
        /// P_rest. The "energy cutoff for screening bands" — a convergence
        /// parameter, not a physical one, and quoted with every U.
        double screeningCutoff = 30.0;
        /// Occupation temperature in eV for the Fermi factors. Small but
        /// non-zero: a step function makes the transition set discontinuous in
        /// the k-mesh, and U then jumps as the mesh is refined.
        double smearing = 0.05;
    };

    /// The interaction, as the many-body solver downstream wants it.
    struct Interaction {
        /// Averaged intra-orbital U = ⟨W_mm⟩ over the correlated orbitals, eV.
        double u = 0.0;
        /// Averaged inter-orbital U' = ⟨W_mm'⟩, m ≠ m', eV.
        double uPrime = 0.0;
        /// Hund's J from the Kanamori relation J = (U − U')/2, eV.
        ///
        /// EXACTLY ZERO for a perfectly degenerate shell in this model, and
        /// that is the correct limit rather than a defect: orbitals sharing a
        /// centre and a spread have identical spherical charge densities, so
        /// every density-density element is the same and U' = U. A finite J
        /// appears as soon as the correlated orbitals are inequivalent —
        /// different spreads (t2g vs eg), different centres (a dimer), or
        /// different sites.
        ///
        /// Recovering J for a genuinely degenerate shell needs the ANGULAR
        /// structure the spherical-Gaussian model discards: the Slater
        /// integrals F² and F⁴, from which J = (F² + F⁴)/14 for d electrons.
        /// That requires real-space Wannier functions, which centres and
        /// spreads do not determine — the same limitation stated on the class.
        double j = 0.0;
        /// The full screened matrix restricted to the correlated subspace.
        std::vector<std::vector<double>> screenedMatrix;
        /// The same for the bare interaction, so the screening ratio U/U_bare
        /// is reportable — the number that says whether the constraint did
        /// anything at all.
        double uBare = 0.0;

        /// Hund's J from the Slater-Condon route, eV. Non-zero only when the
        /// correlated orbitals declare an angular momentum:
        ///     l = 1 (p):  J = F²/5
        ///     l = 2 (d):  J = (F² + F⁴)/14
        ///     l = 3 (f):  J = (286 F² + 195 F⁴ + 250 F⁶)/6435
        /// These are the standard atomic relations; the F^k below are the
        /// radial integrals of the Gaussian model.
        double jSlater = 0.0;
        /// Hund's J from the Kanamori route, (U − U')/2. Zero for a degenerate
        /// shell by construction.
        double jKanamori = 0.0;
        /// Radial Slater integrals F⁰, F², F⁴, F⁶ of the correlated shell, eV.
        ///
        /// F⁰ here is the BARE monopole and must agree with uBare — two
        /// independent routes to the same number, which the tests exploit.
        /// The higher multipoles are reported UNSCREENED: W is a two-index
        /// density-density object and carries no k > 0 channel to screen them
        /// with. That is the usual approximation (higher multipoles are
        /// screened far less than F⁰), and it is an approximation, not an
        /// identity.
        std::array<double, 4> slaterF{0.0, 0.0, 0.0, 0.0};
        /// Which route `j` was taken from, for the report.
        bool jFromSlater = false;
    };

    CrpaSolver(Model model, Options options);
    explicit CrpaSolver(Model model);

    /// Bare Coulomb matrix V_ij in the Wannier basis, eV.
    ///
    /// Two normalized isotropic Gaussian charge densities of per-dimension
    /// widths s_i, s_j separated by R interact with the closed-form energy
    ///
    ///     V(R) = erf( R / sqrt(2(s_i² + s_j²)) ) / R,
    ///
    /// in Hartree atomic units, whose R → 0 limit is sqrt(2/(π(s_i²+s_j²))).
    /// Both limits are asserted in the tests.
    std::vector<std::vector<double>> bareCoulomb() const;

    /// Constrained RPA polarizability P_rest(ω) at one real frequency, eV⁻¹.
    ///
    /// Transitions with BOTH the occupied and the empty state inside the
    /// correlated subspace are excluded — that exclusion is the whole content
    /// of the "constrained" in cRPA, and removing it (see
    /// `includeCorrelatedTransitions`) must reproduce ordinary RPA.
    std::vector<std::vector<std::complex<double>>> polarizability(
        double omega, bool includeCorrelatedTransitions = false) const;

    /// W(ω) = [1 − V P(ω)]⁻¹ V, eV.
    std::vector<std::vector<std::complex<double>>> screenedCoulomb(
        double omega, bool includeCorrelatedTransitions = false) const;

    /// U, U' and J from the static limit W(ω = 0).
    Interaction staticInteraction() const;

    /// Radial Slater integrals F⁰, F², F⁴, F⁶ (eV) of a Gaussian shell of
    /// spread Ω (Å²), by quadrature of
    ///
    ///     F^k = ∫∫ P(r₁) P(r₂) r_<^k / r_>^{k+1} dr₁ dr₂,
    ///
    /// with P the normalised radial density of the isotropic Gaussian.
    ///
    /// Static because it depends on nothing but the spread — which also makes
    /// the scaling invariant testable: every F^k scales as 1/s, so the ratios
    /// F^k/F⁰ are pure numbers independent of the spread.
    static std::array<double, 4> slaterIntegrals(double spread,
                                                 int samples = 4001);

    /// Indices of the orbitals flagged correlated.
    std::vector<std::size_t> correlatedIndices() const;

    /// H(k) assembled from the H(R) blocks, for diagnostics and tests.
    std::vector<std::vector<std::complex<double>>> hamiltonianAt(
        const std::array<double, 3>& kFractional) const;

    const Model& model() const { return model_; }

private:
    /// Eigenvalues and eigenvectors of H(k). Columns of `vectors` are states.
    struct Bands {
        std::vector<double> energies;
        std::vector<std::vector<std::complex<double>>> vectors;
    };
    Bands diagonalize(const std::array<double, 3>& kFractional) const;
    /// Fermi level from the requested electron count on the current k-mesh.
    double fermiLevel() const;
    std::vector<std::array<double, 3>> kPoints() const;

    Model model_;
    Options options_;
};

} // namespace calango::core
