// Dislocation builder test.
//
// A dislocation is defined by ONE property: carry a closed circuit around the
// line and the displacement fails to close by exactly the Burgers vector.
// Everything else — how the field decays, which components are non-zero,
// whether the cell survives — follows from that or is bookkeeping around it.
// So that is what is measured here, from the returned field and the returned
// coordinates, rather than from the builder's own report.
//
// The check that matters most is the last kind: the anisotropic (Stroh)
// solution is a sextic eigenvalue problem with a complex 6x6 boundary
// condition, and a sign error anywhere in it produces a smooth, plausible,
// completely wrong displacement field. It is pinned by feeding it ISOTROPIC
// elastic constants and demanding it reproduce the textbook closed form it
// must reduce to — a test the implementation cannot pass by accident.
//
// GUI-free, Python-free.

#include "core/DislocationBuilder.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using namespace calango::core;

namespace {

int failures = 0;
constexpr double kPi = std::numbers::pi;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

void checkClose(double actual, double expected, double tolerance,
                const std::string& what)
{
    const bool ok = std::abs(actual - expected) <= tolerance;
    std::printf("  %s %s  (got %.6f, expected %.6f)\n", ok ? "ok  " : "FAIL",
                what.c_str(), actual, expected);
    if (!ok)
        ++failures;
}

/// A simple-cubic block, nx x ny x nz cells of side `a`. Simple cubic rather
/// than fcc on purpose: the nearest-neighbour distance is exactly `a`, so
/// "did the core fuse two atoms" has an unambiguous answer.
Structure cubicBlock(int nx, int ny, int nz, double a)
{
    Structure s;
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            for (int k = 0; k < nz; ++k) {
                Atom atom;
                atom.atomicNumber = 13; // Al, so the symbols read sensibly
                atom.position = {i * a, j * a, k * a};
                s.addAtom(atom);
            }
    s.setCell(UnitCell({nx * a, 0, 0}, {0, ny * a, 0}, {0, 0, nz * a},
                       {true, true, true}));
    return s;
}

/// Isotropic Voigt tensor and the Poisson ratio that belongs to it.
/// lambda = C12, mu = C44 = (C11 - C12)/2, so nu = C12 / (C11 + C12).
std::array<std::array<double, 6>, 6> isotropicVoigt(double c11, double c12)
{
    return DislocationBuilder::elasticTensor(
        DislocationBuilder::ElasticSymmetry::Isotropic, c11, c12, 0.0, 0.0, 0.0);
}

double isotropicPoisson(double c11, double c12) { return c12 / (c11 + c12); }

void testScrewField()
{
    std::printf("screw displacement field\n");
    const double b = 2.5;

    // The defining property: one turn around the line, one Burgers vector.
    // Sampled just above and just below the cut on the negative x1 axis.
    const double above = DislocationBuilder::screwDisplacement(-5.0, 1e-9, b).z;
    const double below = DislocationBuilder::screwDisplacement(-5.0, -1e-9, b).z;
    checkClose(above - below, b, 1e-6,
               "the circuit closes short by exactly b");

    // Anti-plane: nothing moves in the plane normal to the line.
    const Vec3 u = DislocationBuilder::screwDisplacement(3.0, 4.0, b);
    check(std::abs(u.x) < 1e-15 && std::abs(u.y) < 1e-15,
          "and only along the line — a screw has no in-plane displacement");

    // Scale-free: u depends on the angle alone, so doubling the radius along
    // a ray changes nothing. This is what makes a screw's strain go as 1/r.
    checkClose(DislocationBuilder::screwDisplacement(6.0, 8.0, b).z, u.z, 1e-12,
               "the field is a function of the angle only");

    // Antisymmetric about the glide plane, as the winding demands.
    checkClose(DislocationBuilder::screwDisplacement(3.0, -4.0, b).z, -u.z, 1e-12,
               "and antisymmetric across x2 = 0");
}

void testEdgeField()
{
    std::printf("edge displacement field\n");
    const double b = 2.5;
    const double nu = 0.33;

    const double above = DislocationBuilder::edgeDisplacement(-5.0, 1e-9, b, nu).x;
    const double below =
        DislocationBuilder::edgeDisplacement(-5.0, -1e-9, b, nu).x;
    checkClose(above - below, b, 1e-6,
               "the circuit closes short by exactly b, along the Burgers "
               "direction");

    // No cut on the far side: the crystal there is continuous.
    const double rightAbove =
        DislocationBuilder::edgeDisplacement(5.0, 1e-9, b, nu).x;
    const double rightBelow =
        DislocationBuilder::edgeDisplacement(5.0, -1e-9, b, nu).x;
    checkClose(rightAbove - rightBelow, 0.0, 1e-6,
               "and not at all on the other side of the line");

    // No displacement along the line: an edge dislocation is plane strain.
    check(std::abs(DislocationBuilder::edgeDisplacement(3.0, 4.0, b, nu).z)
              < 1e-15,
          "nothing moves along the line — an edge is plane strain");

    // The strain field decays as 1/r. Measured as a finite difference on a
    // ray, doubling the radius must halve the gradient.
    const auto gradient = [b, nu](double r) {
        const double h = 1e-4 * r;
        const Vec3 forward =
            DislocationBuilder::edgeDisplacement(r + h, 0.7 * r, b, nu);
        const Vec3 backward =
            DislocationBuilder::edgeDisplacement(r - h, 0.7 * r, b, nu);
        return (forward - backward).norm() / (2.0 * h);
    };
    checkClose(gradient(20.0) / gradient(40.0), 2.0, 0.02,
               "and the strain falls off as 1/r");
}

/// The Stroh machinery, handed elastic constants it must agree with the
/// textbook on. Compared as DIFFERENCES between two points: the two solutions
/// are equal only up to the arbitrary additive constant that the logarithm's
/// scale leaves free, and demanding they agree pointwise would be testing the
/// gauge rather than the physics.
void testAnisotropicReducesToIsotropic()
{
    std::printf("Stroh solution against the isotropic closed form\n");
    const double c11 = 250.0;
    const double c12 = 150.0;
    const auto voigt = isotropicVoigt(c11, c12);
    const double nu = isotropicPoisson(c11, c12);
    const double b = 2.5;

    struct Point {
        double x1, x2;
    };
    // Sampled away from the cut (x1 < 0, x2 -> 0) so both formulas are on the
    // same branch; a point pair straddling it would differ by exactly b, which
    // is correct behaviour and useless as an agreement test.
    const Point samples[] = {{4.0, 3.0},  {7.0, 1.0},  {2.0, 9.0},
                             {-3.0, 6.0}, {-8.0, 2.0}, {5.0, -4.0}};

    // -- Pure screw ---------------------------------------------------------
    {
        const Vec3 burgers{0.0, 0.0, b};
        double worst = 0.0;
        for (int i = 1; i < 6; ++i) {
            const Vec3 aniso =
                DislocationBuilder::anisotropicDisplacement(
                    samples[i].x1, samples[i].x2, burgers, voigt)
                - DislocationBuilder::anisotropicDisplacement(
                    samples[0].x1, samples[0].x2, burgers, voigt);
            const Vec3 iso =
                DislocationBuilder::screwDisplacement(samples[i].x1,
                                                      samples[i].x2, b)
                - DislocationBuilder::screwDisplacement(samples[0].x1,
                                                        samples[0].x2, b);
            worst = std::max(worst, (aniso - iso).norm());
        }
        checkClose(worst, 0.0, 2e-3,
                   "a pure screw in an isotropic tensor reproduces "
                   "b/2pi * atan2(x2, x1)");
    }

    // -- Pure edge ----------------------------------------------------------
    {
        const Vec3 burgers{b, 0.0, 0.0};
        double worst = 0.0;
        for (int i = 1; i < 6; ++i) {
            const Vec3 aniso =
                DislocationBuilder::anisotropicDisplacement(
                    samples[i].x1, samples[i].x2, burgers, voigt)
                - DislocationBuilder::anisotropicDisplacement(
                    samples[0].x1, samples[0].x2, burgers, voigt);
            const Vec3 iso =
                DislocationBuilder::edgeDisplacement(samples[i].x1,
                                                     samples[i].x2, b, nu)
                - DislocationBuilder::edgeDisplacement(samples[0].x1,
                                                       samples[0].x2, b, nu);
            worst = std::max(worst, (aniso - iso).norm());
        }
        checkClose(worst, 0.0, 5e-3,
                   "and a pure edge reproduces the Volterra edge field, "
                   "Poisson ratio and all");
    }

    // The Burgers circuit is the boundary condition the D coefficients were
    // solved from, so it is the one thing that must hold for ANY tensor.
    {
        const auto cubic = DislocationBuilder::elasticTensor(
            DislocationBuilder::ElasticSymmetry::Cubic, 168.4, 121.4, 75.4, 0.0,
            0.0);
        const Vec3 burgers{b, 0.0, 0.0};
        const Vec3 above = DislocationBuilder::anisotropicDisplacement(
            -6.0, 1e-7, burgers, cubic);
        const Vec3 below = DislocationBuilder::anisotropicDisplacement(
            -6.0, -1e-7, burgers, cubic);
        const Vec3 jump = above - below;
        checkClose(jump.x, b, 1e-4,
                   "a genuinely anisotropic (copper-like) tensor still closes "
                   "the circuit by exactly b");
        check(std::abs(jump.y) < 1e-4 && std::abs(jump.z) < 1e-4,
              "and by nothing at all in the other two directions");
    }

    // A mixed dislocation: the circuit must recover the full vector, not just
    // its length. This is the case the isotropic formulas cannot express at
    // all, and the reason the anisotropic type exists.
    {
        const auto cubic = DislocationBuilder::elasticTensor(
            DislocationBuilder::ElasticSymmetry::Cubic, 168.4, 121.4, 75.4, 0.0,
            0.0);
        const Vec3 burgers{1.5, 0.0, 2.0};
        const Vec3 jump =
            DislocationBuilder::anisotropicDisplacement(-6.0, 1e-7, burgers,
                                                        cubic)
            - DislocationBuilder::anisotropicDisplacement(-6.0, -1e-7, burgers,
                                                          cubic);
        checkClose(jump.x, 1.5, 1e-4, "a mixed dislocation recovers b_edge");
        checkClose(jump.z, 2.0, 1e-4, "and b_screw at the same time");
    }
}

void testSingleDislocations()
{
    std::printf("single dislocations in a crystal\n");
    const Structure block = cubicBlock(12, 12, 4, 3.0);

    DislocationBuilder::Params params;
    params.type = DislocationBuilder::Type::Screw;
    params.lineAxis = DislocationBuilder::Axis::Z;
    params.burgers = 3.0;
    // Off the atomic positions, or the line lands on an atom and the field is
    // evaluated at its own singularity.
    params.center = {0.5083, 0.5083};

    const auto screw = DislocationBuilder::generate(block, params);
    check(screw.structure.size() == block.size(),
          "a screw dislocation neither creates nor destroys an atom");
    check(screw.atomsRemoved == 0, "nothing is removed");
    checkClose(screw.netBurgers.z, 3.0, 1e-9,
               "the net Burgers vector runs along the line");
    check(std::abs(screw.netBurgers.x) < 1e-9
              && std::abs(screw.netBurgers.y) < 1e-9,
          "and has no edge component");
    const auto pbc = screw.structure.cell().pbc();
    check(pbc[2] && !pbc[0] && !pbc[1],
          "the cell is left periodic along the line only — a single "
          "dislocation cannot be periodic normal to it");
    check(screw.maxDisplacement <= 3.0 * 1.01,
          "no atom moves further than one Burgers vector");
    check(!screw.warnings.empty(),
          "and the loss of lateral periodicity is reported, not hidden");

    // Every atom must actually have moved by the analytic field. Re-derive it
    // independently and compare, so a builder that silently applied nothing
    // (or applied it twice) fails here.
    {
        const auto& before = block.atoms();
        const auto& after = screw.structure.atoms();
        const double c1 = 0.5083 * 33.0; // extent is 11 * 3.0 = 33 A
        const double c2 = c1;
        double worst = 0.0;
        for (std::size_t i = 0; i < before.size(); ++i) {
            const Vec3 expected = DislocationBuilder::screwDisplacement(
                before[i].position.x - c1, before[i].position.y - c2, 3.0);
            const Vec3 actual = after[i].position - before[i].position;
            worst = std::max(worst, (actual - expected).norm());
        }
        checkClose(worst, 0.0, 1e-9,
                   "and every atom sits exactly where the analytic field puts "
                   "it");
    }

    params.type = DislocationBuilder::Type::Edge;
    const auto edge = DislocationBuilder::generate(block, params);
    check(edge.structure.size() == block.size(),
          "an edge dislocation preserves the atom count too — it is the "
          "elastic field, not a missing plane");
    checkClose(edge.netBurgers.x, 3.0, 1e-9,
               "with the Burgers vector normal to the line");

    params.type = DislocationBuilder::Type::Anisotropic;
    params.symmetry = DislocationBuilder::ElasticSymmetry::Cubic;
    params.c11 = 168.4;
    params.c12 = 121.4;
    params.c44 = 75.4;
    params.burgersDirection = {1.0, 0.0, 0.0};
    const auto aniso = DislocationBuilder::generate(block, params);
    check(aniso.structure.size() == block.size(),
          "so does the anisotropic construction");
    bool finite = true;
    for (const Atom& atom : aniso.structure.atoms())
        finite = finite && std::isfinite(atom.position.x)
            && std::isfinite(atom.position.y) && std::isfinite(atom.position.z);
    check(finite, "and every atom lands at a finite position");
}

void testDipoles()
{
    std::printf("glide and climb dipoles\n");
    const Structure block = cubicBlock(16, 16, 3, 3.0);

    DislocationBuilder::Params params;
    params.type = DislocationBuilder::Type::Glide;
    params.lineAxis = DislocationBuilder::Axis::Z;
    params.burgers = 3.0;
    params.center = {0.5083, 0.5083};
    params.dipoleSeparation = 15.0;

    const auto glide = DislocationBuilder::generate(block, params);
    check(glide.structure.size() == block.size(),
          "glide is CONSERVATIVE: not one atom is created or destroyed");
    check(glide.atomsRemoved == 0, "nothing is removed");
    check(glide.cores.size() == 2, "two cores are inserted");
    check(glide.cores[0].second == -glide.cores[1].second,
          "of opposite sign");
    check(glide.netBurgers.norm() < 1e-9,
          "so the net Burgers vector is zero — which is what lets the cell "
          "stay periodic");
    const auto glidePbc = glide.structure.cell().pbc();
    check(glidePbc[0] && glidePbc[1] && glidePbc[2],
          "and all three directions stay periodic");

    // The compensating distortion is not cosmetic: without it the cell no
    // longer describes the atoms it contains. A sheared cell is the visible
    // sign that it was applied.
    const auto glideCell = glide.structure.cell().vectors();
    check(std::abs(glideCell[1].x) > 1e-6,
          "the cell is sheared to absorb the slip the dipole carries");
    // beta^p = |b| * d * L_line / V = 3 * 15 * 9 / (48*48*9) = b*d/(48*48),
    // and a2 = (0, 48, 0) is sheared by -beta^p * 48 along e1.
    checkClose(glideCell[1].x, -3.0 * 15.0 / (48.0 * 48.0) * 48.0, 1e-9,
               "by exactly b*d/A_cell, the average plastic distortion");

    // The slipped ribbon lies BETWEEN the cores and nowhere else. Measured on
    // the analytic field, either side of the glide plane.
    {
        const double b = 3.0;
        const double nu = params.poisson;
        const double d = 15.0;
        const auto jumpAt = [b, nu, d](double x1) {
            const auto total = [&](double x2) {
                return DislocationBuilder::edgeDisplacement(x1 + 0.5 * d, x2, b,
                                                            nu)
                    - DislocationBuilder::edgeDisplacement(x1 - 0.5 * d, x2, b,
                                                           nu);
            };
            return (total(1e-8) - total(-1e-8)).x;
        };
        checkClose(std::abs(jumpAt(0.0)), b, 1e-5,
                   "between the cores the crystal has slipped by exactly b");
        checkClose(jumpAt(-2.0 * d), 0.0, 1e-5,
                   "outside them it has not slipped at all");
        checkClose(jumpAt(2.0 * d), 0.0, 1e-5, "on either side");
    }

    params.type = DislocationBuilder::Type::Climb;
    const auto climb = DislocationBuilder::generate(block, params);
    check(climb.atomsRemoved > 0,
          "climb is NON-CONSERVATIVE: it removes material");
    check(climb.structure.size() == block.size()
              - static_cast<std::size_t>(climb.atomsRemoved),
          "and the structure is exactly that many atoms shorter");
    // The platelet is one Burgers vector thick and spans the two cores: with
    // a = b = 3 A that is one atomic plane, 5 rows tall (15 A / 3 A), 3 deep.
    check(climb.atomsRemoved == 5 * 3,
          "the vacancy platelet is one plane thick and spans the core "
          "separation");
    check(climb.netBurgers.norm() < 1e-9,
          "the two cores still cancel");
    const auto climbPbc = climb.structure.cell().pbc();
    check(climbPbc[0] && climbPbc[1] && climbPbc[2],
          "so climb stays fully periodic as well");
    const auto climbCell = climb.structure.cell().vectors();
    check(climbCell[0].x < 48.0,
          "and the cell contracts to account for the material that is gone");
}

void testRefusals()
{
    std::printf("refusals\n");
    const auto refuses = [](const Structure& s,
                            const DislocationBuilder::Params& p) {
        try {
            DislocationBuilder::generate(s, p);
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };

    const Structure block = cubicBlock(6, 6, 2, 3.0);
    DislocationBuilder::Params ok;
    ok.burgers = 3.0;

    Structure noCell = block;
    noCell.setCell(UnitCell{});
    check(refuses(noCell, ok), "a structure without a cell is refused");

    check(refuses(Structure{}, ok), "so is an empty one");

    DislocationBuilder::Params zeroBurgers = ok;
    zeroBurgers.burgers = 0.0;
    check(refuses(block, zeroBurgers),
          "a dislocation with no Burgers vector is not a dislocation");

    DislocationBuilder::Params incompressible = ok;
    incompressible.poisson = 0.5;
    check(refuses(block, incompressible),
          "nu = 0.5 is refused: the edge field is singular there");

    DislocationBuilder::Params tooWide = ok;
    tooWide.type = DislocationBuilder::Type::Glide;
    tooWide.dipoleSeparation = 1000.0;
    check(refuses(block, tooWide),
          "a dipole wider than the cell is refused rather than wrapped");
}

} // namespace

int main()
{
    testScrewField();
    testEdgeField();
    testAnisotropicReducesToIsotropic();
    testSingleDislocations();
    testDipoles();
    testRefusals();

    if (failures == 0) {
        std::printf("\nAll dislocation checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d dislocation check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
