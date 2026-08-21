// Native SCC-DFTB engine (src/dftb).
//
// Grown alongside the engine itself: this file currently covers the .skf
// Slater-Koster parser (SlaterKosterFile.hpp) against hand-written fixtures
// where every value is chosen by this test, so every parsed field can be
// checked exactly rather than merely "did not crash". Later pieces
// (Hamiltonian construction, SCC, forces, unfolding, optics) get their own
// sections here as they land — see FUTURE.md / git history for the order.

#include "core/Structure.hpp"
#include "dft/Constants.hpp"
#include "dft/LinearAlgebra.hpp"
#include "dftb/DftbBasis.hpp"
#include "dftb/DftbForces.hpp"
#include "dftb/DftbGamma.hpp"
#include "dftb/DftbHamiltonian.hpp"
#include "dftb/DftbOptics.hpp"
#include "dftb/DftbPdos.hpp"
#include "dftb/DftbScf.hpp"
#include "dftb/DftbUnfolding.hpp"
#include "dftb/SlaterKosterFile.hpp"
#include "dftb/SlaterKosterTable.hpp"
#include "dftb/SlaterKosterTransform.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using calango::core::Atom;
using calango::core::Structure;
using calango::core::UnitCell;
using calango::core::Vec3;
using calango::dftb::DftbBasis;
using calango::dftb::DftbHamiltonianBuilder;
using calango::dftb::SkChannel;
using calango::dftb::skBlock;
using calango::dftb::SlaterKosterFile;
using calango::dftb::SlaterKosterTable;
using calango::dftb::SpIntegrals;

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

bool near(double a, double b, double tol = 1.0e-9)
{
    return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b));
}

// A minimal, hand-written HOMONUCLEAR "C-C.skf" — 5 grid points (so 4 table
// rows, at r = 0.4, 0.8, 1.2, 1.6 bohr), a polynomial repulsive header
// (deliberately inert here, since the Spline block below takes priority —
// SlaterKosterTest exercises the polynomial path on the heteronuclear
// fixture instead) and a two-interval Spline block (one cubic, one quintic,
// per the format's own asymmetry).
//
// Table values follow value = base + row * 0.1 + col * 0.01 (H columns
// base 1.0, S columns base 2.0), a pattern chosen so each of the 20 columns
// is uniquely reconstructible and a column-order bug cannot hide.
const char* kHomonuclear = R"SKF(
0.4 5
-0.5 -0.3 -0.2 0.0 0.30 0.35 0.40 0.0 2.0 2.0
12.011 0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 2.5 0 0 0 0 0 0 0 0 0 0
1.00 1.01 1.02 1.03 1.04 1.05 1.06 1.07 1.08 1.09 2.00 2.01 2.02 2.03 2.04 2.05 2.06 2.07 2.08 2.09
1.10 1.11 1.12 1.13 1.14 1.15 1.16 1.17 1.18 1.19 2.10 2.11 2.12 2.13 2.14 2.15 2.16 2.17 2.18 2.19
1.20 1.21 1.22 1.23 1.24 1.25 1.26 1.27 1.28 1.29 2.20 2.21 2.22 2.23 2.24 2.25 2.26 2.27 2.28 2.29
1.30 1.31 1.32 1.33 1.34 1.35 1.36 1.37 1.38 1.39 2.30 2.31 2.32 2.33 2.34 2.35 2.36 2.37 2.38 2.39
Spline
2 2.0
1.5 0.2 0.05
0.4 1.0 1.0 -0.5 0.1 -0.02
1.0 2.0 0.5 -0.3 0.05 -0.01 0.002 -0.0005
)SKF";

// A minimal HETERONUCLEAR "C-N.skf" — no on-site line, no Spline block (so
// the polynomial repulsive of Eq. 1 is the one actually evaluated), 3 grid
// points (2 table rows).
const char* kHeteronuclear = R"SKF(
0.5 3
0.0 0.2 0.3 0.4 0.5 0.6 0.7 0.8 0.9 2.0 0 0 0 0 0 0 0 0 0 0
3.00 3.01 3.02 3.03 3.04 3.05 3.06 3.07 3.08 3.09 4.00 4.01 4.02 4.03 4.04 4.05 4.06 4.07 4.08 4.09
3.10 3.11 3.12 3.13 3.14 3.15 3.16 3.17 3.18 3.19 4.10 4.11 4.12 4.13 4.14 4.15 4.16 4.17 4.18 4.19
)SKF";

// The extended ('@'-prefixed, angular momentum up to f) header — not yet
// supported; must be refused cleanly, not misparsed.
const char* kExtendedStub = "@ extended header stub\n0.4 5\n";

} // namespace

int main()
{
    std::printf("Slater-Koster .skf parser:\n");
    {
        SlaterKosterFile sk;
        const auto outcome = calango::dftb::parseSlaterKosterFile(
            kHomonuclear, sk);
        check(outcome.ok(), "homonuclear fixture parses: " + outcome.message);
        check(near(sk.gridDistanceBohr, 0.4), "gridDist read correctly");
        check(sk.gridPointCount == 5, "nGridPoints read correctly");
        check(sk.homonuclear, "detected as homonuclear (10-field line 2)");
        check(sk.table.size() == 4,
              "table has nGridPoints - 1 = 4 rows, not 5");

        // On-site line order is Ed Ep Es SPE Ud Up Us fd fp fs — d BEFORE s.
        // AngularMomentum indexes 0=s,1=p,2=d.
        check(near(sk.onsite[2].energyHartree, -0.5), "Ed -> onsite[d]");
        check(near(sk.onsite[1].energyHartree, -0.3), "Ep -> onsite[p]");
        check(near(sk.onsite[0].energyHartree, -0.2), "Es -> onsite[s]");
        check(near(sk.onsite[2].hubbardUHartree, 0.30), "Ud -> onsite[d].U");
        check(near(sk.onsite[1].hubbardUHartree, 0.35), "Up -> onsite[p].U");
        check(near(sk.onsite[0].hubbardUHartree, 0.40), "Us -> onsite[s].U");
        check(near(sk.onsite[2].occupation, 0.0), "fd -> onsite[d].occ");
        check(near(sk.onsite[1].occupation, 2.0), "fp -> onsite[p].occ");
        check(near(sk.onsite[0].occupation, 2.0), "fs -> onsite[s].occ");

        check(near(sk.massAmu, 12.011), "mass read correctly");
        check(near(sk.polyCoefficients[0], 0.1) && near(sk.polyCoefficients[7], 0.8),
              "polynomial coefficients c2..c9 read correctly, in order");
        check(near(sk.polyRcutBohr, 2.5), "polynomial rcut read correctly");

        // Column mapping: row 0 (r = 0.4 bohr), Hss0 is SkChannel::Sssigma,
        // the LAST H column (index 9); value = 1.0 + 0*0.1 + 9*0.01 = 1.09.
        check(near(sk.table[0][static_cast<int>(SkChannel::Sssigma)], 1.09),
              "Hss0 lands in the last H column, row 0");
        // Sss0 is the last S column (index 19); value = 2.0 + 0*0.1 + 9*0.01.
        check(near(sk.table[0][10 + static_cast<int>(SkChannel::Sssigma)], 2.09),
              "Sss0 lands in the last S column, row 0");
        // Hdd0 (Ddsigma) is H column 0; row 3, value = 1.0 + 3*0.1 + 0*0.01.
        check(near(sk.table[3][static_cast<int>(SkChannel::Ddsigma)], 1.30),
              "Hdd0 lands in H column 0, row 3");
        // Hpp0/Hpp1 (Ppsigma/Pppi) are H columns 5/6 — the pair the s,p
        // Hamiltonian actually reads.
        check(near(sk.table[1][static_cast<int>(SkChannel::Ppsigma)], 1.15),
              "Hpp0 (Ppsigma) lands in H column 5, row 1");
        check(near(sk.table[1][static_cast<int>(SkChannel::Pppi)], 1.16),
              "Hpp1 (Pppi) lands in H column 6, row 1");

        // Interpolation must reproduce a tabulated value exactly at a node.
        check(near(sk.integral(false, SkChannel::Ppsigma, 0.8), 1.15, 1.0e-8),
              "integral() reproduces the tabulated Hpp0 exactly at its node");
        check(near(sk.integral(true, SkChannel::Sssigma, 1.2), 2.29, 1.0e-8),
              "integral() reproduces the tabulated Sss0 exactly at its node");
        check(near(sk.integral(false, SkChannel::Ppsigma, 0.1), 0.0),
              "integral() is 0 below the first grid point");
        check(near(sk.integral(false, SkChannel::Ppsigma, 5.0), 0.0),
              "integral() is 0 beyond the last grid point");

        // Regression: a bond distance an ordinary floating-point epsilon
        // below gridDist itself — the FIRST tabulated point, r = 0.4 for
        // this fixture — from an Angstrom<->bohr round trip, or a file
        // written with limited decimal precision (found via a real
        // end-to-end calango-dftb-run, where a hand-written extxyz's
        // 6-decimal position was enough to trigger it), must still read the
        // tabulated boundary value, NOT silently fall through to 0 as
        // though the bond were genuinely shorter than the table.
        check(near(sk.integral(false, SkChannel::Ppsigma, 0.4 - 1.0e-9),
                   1.05, 1.0e-6),
              "a sub-nanometer excursion below gridDist itself still reads "
              "the boundary value (Ppsigma at the first grid point), not 0");
        check(near(sk.integral(false, SkChannel::Ppsigma, 0.4 - 1.0e-3), 0.0),
              "while a GENUINELY shorter distance (not just rounding noise) "
              "still correctly reads 0 — the tolerance is narrow, not a "
              "blanket clamp");

        // The analytic derivative must agree with a central finite
        // difference of the SAME function — a formula-independent check.
        {
            const double r = 1.0;
            const double h = 1.0e-5;
            const double fd = (sk.integral(false, SkChannel::Ppsigma, r + h)
                                - sk.integral(false, SkChannel::Ppsigma, r - h))
                / (2.0 * h);
            const double analytic =
                sk.integralDerivative(false, SkChannel::Ppsigma, r);
            check(std::fabs(fd - analytic) < 1.0e-4,
                  "integralDerivative() matches a central finite difference "
                  "(fd=" + std::to_string(fd)
                  + ", analytic=" + std::to_string(analytic) + ")");
        }

        // Spline repulsive.
        check(sk.hasSpline, "Spline block detected");
        check(sk.splineIntervals.size() == 2, "two spline intervals parsed");
        check(near(sk.splineCutoffBohr, 2.0), "spline cutoff parsed");
        check(near(sk.splineExpA1, 1.5) && near(sk.splineExpA2, 0.2)
                  && near(sk.splineExpA3, 0.05),
              "exponential a1 a2 a3 parsed");
        check(near(sk.splineIntervals[0].startBohr, 0.4)
                  && near(sk.splineIntervals[0].c[0], 1.0)
                  && near(sk.splineIntervals[0].c[3], -0.02),
              "first (cubic) spline interval parsed, c4/c5 unused");
        check(near(sk.splineIntervals[1].startBohr, 1.0)
                  && near(sk.splineIntervals[1].c[4], 0.002)
                  && near(sk.splineIntervals[1].c[5], -0.0005),
              "last (quintic) spline interval parsed, including c4/c5");

        // repulsiveEnergyHartree against the format's own Eq. 5-7, evaluated
        // in the test rather than hand-computed, so this checks the
        // parser+evaluator PAIR against the documented formula.
        const double r0 = 0.2; // below the first interval -> exponential
        const double expExpected =
            std::exp(-1.5 * r0 + 0.2) + 0.05;
        check(near(sk.repulsiveEnergyHartree(r0), expExpected, 1.0e-10),
              "repulsive energy uses the exponential form below the first "
              "spline interval");

        const double r1 = 0.7; // inside the first (cubic) interval
        const double dr1 = r1 - 0.4;
        const double cubicExpected =
            1.0 + (-0.5) * dr1 + 0.1 * dr1 * dr1 + (-0.02) * dr1 * dr1 * dr1;
        check(near(sk.repulsiveEnergyHartree(r1), cubicExpected, 1.0e-10),
              "repulsive energy uses the cubic form inside interval 1");

        const double r2 = 1.5; // inside the last (quintic) interval
        const double dr2 = r2 - 1.0;
        const double quinticExpected = 0.5 + (-0.3) * dr2 + 0.05 * dr2 * dr2
            + (-0.01) * dr2 * dr2 * dr2 + 0.002 * dr2 * dr2 * dr2 * dr2
            + (-0.0005) * dr2 * dr2 * dr2 * dr2 * dr2;
        check(near(sk.repulsiveEnergyHartree(r2), quinticExpected, 1.0e-10),
              "repulsive energy uses the quintic form on the last interval");

        check(near(sk.repulsiveEnergyHartree(2.5), 0.0),
              "repulsive energy is 0 beyond the spline cutoff");

        // Repulsive-derivative sanity against a finite difference of the
        // SAME piecewise function, across all three regimes.
        for (double r : {0.2, 0.7, 1.5}) {
            const double h = 1.0e-6;
            const double fd = (sk.repulsiveEnergyHartree(r + h)
                                - sk.repulsiveEnergyHartree(r - h)) / (2.0 * h);
            const double analytic = sk.repulsiveEnergyDerivativeHartree(r);
            check(std::fabs(fd - analytic) < 1.0e-3,
                  "repulsive derivative matches finite difference at r="
                  + std::to_string(r));
        }
    }

    {
        SlaterKosterFile sk;
        const auto outcome = calango::dftb::parseSlaterKosterFile(
            kHeteronuclear, sk);
        check(outcome.ok(),
              "heteronuclear fixture parses: " + outcome.message);
        check(!sk.homonuclear, "detected as heteronuclear (20-field line 2)");
        check(sk.table.size() == 2, "table has nGridPoints - 1 = 2 rows");
        check(near(sk.onsite[0].energyHartree, 0.0)
                  && near(sk.onsite[1].energyHartree, 0.0),
              "no on-site line for a heteronuclear file -> onsite stays "
              "default");
        check(near(sk.polyCoefficients[0], 0.2), "c2 read from line 2 "
              "directly (no on-site line shifts it down)");
        check(near(sk.polyRcutBohr, 2.0), "rcut read correctly");
        check(!sk.hasSpline, "no Spline block -> polynomial repulsive used");

        // Polynomial repulsive against Eq. 1, evaluated from the SAME
        // coefficients the test wrote into the fixture.
        const double r = 1.0;
        const double base = 2.0 - r; // rcut - r
        double expected = 0.0;
        const double coeffs[8] = {0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9};
        double power = base * base;
        for (double c : coeffs) {
            expected += c * power;
            power *= base;
        }
        check(near(sk.repulsiveEnergyHartree(r), expected, 1.0e-10),
              "polynomial repulsive matches Eq. 1 with the fixture's own "
              "coefficients");
        check(near(sk.repulsiveEnergyHartree(2.5), 0.0),
              "polynomial repulsive is 0 beyond rcut");
    }

    {
        SlaterKosterFile sk;
        const auto outcome =
            calango::dftb::parseSlaterKosterFile(kExtendedStub, sk);
        check(!outcome.ok(),
              "the extended ('@') header format is refused, not misparsed");
    }

    {
        SlaterKosterFile sk;
        const auto outcome =
            calango::dftb::parseSlaterKosterFile("not a valid file", sk);
        check(!outcome.ok(), "garbage input is refused with InvalidInput, "
              "not a crash or a silently zeroed table");
    }

    std::printf("\nSlater-Koster s,p angular transform:\n");
    {
        SpIntegrals sp;
        sp.ssSigma = -0.5;
        sp.spSigmaForward = 0.6;
        sp.spSigmaReverse = 0.6; // homonuclear special case (same file)
        sp.ppSigma = 1.2;
        sp.ppPi = -0.3;

        // Bond exactly along x: p_x is fully "sigma" (along the bond),
        // p_y/p_z are fully "pi" (perpendicular) — the textbook special
        // case that pins down the whole table's normalization and signs.
        {
            const auto b = skBlock(1.0, 0.0, 0.0, sp);
            check(near(b[0 * 4 + 0], sp.ssSigma), "E(s,s) = ssSigma");
            check(near(b[0 * 4 + 1], sp.spSigmaForward),
                  "E(s,px) = +spSigmaForward along x");
            check(near(b[1 * 4 + 0], -sp.spSigmaReverse),
                  "E(px,s) = -spSigmaReverse along x");
            check(near(b[0 * 4 + 2], 0.0) && near(b[0 * 4 + 3], 0.0),
                  "E(s,py) = E(s,pz) = 0 for a bond along x");
            check(near(b[1 * 4 + 1], sp.ppSigma),
                  "E(px,px) = ppSigma exactly (px is fully along the bond)");
            check(near(b[2 * 4 + 2], sp.ppPi) && near(b[3 * 4 + 3], sp.ppPi),
                  "E(py,py) = E(pz,pz) = ppPi exactly (perpendicular to the "
                  "bond)");
            check(near(b[1 * 4 + 2], 0.0) && near(b[1 * 4 + 3], 0.0)
                      && near(b[2 * 4 + 3], 0.0),
                  "every p-p off-diagonal term vanishes for a bond along a "
                  "Cartesian axis");
        }

        // An ARBITRARY in-plane bond (n = 0, the graphene-sheet case): the
        // pz orbital must decouple from s/px/py entirely, and E(pz,pz) must
        // equal ppPi exactly REGARDLESS of the in-plane angle — this is
        // the geometric fact the Dirac-cone milestone leans on, checked
        // here independently of any real .skf data.
        {
            const double theta = 0.37; // radians, an arbitrary in-plane angle
            const double l = std::cos(theta), m = std::sin(theta), n = 0.0;
            const auto b = skBlock(l, m, n, sp);
            check(near(b[3 * 4 + 3], sp.ppPi),
                  "E(pz,pz) = ppPi exactly for ANY in-plane bond direction");
            check(near(b[0 * 4 + 3], 0.0) && near(b[3 * 4 + 0], 0.0),
                  "s-pz decouples for an in-plane bond");
            check(near(b[1 * 4 + 3], 0.0) && near(b[3 * 4 + 1], 0.0)
                      && near(b[2 * 4 + 3], 0.0) && near(b[3 * 4 + 2], 0.0),
                  "px-pz and py-pz decouple for an in-plane bond");
        }

        // p-p is symmetric under swapping the two atoms' orbitals (both
        // read from the same file/direction); s-p is antisymmetric IN THE
        // HOMONUCLEAR SPECIAL CASE (spSigmaForward == spSigmaReverse here) —
        // both properties must hold for an arbitrary direction, not just the
        // axis-aligned special case above.
        {
            const double l = 0.2, m = -0.5, n = std::sqrt(1.0 - 0.04 - 0.25);
            const auto b = skBlock(l, m, n, sp);
            check(near(b[1 * 4 + 2], b[2 * 4 + 1])
                      && near(b[1 * 4 + 3], b[3 * 4 + 1])
                      && near(b[2 * 4 + 3], b[3 * 4 + 2]),
                  "p-p off-diagonal blocks are symmetric for a general "
                  "direction");
            check(near(b[0 * 4 + 1], -b[1 * 4 + 0])
                      && near(b[0 * 4 + 2], -b[2 * 4 + 0])
                      && near(b[0 * 4 + 3], -b[3 * 4 + 0]),
                  "s-p blocks are antisymmetric for a general direction "
                  "when spSigmaForward == spSigmaReverse (homonuclear)");
        }

        // HETERONUCLEAR case: forward and reverse sp-sigma genuinely differ
        // (different atomic-orbital shapes on each side), so the simple
        // antisymmetry above must NOT hold — this is the exact bug the
        // derivation comment in SlaterKosterTransform.hpp warns against,
        // caught here by checking its absence rather than its presence.
        {
            SpIntegrals hetero = sp;
            hetero.spSigmaForward = 0.6;
            hetero.spSigmaReverse = 0.9; // deliberately different
            const auto b = skBlock(1.0, 0.0, 0.0, hetero);
            check(near(b[0 * 4 + 1], 0.6), "E(s,px) uses spSigmaForward");
            check(near(b[1 * 4 + 0], -0.9), "E(px,s) uses spSigmaReverse, "
                  "NOT -spSigmaForward");
            check(!near(b[0 * 4 + 1], -b[1 * 4 + 0]),
                  "the heteronuclear block is genuinely NOT antisymmetric "
                  "when the two files disagree");
        }
    }

    std::printf("\nSlater-Koster parameter-set directory (SlaterKosterTable):\n");
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "calango_dftb_test_skdir";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);

        const auto writeFile = [&](const std::string& name,
                                    const std::string& content) {
            std::ofstream out(dir / name);
            out << content;
        };
        // One shared 2-row table body (values are not checked by this
        // section — SlaterKosterTest already covers column mapping); the
        // first H column is varied per file so C-H and H-C can be told
        // apart by content, not just by which map key returned them.
        const auto tableRows = [](double firstColumn) {
            std::ostringstream out;
            for (int row = 0; row < 2; ++row) {
                out << firstColumn + row;
                for (int col = 1; col < 20; ++col)
                    out << " 0.01";
                out << "\n";
            }
            return out.str();
        };
        writeFile("C-C.skf",
                  "0.5 3\n"
                  "0.0 -0.2 -0.4 0.0 0.0 0.30 0.35 0.0 2.0 2.0\n"
                  "12.011 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
                  + tableRows(9.0));
        writeFile("H-H.skf",
                  "0.5 3\n"
                  // fp = 0: hydrogen carries no p shell in this fixture.
                  "0.0 0.0 -0.5 0.0 0.0 0.0 0.40 0.0 0.0 1.0\n"
                  "1.008 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
                  + tableRows(8.0));
        writeFile("C-H.skf", "0.5 3\n"
                  "0 0 0 0 0 0 0 0 2.0 0 0 0 0 0 0 0 0 0 0 0\n"
                  + tableRows(1.0));
        writeFile("H-C.skf", "0.5 3\n"
                  "0 0 0 0 0 0 0 0 2.0 0 0 0 0 0 0 0 0 0 0 0\n"
                  + tableRows(7.0));

        SlaterKosterTable table;
        const auto outcome = table.load(dir.string(), {6, 1}); // C, H
        check(outcome.ok(), "loads a complete 2-element directory: "
              + outcome.message);
        check(table.missingPairs().empty(),
              "no missing pairs reported for a complete directory");
        check(table.pair(6, 6) != nullptr && table.pair(1, 1) != nullptr,
              "both homonuclear pairs loaded");
        check(table.pair(6, 1) != nullptr && table.pair(1, 6) != nullptr,
              "both heteronuclear orderings loaded");
        check(!near(table.pair(6, 1)->table[0][0],
                    table.pair(1, 6)->table[0][0]),
              "C-H.skf and H-C.skf are indexed as DISTINCT files, not "
              "aliases of one ordering");
        check(table.hasPShell(6), "carbon: fp > 0 in its own on-site line "
              "-> has a p shell");
        check(!table.hasPShell(1), "hydrogen: fp == 0 in this fixture -> "
              "no p shell");
        const auto* carbonOnsite = table.onsite(6);
        check(carbonOnsite != nullptr
                  && near((*carbonOnsite)[1].occupation, 2.0),
              "onsite() returns carbon's own on-site shell data");

        // Missing-pair reporting: request an element with no file present.
        SlaterKosterTable incomplete;
        const auto incompleteOutcome =
            incomplete.load(dir.string(), {6, 8}); // C, O — no O files here
        check(!incompleteOutcome.ok(),
              "a directory missing a required element's files is reported, "
              "not silently accepted");
        check(incomplete.missingPairs().size() >= 3,
              "missing O-O.skf, C-O.skf and O-C.skf are each reported "
              "individually (found "
              + std::to_string(incomplete.missingPairs().size()) + ")");

        fs::remove_all(dir, ec);
    }

    std::printf("\nHamiltonian construction — homonuclear H2 dimer levels:\n");
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "calango_dftb_test_h2dir";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);

        // gridDist = 1.0 bohr, 3 points -> rows at r = 1.0 and r = 2.0 bohr.
        // The test molecule sits at r = 1.0 EXACTLY (row 0), so
        // interpolation reproduces the tabulated Hss0/Sss0 exactly and the
        // eigenvalues can be checked against the textbook closed form with
        // no interpolation error to budget for.
        //
        // Es = -0.5, Hss0(1.0 bohr) = -0.3, Sss0(1.0 bohr) = 0.2. Every
        // other column is 0 (fp = 0, hydrogen carries no p shell here).
        // Column layout is Ddsigma..Sssigma (H, 10 cols) then the same 10
        // channels for S — Sssigma is column INDEX 9 (H) / 19 (S), the
        // LAST column of each half, not the 9th.
        std::ofstream out(dir / "H-H.skf");
        out << "1.0 3\n"
               "0.0 0.0 -0.5 0.0 0.0 0.0 0.0 0.0 0.0 1.0\n"
               "1.008 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
               "0 0 0 0 0 0 0 0 0 -0.3 0 0 0 0 0 0 0 0 0 0.2\n"
               "0 0 0 0 0 0 0 0 0  0.0 0 0 0 0 0 0 0 0 0 0.0\n";
        out.close();

        SlaterKosterTable table;
        const auto tableOutcome = table.load(dir.string(), {1});
        check(tableOutcome.ok(), "H2 fixture parameter set loads: "
              + tableOutcome.message);
        check(!table.hasPShell(1), "hydrogen has no p shell in this fixture");

        DftbBasis basis;
        const auto basisOutcome = DftbBasis::build({1, 1}, table, basis);
        check(basisOutcome.ok(), "basis builds for a 2-atom H2 molecule");
        check(basis.totalOrbitals == 2,
              "dimension is 2 (one s orbital per atom)");

        Structure molecule;
        molecule.addAtom(Atom{1, {0.0, 0.0, 0.0}});
        // r = 1.0 bohr = 1.0 / kBohrPerAngstrom angstrom.
        const double bondLengthA = 1.0 / calango::dft::kBohrPerAngstrom;
        molecule.addAtom(Atom{1, {bondLengthA, 0.0, 0.0}});

        DftbHamiltonianBuilder builder;
        const auto buildOutcome = builder.build(molecule, table, basis);
        check(buildOutcome.ok(), "Hamiltonian builds for the H2 molecule: "
              + buildOutcome.message);
        check(builder.pairs().size() == 2,
              "exactly 2 real-space pairs for a 2-atom molecule (i,j) and "
              "(j,i), no periodic images");

        std::vector<std::complex<double>> h, s;
        builder.blochMatrices({0.0, 0.0, 0.0}, h, s);

        // Hermiticity — checked numerically, at an arbitrary (here trivial,
        // since this is a molecule) k, per the task's own requirement.
        bool hermitian = true;
        for (int row = 0; row < 2 && hermitian; ++row)
            for (int col = 0; col < 2 && hermitian; ++col)
                if (std::abs(h[static_cast<std::size_t>(row * 2 + col)]
                             - std::conj(h[static_cast<std::size_t>(col * 2 + row)]))
                    > 1.0e-10)
                    hermitian = false;
        check(hermitian, "H(k) is Hermitian");
        check(near(s[0].real(), 1.0) && near(s[3].real(), 1.0),
              "S diagonal is 1 (on-site orthonormality)");
        check(near(s[1].real(), 0.2) && near(s[2].real(), 0.2),
              "S off-diagonal reproduces the tabulated Sss0 exactly");
        check(near(h[0].real(), -0.5) && near(h[3].real(), -0.5),
              "H diagonal reproduces Es exactly");
        check(near(h[1].real(), -0.3) && near(h[2].real(), -0.3),
              "H off-diagonal reproduces the tabulated Hss0 exactly");

        std::vector<double> eigenvalues;
        std::vector<std::complex<double>> eigenvectors;
        const auto eigenOutcome = calango::dft::linalg::solveGeneralizedHermitian(
            h, s, 2, eigenvalues, eigenvectors);
        check(eigenOutcome.ok(), "generalized eigenproblem solves: "
              + eigenOutcome.message);
        check(eigenvalues.size() == 2, "2 eigenvalues for a 2x2 problem");

        // Textbook homonuclear-dimer closed form (symmetric/antisymmetric
        // combinations diagonalize the 2x2 problem exactly):
        //   eps_sym     = (Es + Hss) / (1 + Sss)   (bonding)
        //   eps_antisym = (Es - Hss) / (1 - Sss)   (antibonding)
        const double es = -0.5, hss = -0.3, sss = 0.2;
        const double epsSym = (es + hss) / (1.0 + sss);
        const double epsAntisym = (es - hss) / (1.0 - sss);
        check(near(eigenvalues[0], epsSym, 1.0e-8)
                  && near(eigenvalues[1], epsAntisym, 1.0e-8),
              "eigenvalues match the closed-form dimer levels exactly "
              "(computed=" + std::to_string(eigenvalues[0]) + ", "
              + std::to_string(eigenvalues[1]) + "; expected="
              + std::to_string(epsSym) + ", "
              + std::to_string(epsAntisym) + ")");
        check(epsSym < epsAntisym,
              "the bonding combination is lower in energy than the "
              "antibonding one, as it must be for Hss < 0");

        fs::remove_all(dir, ec);
    }

    std::printf("\nPeriodic Bloch sums — graphene pz bands (Dirac point):\n");
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "calango_dftb_test_graphenedir";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);

        // A MINIMAL synthetic carbon-carbon parameter set — not a real
        // parameter set (see the module doc for why a real one is not
        // bundled) — isolating exactly the physics this test checks: a flat
        // sheet's pz-pz (ppPi) hopping decouples from every other channel by
        // symmetry (already checked in isolation above), so setting every
        // OTHER channel to 0 leaves a textbook nearest-neighbor pz
        // tight-binding model with a known, hand-derivable band structure —
        // while still going through the exact same parser, angular
        // transform and Bloch-sum code path a real .skf would.
        //
        // Constant Pppi = -0.1 Hartree out to 3.0 bohr (comfortably past the
        // 1.42 Angstrom = 2.684 bohr nearest-neighbor distance, comfortably
        // short of the 2.46 Angstrom = 4.648 bohr next-nearest one), zero
        // overlap (S off-diagonal = 0) so the eigenvalues of the 2x2 pz
        // block are exactly Ep +/- |offdiag| with no denominator to carry.
        {
            std::ofstream out(dir / "C-C.skf");
            out << "0.1 35\n"
                   "0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 2.0 2.0\n"
                   "12.011 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n";
            for (int row = 0; row < 34; ++row) {
                for (int col = 0; col < 20; ++col)
                    out << (col == 6 ? "-0.1" : "0") << (col == 19 ? "\n" : " ");
            }
        }

        SlaterKosterTable table;
        const auto tableOutcome = table.load(dir.string(), {6});
        check(tableOutcome.ok(), "graphene fixture loads: "
              + tableOutcome.message);

        DftbBasis basis;
        DftbBasis::build({6, 6}, table, basis);
        check(basis.totalOrbitals == 8,
              "dimension is 8 (two carbons, s+p each)");

        const double aLat = 2.46; // Angstrom, the real graphene value
        const Vec3 a1{aLat, 0.0, 0.0};
        const Vec3 a2{-aLat / 2.0, aLat * std::sqrt(3.0) / 2.0, 0.0};
        const Vec3 a3{0.0, 0.0, 15.0}; // vacuum, non-periodic
        Structure graphene;
        graphene.setCell(UnitCell(a1, a2, a3, {true, true, false}));
        graphene.addAtom(Atom{6, {0.0, 0.0, 0.0}});
        // Fractional (1/3, 2/3, 0) is the standard honeycomb B-sublattice
        // position (NOT (1/3,1/3) — that gives the wrong, a/3 rather than
        // a/sqrt(3), nearest-neighbor distance; checked directly below).
        const Vec3 posB = a1 * (1.0 / 3.0) + a2 * (2.0 / 3.0);
        graphene.addAtom(Atom{6, {posB.x, posB.y, posB.z}});
        const double bondLengthA = (posB - Vec3{0, 0, 0}).norm();
        check(near(bondLengthA, aLat / std::sqrt(3.0), 1.0e-6),
              "the B sublattice sits at the real 1.42 Angstrom C-C bond "
              "length from A (bondLength=" + std::to_string(bondLengthA)
              + ")");

        DftbHamiltonianBuilder builder;
        const auto buildOutcome = builder.build(graphene, table, basis);
        check(buildOutcome.ok(), "graphene Hamiltonian builds: "
              + buildOutcome.message);
        // 3 neighbors per atom, each pair counted once per direction (i,j)
        // and (j,i) -> 2 atoms * 3 neighbors = 6 pair records.
        check(builder.pairs().size() == 6,
              "exactly 3 nearest-neighbor bonds per atom found ("
              + std::to_string(builder.pairs().size()) + " pair records)");

        // pz(A) = orbital index 3, pz(B) = orbital index 7 ([s,px,py,pz] per
        // atom, atom A first). |H_pz,pz(k)| (S is 0 off-diagonal in this
        // fixture, so this alone sets the gap) is the quantity graphene
        // theory calls |f(k)| * |ppPi|.
        const auto pzOffdiagMagnitude = [&](std::array<double, 3> kFrac) {
            std::vector<std::complex<double>> h, s;
            builder.blochMatrices(kFrac, h, s);
            return std::abs(h[3 * 8 + 7]);
        };

        // Gamma: every phase is 1, so f(Gamma) = 3 exactly (3 neighbors, in
        // phase) -> |offdiag| = 3 * |ppPi| = 0.3 Hartree exactly.
        const double gammaOffdiag = pzOffdiagMagnitude({0.0, 0.0, 0.0});
        check(near(gammaOffdiag, 0.3, 1.0e-8),
              "at Gamma, |H_pz,pz| = 3*|ppPi| exactly (got "
              + std::to_string(gammaOffdiag) + ")");

        // Locate the Dirac point by DIRECT SEARCH (no assumption about
        // which fractional coordinate it sits at, or which lattice-vector
        // convention this test happens to use) — a coarse scan followed by
        // compass-search refinement on a smooth (away from the zero,
        // piecewise-linear at it) objective.
        double bestGap = gammaOffdiag;
        std::array<double, 2> bestK{0.0, 0.0};
        for (int i = 0; i < 41; ++i) {
            for (int j = 0; j < 41; ++j) {
                const std::array<double, 3> k{
                    static_cast<double>(i) / 41.0,
                    static_cast<double>(j) / 41.0, 0.0};
                const double gap = pzOffdiagMagnitude(k);
                if (gap < bestGap) {
                    bestGap = gap;
                    bestK = {k[0], k[1]};
                }
            }
        }
        double step = 1.0 / 41.0;
        for (int iter = 0; iter < 60 && step > 1.0e-12; ++iter) {
            bool improved = false;
            for (int di = -1; di <= 1; ++di) {
                for (int dj = -1; dj <= 1; ++dj) {
                    if (di == 0 && dj == 0)
                        continue;
                    const std::array<double, 3> candidate{
                        bestK[0] + di * step, bestK[1] + dj * step, 0.0};
                    const double gap = pzOffdiagMagnitude(candidate);
                    if (gap < bestGap) {
                        bestGap = gap;
                        bestK = {candidate[0], candidate[1]};
                        improved = true;
                    }
                }
            }
            if (!improved)
                step *= 0.5;
        }
        check(bestGap < 1.0e-8,
              "a Dirac point exists: |H_pz,pz| reaches "
              + std::to_string(bestGap) + " at k=("
              + std::to_string(bestK[0]) + ", " + std::to_string(bestK[1])
              + ") — exactly the graphene pz/pz* band touching, found by "
              "direct search rather than assumed");
        check(bestGap < gammaOffdiag * 1.0e-6,
              "the Dirac point's gap is many orders of magnitude below the "
              "Gamma-point value, not merely 'smaller'");

        fs::remove_all(dir, ec);
    }

    std::printf("\nSCC gamma functional (molecular, no periodicity):\n");
    {
        using calango::dftb::gammaFunctional;
        using calango::dftb::gammaShortRange;

        check(near(gammaFunctional(0.35, 0.35, 0.0), 0.35),
              "gamma_AA(0) = U_A exactly (on-site, same-U branch)");
        check(near(gammaFunctional(0.30, 0.40, 0.0), 0.30),
              "gamma_AA(0) returns U_A (not U_B) — the on-site term is a "
              "property of the ONE atom, U_B is irrelevant there");

        // The analytic R -> 0 limit derived in DftbGamma.hpp's own doc
        // comment (1/R - gammaShortRange(R) -> 5*tau/16 = U as R -> 0):
        // verified NUMERICALLY here, independent of the R < 1e-9 shortcut
        // gammaFunctional() itself takes.
        {
            const double u = 0.35;
            const double tau = calango::dftb::gammaDecayConstant(u);
            const double tinyR = 1.0e-5;
            const double numeric = 1.0 / tinyR - gammaShortRange(u, u, tinyR);
            check(std::fabs(numeric - u) < 1.0e-6,
                  "1/R - gammaShortRange(R) -> U as R -> 0, confirmed "
                  "numerically at R=1e-5 bohr (got "
                  + std::to_string(numeric) + ", expected " + std::to_string(u)
                  + ")");
        }

        // Large-R: gamma_AB(R) -> 1/R (bare Coulomb of two well-separated
        // point charges — the DEFINING long-range property of the whole
        // functional, independent of U).
        {
            const double r = 60.0; // bohr, far past any real Hubbard-U decay
            const double g = gammaFunctional(0.35, 0.42, r);
            check(std::fabs(g - 1.0 / r) < 1.0e-6,
                  "gamma_AB(R) -> 1/R at large R (got " + std::to_string(g)
                  + ", 1/R=" + std::to_string(1.0 / r) + ")");
        }

        // Symmetry: gamma_AB == gamma_BA.
        {
            const double r = 3.0;
            check(near(gammaFunctional(0.30, 0.45, r),
                       gammaFunctional(0.45, 0.30, r), 1.0e-12),
                  "gamma_AB(R) = gamma_BA(R)");
        }

        // Continuity between the same-U and different-U branches: as
        // U_B -> U_A, the different-tau formula must approach the same-tau
        // one — this is the strongest cross-check between the two branches
        // of gammaShortRange, since a transcription error in either would
        // show up as a jump here.
        {
            const double r = 4.0;
            const double uA = 0.35;
            const double same = gammaFunctional(uA, uA, r);
            const double near1 = gammaFunctional(uA, uA + 1.0e-4, r);
            check(std::fabs(same - near1) < 1.0e-4,
                  "the different-tau branch converges to the same-tau one "
                  "as U_B -> U_A (same=" + std::to_string(same) + ", "
                  "near=" + std::to_string(near1) + ")");
        }
    }

    std::printf("\nPeriodic electrostatics (DftbEwaldSum) — NaCl Madelung "
                "constant:\n");
    {
        // Rock-salt structure, primitive (2-atom) FCC cell: Na+ at the
        // origin, Cl- displaced by (a/2, 0, 0) — the STANDARD rock-salt
        // basis (nearest-neighbor distance a/2 along a cube edge; NOT
        // (a/2,a/2,a/2), which is the CsCl cation-anion relationship, a
        // different structure with a different Madelung constant).
        const double aLat = 10.0; // bohr, arbitrary — the test is scale-free
        const double rNN = aLat / 2.0;
        const Vec3 a1{0.0, aLat / 2.0, aLat / 2.0};
        const Vec3 a2{aLat / 2.0, 0.0, aLat / 2.0};
        const Vec3 a3{aLat / 2.0, aLat / 2.0, 0.0};

        Structure rocksalt;
        rocksalt.setCell(UnitCell(a1 / calango::dft::kBohrPerAngstrom,
                                   a2 / calango::dft::kBohrPerAngstrom,
                                   a3 / calango::dft::kBohrPerAngstrom,
                                   {true, true, true}));
        rocksalt.addAtom(Atom{11, {0.0, 0.0, 0.0}}); // Na (placeholder Z)
        rocksalt.addAtom(
            Atom{17, {rNN / calango::dft::kBohrPerAngstrom, 0.0, 0.0}}); // Cl

        // U chosen large enough that gammaShortRange is utterly negligible
        // at rNN (tau = 3.2*5 = 16/bohr; exp(-16*5) ~ 1e-35) — isolating the
        // Ewald lattice-sum machinery from the short-range piece already
        // validated in isolation above.
        const double u = 5.0;
        calango::dftb::DftbEwaldSum ewald;
        const auto outcome = ewald.build(rocksalt, {u, u});
        check(outcome.ok(), "Ewald sum builds for the rock-salt cell: "
              + outcome.message);

        const std::vector<double> deltaQ = {+1.0, -1.0}; // Na+, Cl-
        const auto shift = ewald.potentialShift(deltaQ);
        check(shift.size() == 2, "one potential shift per atom");

        // E_total (per primitive cell = per formula unit here) =
        // 0.5 * sum_A q_A * shift_A, minus the on-site U*q_A^2 contribution
        // this test's own large-U choice adds to each shift (see
        // DftbGamma.hpp: shift includes gamma_AA(0)*dQ_A = U*dQ_A).
        const double eFullShift =
            0.5 * (deltaQ[0] * shift[0] + deltaQ[1] * shift[1]);
        const double eOnsite =
            0.5 * (u * deltaQ[0] * deltaQ[0] + u * deltaQ[1] * deltaQ[1]);
        const double eMadelung = eFullShift - eOnsite;

        // The rock-salt (NaCl-structure) Madelung constant, alpha =
        // 1.747565 — one of the most widely tabulated numbers in solid
        // state physics (Kittel, Ashcroft & Mermin, and every crystal-
        // electrostatics reference agree to this precision).
        const double alphaMadelung = 1.747565;
        const double expected = -alphaMadelung / rNN;
        check(std::fabs(eMadelung - expected) < 1.0e-4,
              "the Ewald-summed lattice energy per formula unit matches "
              "-alpha_Madelung/R_NN = " + std::to_string(expected)
              + " Hartree (got " + std::to_string(eMadelung) + ")");
    }

    std::printf("\nSCF driver — non-SCC (DFTB0) cross-check against the "
                "closed-form dimer:\n");
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "calango_dftb_test_scf_h2dir";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        std::ofstream out(dir / "H-H.skf");
        out << "1.0 3\n"
               "0.0 0.0 -0.5 0.0 0.0 0.0 0.0 0.0 0.0 1.0\n"
               "1.008 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
               "0 0 0 0 0 0 0 0 0 -0.3 0 0 0 0 0 0 0 0 0 0.2\n"
               "0 0 0 0 0 0 0 0 0  0.0 0 0 0 0 0 0 0 0 0 0.0\n";
        out.close();

        SlaterKosterTable table;
        table.load(dir.string(), {1});
        DftbBasis basis;
        DftbBasis::build({1, 1}, table, basis);
        Structure molecule;
        molecule.addAtom(Atom{1, {0.0, 0.0, 0.0}});
        molecule.addAtom(
            Atom{1, {1.0 / calango::dft::kBohrPerAngstrom, 0.0, 0.0}});
        DftbHamiltonianBuilder builder;
        builder.build(molecule, table, basis);

        calango::dftb::DftbScf scf;
        calango::dftb::DftbScfResult result;
        calango::dftb::DftbScfSettings settings;
        settings.sccEnabled = false;
        const std::vector<calango::dftb::DftbKPoint> gammaOnly = {{{0.0, 0.0, 0.0}, 1.0}};
        const auto scfOutcome =
            scf.run(molecule, table, basis, builder, gammaOnly, settings, result);
        check(scfOutcome.ok(), "non-SCC SCF run succeeds: " + scfOutcome.message);
        check(result.converged, "DFTB0 (non-SCC) always reports converged "
              "(single shot)");
        check(result.kpoints.size() == 1
                  && result.kpoints[0].eigenvaluesHartree.size() == 2,
              "one Gamma point, 2 eigenvalues");
        const double epsSym = (-0.5 + -0.3) / (1.0 + 0.2);
        const double epsAntisym = (-0.5 - -0.3) / (1.0 - 0.2);
        check(near(result.kpoints[0].eigenvaluesHartree[0], epsSym, 1.0e-8)
                  && near(result.kpoints[0].eigenvaluesHartree[1], epsAntisym,
                          1.0e-8),
              "the SCF driver's own eigenvalues match the SAME closed-form "
              "dimer levels checked earlier against the raw Hamiltonian "
              "builder (got " + std::to_string(result.kpoints[0].eigenvaluesHartree[0])
              + ", " + std::to_string(result.kpoints[0].eigenvaluesHartree[1]) + ")");
        // 2 electrons total (H2, one bonding pair) -> both fill the LOWER
        // (bonding) level only, at T -> 0: occupation 2, 0.
        check(near(result.kpoints[0].occupations[0], 2.0, 1.0e-3)
                  && near(result.kpoints[0].occupations[1], 0.0, 1.0e-3),
              "both electrons occupy the bonding level, none the "
              "antibonding one (occ=" + std::to_string(result.kpoints[0].occupations[0])
              + ", " + std::to_string(result.kpoints[0].occupations[1]) + ")");
        check(near(result.totalEnergyHartree, 2.0 * epsSym, 1.0e-6),
              "total energy = 2 * eps_sym (band energy only, no SCC/repulsive "
              "term configured in this fixture)");

        fs::remove_all(dir, ec);
    }

    std::printf("\nSCF driver — SCC charge neutrality and electronegativity "
                "direction on a heteronuclear molecule:\n");
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "calango_dftb_test_scf_lifdir";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);

        const auto writeFile = [&](const std::string& name,
                                    const std::string& content) {
            std::ofstream out(dir / name);
            out << content;
        };
        // A synthetic s-only "Li-F-like" heteronuclear pair: Li (Z=3) has
        // the HIGHER (less attractive) on-site s energy and one valence
        // electron; "F" (Z=9) has the LOWER (more attractive, more
        // electronegative) on-site s energy and one valence electron —
        // NOT real lithium/fluorine physics (a real F is p-dominated), a
        // minimal synthetic pair chosen only to give an unambiguous
        // electronegativity direction to check SCC charge transfer against.
        writeFile("Li-Li.skf",
                  "1.0 3\n"
                  "0.0 0.0 -0.3 0.0 0.0 0.0 0.30 0.0 0.0 1.0\n"
                  "6.941 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
                  "0 0 0 0 0 0 0 0 0 -0.15 0 0 0 0 0 0 0 0 0 0.10\n"
                  "0 0 0 0 0 0 0 0 0  0.00 0 0 0 0 0 0 0 0 0 0.00\n");
        writeFile("F-F.skf",
                  "1.0 3\n"
                  "0.0 0.0 -0.9 0.0 0.0 0.0 0.40 0.0 0.0 1.0\n"
                  "18.998 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
                  "0 0 0 0 0 0 0 0 0 -0.15 0 0 0 0 0 0 0 0 0 0.10\n"
                  "0 0 0 0 0 0 0 0 0  0.00 0 0 0 0 0 0 0 0 0 0.00\n");
        const std::string heteroTable =
            "1.0 3\n"
            "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
            "0 0 0 0 0 0 0 0 0 -0.15 0 0 0 0 0 0 0 0 0 0.10\n"
            "0 0 0 0 0 0 0 0 0  0.00 0 0 0 0 0 0 0 0 0 0.00\n";
        writeFile("Li-F.skf", heteroTable);
        writeFile("F-Li.skf", heteroTable);

        SlaterKosterTable table;
        const auto tableOutcome = table.load(dir.string(), {3, 9});
        check(tableOutcome.ok(), "LiF-like fixture loads: " + tableOutcome.message);

        DftbBasis basis;
        DftbBasis::build({3, 9}, table, basis);
        check(basis.totalOrbitals == 2, "s-only heteronuclear pair -> "
              "dimension 2");

        Structure molecule;
        molecule.addAtom(Atom{3, {0.0, 0.0, 0.0}});   // Li
        molecule.addAtom(
            Atom{9, {1.0 / calango::dft::kBohrPerAngstrom, 0.0, 0.0}}); // F

        DftbHamiltonianBuilder builder;
        const auto buildOutcome = builder.build(molecule, table, basis);
        check(buildOutcome.ok(), "heteronuclear Hamiltonian builds: "
              + buildOutcome.message);

        calango::dftb::DftbScf scf;
        calango::dftb::DftbScfResult result;
        calango::dftb::DftbScfSettings settings; // SCC on by default
        const std::vector<calango::dftb::DftbKPoint> gammaOnly = {{{0.0, 0.0, 0.0}, 1.0}};
        const auto scfOutcome =
            scf.run(molecule, table, basis, builder, gammaOnly, settings, result);
        check(scfOutcome.ok(), "SCC SCF run succeeds: " + scfOutcome.message);
        check(result.converged,
              "SCC converges within " + std::to_string(settings.maxSccIterations)
              + " iterations (took " + std::to_string(result.iterations) + ")");
        check(result.deltaQ.size() == 2, "one charge fluctuation per atom");

        const double sumDeltaQ = result.deltaQ[0] + result.deltaQ[1];
        check(std::fabs(sumDeltaQ) < 1.0e-6,
              "charge neutrality: dQ_Li + dQ_F = 0 to high precision (got "
              + std::to_string(sumDeltaQ) + ")");

        // dQ (this test's convention: population - reference) is POSITIVE
        // for the atom that GAINED electron density. F has the lower
        // (more attractive) on-site energy, so electron density should
        // flow FROM Li TO F: dQ_F > 0, dQ_Li < 0.
        check(result.deltaQ[1] > 1.0e-3 && result.deltaQ[0] < -1.0e-3,
              "charge flows toward the more electronegative atom (F): "
              "dQ_Li=" + std::to_string(result.deltaQ[0]) + ", dQ_F="
              + std::to_string(result.deltaQ[1]));
        check(std::fabs(result.maxChargeResidual) < settings.sccToleranceElectrons,
              "the reported residual is actually below the requested "
              "tolerance at convergence");

        fs::remove_all(dir, ec);
    }

    std::printf("\nForces (finite difference) — H2 dimer against an "
                "INDEPENDENT closed-form reference:\n");
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "calango_dftb_test_forces_h2dir";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        // A wider grid than the earlier H2 fixture (6 points instead of 3)
        // so the force test's +/- 0.01 Angstrom perturbations, and the
        // independent reference's own finite-difference step, both stay
        // safely inside the tabulated range with real (non-flat, non-edge)
        // interpolation behavior on both sides of r = 1.0 bohr.
        std::ofstream out(dir / "H-H.skf");
        out << "0.4 6\n"
               "0.0 0.0 -0.5 0.0 0.0 0.0 0.0 0.0 0.0 1.0\n"
               "1.008 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n";
        // Rows at r = 0.4, 0.8, 1.2, 1.6, 2.0 bohr — a smoothly decaying
        // Hss0/Sss0 (not a single spike) so the interpolated slope at
        // r = 1.0 is well defined and representative.
        const double hssAt[5] = {-0.45, -0.34, -0.24, -0.15, -0.08};
        const double sssAt[5] = {0.30, 0.24, 0.17, 0.10, 0.05};
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 9; ++col)
                out << "0 ";
            out << hssAt[row] << " ";
            for (int col = 0; col < 9; ++col)
                out << "0 ";
            out << sssAt[row] << "\n";
        }
        out.close();

        SlaterKosterTable table;
        table.load(dir.string(), {1});

        // Independent reference: the SAME closed-form dimer energy used
        // earlier, E(R) = 2 * (Es + Hss(R)) / (1 + Sss(R)) (both electrons
        // in the bonding level), built directly from
        // SlaterKosterFile::integral() — NOT by calling the SCF driver —
        // so this is a genuinely separate path from computeDftbForces().
        const SlaterKosterFile* skFile = table.pair(1, 1);
        check(skFile != nullptr, "H-H.skf (wide grid) loads");
        const double es = -0.5;
        const auto energyClosedForm = [&](double rBohr) {
            const double hss = skFile->integral(false, SkChannel::Sssigma, rBohr);
            const double sss = skFile->integral(true, SkChannel::Sssigma, rBohr);
            return 2.0 * (es + hss) / (1.0 + sss);
        };
        const double r0 = 1.0; // bohr
        const double h = 1.0e-4; // bohr — independent of the force
                                  // evaluator's own 0.01 Angstrom step
        const double expectedForceHartreePerBohr =
            -(energyClosedForm(r0 + h) - energyClosedForm(r0 - h)) / (2.0 * h);
        const double expectedForceEvPerA = expectedForceHartreePerBohr
            * calango::dft::kHartreeToEv * calango::dft::kBohrPerAngstrom;

        DftbBasis basis;
        DftbBasis::build({1, 1}, table, basis);
        Structure molecule;
        molecule.addAtom(Atom{1, {0.0, 0.0, 0.0}});
        molecule.addAtom(
            Atom{1, {r0 / calango::dft::kBohrPerAngstrom, 0.0, 0.0}});

        calango::dftb::DftbScfSettings settings;
        settings.sccEnabled = false; // homonuclear, no charge transfer to speak of
        const std::vector<calango::dftb::DftbKPoint> gammaOnly = {{{0.0, 0.0, 0.0}, 1.0}};
        calango::dftb::DftbForces forces;
        const auto forcesOutcome = calango::dftb::computeDftbForces(
            molecule, table, gammaOnly, settings, forces, 0.01);
        check(forcesOutcome.ok(), "force evaluation succeeds: "
              + forcesOutcome.message);
        check(forces.forcesEvPerAngstrom.size() == 2, "one force per atom");

        check(near(forces.forcesEvPerAngstrom[0].x,
                   -forces.forcesEvPerAngstrom[1].x, 1.0e-6),
              "Newton's third law: the x-forces on the two atoms are equal "
              "and opposite (F0=" + std::to_string(forces.forcesEvPerAngstrom[0].x)
              + ", F1=" + std::to_string(forces.forcesEvPerAngstrom[1].x) + ")");
        check(near(forces.forcesEvPerAngstrom[0].y, 0.0, 1.0e-6)
                  && near(forces.forcesEvPerAngstrom[0].z, 0.0, 1.0e-6),
              "no spurious transverse force on a bond lying along x");

        // Atom 0's force is +x when the bond wants to CONTRACT (attractive
        // regime) — check the SCF-driven finite difference against the
        // independent closed-form reference to a loose-ish but meaningful
        // tolerance (two different finite-difference step sizes and two
        // independently written energy-evaluation paths, so exact
        // agreement to machine precision is not expected).
        check(std::fabs(forces.forcesEvPerAngstrom[1].x - expectedForceEvPerA)
                  < 0.05 * std::fabs(expectedForceEvPerA),
              "the force matches an INDEPENDENTLY computed closed-form "
              "reference to 5% (got "
              + std::to_string(forces.forcesEvPerAngstrom[1].x) + " eV/A, "
              "expected " + std::to_string(expectedForceEvPerA) + " eV/A)");

        fs::remove_all(dir, ec);
    }

    std::printf("\nPDOS (Mulliken-projected) — sum rule:\n");
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "calango_dftb_test_pdos_h2dir";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        std::ofstream out(dir / "H-H.skf");
        out << "1.0 3\n"
               "0.0 0.0 -0.5 0.0 0.0 0.0 0.0 0.0 0.0 1.0\n"
               "1.008 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
               "0 0 0 0 0 0 0 0 0 -0.3 0 0 0 0 0 0 0 0 0 0.2\n"
               "0 0 0 0 0 0 0 0 0  0.0 0 0 0 0 0 0 0 0 0 0.0\n";
        out.close();

        SlaterKosterTable table;
        table.load(dir.string(), {1});
        DftbBasis basis;
        DftbBasis::build({1, 1}, table, basis);
        Structure molecule;
        molecule.addAtom(Atom{1, {0.0, 0.0, 0.0}});
        molecule.addAtom(
            Atom{1, {1.0 / calango::dft::kBohrPerAngstrom, 0.0, 0.0}});
        DftbHamiltonianBuilder builder;
        builder.build(molecule, table, basis);

        calango::dftb::DftbScf scf;
        calango::dftb::DftbScfResult result;
        calango::dftb::DftbScfSettings settings;
        settings.sccEnabled = false;
        const std::vector<calango::dftb::DftbKPoint> gammaOnly = {{{0.0, 0.0, 0.0}, 1.0}};
        scf.run(molecule, table, basis, builder, gammaOnly, settings, result);

        calango::dftb::DftbPdosResult pdos;
        const auto pdosOutcome = calango::dftb::computeDftbPdos(
            result, builder, basis, table, 0.02, 0.005, pdos);
        check(pdosOutcome.ok(), "PDOS computes: " + pdosOutcome.message);
        check(pdos.projections.count("H s") == 1,
              "the only group present is 'H s' (this fixture has no p "
              "shell)");

        // Sum rule: integrating EVERY group's PDOS over ALL energy (a
        // properly normalized broadening kernel, generously padded) must
        // recover exactly the total orbital count — independent of the
        // broadening/bin-width parameters, and independent of occupation
        // (PDOS counts every state, not just occupied ones).
        double totalIntegral = 0.0;
        for (const auto& [name, values] : pdos.projections)
            for (double v : values)
                totalIntegral += v * pdos.binWidthEv;
        check(std::fabs(totalIntegral - basis.totalOrbitals) < 1.0e-3,
              "the PDOS sum rule holds: integral over all energy and all "
              "groups = " + std::to_string(totalIntegral) + " (expected "
              + std::to_string(basis.totalOrbitals) + " orbitals)");

        // A finer grid must give essentially the SAME integral — the
        // invariant is a property of the physics, not of the histogram.
        calango::dftb::DftbPdosResult pdosFine;
        calango::dftb::computeDftbPdos(result, builder, basis, table, 0.01,
                                        0.001, pdosFine);
        double totalFine = 0.0;
        for (const auto& [name, values] : pdosFine.projections)
            for (double v : values)
                totalFine += v * pdosFine.binWidthEv;
        check(std::fabs(totalFine - basis.totalOrbitals) < 1.0e-3,
              "the sum rule holds at a different (finer) broadening/bin "
              "width too (got " + std::to_string(totalFine) + ")");

        fs::remove_all(dir, ec);
    }

    std::printf("\nEffective Band Structure unfolding — 1D chain, exact "
                "dispersion:\n");
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "calango_dftb_test_unfold_dir";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        // s-only, orthogonal (Sss0 = 0) so E(k) = Es + 2*t*cos(2*pi*k)
        // EXACTLY, with no S(k)-induced renormalization to account for.
        // Nearest-neighbor distance is 2.0 Angstrom in EITHER the primitive
        // (period a = 2.0 A) or the supercell (period 2a = 4.0 A, 2 atoms)
        // description, so the same fixture, at the same tabulated distance,
        // describes both.
        std::ofstream out(dir / "H-H.skf");
        out << "2.0 3\n" // gridDist = 2.0 bohr... using Angstrom-scale
                          // numbers directly as if bohr for a clean fixture
                          // (only the RATIOS/topology matter for this test)
               "0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 1.0\n"
               "1.008 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
               "0 0 0 0 0 0 0 0 0 -0.2 0 0 0 0 0 0 0 0 0 0.0\n"
               "0 0 0 0 0 0 0 0 0  0.0 0 0 0 0 0 0 0 0 0 0.0\n";
        out.close();

        SlaterKosterTable table;
        table.load(dir.string(), {1});

        // Primitive: 1 atom, period a. Distance to its own neighbor image
        // (r = gridDist = 2.0, matching the fixture's own units) — this
        // test treats the fixture's distance unit as Angstrom throughout
        // (both structures below use the SAME 2.0/4.0 Angstrom spacing),
        // so the r = 2.0 "bohr" grid point in the .skf lands exactly on
        // the real (Angstrom-converted) nearest-neighbor bond in BOTH
        // structures — self-consistent within this test, not a claim
        // about real hydrogen.
        const double period = 2.0 / calango::dft::kBohrPerAngstrom; // A
        const Vec3 aChain{period, 0.0, 0.0};
        const Vec3 aPerp1{0.0, 15.0, 0.0}, aPerp2{0.0, 0.0, 15.0};

        Structure primitive;
        primitive.setCell(UnitCell(aChain, aPerp1, aPerp2, {true, false, false}));
        primitive.addAtom(Atom{1, {0.0, 0.0, 0.0}});

        Structure supercellStruct;
        supercellStruct.setCell(
            UnitCell(aChain * 2.0, aPerp1, aPerp2, {true, false, false}));
        supercellStruct.addAtom(Atom{1, {0.0, 0.0, 0.0}});
        supercellStruct.addAtom(Atom{1, {period, 0.0, 0.0}});

        DftbBasis basis;
        DftbBasis::build({1, 1}, table, basis);
        DftbHamiltonianBuilder builder;
        const auto buildOutcome = builder.build(supercellStruct, table, basis);
        check(buildOutcome.ok(), "supercell Hamiltonian builds: "
              + buildOutcome.message);

        calango::dftb::DftbUnfoldingMap map;
        double residual = 0.0;
        const auto mapOutcome = calango::dftb::DftbUnfoldingMap::build(
            supercellStruct, primitive, map, &residual);
        check(mapOutcome.ok(), "unfolding map builds: " + mapOutcome.message);
        check(map.imageCount == 2,
              "2 primitive images in the supercell (got "
              + std::to_string(map.imageCount) + ")");
        check(residual < 1.0e-6,
              "the supercell is EXACTLY commensurate with the primitive "
              "cell (residual=" + std::to_string(residual) + ")");

        // K = fold(k=0.2, M) = 0.4 (no BZ-boundary wraparound ambiguity).
        const double kPrim = 0.2;
        const Vec3 kFolded =
            calango::core::foldToSupercell({kPrim, 0.0, 0.0}, map.matrix);
        check(near(kFolded.x, 0.4, 1.0e-9),
              "K = fold(0.2) = 0.4 for a 2x supercell (got "
              + std::to_string(kFolded.x) + ")");

        std::vector<std::complex<double>> h, s;
        builder.blochMatrices({kFolded.x, kFolded.y, kFolded.z}, h, s);
        std::vector<double> eigenvalues;
        std::vector<std::complex<double>> eigenvectors;
        const auto eigenOutcome = calango::dft::linalg::solveGeneralizedHermitian(
            h, s, 2, eigenvalues, eigenvectors);
        check(eigenOutcome.ok(), "supercell eigenproblem at K solves: "
              + eigenOutcome.message);

        const auto weightsAt02 = calango::dftb::dftbUnfoldingWeights(
            eigenvectors, s, basis, map, {kPrim, 0.0, 0.0});
        const double kPartner = kPrim - 0.5; // the OTHER primitive k folding to K
        const auto weightsAtPartner = calango::dftb::dftbUnfoldingWeights(
            eigenvectors, s, basis, map, {kPartner, 0.0, 0.0});

        check(weightsAt02.size() == 2 && weightsAtPartner.size() == 2,
              "one weight per supercell state, at each primitive k");

        // Partition identity: for EVERY supercell state, weight(0.2) +
        // weight(-0.3) = 1 exactly — the defining, formula-independent
        // invariant this whole scheme rests on (see DftbUnfolding.hpp).
        for (int i = 0; i < 2; ++i) {
            const double total = weightsAt02[static_cast<std::size_t>(i)]
                + weightsAtPartner[static_cast<std::size_t>(i)];
            check(near(total, 1.0, 1.0e-8),
                  "partition identity holds for state " + std::to_string(i)
                  + ": weight(0.2) + weight(-0.3) = "
                  + std::to_string(total));
        }

        // Physical check: whichever state has weight ~1 at k=0.2 must have
        // the ENERGY the primitive dispersion predicts there —
        // E(k) = Es + 2*t*cos(2*pi*k), Es=0, t=-0.2 (this fixture's own
        // values, matching the s-only, orthogonal-basis case exactly, with
        // NO overlap-induced correction to account for).
        const double expectedEnergy =
            2.0 * (-0.2) * std::cos(2.0 * calango::dft::kPi * kPrim);
        bool foundDominant = false;
        for (int i = 0; i < 2; ++i) {
            if (weightsAt02[static_cast<std::size_t>(i)] > 0.9) {
                foundDominant = true;
                check(near(eigenvalues[static_cast<std::size_t>(i)],
                           expectedEnergy, 1.0e-6),
                      "the state carrying primitive character at k=0.2 has "
                      "exactly the primitive-dispersion energy (got "
                      + std::to_string(eigenvalues[static_cast<std::size_t>(i)])
                      + ", expected " + std::to_string(expectedEnergy) + ")");
            }
        }
        check(foundDominant,
              "exactly one supercell state carries dominant (>0.9) weight "
              "at k=0.2, as a defect-free 2x supercell must");

        fs::remove_all(dir, ec);
    }

    std::printf("\nOptics — velocity operator, 1D chain exact dispersion:\n");
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "calango_dftb_test_velocity_dir";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        std::ofstream out(dir / "H-H.skf");
        out << "2.0 3\n"
               "0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 1.0\n"
               "1.008 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
               "0 0 0 0 0 0 0 0 0 -0.2 0 0 0 0 0 0 0 0 0 0.0\n"
               "0 0 0 0 0 0 0 0 0  0.0 0 0 0 0 0 0 0 0 0 0.0\n";
        out.close();

        SlaterKosterTable table;
        table.load(dir.string(), {1});
        DftbBasis basis;
        DftbBasis::build({1}, table, basis);

        // Single-atom PRIMITIVE cell this time (not the 2-atom supercell —
        // the velocity operator only needs one atom to test cleanly).
        const double period = 2.0 / calango::dft::kBohrPerAngstrom;
        Structure chain;
        chain.setCell(UnitCell({period, 0.0, 0.0}, {0.0, 15.0, 0.0},
                               {0.0, 0.0, 15.0}, {true, false, false}));
        chain.addAtom(Atom{1, {0.0, 0.0, 0.0}});

        DftbHamiltonianBuilder builder;
        builder.build(chain, table, basis);
        const std::array<Vec3, 3> latticeBohr{
            Vec3{period * calango::dft::kBohrPerAngstrom, 0.0, 0.0},
            Vec3{0.0, 15.0 * calango::dft::kBohrPerAngstrom, 0.0},
            Vec3{0.0, 0.0, 15.0 * calango::dft::kBohrPerAngstrom}};

        const auto energyAt = [&](double kFracX) {
            std::vector<std::complex<double>> h, s;
            builder.blochMatrices({kFracX, 0.0, 0.0}, h, s);
            std::vector<double> eigenvalues;
            std::vector<std::complex<double>> eigenvectors;
            calango::dft::linalg::solveGeneralizedHermitian(h, s, 1,
                                                             eigenvalues,
                                                             eigenvectors);
            return std::make_pair(eigenvalues, eigenvectors);
        };

        for (double kTest : {0.0, 0.15, 0.25}) {
            const auto [eigenvalues, eigenvectors] = energyAt(kTest);
            const auto v = calango::dftb::dftbVelocityMatrix(
                builder, latticeBohr, {kTest, 0.0, 0.0}, {}, eigenvectors,
                eigenvalues, 0);
            check(v.size() == 1, "1x1 velocity matrix for a 1-orbital basis");

            // Independent reference: central finite difference of the
            // EIGENVALUE itself with respect to a small CARTESIAN k step —
            // dE/dk_x, which the Hellmann-Feynman identity says must equal
            // v_nn exactly for a normalized eigenstate.
            const double stepInverseBohr = 1.0e-4;
            const double dkFrac =
                latticeBohr[0].x / (2.0 * calango::dft::kPi) * stepInverseBohr;
            const double ePlus = energyAt(kTest + dkFrac).first[0];
            const double eMinus = energyAt(kTest - dkFrac).first[0];
            const double dEdk = (ePlus - eMinus) / (2.0 * stepInverseBohr);

            check(near(v[0].real(), dEdk, 1.0e-6)
                      && near(v[0].imag(), 0.0, 1.0e-9),
                  "group velocity v_nn matches dE/dk (Hellmann-Feynman) at "
                  "k=" + std::to_string(kTest) + " (v=" + std::to_string(v[0].real())
                  + ", dE/dk=" + std::to_string(dEdk) + ")");
        }

        // Group velocity vanishes exactly at Gamma (a symmetric point of
        // an inversion-symmetric 1D chain — E(k) = E(-k) forces dE/dk = 0
        // there), independent of the finite-difference cross-check above.
        {
            const auto [eigenvalues, eigenvectors] = energyAt(0.0);
            const auto v = calango::dftb::dftbVelocityMatrix(
                builder, latticeBohr, {0.0, 0.0, 0.0}, {}, eigenvectors,
                eigenvalues, 0);
            check(near(v[0].real(), 0.0, 1.0e-6),
                  "v_nn = 0 exactly at Gamma (got "
                  + std::to_string(v[0].real()) + ")");
        }

        fs::remove_all(dir, ec);
    }

    std::printf("\nOptics — eps2 >= 0 (passivity), graphene fixture:\n");
    {
        namespace fs = std::filesystem;
        const fs::path dir =
            fs::temp_directory_path() / "calango_dftb_test_optics_dir";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        {
            std::ofstream out(dir / "C-C.skf");
            out << "0.1 35\n"
                   "0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 2.0 2.0\n"
                   "12.011 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n";
            for (int row = 0; row < 34; ++row) {
                for (int col = 0; col < 20; ++col)
                    out << (col == 6 ? "-0.1" : "0") << (col == 19 ? "\n" : " ");
            }
        }

        SlaterKosterTable table;
        table.load(dir.string(), {6});
        DftbBasis basis;
        DftbBasis::build({6, 6}, table, basis);

        const double aLat = 2.46;
        const Vec3 a1{aLat, 0.0, 0.0};
        const Vec3 a2{-aLat / 2.0, aLat * std::sqrt(3.0) / 2.0, 0.0};
        const Vec3 a3{0.0, 0.0, 15.0};
        Structure graphene;
        graphene.setCell(UnitCell(a1, a2, a3, {true, true, false}));
        graphene.addAtom(Atom{6, {0.0, 0.0, 0.0}});
        const Vec3 posB = a1 * (1.0 / 3.0) + a2 * (2.0 / 3.0);
        graphene.addAtom(Atom{6, {posB.x, posB.y, posB.z}});

        DftbHamiltonianBuilder builder;
        builder.build(graphene, table, basis);

        std::vector<int> atomicNumbers = {6, 6};
        std::vector<calango::dftb::DftbKPoint> kpoints;
        // A modest explicit mesh (not the SCF's own reduced one — Optics
        // wants every k, matching how PDOS/bands also use an unreduced set
        // for post-processing) — small on purpose, this is a physics-sign
        // check, not a converged spectrum.
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 6; ++j)
                kpoints.push_back({{i / 6.0, j / 6.0, 0.0}, 1.0 / 36.0});

        calango::dftb::DftbScf scf;
        calango::dftb::DftbScfResult result;
        calango::dftb::DftbScfSettings settings;
        settings.sccEnabled = false;
        const auto scfOutcome =
            scf.run(graphene, table, basis, builder, kpoints, settings, result);
        check(scfOutcome.ok(), "graphene SCF (non-SCC) runs: " + scfOutcome.message);

        const std::array<Vec3, 3> latticeBohr{
            a1 * calango::dft::kBohrPerAngstrom,
            a2 * calango::dft::kBohrPerAngstrom,
            a3 * calango::dft::kBohrPerAngstrom};
        const double volumeBohr3 = std::fabs(
            latticeBohr[0].dot(latticeBohr[1].cross(latticeBohr[2])));

        calango::dftb::DftbOpticsOptions options;
        for (double e = 0.0; e <= 8.0; e += 0.5)
            options.frequenciesEv.push_back(e);
        options.broadeningEv = 0.3;
        options.direction = 0;

        calango::dftb::DftbOpticsResult optics;
        const auto opticsOutcome = calango::dftb::computeDftbOptics(
            result, builder, latticeBohr, volumeBohr3, {}, options, optics);
        check(opticsOutcome.ok(), "optics computes: " + opticsOutcome.message);
        check(optics.eps2.size() == options.frequenciesEv.size(),
              "one eps2 value per requested frequency");

        bool allNonNegative = true;
        double minEps2 = 1.0e300;
        for (double e2 : optics.eps2) {
            if (e2 < -1.0e-9)
                allNonNegative = false;
            minEps2 = std::min(minEps2, e2);
        }
        check(allNonNegative,
              "eps2(omega) >= 0 for every omega > 0 — a passive medium's "
              "absorptive response cannot be negative (min value seen: "
              + std::to_string(minEps2) + "); this is exactly the sign "
              "bug a real end-to-end run caught (see DftbOptics.cpp's own "
              "comment at the (f_m - f_n) weight)");

        bool anyAbsorption = false;
        for (double e2 : optics.eps2)
            if (e2 > 1.0e-6)
                anyAbsorption = true;
        check(anyAbsorption,
              "at least one frequency shows genuine (non-zero) absorption "
              "— i.e. this is not trivially passing by every value being "
              "exactly zero");

        fs::remove_all(dir, ec);
    }

    if (failures == 0) {
        std::printf("\nAll native DFTB checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d native DFTB check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
