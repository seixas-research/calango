// Berry-phase quantities, checked against models whose answers are integers.
//
// Quantisation is the ideal test: a Chern number is 0 or +-1 and nothing else,
// so a formula with a wrong sign, a wrong factor of 2pi or a gauge leak cannot
// land on the right value by accident the way a continuous quantity can.
//
//   Qi-Wu-Zhang two-band model
//       H(k) = sin(kx) sx + sin(ky) sy + (m + cos kx + cos ky) sz
//   is the cleanest lattice Chern insulator: |C| = 1 for |m| < 2, C = 0 for
//   |m| > 2, and C changes sign with m. Checked three ways — the BZ integral
//   of the curvature, the winding of the hybrid Wannier centres, and the
//   quantised Hall conductance in units of e^2/h — which are three different
//   code paths that must agree.
//
//   SSH chain
//       H(k) = (v + w cos k) sx + w sin k sy
//   has a Zak phase that differs by exactly pi between its two phases. The
//   ABSOLUTE Zak phase depends on where the orbitals are placed, so the
//   difference is what is asserted; the quantisation of each is asserted too,
//   since inversion symmetry forces both onto 0 or pi.

#include "core/BerryPhase.hpp"
#include "core/WannierHamiltonian.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using calango::core::BerryPhase;
using calango::core::WannierHamiltonian;

namespace {

int failures = 0;
constexpr double kPi = 3.14159265358979323846;

void check(bool ok, const std::string& what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

void checkClose(double got, double want, double tol, const std::string& what)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  %-4s %s (got %.6g, want %.6g, tol %g)\n", ok ? "ok" : "FAIL",
                what.c_str(), got, want, tol);
    if (!ok)
        ++failures;
}

/// Build a 2x2 block from Pauli coefficients: c0*I + cx*sx + cy*sy + cz*sz.
WannierHamiltonian::HoppingBlock pauliBlock(std::array<int, 3> lattice,
                                            double c0, double cx, double cy,
                                            double cz)
{
    WannierHamiltonian::HoppingBlock block;
    block.lattice = lattice;
    // Row-major 2x2. sx = [[0,1],[1,0]], sy = [[0,-i],[i,0]], sz = [[1,0],[0,-1]]
    block.matrix = {c0 + cz, cx, cx, c0 - cz};
    block.imaginary = {0.0, -cy, cy, 0.0};
    return block;
}

/// Qi-Wu-Zhang model on a square lattice of side a, with a thick third axis so
/// the cell has a well-defined volume.
WannierHamiltonian qwz(double m, double a = 1.0)
{
    std::vector<WannierHamiltonian::HoppingBlock> hoppings;
    // On site: m sz.
    hoppings.push_back(pauliBlock({0, 0, 0}, 0.0, 0.0, 0.0, m));
    // +x: -i/2 sx + 1/2 sz ; -x: +i/2 sx + 1/2 sz
    // Written through the Pauli helper as (cx, cy, cz) with cx imaginary is
    // not possible, so the two blocks are given explicitly.
    {
        WannierHamiltonian::HoppingBlock px;
        px.lattice = {1, 0, 0};
        px.matrix = {0.5, 0.0, 0.0, -0.5};
        px.imaginary = {0.0, -0.5, -0.5, 0.0};
        hoppings.push_back(px);
        WannierHamiltonian::HoppingBlock mx;
        mx.lattice = {-1, 0, 0};
        mx.matrix = {0.5, 0.0, 0.0, -0.5};
        mx.imaginary = {0.0, 0.5, 0.5, 0.0};
        hoppings.push_back(mx);
    }
    // +y: -i/2 sy + 1/2 sz = [[1/2, -1/2], [1/2, -1/2]] (real)
    {
        WannierHamiltonian::HoppingBlock py;
        py.lattice = {0, 1, 0};
        py.matrix = {0.5, -0.5, 0.5, -0.5};
        hoppings.push_back(py);
        WannierHamiltonian::HoppingBlock my;
        my.lattice = {0, -1, 0};
        my.matrix = {0.5, 0.5, -0.5, -0.5};
        hoppings.push_back(my);
    }
    return WannierHamiltonian(
        2, {{{a, 0.0, 0.0}, {0.0, a, 0.0}, {0.0, 0.0, 10.0}}},
        std::move(hoppings));
}

/// SSH chain: intra-cell v, inter-cell w.
WannierHamiltonian ssh(double v, double w, double a = 1.0)
{
    std::vector<WannierHamiltonian::HoppingBlock> hoppings;
    hoppings.push_back(pauliBlock({0, 0, 0}, 0.0, v, 0.0, 0.0));
    {
        WannierHamiltonian::HoppingBlock plus;
        plus.lattice = {1, 0, 0};
        plus.matrix = {0.0, 0.0, w, 0.0};
        hoppings.push_back(plus);
        WannierHamiltonian::HoppingBlock minus;
        minus.lattice = {-1, 0, 0};
        minus.matrix = {0.0, w, 0.0, 0.0};
        hoppings.push_back(minus);
    }
    return WannierHamiltonian(
        2, {{{a, 0.0, 0.0}, {0.0, 10.0, 0.0}, {0.0, 0.0, 10.0}}},
        std::move(hoppings));
}

BerryPhase::Options lowerBandOnly(std::array<int, 3> mesh)
{
    BerryPhase::Options options;
    options.occupiedBands = {0}; // the lower of the two bands
    options.kmesh = mesh;
    options.loopPoints = 64;
    return options;
}

// ---------------------------------------------------------------------------

void testQwzGapAndModel()
{
    std::printf("Qi-Wu-Zhang model is built correctly:\n");
    const auto model = qwz(1.0);
    // At Gamma: H = (m + 2) sz, so the gap is 2|m+2| = 6.
    const auto gamma = model.bands({0.0, 0.0, 0.0}, false);
    checkClose(gamma.energies[1] - gamma.energies[0], 6.0, 1e-12,
               "the gap at Gamma is 2|m+2|");
    // At M = (1/2, 1/2): H = (m - 2) sz, gap 2|m-2| = 2.
    const auto mPoint = model.bands({0.5, 0.5, 0.0}, false);
    checkClose(mPoint.energies[1] - mPoint.energies[0], 2.0, 1e-12,
               "and 2|m-2| at M");
    // Gapped everywhere for |m| < 2, which is what makes the Chern number
    // well defined.
    double smallest = 1e9;
    for (int i = 0; i < 24; ++i)
        for (int j = 0; j < 24; ++j) {
            const auto b = model.bands({i / 24.0, j / 24.0, 0.0}, false);
            smallest = std::min(smallest, b.energies[1] - b.energies[0]);
        }
    check(smallest > 0.1, "and the band structure is gapped across the zone");
}

void testChernNumberQuantisation()
{
    std::printf("Chern number from the Berry-curvature integral:\n");
    struct Case {
        double m;
        double expected;
        const char* what;
    };
    const Case cases[] = {
        {1.0, -1.0, "0 < m < 2 gives |C| = 1"},
        {-1.0, 1.0, "and m -> -m flips its sign"},
        {3.0, 0.0, "|m| > 2 is trivial: C = 0"},
        {-3.0, 0.0, "on both sides"},
    };

    double firstSign = 0.0;
    for (const auto& item : cases) {
        const BerryPhase berry(qwz(item.m), lowerBandOnly({64, 64, 1}));
        const auto hall = berry.anomalousHall(0, 1);
        // The SIGN convention of C depends on the orientation chosen for the
        // curvature; what is physical is that |C| = 1 in the topological
        // phases, 0 in the trivial ones, and that it reverses with m. So the
        // magnitude is asserted absolutely and the sign relatively.
        checkClose(std::abs(hall.chernNumber), std::abs(item.expected), 0.02,
                   item.what);
        if (item.m == 1.0)
            firstSign = hall.chernNumber;
        if (item.m == -1.0)
            check(hall.chernNumber * firstSign < 0.0,
                  "  and the two topological phases have opposite sign");
        std::printf("       m = %+.1f  C = %+.4f\n", item.m, hall.chernNumber);
    }
}

void testHallConductanceQuantum()
{
    std::printf("Anomalous Hall conductance in units of e^2/h:\n");
    const BerryPhase berry(qwz(1.0), lowerBandOnly({64, 64, 1}));
    const auto hall = berry.anomalousHall(0, 1);
    checkClose(std::abs(hall.sigmaInConductanceQuanta), 1.0, 0.02,
               "a Chern insulator carries exactly one conductance quantum");
    check(hall.sigmaSI != 0.0, "and the SI conductivity is reported too");

    // Trivial phase: no Hall response at all.
    const BerryPhase trivial(qwz(3.0), lowerBandOnly({64, 64, 1}));
    checkClose(std::abs(trivial.anomalousHall(0, 1).sigmaInConductanceQuanta),
               0.0, 0.02, "and a trivial insulator carries none");
}

void testWannierCentreWinding()
{
    std::printf("Hybrid Wannier centre flow — the same C, a different path:\n");
    // The Chern number is also the winding of the summed Wilson-loop phases as
    // the transverse momentum sweeps a full period. Computing it this way
    // shares no code with the curvature integral above beyond the Hamiltonian,
    // so agreement between them is a real cross-check rather than a tautology.
    const BerryPhase topological(qwz(1.0), lowerBandOnly({48, 48, 1}));
    const auto flow = topological.wannierCentreFlow(/*loopAxis=*/1,
                                                    /*transverseAxis=*/0, 96);
    checkClose(std::abs(flow.winding), 1.0, 0.05,
               "the centres wind exactly once in the topological phase");

    const BerryPhase trivial(qwz(3.0), lowerBandOnly({48, 48, 1}));
    const auto flat = trivial.wannierCentreFlow(1, 0, 96);
    checkClose(std::abs(flat.winding), 0.0, 0.05,
               "and do not wind at all in the trivial one");
    std::printf("       winding: topological %+.4f, trivial %+.4f\n",
                flow.winding, flat.winding);
}

void testSshZakPhase()
{
    std::printf("SSH Zak phase, quantised by inversion symmetry:\n");
    BerryPhase::Options options;
    options.occupiedBands = {0};
    options.loopPoints = 256;

    const BerryPhase trivial(ssh(1.0, 0.3), options);
    const BerryPhase topological(ssh(0.3, 1.0), options);
    const double zakTrivial =
        trivial.wilsonLoopAlong(0, {0.0, 0.0, 0.0}).berryPhase;
    const double zakTopological =
        topological.wilsonLoopAlong(0, {0.0, 0.0, 0.0}).berryPhase;

    // Each is 0 or pi modulo 2pi — inversion symmetry admits nothing else.
    const auto quantised = [](double phase) {
        const double folded = std::abs(std::fmod(std::abs(phase), 2.0 * kPi));
        return std::min(folded, std::abs(folded - kPi)) < 1e-6
            || std::abs(folded - 2.0 * kPi) < 1e-6;
    };
    check(quantised(zakTrivial), "the trivial phase is quantised");
    check(quantised(zakTopological), "and so is the topological one");

    // The DIFFERENCE is pi, and that is convention independent — the absolute
    // value depends on where the two orbitals are placed inside the cell.
    const double difference = std::abs(zakTopological - zakTrivial);
    checkClose(std::min(difference, 2.0 * kPi - difference), kPi, 1e-5,
               "and they differ by exactly pi");
    std::printf("       Zak: trivial %+.6f, topological %+.6f rad\n",
                zakTrivial, zakTopological);
}

void testGaugeInvariance()
{
    std::printf("Gauge covariance of the Wilson loop:\n");
    // The loop must not depend on how many points discretise it, beyond the
    // discretisation error itself: a gauge leak would show up as a phase that
    // drifts with the sampling.
    BerryPhase::Options coarse;
    coarse.occupiedBands = {0};
    coarse.loopPoints = 32;
    BerryPhase::Options fine = coarse;
    fine.loopPoints = 512;

    const double a = BerryPhase(ssh(0.3, 1.0), coarse)
                         .wilsonLoopAlong(0, {0.0, 0.0, 0.0})
                         .berryPhase;
    const double b = BerryPhase(ssh(0.3, 1.0), fine)
                         .wilsonLoopAlong(0, {0.0, 0.0, 0.0})
                         .berryPhase;
    checkClose(a, b, 1e-6,
               "32 and 512 loop points agree — no phase leaks in per step");

    // Curvature is a k-local quantity and must be smooth away from the gap
    // closing; sampling it twice at the same point must give the same number
    // even though the eigensolver is free to return different phases.
    const auto model = qwz(1.0);
    const BerryPhase berry(model, lowerBandOnly({16, 16, 1}));
    const double first = berry.bandCurvature({0.21, 0.37, 0.0}, 0, 0, 1);
    const double again = berry.bandCurvature({0.21, 0.37, 0.0}, 0, 0, 1);
    checkClose(first, again, 0.0, "curvature is reproducible at a point");
    // Antisymmetry in the Cartesian indices: Omega_xy = -Omega_yx, an identity
    // of the definition.
    checkClose(berry.bandCurvature({0.21, 0.37, 0.0}, 0, 1, 0), -first, 1e-12,
               "and antisymmetric: Omega_yx = -Omega_xy");
}

void testPolarization()
{
    std::printf("Electric polarization from the Berry phase:\n");
    BerryPhase::Options options;
    options.occupiedBands = {0};
    options.loopPoints = 128;
    const BerryPhase berry(ssh(0.3, 1.0), options);
    const auto p = berry.polarization(0, /*transverseSamples=*/2);

    check(p.quantumSI > 0.0, "the polarization quantum is reported");
    // P is defined modulo the quantum, so the meaningful assertion is that the
    // reported value lies inside one branch of it.
    check(std::abs(p.siValue) <= p.quantumSI + 1e-12,
          "and the value lies within one quantum, as a branch of P must");
    checkClose(p.dipolePerCell, p.phaseRadians / (2.0 * kPi) * 1.0, 1e-12,
               "dipole per cell follows phase/(2 pi) times the lattice vector");
}

void testCurvatureMap()
{
    std::printf("Curvature map for the colour plot:\n");
    const BerryPhase berry(qwz(1.0), lowerBandOnly({16, 16, 1}));
    const auto map = berry.curvaturePlane(0, 1, 24, 24, 0.0, 0, 1);
    check(map.values.size() == 24 && map.values[0].size() == 24,
          "the map is on the requested grid");
    check(map.maximum > map.minimum, "and spans a real range");

    // In the topological phase the curvature is concentrated near the smallest
    // gap, which for m = 1 is at M = (1/2, 1/2). Its magnitude there must
    // exceed the value at Gamma, where the gap is three times larger.
    const double atM = std::abs(berry.totalCurvature({0.5, 0.5, 0.0}, 0, 1));
    const double atGamma = std::abs(berry.totalCurvature({0.0, 0.0, 0.0}, 0, 1));
    check(atM > atGamma,
          "and peaks where the gap is smallest, as the Kubo form requires");
    std::printf("       |Omega| at M = %.4g, at Gamma = %.4g A^2\n", atM,
                atGamma);
}

} // namespace

int main()
{
    std::printf("BerryPhase - Wilson loops, curvature, Chern numbers\n\n");
    testQwzGapAndModel();
    testChernNumberQuantisation();
    testHallConductanceQuantum();
    testWannierCentreWinding();
    testSshZakPhase();
    testGaugeInvariance();
    testPolarization();
    testCurvatureMap();

    std::printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
