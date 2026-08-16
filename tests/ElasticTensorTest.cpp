// The elastic-tensor assembly pipeline, tested against literature reference
// tensors and synthetic data rather than a real DFT run — the same fallback
// PiezoelectricTensorTest uses, since there is no cheap oracle for a real
// finite-strain calculation to compare against in a unit test.
//
// Five pieces, each independently testable:
//   1. Voigt assembly / fit primitives (quadraticCurvature, crossCurvature)
//      the energy-strain method's Python mirror uses.
//   2. The general Born criterion (symmetricEigenvalues6x6): every
//      eigenvalue of C positive.
//   3. The crystal-class closed-form Born criteria (Mouhat & Coudert, PRB
//      90, 224104 (2014)) against LITERATURE elastic constants for cubic Cu
//      and Si, cross-checked against the general eigenvalue criterion.
//   4. Voigt/Reuss/Hill moduli and 2D (layer modulus, in-plane Young's/
//      Poisson) quantities, hand-verified against their defining formulas.
//   5. Point-group symmetrization (the rank-4 analogue of
//      symmetrizePiezoelectricTensor) and crystal-class classification.

#include "core/ElasticTensor.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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

/// A cubic elastic tensor from its three independent constants — C12 filled
/// into every off-diagonal normal pair, C44 onto every shear diagonal, zero
/// elsewhere.
Matrix6x6 cubicTensor(double c11, double c12, double c44)
{
    Matrix6x6 c{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            c[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = (i == j) ? c11 : c12;
    for (int i = 3; i < 6; ++i)
        c[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)] = c44;
    return c;
}

void testQuadraticCurvatureExactParabola()
{
    std::printf("\nquadraticCurvature: exact recovery on synthetic E(eps)\n");
    // E(eps) = 5.0 + 0.02*eps - 3.0*eps^2 (eV): the a=5, b=0.02 terms are
    // irrelevant to the curvature; only c=-3.0 matters, giving
    // d2E/deps^2 = 2*c = -6.0.
    std::vector<double> x = {-0.01, 0.0, 0.01};
    std::vector<double> y;
    for (double e : x)
        y.push_back(5.0 + 0.02 * e - 3.0 * e * e);
    checkClose(quadraticCurvature(x, y), -6.0, 1e-8,
        "3-point stencil recovers the exact curvature");

    // A richer 5-point stencil (+-delta, +-2delta, 0) least-squares fits the
    // same parabola — still exact, since the underlying data IS a parabola.
    std::vector<double> x5 = {-0.02, -0.01, 0.0, 0.01, 0.02};
    std::vector<double> y5;
    for (double e : x5)
        y5.push_back(5.0 + 0.02 * e - 3.0 * e * e);
    checkClose(quadraticCurvature(x5, y5), -6.0, 1e-6,
        "5-point stencil agrees with the 3-point one on the same parabola");

    check(quadraticCurvature({0.0, 0.0}, {1.0, 1.0}) == 0.0,
        "fewer than 3 distinct x values returns 0, not a spurious fit");
}

void testCrossCurvatureExactSaddle()
{
    std::printf("\ncrossCurvature: exact recovery on a synthetic bilinear term\n");
    // E(ej, ek) = 4.0 + 2.5*ej*ek (eV) -- pure cross term, d2E/(dej dek) =
    // 2.5 exactly, independent of the sample magnitude.
    const double d = 0.01;
    auto e = [](double ej, double ek) { return 4.0 + 2.5 * ej * ek; };
    const double got = crossCurvature(e(d, d), e(d, -d), e(-d, d), e(-d, -d), d);
    checkClose(got, 2.5, 1e-8, "4-point combined stencil recovers the exact cross term");
    check(crossCurvature(1.0, 1.0, 1.0, 1.0, 0.0) == 0.0,
        "zero magnitude returns 0 rather than dividing by zero");
}

void testSymmetricEigenvalues6x6()
{
    std::printf("\nsymmetricEigenvalues6x6: known eigenvalues from 2x2 blocks\n");
    // Three independent 2x2 blocks [[a,b],[b,a]] along the diagonal, each
    // with exactly-known eigenvalues a+b and a-b (eigenvectors (1,1) and
    // (1,-1)) -- six predictable eigenvalues in total, hand-computable
    // without needing a full symmetric-matrix reference solver.
    Matrix6x6 c{};
    // Block 1 (indices 0,1): a=10, b=3 -> eigenvalues 13, 7.
    c[0][0] = 10.0;
    c[1][1] = 10.0;
    c[0][1] = c[1][0] = 3.0;
    // Block 2 (indices 2,3): a=5, b=1 -> eigenvalues 6, 4.
    c[2][2] = 5.0;
    c[3][3] = 5.0;
    c[2][3] = c[3][2] = 1.0;
    // Block 3 (indices 4,5): a=2, b=-2 -> eigenvalues 0, 4.
    c[4][4] = 2.0;
    c[5][5] = 2.0;
    c[4][5] = c[5][4] = -2.0;

    const auto eig = symmetricEigenvalues6x6(c);
    const std::vector<double> want = {0.0, 4.0, 4.0, 6.0, 7.0, 13.0}; // sorted
    for (std::size_t i = 0; i < 6; ++i)
        checkClose(eig[i], want[i], 1e-6,
            "eigenvalue[" + std::to_string(i) + "] from the block-diagonal construction");
}

void testBornCriteriaCubicCopper()
{
    std::printf("\nbornCriteriaForClass: cubic Cu against literature (Simmons & Wang)\n");
    // Copper, room temperature: C11 = 168.4 GPa, C12 = 121.4 GPa,
    // C44 = 75.4 GPa (Simmons & Wang, "Single Crystal Elastic Constants and
    // Calculated Aggregate Properties", commonly cited room-T values).
    const Matrix6x6 c = cubicTensor(168.4, 121.4, 75.4);
    const auto crit = bornCriteriaForClass(c, CrystalClass::Cubic);
    check(crit.size() == 3, "cubic reports exactly 3 named criteria");
    checkClose(crit[0].value, 168.4 - 121.4, 1e-9, "C11 - C12");
    check(crit[0].satisfied, "C11 - C12 > 0 for Cu (mechanically stable)");
    checkClose(crit[1].value, 168.4 + 2.0 * 121.4, 1e-9, "C11 + 2*C12");
    check(crit[1].satisfied, "C11 + 2*C12 > 0 for Cu");
    checkClose(crit[2].value, 75.4, 1e-9, "C44");
    check(crit[2].satisfied, "C44 > 0 for Cu");

    const auto eig = symmetricEigenvalues6x6(c);
    bool allPositive = true;
    for (double e : eig)
        allPositive = allPositive && e > 0.0;
    check(allPositive, "the general eigenvalue criterion agrees: Cu is stable");

    const auto m = computeElasticModuli(c);
    // B_V for a cubic tensor reduces to (C11 + 2*C12) / 3 exactly.
    checkClose(m.bulkVoigtGPa, (168.4 + 2.0 * 121.4) / 3.0, 1e-9,
        "Voigt bulk modulus matches the cubic closed form (C11+2C12)/3");
    // G_V for a cubic tensor reduces to (C11 - C12 + 3*C44) / 5 exactly.
    checkClose(m.shearVoigtGPa, (168.4 - 121.4 + 3.0 * 75.4) / 5.0, 1e-9,
        "Voigt shear modulus matches the cubic closed form (C11-C12+3C44)/5");
    check(m.reussValid, "Cu's compliance inverts cleanly (Reuss/Hill are valid)");
    // The experimental bulk modulus of Cu is ~137-140 GPa -- the Voigt bound
    // computed above should land in that neighbourhood, a sanity check on
    // top of the exact closed-form check.
    check(m.bulkVoigtGPa > 130.0 && m.bulkVoigtGPa < 145.0,
        "Cu's bulk modulus is in the textbook ~130-145 GPa range");
    check(m.bulkHillGPa > 0.0 && m.shearHillGPa > 0.0 && m.youngHillGPa > 0.0,
        "Hill-averaged moduli are all positive for a stable, well-conditioned tensor");
    check(m.poissonHill > 0.2 && m.poissonHill < 0.45,
        "Cu's isotropic Poisson ratio is in the physically ordinary 0.2-0.45 range");
}

void testBornCriteriaCubicSilicon()
{
    std::printf("\nbornCriteriaForClass: cubic Si against literature (McSkimin)\n");
    // Silicon, room temperature: C11 = 165.7 GPa, C12 = 63.9 GPa,
    // C44 = 79.6 GPa (McSkimin 1953, the standard reference values).
    const Matrix6x6 c = cubicTensor(165.7, 63.9, 79.6);
    const auto crit = bornCriteriaForClass(c, CrystalClass::Cubic);
    check(crit[0].satisfied && crit[1].satisfied && crit[2].satisfied,
        "diamond-structure Si satisfies every cubic Born criterion");
    const auto m = computeElasticModuli(c);
    checkClose(m.bulkVoigtGPa, (165.7 + 2.0 * 63.9) / 3.0, 1e-9,
        "Si's Voigt bulk modulus matches (C11+2C12)/3");
    // Si's textbook bulk modulus is ~98 GPa.
    check(m.bulkVoigtGPa > 90.0 && m.bulkVoigtGPa < 105.0,
        "Si's bulk modulus is in the textbook ~90-105 GPa range");
}

void testBornCriteriaDetectsInstability()
{
    std::printf("\nbornCriteriaForClass: a synthetic mechanically UNSTABLE cubic tensor\n");
    // C11 < C12 violates the very first cubic criterion outright -- an
    // artificial but unambiguous instability, independent of any real
    // material's numbers.
    const Matrix6x6 c = cubicTensor(50.0, 80.0, 40.0);
    const auto crit = bornCriteriaForClass(c, CrystalClass::Cubic);
    check(!crit[0].satisfied, "C11 - C12 > 0 correctly FAILS when C12 > C11");
    const auto eig = symmetricEigenvalues6x6(c);
    bool anyNegative = false;
    for (double e : eig)
        anyNegative = anyNegative || e < 0.0;
    check(anyNegative, "the general eigenvalue criterion agrees: at least one eigenvalue is negative");
}

void testBornCriteriaHexagonalAndOrthorhombic()
{
    std::printf("\nbornCriteriaForClass: hexagonal and orthorhombic closed forms\n");
    // Graphite-like hexagonal tensor (illustrative, hand-picked to satisfy
    // every inequality comfortably): C11=C22, C33, C12, C13, C44.
    Matrix6x6 hex{};
    hex[0][0] = hex[1][1] = 1060.0;
    hex[2][2] = 36.5;
    hex[0][1] = hex[1][0] = 180.0;
    hex[0][2] = hex[2][0] = hex[1][2] = hex[2][1] = 15.0;
    hex[3][3] = hex[4][4] = 0.3;
    hex[5][5] = (hex[0][0] - hex[0][1]) / 2.0; // C66 = (C11-C12)/2 for hexagonal
    const auto critHex = bornCriteriaForClass(hex, CrystalClass::Hexagonal);
    check(critHex.size() == 3, "hexagonal reports exactly 3 named criteria");
    bool hexStable = true;
    for (const auto& cr : critHex)
        hexStable = hexStable && cr.satisfied;
    check(hexStable, "the hand-built hexagonal tensor satisfies its closed-form criterion");

    // A simple, clearly-stable synthetic orthorhombic tensor.
    Matrix6x6 ortho{};
    ortho[0][0] = 200.0;
    ortho[1][1] = 180.0;
    ortho[2][2] = 160.0;
    ortho[0][1] = ortho[1][0] = 60.0;
    ortho[0][2] = ortho[2][0] = 50.0;
    ortho[1][2] = ortho[2][1] = 40.0;
    ortho[3][3] = 70.0;
    ortho[4][4] = 65.0;
    ortho[5][5] = 60.0;
    const auto critOrtho = bornCriteriaForClass(ortho, CrystalClass::Orthorhombic);
    check(critOrtho.size() == 6, "orthorhombic reports exactly 6 named criteria");
    bool orthoStable = true;
    for (const auto& cr : critOrtho)
        orthoStable = orthoStable && cr.satisfied;
    check(orthoStable, "the hand-built orthorhombic tensor satisfies every closed-form criterion");

    check(bornCriteriaForClass(ortho, CrystalClass::Other).empty(),
        "CrystalClass::Other returns no closed-form criteria (general eigenvalue check only)");
}

void testClassifyPointGroup()
{
    std::printf("\nclassifyPointGroup: spglib symbol -> Laue class\n");
    check(classifyPointGroup("m-3m") == CrystalClass::Cubic, "m-3m (Cu, Si) -> Cubic");
    check(classifyPointGroup("23") == CrystalClass::Cubic, "23 -> Cubic");
    check(classifyPointGroup("6/mmm") == CrystalClass::Hexagonal, "6/mmm -> Hexagonal");
    check(classifyPointGroup("-6m2") == CrystalClass::Hexagonal,
        "-6m2 (2H-MoS2's point group) -> Hexagonal");
    check(classifyPointGroup("4/mmm") == CrystalClass::Tetragonal, "4/mmm -> Tetragonal");
    check(classifyPointGroup("-3m") == CrystalClass::Trigonal, "-3m -> Trigonal");
    check(classifyPointGroup("mmm") == CrystalClass::Orthorhombic, "mmm -> Orthorhombic");
    check(classifyPointGroup("2/m") == CrystalClass::Other, "2/m (monoclinic) -> Other");
    check(classifyPointGroup("") == CrystalClass::Other, "empty string -> Other");
    check(classifyPointGroup("nonsense") == CrystalClass::Other, "unrecognised symbol -> Other");
}

void testSymmetrizeElasticTensorCleansNoise()
{
    std::printf("\nsymmetrizeElasticTensor: a single C4z rotation forces C11 == C22\n");
    // 90-degree rotation about z: x -> y, y -> -x, z -> z.
    const Matrix3 c4z{{{0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}}};
    Matrix6x6 raw{};
    raw[0][0] = 200.0; // C11
    raw[1][1] = 220.0; // C22 -- deliberately different from C11
    raw[2][2] = 150.0; // C33 -- untouched by a z-axis rotation, stays as-is
    raw[0][1] = raw[1][0] = 70.0;
    raw[3][3] = 60.0; // C44
    raw[4][4] = 64.0; // C55 -- deliberately different from C44

    const Matrix6x6 sym = symmetrizeElasticTensor(raw, {Matrix3{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, c4z});
    checkClose(sym[0][0], sym[1][1], 1e-9, "C11 and C22 are forced equal by the C4z average");
    checkClose(sym[0][0], (200.0 + 220.0) / 2.0, 1e-9,
        "the averaged C11 is exactly the mean of the two original diagonal entries");
    checkClose(sym[3][3], sym[4][4], 1e-9, "C44 and C55 are forced equal too (they mix under C4z)");
    checkClose(sym[2][2], 150.0, 1e-9, "C33 is untouched by a rotation about its own axis");

    const Matrix6x6 identityOnly
        = symmetrizeElasticTensor(raw, {Matrix3{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}});
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j)
            checkClose(identityOnly[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)],
                raw[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)], 1e-12,
                "identity-only symmetrization is a no-op at [" + std::to_string(i) + "]["
                    + std::to_string(j) + "]");

    const Matrix6x6 emptyOps = symmetrizeElasticTensor(raw, {});
    checkClose(emptyOps[0][0], raw[0][0], 1e-12, "an empty op list also leaves the tensor untouched");
}

void testElasticModuli2DGraphenelike()
{
    std::printf("\ncomputeElasticModuli2D: graphene-like C11=C22=350, C12=60 N/m\n");
    // Hand-picked to land near graphene's literature values (Young's ~340
    // N/m, Poisson ~0.16-0.19 -- see docs for the literature citation the
    // task asked to verify rather than trust from memory).
    const double c11 = 350.0, c22 = 350.0, c12 = 60.0;
    const auto m = computeElasticModuli2D(c11, c22, c12);
    const double expectedE = (c11 * c22 - c12 * c12) / c22;
    checkClose(m.youngXNPerM, expectedE, 1e-9, "Ex matches (C11*C22-C12^2)/C22 exactly");
    checkClose(m.youngYNPerM, expectedE, 1e-9, "Ey == Ex for the isotropic (C11==C22) case");
    checkClose(m.poissonXY, c12 / c22, 1e-9, "nu_xy matches C12/C22 exactly");
    checkClose(m.poissonYX, c12 / c11, 1e-9, "nu_yx matches C12/C11 exactly");
    checkClose(m.poissonXY, m.poissonYX, 1e-9, "nu_xy == nu_yx for the isotropic case");
    checkClose(m.youngXNPerM * m.poissonYX, m.youngYNPerM * m.poissonXY, 1e-9,
        "reciprocity Ex*nu_yx == Ey*nu_xy holds exactly, isotropic or not");
    checkClose(m.layerModulusNPerM, (c11 + c22 + 2.0 * c12) / 4.0, 1e-9,
        "layer modulus matches (C11+C22+2*C12)/4 exactly");
    check(m.youngXNPerM > 300.0 && m.youngXNPerM < 380.0,
        "the resulting Young's modulus lands near graphene's literature ~340 N/m");
    check(m.poissonXY > 0.10 && m.poissonXY < 0.25,
        "the resulting Poisson ratio lands near graphene's literature ~0.16-0.19");

    const auto stability = bornStability2D(c11, c22, c12, 145.0);
    check(stability.stable, "the graphene-like 2D tensor is Born-stable");
    checkClose(stability.positiveDefinite.value, c11 * c22 - c12 * c12, 1e-9,
        "the 2D positive-definiteness criterion value matches C11*C22-C12^2");

    const auto unstable = bornStability2D(10.0, 10.0, 50.0, 5.0);
    check(!unstable.stable, "a synthetic C12 > sqrt(C11*C22) case is correctly flagged unstable");
}

} // namespace

int main()
{
    std::printf("ElasticTensor - Voigt assembly, Born stability, VRH moduli, "
                "2D quantities, symmetrization\n");
    testQuadraticCurvatureExactParabola();
    testCrossCurvatureExactSaddle();
    testSymmetricEigenvalues6x6();
    testBornCriteriaCubicCopper();
    testBornCriteriaCubicSilicon();
    testBornCriteriaDetectsInstability();
    testBornCriteriaHexagonalAndOrthorhombic();
    testClassifyPointGroup();
    testSymmetrizeElasticTensorCleansNoise();
    testElasticModuli2DGraphenelike();

    std::printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
