#pragma once

#include "core/PiezoelectricTensor.hpp" // Matrix6x6, invert6x6 — the elastic
                                         // tensor is exactly this shape, and
                                         // the Reuss-average compliance needs
                                         // the same small in-repo 6x6 inverse.
#include "core/StrainVoigt.hpp"

#include <array>
#include <string>
#include <vector>

namespace calango::core {

/// Ordinary least-squares quadratic fit y = a + b*x + c*x^2, returning 2c —
/// the second derivative d^2y/dx^2 at x=0, i.e. the CURVATURE the
/// energy-strain method reads a DIAGONAL elastic constant off of:
/// C_jj = curvature / V0. Exact (3 points, 3 unknowns) with exactly
/// {-delta, 0, +delta}; extra points (+-2*delta) improve the fit the same
/// way PiezoelectricTensor::linearFitSlope's extra points do. Returns 0 when
/// there are fewer than 3 distinct x values (no curvature information).
double quadraticCurvature(const std::vector<double>& x, const std::vector<double>& y);

/// The mixed second partial derivative d^2E/(d(eps_j) d(eps_k)) from the
/// standard 4-point central-difference stencil on a COMBINED two-component
/// strain (both components at +-magnitude simultaneously):
///
///     d2E/(dj dk) = [E(+d,+d) - E(+d,-d) - E(-d,+d) + E(-d,-d)] / (4 d^2)
///
/// This is what the energy-strain method needs for an OFF-DIAGONAL elastic
/// constant C_jk (j != k): a single-component sweep of either j or k alone
/// only ever sees the DIAGONAL curvature (C_jj or C_kk) — recovering the
/// cross term needs energies from strain states where BOTH components are
/// deformed together, the standard "independent/combined strain" technique
/// (Ravindran et al., J. Appl. Phys. 84, 4891 (1998); Fast et al., PRB 51,
/// 17431 (1995)). `magnitude` is the common strain magnitude used for both
/// components (ElasticConfig keeps a single strainMagnitude, so the four
/// combined points always share it).
double crossCurvature(double ePlusPlus, double ePlusMinus,
                      double eMinusPlus, double eMinusMinus,
                      double magnitude);

/// The 6 eigenvalues of a real symmetric 6x6 matrix, ascending order, via
/// the classic cyclic Jacobi rotation method — a small in-repo solver (like
/// invert6x6 in PiezoelectricTensor.hpp) rather than a linear-algebra
/// dependency, since every matrix this module diagonalizes is this same
/// fixed, tiny size. This is the GENERAL Born mechanical-stability check
/// (Born & Huang): a crystal is mechanically stable if and only if every
/// eigenvalue of the elastic stiffness tensor is strictly positive —
/// universal across every crystal class, unlike the closed forms below,
/// which are a faster-to-read cross-check on it, not a replacement for it.
std::array<double, 6> symmetricEigenvalues6x6(const Matrix6x6& c);

/// The Laue/crystal classes this module has a closed-form Born criterion
/// for, keyed off the point group spglib reports. `Other` covers monoclinic
/// and triclinic (and any point-group string this module does not
/// recognise) — the general eigenvalue criterion above still applies; only
/// the faster-to-read closed form is unavailable for those classes.
enum class CrystalClass {
    Cubic,
    Hexagonal,
    Tetragonal,
    Trigonal,
    Orthorhombic,
    Other,
};

/// Classify a spglib Hermann-Mauguin point-group SYMBOL (e.g. "m-3m",
/// "6/mmm", "-3m") into the Laue class its Born criterion below is written
/// for. Unrecognised or empty input returns CrystalClass::Other.
CrystalClass classifyPointGroup(const std::string& spglibPointGroup);

/// One named Born stability inequality, evaluated against an actual tensor:
/// `expression` is the human-readable left-hand side (e.g. "C11 - C12"),
/// `value` its computed number, and `satisfied` whether it is > 0 — every
/// closed-form criterion this module implements is a strict "> 0"
/// inequality (Mouhat & Coudert's forms, referenced below).
struct BornCriterion {
    std::string expression;
    double value = 0.0;
    bool satisfied = false;
};

/// Every crystal-class-specific Born criterion this module implements, from
/// Mouhat & Coudert, "Necessary and sufficient elastic stability conditions
/// in various crystal systems", Phys. Rev. B 90, 224104 (2014):
///
///   Cubic:         C11-C12>0;  C11+2*C12>0;  C44>0.
///   Hexagonal:     C11-C12>0;  2*C13^2 < C33*(C11+C12);  C44>0.
///   Tetragonal:    C11-C12>0;  2*C13^2 < C33*(C11+C12);  C44>0;  C66>0.
///                  (the common 4/mmm-family form; the rarer C16-carrying
///                  tetragonal(II) classes are not distinguished here.)
///   Trigonal:      C11-C12>0;  C44>0;  2*C13^2 < C33*(C11+C12);
///                  2*C14^2 < C44*(C11-C12).
///                  (the -3m/32/3m form with C14 != 0, C15 == 0 — the common
///                  case; the rarer -3-class C15 term is not included.)
///   Orthorhombic:  C11>0;  C11*C22>C12^2;
///                  C11*C22*C33 + 2*C12*C13*C23 - C11*C23^2 - C22*C13^2
///                      - C33*C12^2 > 0;
///                  C44>0;  C55>0;  C66>0.
///
/// Returns an empty vector for CrystalClass::Other — call
/// symmetricEigenvalues6x6() directly for those, and for a cross-check on
/// every class above.
std::vector<BornCriterion> bornCriteriaForClass(const Matrix6x6& c, CrystalClass crystalClass);

/// Bulk and shear moduli (GPa) from the Voigt (uniform-strain, upper bound),
/// Reuss (uniform-stress, lower bound, via the compliance S = C^-1) and Hill
/// (arithmetic mean of the two — the standard practical isotropic estimate)
/// averages: Hill, Proc. Phys. Soc. A 65, 349 (1952).
struct ElasticModuli {
    double bulkVoigtGPa = 0.0;
    double bulkReussGPa = 0.0;
    double bulkHillGPa = 0.0;
    double shearVoigtGPa = 0.0;
    double shearReussGPa = 0.0;
    double shearHillGPa = 0.0;
    /// Isotropic (Hill) estimate: E = 9*B_H*G_H / (3*B_H + G_H).
    double youngHillGPa = 0.0;
    /// Isotropic (Hill) estimate: nu = (3*B_H - 2*G_H) / (2*(3*B_H + G_H)).
    double poissonHill = 0.0;
    /// false when C is numerically singular (Reuss/Hill undefined) — every
    /// *Reuss* and *Hill* field above is then left at 0 and should not be
    /// trusted; the Voigt fields remain valid regardless (they never invert
    /// C).
    bool reussValid = true;
};
ElasticModuli computeElasticModuli(const Matrix6x6& c);

/// Symmetrize a 6x6 elastic stiffness tensor under a crystallographic point
/// group, given as Cartesian rotation/rotoinversion matrices — the rank-4
/// analogue of symmetrizePiezoelectricTensor: C'_ijkl = R_ii' R_jj' R_kk'
/// R_ll' C_i'j'k'l', averaged over every operation and expressed back in
/// Voigt notation. This both zeroes whatever component symmetry forbids and
/// cleans up the finite-difference numerical noise in the components it
/// allows. Unlike the piezoelectric tensor, the elastic tensor is NEVER
/// forced to zero by inversion symmetry — every rank-4 tensor is inversion-
/// invariant — so there is no centrosymmetric refusal counterpart here.
Matrix6x6 symmetrizeElasticTensor(const Matrix6x6& raw, const std::vector<Matrix3>& pointGroupOps);

// --- 2D (monolayer) quantities ---------------------------------------------
//
// A 2D elastic tensor is reported in N/m (an areal, not volumetric,
// normalization) — see ElasticScriptGenerator's "2D coefficients" comment
// for why a bulk-style GPa value multiplied by the vacuum height would be
// vacuum-dependent and wrong. Every function below takes the already-2D
// (N/m) in-plane entries directly.

/// In-plane Young's moduli and Poisson's ratios for an orthotropic 2D sheet:
///
///     Ex = (C11*C22 - C12^2) / C22,   Ey = (C11*C22 - C12^2) / C11,
///     nu_xy = C12 / C22,              nu_yx = C12 / C11,
///
/// satisfying the reciprocity Ex*nu_yx == Ey*nu_xy exactly (both equal
/// C12*(C11*C22-C12^2)/(C11*C22)). For an ISOTROPIC sheet (C11 == C22, e.g.
/// any hexagonal monolayer) these collapse to the textbook single Ex == Ey
/// and nu_xy == nu_yx == C12/C11 the task asks to report as the headline
/// number; the general (Ex, Ey, nu_xy, nu_yx) form is what a lower-symmetry
/// 2D crystal needs.
struct ElasticModuli2D {
    double youngXNPerM = 0.0;
    double youngYNPerM = 0.0;
    double poissonXY = 0.0;
    double poissonYX = 0.0;
    /// 2D layer modulus (resistance to isotropic in-plane compression,
    /// sometimes called the "2D bulk modulus"): (C11+C22+2*C12)/4 for the
    /// orthotropic case here, reducing to the textbook (C11+C12)/2 when
    /// C11 == C22.
    double layerModulusNPerM = 0.0;
};
ElasticModuli2D computeElasticModuli2D(double c11, double c22, double c12);

/// 2D Born stability: the general in-plane criterion — C11*C22 - C12^2 > 0
/// and C66 > 0 — equivalent to the 3x3 in-plane Voigt block
/// [[C11,C12,0],[C12,C22,0],[0,0,C66]] having every eigenvalue positive,
/// i.e. the 2D restriction of the bulk eigenvalue criterion to the in-plane
/// components a monolayer's strain is ever applied to.
struct BornStability2D {
    BornCriterion positiveDefinite; ///< "C11*C22 - C12^2"
    BornCriterion shearPositive;    ///< "C66"
    bool stable = false;
};
BornStability2D bornStability2D(double c11, double c22, double c12, double c66);

} // namespace calango::core
