#pragma once

#include "dft/DftTypes.hpp"

#include <cstddef>
#include <vector>

namespace calango::dft {

/// Exchange-correlation energy density and potential, evaluated pointwise.
///
/// The interface is deliberately the narrow one every local functional shares:
/// in goes a density at a point, out come ε_xc (energy per electron) and v_xc
/// (the functional derivative that enters the Hamiltonian). That is the whole
/// contract for LDA, and it is the contract a libxc binding would implement
/// unchanged if one is ever added — the engine calls `Lda::evaluate`, not a
/// parameterisation.
///
/// Why the two quantities are separate and both returned: the energy is
/// ∫ρ ε_xc while the Hamiltonian needs v_xc = d(ρ ε_xc)/dρ, and they differ.
/// Computing the energy as ∫ρ v_xc — a mistake that survives an SCF because
/// the density still converges — overestimates the exchange energy by exactly
/// a third.
///
/// Hartree atomic units throughout: density in electrons per bohr³, energies
/// in hartree.
struct XcResult {
    double energyPerElectron = 0.0; ///< ε_xc(ρ), hartree
    double potential = 0.0;         ///< v_xc(ρ) = d(ρ ε_xc)/dρ, hartree
};

/// Local-density approximation.
///
/// Exchange is Dirac-Slater, which is not a parameterisation of anything — it
/// is the exact exchange energy of the uniform electron gas,
/// ε_x = −(3/4)(3ρ/π)^{1/3}. Correlation is a fit to the Ceperley-Alder
/// quantum Monte Carlo electron gas, and the two fits below differ only in how
/// that same data was interpolated:
///
///   * Vosko-Wilk-Nusair (VWN5) — a Padé fit in √r_s, analytic everywhere and
///     the standard reference LDA. This is the default here.
///   * Perdew-Zunger (PZ81) — separate high- and low-density forms joined at
///     r_s = 1. Cheaper, marginally discontinuous in the second derivative at
///     the join, and what much of the older literature means by "LDA".
///
/// They agree to well under a millihartree per electron over the density range
/// that matters, so the choice changes total energies in the fourth decimal
/// and structural results not at all. Both are here because reproducing a
/// published number requires using the same one it was computed with.
class Lda {
public:
    /// ε_xc and v_xc at a single density. Densities at or below zero — which
    /// a quadrature of a difference density can produce — return zeros rather
    /// than a NaN from ρ^{1/3} of a negative number.
    static XcResult evaluate(double density, XcFunctional functional);

    /// Exchange only, and correlation only. Split out because they are
    /// validated separately: exchange against its closed form, correlation
    /// against tabulated electron-gas values.
    static XcResult exchange(double density);
    static XcResult correlation(double density, XcFunctional functional);

    /// ε_xc and v_xc over a whole grid, plus ∫ρ ε_xc dr accumulated with the
    /// supplied quadrature weights. One pass, because this is called once per
    /// SCF iteration over every grid point and the density array is the
    /// largest thing the engine touches.
    ///
    /// Returns the exchange-correlation ENERGY (hartree). `potential` is
    /// resized to match `density`.
    static double evaluateGrid(const std::vector<double>& density,
                               const std::vector<double>& weights,
                               XcFunctional functional,
                               std::vector<double>& potential);

    /// True when this class implements `functional`. The GGA entry in the
    /// enum is a declared intention, not an implementation, and an engine that
    /// silently substituted LDA for it would report PBE numbers that are not
    /// PBE numbers.
    static bool supports(XcFunctional functional);
};

/// One point of a general (local or gradient-corrected) functional.
///
/// The energy is written as a density per unit VOLUME, f(ρ, σ), rather than
/// per electron, because that is the form a gradient correction has: with
/// σ = |∇ρ|² there is no natural "per electron" split. For an LDA,
/// f = ρ ε_xc and ∂f/∂σ is identically zero, so the same three numbers
/// describe both families and the assembler needs one code path.
struct XcPoint {
    double energyDensity = 0.0; ///< f(ρ, σ), hartree per bohr³
    double dfdrho = 0.0;        ///< ∂f/∂ρ
    double dfdsigma = 0.0;      ///< ∂f/∂σ, zero for a local functional
};

/// Exchange-correlation, local and gradient-corrected.
///
/// A GGA cannot be applied the way an LDA is. Its potential contains
/// ∇·(∂f/∂∇ρ), and taking a divergence numerically on an unstructured
/// multicentre grid is both awkward and noisy. The matrix element is therefore
/// integrated by parts,
///
///     ⟨φ_i|v_xc|φ_j⟩ = ∫ (∂f/∂ρ) φ_i φ_j
///                    + ∫ 2(∂f/∂σ) ∇ρ · ∇(φ_i φ_j),
///
/// which needs only ∇φ — no second derivatives, no divergence of a sampled
/// vector field, and manifestly Hermitian. The engine tabulates ∇φ alongside
/// φ for exactly this, and the same quantity is what Pulay forces will need.
class Xc {
public:
    static bool supports(XcFunctional functional);
    /// Whether ∇ρ has to be built at all. False for every LDA, which is what
    /// keeps the local path free of the memory and time a gradient costs.
    static bool needsGradients(XcFunctional functional);

    /// f, ∂f/∂ρ and ∂f/∂σ at one point. `sigma` = |∇ρ|², ignored by an LDA.
    static XcPoint evaluate(double density, double sigma,
                            XcFunctional functional);

    /// Over a whole grid. Returns ∫f dV; fills the two derivative arrays,
    /// which is what the assembler needs to build the matrix.
    static double evaluateGrid(const std::vector<double>& density,
                               const std::vector<double>& sigma,
                               const std::vector<double>& weights,
                               XcFunctional functional,
                               std::vector<double>& dfdrho,
                               std::vector<double>& dfdsigma);
};

} // namespace calango::dft
