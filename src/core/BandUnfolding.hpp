#pragma once

#include "core/UnitCell.hpp"
#include "core/Vec3.hpp"

#include <array>
#include <string>
#include <vector>

namespace calango::core {

/// Effective band structure by the Popescu-Zunger unfolding scheme
/// (Phys. Rev. B 85, 085201 (2012)).
///
/// A supercell calculation folds the primitive Brillouin zone onto a smaller
/// one, so a defect/alloy supercell's bands are an uninterpretable tangle.
/// Unfolding projects each supercell eigenstate |Km> back onto the primitive
/// Bloch basis at a chosen primitive wavevector k, giving the spectral weight
///
///     P_Km(k) = sum_g |<Km | k + g>|^2
///
/// summed over primitive reciprocal vectors g. The spectral function is then
///
///     A(k, E) = sum_m P_Km(k) * delta(E - E_m)
///
/// with the delta broadened into a Gaussian for plotting. P = 1 means the
/// state is a pure primitive Bloch state at k (the pristine limit); P near 0
/// means it has no primitive character there and should not be drawn.
///
/// This header owns the *geometry and bookkeeping* of the scheme — the
/// supercell/primitive relationship, which K each k folds onto, and the
/// broadening of weighted eigenvalues into A(k, E). The projection weights
/// themselves come from the plane-wave coefficients of a DFT run and are
/// computed in the generated Python (GPAW/QE/SIESTA expose them); this side
/// consumes them.

/// Integer transformation relating the two cells: supercell = M · primitive,
/// stored row-major (m[i] is row i). |det M| is the number of primitive cells
/// contained in the supercell.
struct SupercellMatrix {
    int m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    int determinant() const;
    /// True when M is a usable (non-singular, integer) transformation.
    bool valid() const { return determinant() != 0; }
};

/// Best-fit integer M for a supercell/primitive pair: M = S · P⁻¹, rounded.
/// `residual` receives the largest absolute deviation from an integer before
/// rounding — a value much above ~1e-3 means the two cells are not actually
/// commensurate and the unfolding would be meaningless.
SupercellMatrix deduceSupercellMatrix(const UnitCell& primitive,
                                      const UnitCell& supercell,
                                      double* residual = nullptr);

/// Fold a primitive-cell wavevector k (fractional, primitive reciprocal
/// basis) onto the supercell Brillouin zone: K = k · Mᵀ reduced into
/// [-0.5, 0.5). Returns the supercell fractional coordinates.
///
/// This is the map the projection needs: the supercell calculation only has
/// eigenstates at K, and every primitive k that folds onto the same K shares
/// them — the weights are what tell those k apart.
Vec3 foldToSupercell(const Vec3& kPrimitive, const SupercellMatrix& matrix);

/// One supercell eigenstate contributing to the effective band structure.
struct UnfoldedState {
    double energy = 0.0; ///< eV, absolute
    double weight = 0.0; ///< Popescu-Zunger spectral weight P_Km(k), in [0, 1]
};

/// One column of the A(k, E) heatmap: every state at a single k-path point.
struct UnfoldedColumn {
    double pathCoordinate = 0.0; ///< cumulative distance along the k-path
    std::vector<UnfoldedState> states;
};

/// Broadening parameters for turning weighted delta functions into A(k, E).
struct SpectralFunctionOptions {
    double energyMin = -10.0; ///< eV, relative to the reference used by caller
    double energyMax = 10.0;
    int energyBins = 400;
    /// Gaussian sigma (eV). Must exceed roughly the eigenvalue spacing or the
    /// map degenerates into isolated dots.
    double sigma = 0.05;
    /// States below this weight are skipped entirely. Unfolding produces a
    /// long tail of ~1e-6 weights that add nothing but cost.
    double weightThreshold = 1e-4;
};

/// Sampled spectral function: `intensity[column][bin]`, plus the axes.
struct SpectralFunction {
    std::vector<double> pathCoordinates; ///< one per column
    std::vector<double> energies;        ///< bin centers (eV)
    std::vector<std::vector<double>> intensity;
    double maxIntensity = 0.0;

    bool valid() const { return !intensity.empty() && !energies.empty(); }
};

/// Broaden weighted eigenvalues into A(k, E) on a regular energy grid.
///
/// Each state contributes weight · exp(-(E - E_m)² / 2σ²) / (σ·sqrt(2π)), so
/// the integral over E of every state's contribution is its spectral weight
/// and columns stay comparable regardless of the bin count.
SpectralFunction computeSpectralFunction(
    const std::vector<UnfoldedColumn>& columns,
    const SpectralFunctionOptions& options);

} // namespace calango::core
