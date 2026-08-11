#pragma once

#include <cstddef>
#include <vector>

namespace calango::dft {

/// A logarithmic radial mesh, its quadrature weights, and the operations that
/// only need one radial coordinate.
///
/// Every all-electron quantity is dominated by what happens within a fraction
/// of a bohr of the nucleus: the wavefunction has a cusp there, the 1s density
/// of a heavy element is concentrated in a region a thousand times smaller
/// than the bond length, and the potential diverges. A uniform mesh fine
/// enough for that region would need millions of points to also reach the
/// valence tail. So the mesh is
///
///     r(i) = a (e^{b i} − 1),      i = 0 … n−1
///
/// which starts exactly AT the nucleus (r(0) = 0, so the cusp sits on a grid
/// point rather than between two), spaces points as ~a·b·e^{bi} — dense at
/// small r, coarse at large r — and turns every radial integral into a
/// uniformly spaced one in `i`, where Simpson's rule applies directly:
///
///     ∫ f(r) dr = ∫ f(r(i)) (dr/di) di,     dr/di = a b e^{b i}.
///
/// `a` sets how fine the innermost spacing is and `b` follows from wanting
/// r(n−1) = rMax. Both are exposed because the right values depend on the
/// nuclear charge, and a mesh chosen for carbon is wrong for gold.
///
/// All lengths are BOHR and all energies HARTREE inside this class. The
/// conversion to Å/eV happens at the engine boundary, once, so the physics
/// here reads the way the literature writes it.
class RadialGrid {
public:
    /// Build a mesh of `points` points reaching `outerRadiusBohr`, with the
    /// innermost spacing controlled by `innerScaleBohr` (the `a` above).
    ///
    /// The defaults are a reasonable light-element mesh; heavy elements want a
    /// smaller `innerScaleBohr`, because the 1s shell of a Z = 79 atom lives
    /// around r ≈ 1/Z bohr and a mesh that does not resolve it will report a
    /// total energy wrong in the second digit while converging happily.
    RadialGrid(std::size_t points = 400, double outerRadiusBohr = 30.0,
               double innerScaleBohr = 1.0e-4);

    std::size_t size() const { return r_.size(); }
    bool empty() const { return r_.empty(); }

    /// Radii, ascending, r[0] == 0.
    const std::vector<double>& r() const { return r_; }
    /// dr/di at each point — the Jacobian that turns an `i`-sum into an
    /// `r`-integral.
    const std::vector<double>& drdi() const { return drdi_; }
    /// Simpson weights already multiplied by dr/di, so
    /// ∫f(r)dr == Σ_i weight[i] · f(r[i]).
    const std::vector<double>& weights() const { return weights_; }

    double outerRadius() const { return r_.empty() ? 0.0 : r_.back(); }

    /// ∫ f(r) dr over the mesh. `values` must be f sampled at r().
    ///
    /// Returns 0 for a size mismatch rather than reading out of bounds: this
    /// is called from inside loops where an exception would be the wrong
    /// currency, and a silent zero is caught by the callers' own norm checks.
    double integrate(const std::vector<double>& values) const;

    /// ∫ f(r) r² dr — the radial part of a spherical volume integral, which
    /// is what nearly every physical quantity here actually needs.
    double integrateSpherical(const std::vector<double>& values) const;

    /// f(r) at an arbitrary radius by cubic interpolation in `i`.
    ///
    /// Interpolating in the index rather than in r is the point: the mesh is
    /// uniform in i, so a polynomial there is well conditioned everywhere,
    /// while in r the same polynomial would be fitted across points a
    /// thousandfold apart in spacing. Outside the mesh returns 0 — a basis
    /// function beyond its confinement radius IS zero, and that is the
    /// commonest reason to ask.
    double interpolate(const std::vector<double>& values, double radius) const;

    /// Running integral ∫₀^{r_i} f(r) dr at every mesh point.
    ///
    /// Not a partial sum of weights(): the composite Simpson pattern is only a
    /// valid quadrature for the whole interval, and truncating it leaves a 4
    /// or a 2 where an endpoint 1 belongs — an O(h) error at every point
    /// instead of O(h⁴). This uses the cumulative form (Simpson across each
    /// pair of intervals, the (5, 8, −1)/12 rule for the odd points between),
    /// which is what an electrostatic potential needs.
    std::vector<double> cumulative(const std::vector<double>& values) const;

    /// Spherical Hartree potential of a spherically symmetric density.
    ///
    /// Solves ∇²V = −4πρ in the only form it takes for a radial density:
    ///
    ///     V(r) = (4π/r) ∫₀^r ρ(r')r'² dr' + 4π ∫_r^∞ ρ(r') r' dr'
    ///
    /// — the enclosed charge acting as a point charge, plus the potential of
    /// every shell outside r, which is constant inside it. Two running
    /// integrals rather than a differential-equation solve: it is O(n),
    /// unconditionally stable, and exact for the mesh's own quadrature.
    ///
    /// `density` is ρ(r) (electrons per bohr³, NOT 4πr²ρ). The returned
    /// potential is in hartree and is finite at r = 0.
    std::vector<double> hartreePotential(
        const std::vector<double>& density) const;

private:
    std::vector<double> r_;
    std::vector<double> drdi_;
    std::vector<double> weights_;
};

} // namespace calango::dft
