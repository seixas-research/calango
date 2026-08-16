#pragma once

#include "core/StrainVoigt.hpp"

#include <array>
#include <optional>
#include <vector>

namespace calango::core {

/// 3 Cartesian polarization rows x 6 Voigt strain columns — the
/// piezoelectric STRESS tensor e_ij. Units are the caller's responsibility;
/// the script generator is what attaches C/m^2 to the raw numbers.
using Matrix3x6 = std::array<std::array<double, 6>, 3>;

/// 6x6 Voigt elastic tensor (stiffness C or compliance S).
using Matrix6x6 = std::array<std::array<double, 6>, 6>;

/// Continuity-preserving branch fix for a Berry-phase polarization series
/// sampled at monotonically increasing strain magnitude.
///
/// Mirrors `numpy.unwrap`: walks the sequence in order and adds whatever
/// integer multiple of `period` keeps each step within
/// (-period/2, period/2] of the previous one. "Choose the branch that makes
/// ΔP continuous" (the classic pitfall of the finite-difference
/// piezoelectric method — see PiezoelectricScriptGenerator) is exactly that
/// definition, which is why the generated Python script calls `np.unwrap`
/// directly rather than re-deriving it. This C++ copy exists so the
/// algorithm itself has a Calango-side unit test against synthetic
/// multivalued data, and so the mocked-polarization regression test can
/// exercise the full branch-fix -> fit -> correction -> symmetrize pipeline
/// without a Python interpreter or a DFT run.
std::vector<double> unwrapPhaseBranch(
    std::vector<double> phases,
    double period = 6.283185307179586476925286766559);

/// Ordinary least-squares slope of y = a + b*x through the sample points,
/// returning b — the finite-difference derivative dP/dε at one strain
/// component. Two points reproduce the exact central difference; more
/// points (extra δ multiples per component) give the "better fit" the wizard
/// offers, at the cost of extra ground-state runs. Points must not all share
/// the same x; returns 0 when they do (no information to fit).
double linearFitSlope(const std::vector<double>& x, const std::vector<double>& y);

/// The improper -> proper piezoelectric tensor correction term (added to the
/// raw finite-difference tensor to obtain the proper one), in Voigt
/// notation.
///
/// Vanderbilt, "Berry-phase theory of proper piezoelectric response",
/// J. Phys. Chem. Solids 61, 147 (2000); arXiv:cond-mat/9903137, Eq. (15):
///
///     c~_ijk = c_ijk + delta_jk P_i - delta_ij P_k
///
/// with i the polarization Cartesian index and (j, k) the strain Cartesian
/// pair one Voigt column stands for. As printed the formula is not
/// manifestly symmetric under j <-> k, so using it literally depends on an
/// arbitrary choice of which strain axis is "j" and which is "k" — which
/// must not matter, since a Voigt column names an unordered pair. The
/// physically consistent form (the two agree exactly for normal strains,
/// where j == k, and only differ for shear) is the j <-> k symmetrization of
/// the same formula:
///
///     correction_i,(jk) = delta_jk P_i - (1/2)(delta_ij P_k + delta_ik P_j)
///
/// This is what is implemented here. `referencePolarization` is P^0, the
/// polarization of the UNSTRAINED reference geometry (Cartesian, same units
/// as the assembled e_ij).
Matrix3x6 properPiezoelectricCorrection(const std::array<double, 3>& referencePolarization);

/// Symmetrize a piezoelectric tensor under a crystallographic point group,
/// given as Cartesian rotation/rotoinversion matrices (det = +-1, including
/// the identity).
///
/// General tensor transformation, not a per-point-group lookup table:
/// e'_ijk = R_ii' R_jj' R_kk' e_i'j'k', averaged over every operation
/// supplied. This simultaneously (a) zeroes whatever Voigt component
/// symmetry forbids and (b) cleans up the numerical noise a finite
/// difference from a real DFT run leaves in the components symmetry does
/// allow.
///
/// In particular, if the group contains the inversion R = -I, every
/// component of e picks up a factor (-1)^3 = -1 under that one operation, so
/// averaging it against the untransformed tensor gives exactly zero:
/// piezoelectricity vanishing identically in a centrosymmetric point group
/// falls out of this routine as a consequence, rather than being coded as a
/// special case.
Matrix3x6 symmetrizePiezoelectricTensor(const Matrix3x6& raw,
                                        const std::vector<Matrix3>& pointGroupOps);

/// True when `ops` contains the inversion (R == -I, within `tolerance`).
/// Piezoelectricity is forbidden by symmetry in any such point group — using
/// this to refuse BEFORE running any strained ground state is the one
/// unconditional way this module reduces the number of calculations from
/// symmetry, independent of which Voigt components are requested.
bool containsInversion(const std::vector<Matrix3>& ops, double tolerance = 1e-6);

/// 6x6 matrix inverse by Gauss-Jordan elimination with partial pivoting.
/// Returns std::nullopt if the matrix is numerically singular. A small
/// in-repo inverse rather than a linear-algebra dependency, since every
/// matrix this module handles is this same fixed, small size. Used to turn
/// a user-supplied elastic stiffness C into the compliance S = C^-1 the
/// e -> d conversion needs.
std::optional<Matrix6x6> invert6x6(const Matrix6x6& c);

/// Piezoelectric STRESS tensor e -> piezoelectric STRAIN tensor d, given the
/// elastic compliance S (already in units reciprocal to e's, i.e. the caller
/// has converted C from GPa to the same unit system as e before inverting
/// it): d_i,alpha = sum_beta e_i,beta * S_beta,alpha, the standard
/// constitutive relation d = e . S.
Matrix3x6 stressToStrainPiezoelectricTensor(const Matrix3x6& e, const Matrix6x6& compliance);

} // namespace calango::core
