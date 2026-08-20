// Dilute Solution Interpolation (DSI) model — core::Dsim.
//
// Every check here is a closed-form algebraic identity, not a comparison
// against a previous run: Eq. 7's tangent-line construction guarantees
// DeltaH_mix(0) = DeltaH_mix(1) = 0 and a known slope at each end by
// CONSTRUCTION, so those are hand-derivable, exact checks rather than a
// tolerance around some earlier output. The M-matrix fixture in the first
// block is lifted verbatim from oncapintada's own `test_subregular_model.py`
// (`simple_energy_matrix`/`test_Mij_values`) — the reference implementation's
// unit test, hand-computed algebra, not this codebase's own prior run.

#include "core/Dsim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

bool close(double a, double b, double tol = 1e-9)
{
    return std::fabs(a - b) <= tol;
}

} // namespace

int main()
{
    using namespace calango::core;

    // -- M matrix, against oncapintada's own hand-computed fixture ----------
    // energy_matrix = [[1.0, 1.1], [2.1, 2.0]], dilution x0 = 0.10.
    // M_ij = E_ij - (x0*E_ii + (1-x0)*E_jj), i = solute (row), j = solvent
    // (column) -- oncapintada's test_subregular_model.py::test_Mij_values.
    {
        const EnergyMatrix e{{1.0, 1.1}, {2.1, 2.0}};
        const MMatrix m = computeMMatrix(e, 0.10);
        check(close(m[0][0], 0.0) && close(m[1][1], 0.0), "M diagonal is exactly zero");
        check(close(m[0][1], -0.8),
              "M[0][1] = 1.1 - (0.1*1.0 + 0.9*2.0) = -0.8, matches oncapintada's fixture");
        check(close(m[1][0], 1.0),
              "M[1][0] = 2.1 - (0.1*2.0 + 0.9*1.0) = 1.0, matches oncapintada's fixture");
    }

    // -- Binary DeltaH_mix(x): endpoints are exact zero by construction -----
    {
        // A = "1" (host at x=0), B = "2" (host at x=1); N=27 (paper's 3x3x3
        // fcc supercell); made-up but self-consistent per-atom energies.
        const DsimBinaryResult r =
            solveDsimBinary("Au", "Pt", 27,
                            /*E_pureA*/ -3.500, /*E_pureB*/ -5.800,
                            /*E_BinA*/ -3.550, /*E_AinB*/ -5.700,
                            /*points*/ 11);

        check(close(r.dilution, 1.0 / 27.0), "dilution is 1/supercellAtomCount");
        check(r.curve.size() == 11, "curve has the requested number of points");
        check(close(r.curve.front().x, 0.0) && close(r.curve.back().x, 1.0),
              "grid spans the full [0, 1] composition range");
        check(close(r.curve.front().enthalpyEvPerAtom, 0.0)
                  && close(r.curve.back().enthalpyEvPerAtom, 0.0),
              "DeltaH_mix(0) = DeltaH_mix(1) = 0 exactly (Eq. 7's x(1-x) factors)");

        // Eq. 8: the two M parameters are, by construction of Eq. 7, exactly
        // the tangent-line slopes at the two dilute limits.
        const double dx = 1e-6;
        const DsimBinaryResult probe =
            solveDsimBinary("Au", "Pt", 27, -3.500, -5.800, -3.550, -5.700, 3);
        (void)probe;
        // Direct finite difference of Eq. 7 near x=0 and x=1, independent of
        // the M bookkeeping above, to check dHdxAt0/dHdxAt1 against the
        // curve's own construction rather than against themselves.
        auto evalH = [&](double x) {
            return r.mBInA * x * (1.0 - x) * (1.0 - x) + r.mAInB * x * x * (1.0 - x);
        };
        const double slopeAt0 = (evalH(dx) - evalH(0.0)) / dx;
        const double slopeAt1 = (evalH(1.0) - evalH(1.0 - dx)) / dx;
        check(close(slopeAt0, r.dHdxAt0, 1e-4), "finite-difference slope at x=0 matches dHdxAt0");
        check(close(slopeAt1, r.dHdxAt1, 1e-4), "finite-difference slope at x=1 matches dHdxAt1");
        check(close(r.dHdxAt0, r.mBInA), "dHdxAt0 == M_2[1] exactly (Eq. 8)");
        check(close(r.dHdxAt1, -r.mAInB), "dHdxAt1 == -M_1[2] exactly (Eq. 8)");

        // Eq. 7 at x=1/2: x(1-x)^2 = x^2(1-x) = 1/8, so DeltaH_mix(1/2)
        // collapses to (M_2[1] + M_1[2]) / 8.
        const DsimBinaryResult half =
            solveDsimBinary("Au", "Pt", 27, -3.500, -5.800, -3.550, -5.700, 3);
        const double expectedHalf = 0.125 * (half.mBInA + half.mAInB);
        double midEnthalpy = 0.0;
        bool foundMid = false;
        for (const auto& p : half.curve) {
            if (close(p.x, 0.5, 1e-9)) {
                midEnthalpy = p.enthalpyEvPerAtom;
                foundMid = true;
            }
        }
        check(foundMid && close(midEnthalpy, expectedHalf),
              "DeltaH_mix(1/2) = (M_2[1] + M_1[2]) / 8 (Eq. 7 algebraic reduction)");

        check(close(r.curve.front().enthalpyKjPerMol, 0.0)
                  && close(r.curve[5].enthalpyKjPerMol,
                           r.curve[5].enthalpyEvPerAtom * kEvToKjPerMol),
              "kJ/mol column is exactly eV/atom * kEvToKjPerMol");
    }

    // -- General N-component path reduces to the binary formula -------------
    // enthalpyOfMixing() (Eq. 4+6, general N) must agree with the
    // hand-expanded Eq. 7 binary formula bit-for-bit-close at several x,
    // since Eq. 7 is what Eq. 4+6 algebraically reduce to for N=2.
    {
        const EnergyMatrix e{{-3.500, -5.700}, {-3.550, -5.800}};
        const double dilution = 1.0 / 27.0;
        const MMatrix m = computeMMatrix(e, dilution);
        for (double x : {0.0, 0.1, 0.3, 0.5, 0.7, 0.9, 1.0}) {
            const double general = enthalpyOfMixing(m, {1.0 - x, x});
            const double binary = m[1][0] * x * (1.0 - x) * (1.0 - x)
                + m[0][1] * x * x * (1.0 - x);
            check(close(general, binary, 1e-9),
                  "general N-component formula matches the binary Eq. 7 reduction at x="
                      + std::to_string(x));
        }
    }

    // -- N=3 cross-validation against oncapintada.MultiComponentAlloy -------
    // core::Dsim's N-component path (Eq. 4+6) is what a future ternary+
    // DSIM workflow would sit on (the wizard/script generator/viewer ship
    // binary-only today — see docs/simulations/dsim.md's "Extensibility"
    // note) — checked here against a live run of oncapintada's own
    // MultiComponentAlloy on its own test_subregular_model.py ternary
    // fixture, not just the binary reduction above.
    {
        const EnergyMatrix e{
            {1.0, 1.1, 1.2},
            {2.1, 2.0, 2.2},
            {3.2, 3.1, 3.0},
        };
        const MMatrix m = computeMMatrix(e, 0.1);
        // oncapintada's MultiComponentAlloy(energy_matrix=e, dilution=0.1).Mij()
        const double expectedM[3][3] = {
            {0.0, -0.8, -1.6000000000000003},
            {1.0, 0.0, -0.7000000000000002},
            {2.0, 1.0, 0.0},
        };
        bool mMatches = true;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                mMatches = mMatches && close(m[i][j], expectedM[i][j], 1e-12);
        check(mMatches, "N=3 M matrix matches oncapintada's MultiComponentAlloy.Mij()");

        // oncapintada's alloy.simplex_grid(resolution=6) (28 points) and
        // .enthalpy_of_mixing(grid, unit="eV/atom").
        const double grid[28][3] = {
            {1.0, 0.0, 0.0}, {0.8333333333333334, 0.16666666666666666, 0.0},
            {0.8333333333333334, 0.0, 0.16666666666666666}, {0.6666666666666666, 0.3333333333333333, 0.0},
            {0.6666666666666666, 0.16666666666666666, 0.16666666666666666}, {0.6666666666666666, 0.0, 0.3333333333333333},
            {0.5, 0.5, 0.0}, {0.5, 0.3333333333333333, 0.16666666666666666},
            {0.5, 0.16666666666666666, 0.3333333333333333}, {0.5, 0.0, 0.5},
            {0.3333333333333333, 0.6666666666666666, 0.0}, {0.3333333333333333, 0.5, 0.16666666666666666},
            {0.3333333333333333, 0.3333333333333333, 0.3333333333333333}, {0.3333333333333333, 0.16666666666666666, 0.5},
            {0.3333333333333333, 0.0, 0.6666666666666666}, {0.16666666666666666, 0.8333333333333334, 0.0},
            {0.16666666666666666, 0.6666666666666666, 0.16666666666666666}, {0.16666666666666666, 0.5, 0.3333333333333333},
            {0.16666666666666666, 0.3333333333333333, 0.5}, {0.16666666666666666, 0.16666666666666666, 0.6666666666666666},
            {0.16666666666666666, 0.0, 0.8333333333333334}, {0.0, 1.0, 0.0},
            {0.0, 0.8333333333333334, 0.16666666666666666}, {0.0, 0.6666666666666666, 0.3333333333333333},
            {0.0, 0.5, 0.5}, {0.0, 0.3333333333333333, 0.6666666666666666},
            {0.0, 0.16666666666666666, 0.8333333333333334}, {0.0, 0.0, 1.0},
        };
        const double expectedH[28] = {
            0.0, 0.09722222125000003, 0.19444444250000004, 0.088888888, 0.217499997315,
            0.17777777599999997, 0.024999999749999998, 0.16240740499092593, 0.13175925759990742,
            0.04999999949999997, -0.04444444400000001, 0.07902777633013888, 0.04999999924999997,
            -0.027361110891805613, -0.08888888800000005, -0.06944444375000003, 0.02999999953999999,
            0.001944444686388859, -0.07277777664055561, -0.13499999843000007, -0.13888888750000009,
            0.0, 0.0995370360416667, 0.09629629533333332, 0.03749999962499998, -0.029629629333333365,
            -0.0578703697916667, 0.0,
        };
        double maxDev = 0.0;
        for (int p = 0; p < 28; ++p) {
            const double h = enthalpyOfMixing(m, {grid[p][0], grid[p][1], grid[p][2]});
            maxDev = std::max(maxDev, std::fabs(h - expectedH[p]));
        }
        std::printf("  ..  N=3 enthalpy grid vs. oncapintada: max deviation = %.3e eV/atom\n", maxDev);
        check(maxDev < 1e-9, "N=3 enthalpy-of-mixing grid matches oncapintada's "
                             "MultiComponentAlloy to full floating-point precision");

        // -- solveDsimMulticomponent(): the same N=3 fixture through the ----
        // full pipeline (M matrix + simplexGrid() + ternaryGrid), not just
        // the bare enthalpyOfMixing() call above.
        // supercellAtomCount=10 -> dilution=0.1, matching the fixture's own
        // computeMMatrix(e, 0.1) / oncapintada's dilution=0.1 above.
        const DsimMulticomponentResult multi =
            solveDsimMulticomponent({"A", "B", "C"}, e, /*supercellAtomCount=*/10, /*resolution=*/6);
        check(multi.ternaryGrid.size() == 28,
              "solveDsimMulticomponent's N=3 grid has the expected point count "
              "(simplexGrid(3, 6))");
        int matched = 0;
        double multiMaxDev = 0.0;
        for (int p = 0; p < 28; ++p) {
            for (const auto& gp : multi.ternaryGrid) {
                if (close(gp.xB, grid[p][1], 1e-9) && close(gp.xC, grid[p][2], 1e-9)) {
                    ++matched;
                    multiMaxDev = std::max(multiMaxDev, std::fabs(gp.enthalpyEvPerAtom - expectedH[p]));
                    break;
                }
            }
        }
        check(matched == 28 && multiMaxDev < 1e-9,
              "every fixture composition is present in solveDsimMulticomponent's "
              "ternary grid with the matching oncapintada enthalpy");
    }

    // -- solveDsimMulticomponent() reduces to solveDsimBinary() for N=2 -----
    {
        const DsimBinaryResult binary = solveDsimBinary("Au", "Pt", 27, -85.983459,
                                                         -174.085758, -89.245464,
                                                         -170.465236, 11);
        const EnergyMatrix e{{-85.983459, -170.465236}, {-89.245464, -174.085758}};
        const DsimMulticomponentResult multi = solveDsimMulticomponent({"Au", "Pt"}, e, 27, 11);
        check(close(multi.mBInA, binary.mBInA, 1e-12) && close(multi.mAInB, binary.mAInB, 1e-12),
              "solveDsimMulticomponent's N=2 M values match solveDsimBinary's");
        check(multi.binaryCurve.size() == binary.curve.size(), "and the curve has the same length");
        double maxDev = 0.0;
        for (std::size_t i = 0; i < multi.binaryCurve.size(); ++i)
            maxDev = std::max(maxDev, std::fabs(multi.binaryCurve[i].enthalpyEvPerAtom
                                                - binary.curve[i].enthalpyEvPerAtom));
        // solveDsimMulticomponent's binaryCurve goes through the general,
        // epsilon-guarded enthalpyOfMixing() (Eq. 6, eps=1e-8), not the
        // exact Eq. 7 formula solveDsimBinary() spells out directly — a
        // real, tiny (~eps, since X_i+X_j=1 always for a binary) systematic
        // difference, not a bug; 1e-9 comfortably clears it while still
        // catching an actual mismatch (which would be orders of magnitude
        // larger).
        check(maxDev < 1e-9, "and every point matches solveDsimBinary's curve to the "
                             "expected epsilon-guard precision");

        // Pairwise curves: for N=2 there is exactly one pair (0,1), and it
        // must equal the binary curve too (same formula, same inputs).
        check(multi.pairwiseCurves.size() == 1, "N=2 has exactly one pairwise curve");
        if (multi.pairwiseCurves.size() == 1) {
            const auto& pc = multi.pairwiseCurves.front();
            check(pc.speciesI == 0 && pc.speciesJ == 1, "indexed (0, 1)");
            double pairDev = 0.0;
            for (std::size_t i = 0; i < pc.curve.size() && i < multi.binaryCurve.size(); ++i)
                pairDev = std::max(pairDev, std::fabs(pc.curve[i].enthalpyEvPerAtom
                                                       - multi.binaryCurve[i].enthalpyEvPerAtom));
            check(pairDev < 1e-12, "and matches the N=2 binary curve exactly");
        }
    }

    // -- Configurational entropy (Eq. 3) -------------------------------------
    {
        check(close(configurationalEntropyEvPerAtomK({0.0, 1.0}), 0.0)
                  && close(configurationalEntropyEvPerAtomK({1.0, 0.0}), 0.0),
              "S_conf vanishes at either pure element");
        const double sHalf = configurationalEntropyEvPerAtomK({0.5, 0.5});
        const double kB = 8.617333262e-5; // eV/K, same CODATA value as core::PhysicalConstants
        check(close(sHalf, kB * std::log(2.0), 1e-12),
              "S_conf(x=1/2) = k_B ln 2 exactly");
        // Maximum at equimolar composition: symmetric neighbors are lower.
        check(sHalf > configurationalEntropyEvPerAtomK({0.4, 0.6})
                  && sHalf > configurationalEntropyEvPerAtomK({0.6, 0.4}),
              "S_conf peaks at the equimolar composition");
    }

    // -- Gibbs free energy of mixing (Eq. 1) ---------------------------------
    {
        const EnergyMatrix e{{-3.500, -5.700}, {-3.550, -5.800}};
        const MMatrix m = computeMMatrix(e, 1.0 / 27.0);
        const std::vector<double> x{0.4, 0.6};
        check(close(gibbsFreeEnergyOfMixingEvPerAtom(m, x, 0.0), enthalpyOfMixing(m, x)),
              "G_mix(x, T=0) == H_mix(x) exactly (Eq. 1 at T=0)");
        check(gibbsFreeEnergyOfMixingEvPerAtom(m, x, 1000.0)
                  < gibbsFreeEnergyOfMixingEvPerAtom(m, x, 100.0),
              "G_mix decreases with temperature (entropy term is -T*S, S > 0)");
    }

    // -- Cross-validation fixture against oncapintada ------------------------
    // The paper's OWN worked Au-Pt example (oncapintada's own repository,
    // examples/testing_subregular_model.ipynb, `model_gpaw`): real GPAW
    // total supercell energies (eV, 3x3x3 fcc, 27 atoms, dilution=1/27,
    // NOT divided by atom count -- this is exactly the fixture that catches
    // a recurrence of the "divided energies -> curve ~27x too small" bug
    // this file used to carry, found when a real MACE Au-Pt run's curve
    // came out far below Fig. 2a of the paper). Reference numbers recorded
    // from a LIVE run of oncapintada.subregular_model.BinaryAlloy (conda
    // env 'onca') on that notebook's own hardcoded energy_matrix -- see
    // tests/dsim_oncapintada_test.py for the generator and the LIVE
    // (skippable) side of this same check.
    {
        const DsimBinaryResult r = solveDsimBinary(
            "Au", "Pt", 27,
            /*E_pureA (Au, total)*/ -85.983459,
            /*E_pureB (Pt, total)*/ -174.085758,
            /*E_BinA  (Pt-in-Au, total)*/ -89.245464,
            /*E_AinB  (Au-in-Pt, total)*/ -170.465236,
            /*points*/ 11);

        // oncapintada's BinaryAlloy.Mij(), same energy matrix/dilution.
        check(close(r.mBInA, 0.0010431111111159908, 1e-12),
              "M_2[1] (Pt-in-Au) matches oncapintada's Mij()[1,0] to full precision");
        check(close(r.mAInB, 0.357473888888876, 1e-10),
              "M_1[2] (Au-in-Pt) matches oncapintada's Mij()[0,1] to full precision");

        // oncapintada's BinaryAlloy.enthalpy_of_mixing(x, unit="eV/atom") on
        // the same x = linspace(0, 1, 11) grid.
        const double oncapintadaEnthalpyEvPerAtom[11] = {
            0.0, 0.0033017570000002795, 0.01157268266666688, 0.022674192333333242,
            0.034467701333332795, 0.044814624999999, 0.051576378666665285,
            0.05261437766666508, 0.04579003733333183, 0.028964772999998997, 0.0,
        };
        double maxDeviation = 0.0;
        for (std::size_t i = 0; i < r.curve.size(); ++i)
            maxDeviation = std::max(maxDeviation,
                std::fabs(r.curve[i].enthalpyEvPerAtom - oncapintadaEnthalpyEvPerAtom[i]));
        std::printf("  ..  DeltaH_mix(x) vs. oncapintada: max deviation = %.3e eV/atom\n",
                   maxDeviation);
        check(maxDeviation < 1e-9,
              "DeltaH_mix(x) curve matches oncapintada's BinaryAlloy to full "
              "floating-point precision");

        // Sanity anchor against Fig. 2a of the paper (Au-Pt): peak around
        // 4-5 kJ/mol near x=0.7, never single-digit-percent of that. A
        // regression back to the old "divide by natoms" bug would put this
        // curve ~27x too small (~0.19 kJ/mol peak) and fail this check.
        const double peakKjPerMol =
            std::max_element(r.curve.begin(), r.curve.end(),
                             [](const DsimCurvePoint& a, const DsimCurvePoint& b) {
                                 return a.enthalpyKjPerMol < b.enthalpyKjPerMol;
                             })
                ->enthalpyKjPerMol;
        check(peakKjPerMol > 3.0 && peakKjPerMol < 7.0,
              "peak DeltaH_mix falls in the paper's Fig. 2a Au-Pt range "
              "(3-7 kJ/mol), not off by a factor of the supercell size");
    }

    // -- Multi-phase alloys (Fe(bcc)-Co(hcp)) --------------------------------
    // A synthetic, hand-derivable fixture (round supercell size, round
    // energies) rather than a real relaxation, so every number below is
    // checked against closed-form algebra, matching this file's own
    // convention. Both branches' pristine energies are chosen so bcc is
    // Fe's stable structure (pristineA_onA < pristineA_onB) and hcp is
    // Co's (pristineB_onB < pristineB_onA), matching the constructor's own
    // Fe(bcc)-Co(hcp) example.
    {
        const DsimPhaseBranchEnergies bccEnergies{
            /*pristineA (Fe, bcc, stable)  */ -100.0, // -10.0 eV/atom
            /*pristineB (Co, on bcc, meta) */ -97.0,  //  -9.7 eV/atom
            /*bInA (Co-in-Fe-bcc)          */ -100.5,
            /*aInB (Fe-in-Co-on-bcc)       */ -97.4,
        };
        const DsimPhaseBranchEnergies hcpEnergies{
            /*pristineA (Fe, on hcp, meta) */ -96.0, //  -9.6 eV/atom
            /*pristineB (Co, hcp, stable)  */ -98.0, //  -9.8 eV/atom
            /*bInA (Co-in-Fe-on-hcp)       */ -96.3,
            /*aInB (Fe-in-Co-hcp)          */ -98.2,
        };
        const DsimMultiPhaseResult mp = solveDsimMultiPhase(
            "Fe", "bcc", bccEnergies, "Co", "hcp", hcpEnergies, /*supercellAtomCount*/ 10,
            /*compositionPoints*/ 11);

        check(mp.speciesA == "Fe" && mp.speciesB == "Co" && mp.phaseA.phaseLabel == "bcc"
                  && mp.phaseB.phaseLabel == "hcp",
              "labels/species pass through unchanged");

        // Each branch is exactly the ordinary binary solve on its own four
        // energies — no separate code path, just solveDsimBinary called
        // twice.
        const DsimBinaryResult bccOnly = solveDsimBinary("Fe", "Co", 10, bccEnergies.pristineATotalEv,
                                                          bccEnergies.pristineBTotalEv,
                                                          bccEnergies.bInATotalEv,
                                                          bccEnergies.aInBTotalEv, 11);
        check(close(mp.phaseA.raw.mBInA, bccOnly.mBInA, 1e-12)
                  && close(mp.phaseA.raw.mAInB, bccOnly.mAInB, 1e-12),
              "the bcc branch's raw M-values match solveDsimBinary called directly on the same four energies");

        // Both raw branches are zero at BOTH ends by Eq. 7's own
        // construction (the pristine supercells are each branch's own
        // zero reference) -- true regardless of which structure is
        // "really" stable.
        check(close(mp.phaseA.raw.curve.front().enthalpyEvPerAtom, 0.0)
                  && close(mp.phaseA.raw.curve.back().enthalpyEvPerAtom, 0.0),
              "the raw bcc-branch curve is zero at both x=0 (Fe) and x=1 (Co-on-bcc)");
        check(close(mp.phaseB.raw.curve.front().enthalpyEvPerAtom, 0.0)
                  && close(mp.phaseB.raw.curve.back().enthalpyEvPerAtom, 0.0),
              "the raw hcp-branch curve is zero at both x=0 (Fe-on-hcp) and x=1 (Co)");

        // The bcc branch is native to Fe: unshifted at x=0, shifted at
        // x=1 by EXACTLY Co's own bcc-vs-hcp energy difference per atom
        // -- the user's own worked description of the feature.
        const double coLatticeStabilityEvPerAtom =
            bccEnergies.pristineBTotalEv / 10.0 - hcpEnergies.pristineBTotalEv / 10.0;
        check(close(coLatticeStabilityEvPerAtom, 0.1, 1e-12),
              "fixture sanity: Co is 0.1 eV/atom higher in bcc than in hcp");
        check(close(mp.phaseA.shift.atX0Ev, 0.0),
              "the bcc branch (native to Fe) has no shift at x=0");
        check(close(mp.phaseA.shift.atX1Ev, coLatticeStabilityEvPerAtom, 1e-12),
              "and its x=1 shift is exactly Co's bcc-hcp lattice stability, eV/atom");
        check(close(mp.phaseA.corrected.curve.front().enthalpyEvPerAtom, 0.0),
              "so the CORRECTED bcc-branch curve is still zero at Fe (x=0)");
        check(close(mp.phaseA.corrected.curve.back().enthalpyEvPerAtom, coLatticeStabilityEvPerAtom,
                    1e-12),
              "but has a FINITE value at Co (x=1) equal to that lattice stability -- "
              "exactly the behaviour requested for Fe(bcc)-Co(hcp)");

        // Symmetrically, the hcp branch is native to Co.
        const double feLatticeStabilityEvPerAtom =
            hcpEnergies.pristineATotalEv / 10.0 - bccEnergies.pristineATotalEv / 10.0;
        check(close(feLatticeStabilityEvPerAtom, 0.4, 1e-12),
              "fixture sanity: Fe is 0.4 eV/atom higher in hcp than in bcc");
        check(close(mp.phaseB.shift.atX1Ev, 0.0),
              "the hcp branch (native to Co) has no shift at x=1");
        check(close(mp.phaseB.shift.atX0Ev, feLatticeStabilityEvPerAtom, 1e-12),
              "and its x=0 shift is exactly Fe's hcp-bcc lattice stability, eV/atom");
        check(close(mp.phaseB.corrected.curve.back().enthalpyEvPerAtom, 0.0),
              "so the CORRECTED hcp-branch curve is still zero at Co (x=1)");
        check(close(mp.phaseB.corrected.curve.front().enthalpyEvPerAtom, feLatticeStabilityEvPerAtom,
                    1e-12),
              "but has a finite value at Fe (x=0) equal to Fe's own lattice stability");

        // A linear-in-x shift moves endpoint VALUES, never the tangent
        // SLOPES at either end.
        check(close(mp.phaseA.raw.dHdxAt0, mp.phaseA.corrected.dHdxAt0, 1e-12)
                  && close(mp.phaseA.raw.dHdxAt1, mp.phaseA.corrected.dHdxAt1, 1e-12),
              "the shift changes curve values but not the dilute-limit tangent slopes");
    }

    if (failures == 0) {
        std::printf("\nAll DSIM checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d DSIM check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
