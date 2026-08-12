#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace calango::core {

/// Linear-tetrahedron Brillouin-zone integration on a regular Monkhorst-Pack
/// grid.
///
/// Exists because Fermi-surface integrals cannot be done by smearing on any
/// mesh a Calango user will run. The electron-phonon module measured it: on
/// fcc Al at 6x6x6, lambda ran 0.009 to 31 as the Gaussian width went 0.05 to
/// 0.8 eV, with no plateau — the "answer" was whichever smearing happened to
/// be configured. The tetrahedron method removes the parameter rather than
/// asking the user to converge it: epsilon is interpolated linearly inside
/// each tetrahedron and the constant-energy surface is integrated exactly.
///
/// IMPLEMENTED GEOMETRICALLY, not by transcribing Blochl's case formulas.
/// Inside one tetrahedron the surface eps(k) = E is a triangle or a
/// quadrilateral; this class finds it, measures it, and distributes it to the
/// four corners by barycentric coordinates. That is exact for a linear band
/// and a linear integrand — the same approximation the closed forms encode —
/// and it is far easier to check, because every step is a length or an area
/// rather than a branch of an algebraic identity.
///
/// The geometric route is also what makes the next step tractable. The
/// electron-phonon coupling needs the DOUBLE delta
/// delta(eps_k - E_F) delta(eps_{k+q} - E_F), whose surface is the LINE where
/// two such polygons intersect. That falls out of this construction; it does
/// not fall out of the single-delta closed forms, which is why the double
/// delta is a separate published algorithm rather than a corollary.
class TetrahedronBz {
public:
    /// `grid` is the Monkhorst-Pack division count per reciprocal axis.
    /// `reciprocal` holds b1, b2, b3 as ROWS, in inverse Angstrom.
    TetrahedronBz(std::array<int, 3> grid,
                  const std::array<std::array<double, 3>, 3>& reciprocal);

    const std::array<int, 3>& grid() const { return grid_; }
    std::size_t pointCount() const { return pointCount_; }
    /// Volume of the Brillouin zone, Angstrom^-3.
    double brillouinZoneVolume() const { return bzVolume_; }
    std::size_t tetrahedronCount() const { return tetrahedra_.size(); }

    /// Linear index of grid point (i1, i2, i3), wrapped periodically.
    ///
    /// The wrap is the whole reason this is a method rather than arithmetic at
    /// the call site: a microcell on the zone boundary has corners belonging
    /// to the opposite face, and its GEOMETRY must stay unwrapped while its
    /// ENERGIES come from the wrapped point.
    std::size_t index(int i1, int i2, int i3) const;

    /// Accumulate weights w_k with
    ///
    ///     (1/V_BZ) * integral dk delta(level - eps(k)) f(k)  =  sum_k w_k f_k
    ///
    /// into `weights`, which must be `pointCount()` long. Adds rather than
    /// assigns so a caller can sum over bands (and spins) without allocating
    /// per band — the electron-phonon sums do exactly that.
    ///
    /// `energies` is one band sampled on the grid, in the same linear order as
    /// `index()`.
    void accumulateDeltaWeights(const std::vector<double>& energies,
                                double level,
                                std::vector<double>& weights) const;

    /// Density of states at `level`: per unit cell, per spin, states/eV.
    /// Simply the sum of the delta weights, and provided because that identity
    /// is the cheapest way to check a weight set against a known DOS.
    double dos(const std::vector<double>& energies, double level) const;

    /// The tetrahedra, as quadruples of linear grid indices. Exposed for the
    /// double-delta extension, which walks the same decomposition.
    const std::vector<std::array<std::size_t, 4>>& tetrahedra() const
    {
        return tetrahedra_;
    }
    /// Cartesian corner offsets of tetrahedron `t` relative to its microcell
    /// origin, in inverse Angstrom. Shape repeats with period 6.
    const std::array<std::array<double, 3>, 4>&
    tetrahedronGeometry(std::size_t t) const
    {
        return shapes_[t % 6];
    }

    /// Accumulate weights for the DOUBLE delta,
    ///
    ///     (1/V_BZ) * integral dk delta(level - eps1(k)) delta(level - eps2(k))
    ///                        f(k)  =  sum_k w_k f_k
    ///
    /// which is the Fermi-surface restriction electron-phonon coupling needs:
    /// eps1 is the band at k, eps2 the band at k+q, and both are pinned at
    /// E_F. Not separable into two single deltas — the two constraints are
    /// correlated through k — which is why this is its own routine rather
    /// than two calls to the one above.
    ///
    /// Geometrically the single delta selects a SURFACE inside a tetrahedron
    /// and the double delta selects the LINE where two such surfaces meet.
    /// The measure follows from the same change of variables: with both
    /// functions linear, d3k = du dv dw / |grad eps1 x grad eps2|, so the
    /// contribution is the segment's length divided by that cross product.
    void accumulateDoubleDeltaWeights(const std::vector<double>& energies1,
                                      const std::vector<double>& energies2,
                                      double level,
                                      std::vector<double>& weights) const;

    /// The nesting function at `level`: the sum of the double-delta weights.
    ///
    /// For free electrons this has a closed form that the tests use —
    /// zeta(q) = V_cell / (16 pi^2 A^2 q) for 0 < q < 2 k_F and exactly zero
    /// beyond, with A = hbar^2/2m. The independence from k_F is real: the
    /// intersection circle's radius cancels against the gradient cross
    /// product.
    double nesting(const std::vector<double>& energies1,
                   const std::vector<double>& energies2, double level) const;

    /// One tetrahedron's double-delta segment, distributed to its corners.
    /// False when the two surfaces do not meet inside it.
    static bool tetrahedronDoubleDeltaWeights(
        const std::array<std::array<double, 3>, 4>& positions,
        const std::array<double, 4>& energies1,
        const std::array<double, 4>& energies2, double level,
        std::array<double, 4>& cornerWeights);

    /// The delta-surface of ONE tetrahedron: its area divided by |grad eps|,
    /// distributed to the four corners barycentrically.
    ///
    /// Returns false when the level does not cut this tetrahedron at all, in
    /// which case `cornerWeights` is untouched. Public and static because it
    /// is the piece worth testing in isolation — every higher-level number
    /// here is a sum of it.
    static bool tetrahedronDeltaWeights(
        const std::array<std::array<double, 3>, 4>& positions,
        const std::array<double, 4>& energies, double level,
        std::array<double, 4>& cornerWeights);

private:
    std::array<int, 3> grid_{};
    std::size_t pointCount_ = 0;
    double bzVolume_ = 0.0;
    /// The six tetrahedra a microcell is cut into, as Cartesian corner offsets.
    std::array<std::array<std::array<double, 3>, 4>, 6> shapes_{};
    std::vector<std::array<std::size_t, 4>> tetrahedra_;
};

} // namespace calango::core
