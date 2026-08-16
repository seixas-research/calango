#pragma once

#include <array>
#include <cstddef>

namespace calango::core {

/// A strain state in Voigt notation: [e1 e2 e3 e4 e5 e6] =
/// [exx, eyy, ezz, 2*eyz, 2*exz, 2*exy] — ENGINEERING strain, with the shear
/// components doubled. This is the convention the piezoelectric and elastic
/// tensors are conventionally reported in (Nye, "Physical Properties of
/// Crystals", ch. VIII).
using VoigtStrain = std::array<double, 6>;

using Matrix3 = std::array<std::array<double, 3>, 3>;

/// The symmetric TENSOR strain ε_ab this Voigt vector represents:
///
///     [ e1     e6/2   e5/2 ]
///     [ e6/2   e2     e4/2 ]
///     [ e5/2   e4/2   e3   ]
///
/// The 1/2 on the off-diagonal entries is what makes engineering (Voigt)
/// strain and tensor strain agree on the diagonal but differ by a factor of
/// two off it — the single most common Voigt-mapping bug, and the reason
/// this conversion is a named function rather than inlined at each call
/// site.
constexpr Matrix3 strainTensorFromVoigt(const VoigtStrain& v)
{
    return {{
        {v[0], v[5] / 2.0, v[4] / 2.0},
        {v[5] / 2.0, v[1], v[3] / 2.0},
        {v[4] / 2.0, v[3] / 2.0, v[2]},
    }};
}

/// The deformation gradient F = I + ε for this (small) strain.
///
/// Linear in the strain — the regime the finite-difference piezoelectric
/// method is defined in. Squeezed between two errors like the Born-charges
/// displacement (BornChargesScriptGenerator): δ has to stay small enough
/// that F = I + ε and the exact F = exp(ε) agree to within the SCF noise, and
/// the piezoelectric wizard's default (0.5-1%) is chosen with that in mind.
constexpr Matrix3 deformationGradient(const VoigtStrain& v)
{
    Matrix3 f = strainTensorFromVoigt(v);
    f[0][0] += 1.0;
    f[1][1] += 1.0;
    f[2][2] += 1.0;
    return f;
}

/// A pure Voigt strain with only component `voigtIndex` (0-based, 0..5) set
/// to `magnitude` and every other component zero — one column of the
/// piezoelectric tensor's finite-difference stencil.
constexpr VoigtStrain unitVoigtStrain(int voigtIndex, double magnitude)
{
    VoigtStrain v{};
    v[static_cast<std::size_t>(voigtIndex)] = magnitude;
    return v;
}

/// Apply deformation gradient `f` to a cell stored ROW-MAJOR as lattice
/// vectors (ASE/Calango convention: cell[i] is lattice vector a_i).
///
/// Each row transforms as a_i -> F . a_i (F acting on the column vector), so
/// in row-vector form the WHOLE matrix transforms as cell' = cell . F^T.
/// Because F = I + (symmetric strain) is itself always symmetric here,
/// F^T == F numerically — the bug this shape of formula actually invites is
/// therefore NOT "F vs F^T" (the two coincide for every strain this module
/// generates) but swapping which side of the product `cell` sits on:
/// `F . cell` silently does something else entirely for any cell that is not
/// diagonal (i.e. anything but a cubic/orthorhombic-aligned lattice), since
/// `cell . F` and `F . cell` agree only when `cell` and `F` commute. The unit
/// test exercises a triclinic (non-diagonal) cell specifically so a
/// left/right multiplication mix-up shows up as a wrong answer rather than
/// passing by accident on a cubic test cell.
constexpr Matrix3 applyDeformationToCell(const Matrix3& cell, const Matrix3& f)
{
    Matrix3 out{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k)
                sum += cell[i][k] * f[j][k]; // cell . F^T
            out[i][j] = sum;
        }
    return out;
}

/// Voigt strain index -> the (j, k) Cartesian pair it contracts, 0-based:
/// 0=xx, 1=yy, 2=zz, 3=yz, 4=xz, 5=xy. Shared by the piezoelectric tensor
/// assembly (the proper/improper correction and the point-group
/// symmetrization both need to walk the same pair-to-column map) so the two
/// cannot silently disagree on which column is "yz" versus "xz".
constexpr std::array<std::array<int, 2>, 6> kVoigtPairs{{
    {0, 0}, {1, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 1},
}};

} // namespace calango::core
