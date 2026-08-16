// Strain generation for the piezoelectric module: Voigt -> tensor mapping,
// deformation gradients, and applying them to a cell.
//
// The two classic bugs this targets directly: (1) forgetting the factor of
// two that separates ENGINEERING (Voigt) shear from TENSOR shear, and (2)
// getting the row/column convention backwards when applying a deformation
// gradient to a cell stored as rows of lattice vectors — which only shows up
// on a non-diagonal (triclinic) cell, so that is what is used here rather
// than a cubic one a transposed formula would pass by accident.

#include "core/StrainVoigt.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace calango::core;

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

void checkClose(double got, double want, double tol, const std::string& what)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  %-4s %s (got %.10g, want %.10g, tol %g)\n", ok ? "ok" : "FAIL",
                what.c_str(), got, want, tol);
    if (!ok)
        ++failures;
}

void testNormalStrainMapping()
{
    std::printf("Voigt -> tensor: pure normal component\n");
    const auto eps = strainTensorFromVoigt(unitVoigtStrain(1, 0.01)); // eyy
    checkClose(eps[0][0], 0.0, 1e-15, "exx untouched");
    checkClose(eps[1][1], 0.01, 1e-15, "eyy equals the Voigt value directly");
    checkClose(eps[2][2], 0.0, 1e-15, "ezz untouched");
    checkClose(eps[0][1], 0.0, 1e-15, "no shear leaks in");
}

void testShearFactorOfTwo()
{
    std::printf("Voigt -> tensor: shear halves, the classic Voigt bug\n");
    // Voigt component 5 (0-based) is e6 = 2*exy = 0.02, so the TENSOR
    // component exy must come out to 0.01, not 0.02.
    const auto eps = strainTensorFromVoigt(unitVoigtStrain(5, 0.02));
    checkClose(eps[0][1], 0.01, 1e-15, "exy = e6/2, not e6");
    checkClose(eps[1][0], 0.01, 1e-15, "symmetric");
    checkClose(eps[0][0], 0.0, 1e-15, "no normal strain leaks in");
    checkClose(eps[2][2], 0.0, 1e-15, "z axis untouched by an xy shear");
}

void testDeformationGradient()
{
    std::printf("Deformation gradient F = I + eps\n");
    const auto f = deformationGradient(unitVoigtStrain(0, 0.05)); // exx = 0.05
    checkClose(f[0][0], 1.05, 1e-15, "diagonal picks up 1 + strain");
    checkClose(f[1][1], 1.0, 1e-15, "untouched axis stays at 1");
    checkClose(f[2][2], 1.0, 1e-15, "untouched axis stays at 1");
    checkClose(f[0][1], 0.0, 1e-15, "no off-diagonal for a normal strain");
}

void testApplyToTriclinicCell()
{
    std::printf("Apply F to a non-diagonal cell (catches a left/right "
                "multiply bug)\n");
    // A deliberately non-diagonal cell: a cubic or orthorhombic test cell
    // cannot distinguish `cell . F^T` from the wrong `F . cell`, because F is
    // always symmetric here and the two formulas coincide whenever `cell`
    // and `F` commute (true for any diagonal cell against a diagonal or
    // even a general symmetric F acting on it trivially along shared axes).
    const Matrix3 cell{{{4.0, 0.0, 0.0}, {2.0, 3.0, 0.0}, {0.0, 0.0, 5.0}}};
    const auto f = deformationGradient(unitVoigtStrain(5, 0.02)); // xy shear

    const auto got = applyDeformationToCell(cell, f);

    // Hand-computed cell . F^T (F symmetric, so F^T == F here):
    //   row0 = [4,0,0] . F   = [4,        4*0.01,        0]
    //   row1 = [2,3,0] . F   = [2+3*0.01, 2*0.01+3,      0]
    //   row2 = [0,0,5] . F   = [0,        0,             5]
    checkClose(got[0][0], 4.0, 1e-12, "row0 x");
    checkClose(got[0][1], 0.04, 1e-12, "row0 y — this is where F . cell "
                                       "(wrong) would give 0.03 instead");
    checkClose(got[1][0], 2.03, 1e-12, "row1 x — F . cell (wrong) gives "
                                       "4.02 here");
    checkClose(got[1][1], 3.02, 1e-12, "row1 y");
    checkClose(got[2][2], 5.0, 1e-12, "row2 untouched by an xy shear");
}

void testVoigtPairsTable()
{
    std::printf("kVoigtPairs: the Voigt index -> Cartesian pair map\n");
    check(kVoigtPairs[0][0] == 0 && kVoigtPairs[0][1] == 0, "1 = xx");
    check(kVoigtPairs[1][0] == 1 && kVoigtPairs[1][1] == 1, "2 = yy");
    check(kVoigtPairs[2][0] == 2 && kVoigtPairs[2][1] == 2, "3 = zz");
    check(kVoigtPairs[3][0] == 1 && kVoigtPairs[3][1] == 2, "4 = yz");
    check(kVoigtPairs[4][0] == 0 && kVoigtPairs[4][1] == 2, "5 = xz");
    check(kVoigtPairs[5][0] == 0 && kVoigtPairs[5][1] == 1, "6 = xy");
}

} // namespace

int main()
{
    std::printf("StrainVoigt - deformation gradients and Voigt mapping\n\n");
    testNormalStrainMapping();
    testShearFactorOfTwo();
    testDeformationGradient();
    testApplyToTriclinicCell();
    testVoigtPairsTable();

    std::printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
