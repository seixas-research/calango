#pragma once

#include "dft/DftTypes.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace calango::dft {

/// A point on the unit sphere with its quadrature weight (weights sum to 1).
struct AngularPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double weight = 0.0;
};

/// Quadrature on the unit sphere.
///
/// Two families, and the reason for having both is accuracy per point versus
/// generality:
///
///   * LEBEDEV rules are the octahedrally symmetric optimum — they integrate
///     every spherical harmonic up to a given degree exactly with roughly
///     half the points a product rule needs. The small orders (6, 14, 26, 50)
///     have closed-form weights and are written out here.
///   * A GAUSS-LEGENDRE × uniform-φ PRODUCT rule, generated at run time to
///     any degree. Gauss-Legendre in cos θ is exact for polynomials in cos θ,
///     the uniform φ rule is exact for e^{imφ} up to the point count, and
///     together they integrate Y_lm exactly up to a degree set by the caller.
///     It costs more points than Lebedev but needs no table, so an accuracy
///     the tables do not reach is still reachable.
///
/// Every rule here is VERIFIED rather than trusted: `dft_grids` integrates
/// products of real spherical harmonics on each one and requires
/// ∫Y_lm Y_l'm' dΩ = δ to machine precision up to the claimed degree. A
/// mistyped Lebedev weight is otherwise completely silent — the total energy
/// is simply wrong in the third digit.
class AngularGrid {
public:
    /// The smallest available rule that is exact for spherical harmonics up
    /// to `degree`. Falls back to the generated product rule above the
    /// tabulated orders.
    static std::vector<AngularPoint> forDegree(int degree);

    /// A tabulated Lebedev rule of exactly `points` points, or empty when
    /// that order is not tabulated. Available: 6, 14, 26, 50.
    static std::vector<AngularPoint> lebedev(int points);

    /// Gauss-Legendre in cos θ × uniform in φ. `thetaPoints` Legendre nodes
    /// integrate polynomials of degree 2·thetaPoints−1 in cos θ exactly;
    /// `phiPoints` uniform nodes integrate e^{imφ} exactly for |m| < phiPoints.
    static std::vector<AngularPoint> product(int thetaPoints, int phiPoints);

    /// Real spherical harmonics Y_lm(x̂) for every l ≤ lMax, in the order
    /// (l = 0, m = 0), (1, −1), (1, 0), (1, 1), (2, −2) …, normalised so that
    /// ∫Y² dΩ = 1.
    ///
    /// REAL rather than complex, throughout the engine: the basis functions,
    /// the density and the Hamiltonian are all real for a real-valued
    /// potential, and carrying complex harmonics would double the work to
    /// represent an imaginary part that cancels.
    static void realSphericalHarmonics(double x, double y, double z, int lMax,
                                       std::vector<double>& values);

    /// Real SOLID harmonics r^l·Y_lm and their Cartesian gradients.
    ///
    /// The solid harmonic is a homogeneous polynomial in (x, y, z), so its
    /// gradient exists everywhere including the origin — unlike the angular
    /// gradient of Y_lm itself, which carries a 1/sin θ and is singular at the
    /// poles. Writing φ = g(r)·S_lm and differentiating that product is what
    /// keeps ∇φ free of coordinate singularities.
    ///
    /// The gradients are taken by a central difference of the CLOSED-FORM
    /// polynomial, not of tabulated data: the step is relative to r, the
    /// function is smooth, and the result is accurate to about 1e-8 relative
    /// — far below the accuracy of the quadrature that consumes it. The
    /// analytic l ≤ 2 gradients are written out in the test as the check.
    static void realSolidHarmonics(double x, double y, double z, int lMax,
                                   std::vector<double>& values,
                                   std::vector<double>& gradients);

    /// The number of harmonics up to lMax: (lMax+1)².
    static std::size_t harmonicCount(int lMax)
    {
        return static_cast<std::size_t>((lMax + 1) * (lMax + 1));
    }
};

/// One quadrature point of the three-dimensional multicentre grid.
struct GridPoint {
    std::array<double, 3> position{{0.0, 0.0, 0.0}}; ///< bohr, absolute
    /// Quadrature weight INCLUDING the Becke partition function and the r²
    /// of the spherical volume element, so ∫f dV = Σ_g weight_g f(r_g)
    /// with nothing left to remember.
    double weight = 0.0;
    /// Which atom's sphere this point belongs to. Kept because the
    /// electrostatics is solved per centre and needs to gather points back
    /// into atomic shells.
    std::size_t atom = 0;
    /// Index of the radial shell within that atom's sphere, and of the
    /// angular point within the shell. Together with `atom` these address the
    /// point in the (atom, shell, direction) decomposition the radial Poisson
    /// solver works in.
    std::size_t shell = 0;
    std::size_t direction = 0;
};

/// The atom-centred multicentre integration grid.
///
/// There is no analytic integral anywhere in a numerical-orbital
/// all-electron code, so this grid IS the accuracy of every number the engine
/// produces. Its construction answers one question: how do you integrate a
/// function that has a cusp at every nucleus and is smooth in between,
/// without a mesh fine enough for the cusps everywhere?
///
/// Becke's answer, which this implements: give every atom its own spherical
/// grid, dense at its own nucleus, and multiply each by a smooth weight
/// function w_A(r) with
///
///     Σ_A w_A(r) = 1   everywhere,
///     w_A ≈ 1 near nucleus A, w_A ≈ 0 near any other.
///
/// Then ∫f = Σ_A ∫w_A f, and each term is an integral of a function that is
/// smooth except at ITS OWN centre — exactly what a spherical grid is good
/// at. The weight is built from the smoothed step function s(μ) of the
/// elliptical coordinate μ_AB between each pair of atoms, iterated three
/// times to make the transition sharp without making it discontinuous, and
/// shifted by the atoms' Bragg-Slater radii so the boundary between a large
/// atom and a small one sits where the density says it should.
///
/// Periodicity enters in exactly one place: the partition has to see the
/// atoms' periodic IMAGES, or the weight function stops summing to one at
/// the cell boundary and the integrated electron count comes out short by
/// however much charge lives there.
class IntegrationGrid {
public:
    struct Atom {
        int atomicNumber = 0;
        std::array<double, 3> position{{0.0, 0.0, 0.0}}; ///< bohr
    };

    /// Build the grid for `atoms`.
    ///
    /// `lattice` is three lattice vectors in bohr for a periodic system, or
    /// empty for a finite one. `radialShells` and `angularDegree` set the
    /// per-atom accuracy; `outerRadiusBohr` is where each atom's sphere stops
    /// — beyond the largest basis-function cutoff there is nothing to
    /// integrate.
    Outcome build(const std::vector<Atom>& atoms,
                  const std::vector<std::array<double, 3>>& lattice,
                  int radialShells, int angularDegree, double outerRadiusBohr);

    const std::vector<GridPoint>& points() const { return points_; }
    std::size_t size() const { return points_.size(); }

    /// The radii of the shells of one atom's sphere, and the angular
    /// directions shared by every shell. Exposed because the electrostatic
    /// solver works shell by shell rather than point by point.
    const std::vector<double>& shellRadii() const { return shellRadii_; }
    const std::vector<AngularPoint>& directions() const { return directions_; }
    /// Radial quadrature weight ∫…r²dr of each shell (without the 4π, which
    /// the angular weights carry).
    const std::vector<double>& shellWeights() const { return shellWeights_; }

    std::size_t atomCount() const { return atomCount_; }

    /// Index of the point (atom, shell, direction) in `points()`.
    std::size_t index(std::size_t atom, std::size_t shell,
                      std::size_t direction) const
    {
        return (atom * shellRadii_.size() + shell) * directions_.size()
            + direction;
    }

    /// ∫f dV over the whole grid.
    double integrate(const std::vector<double>& values) const;

    /// Bragg-Slater atomic radius in bohr, used to bias the Becke partition
    /// between atoms of different size. Returns a neutral 1.0 Å for elements
    /// outside the tabulated range rather than zero, which would put the
    /// partition boundary on top of the nucleus.
    static double braggRadiusBohr(int atomicNumber);

private:
    std::vector<GridPoint> points_;
    std::vector<double> shellRadii_;
    std::vector<double> shellWeights_;
    std::vector<AngularPoint> directions_;
    std::size_t atomCount_ = 0;
};

} // namespace calango::dft
