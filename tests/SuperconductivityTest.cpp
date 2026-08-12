// T_c from McMillan / Allen-Dynes.
//
// Anchored OUTSIDE the code twice over:
//
//  1. The formulas are closed forms, so they are checked against arithmetic
//     done here independently rather than against a previous run.
//  2. The results are checked against MEASURED transition temperatures of real
//     superconductors, using lambda and omega_log from the literature (Allen &
//     Dynes 1975 and the tabulations that followed) as inputs. If the chain
//     from those inputs to T_c is right, Pb comes out near 7.2 K and Al near
//     1.2 K, because that is what those materials do.
//
// The second is the one that would catch a transcription error in the fit
// constants, which the first cannot.

#include "core/Superconductivity.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

constexpr double kBoltzmannEvPerK = 8.617333262e-5;

double kelvinToEv(double kelvin)
{
    return kelvin * kBoltzmannEvPerK;
}

} // namespace

int main()
{
    using calango::core::estimateSuperconductingTc;
    using calango::core::SuperconductingInput;

    // -- The closed form, against arithmetic done here ----------------------
    std::printf("The Allen-Dynes formula itself:\n");
    {
        SuperconductingInput in;
        in.lambda = 1.0;
        in.omegaLogEv = kelvinToEv(200.0);
        in.muStar = 0.10;
        const auto out = estimateSuperconductingTc(in);
        check(out.ok, "a plain case evaluates");

        // Worked independently: exponent = -1.04*2.0/(1.0 - 0.1*1.62)
        //                                = -2.08/0.838 = -2.482100...
        const double denominator = 1.0 - 0.10 * (1.0 + 0.62);
        const double expected =
            200.0 / 1.2 * std::exp(-1.04 * 2.0 / denominator);
        check(std::abs(out.tcAllenDynesK - expected) < 1e-9 * expected,
              "T_c = (omega_log/1.2) exp(-1.04(1+l)/(l - mu*(1+0.62l))) "
              "exactly");
        // f1 with no omega_2 supplied must leave f2 at exactly 1, and say so.
        check(out.f2 == 1.0,
              "f2 is exactly 1 when no second moment is given");
        check(!out.warnings.empty(),
              "and that omission is reported rather than assumed harmless");
    }

    // -- Lead: strong coupling, the case McMillan's fit gets wrong -----------
    //
    // lambda = 1.55, omega_log = 56 K are the standard tabulated values; the
    // measured T_c is 7.19 K. Pb is the canonical strong-coupling test,
    // which is precisely where the f1/f2 corrections are supposed to earn
    // their place — so the UNCORRECTED number should undershoot and the
    // corrected one should land.
    std::printf("Lead (strong coupling, T_c = 7.19 K measured):\n");
    {
        SuperconductingInput in;
        in.lambda = 1.55;
        in.omegaLogEv = kelvinToEv(56.0);
        in.omegaBar2Ev = kelvinToEv(56.0 * 1.34); // omega_2/omega_log ~ 1.34
        in.muStar = 0.10;
        const auto out = estimateSuperconductingTc(in);
        std::printf("    uncorrected %.2f K, f1 = %.4f, f2 = %.4f, "
                    "corrected %.2f K\n",
                    out.tcAllenDynesK, out.f1, out.f2,
                    out.tcAllenDynesCorrectedK);
        check(out.ok, "lead evaluates");
        check(out.f1 > 1.0 && out.f2 > 1.0,
              "both strong-coupling corrections exceed 1, as they must for "
              "lambda = 1.55");
        check(out.tcAllenDynesK < out.tcAllenDynesCorrectedK,
              "so the corrected T_c is the larger — McMillan's fit "
              "undershoots at strong coupling, which is why Allen and Dynes "
              "wrote the corrections");
        check(std::abs(out.tcAllenDynesCorrectedK - 7.19) < 1.5,
              "and lands within 1.5 K of the measured 7.19 K");
        check(out.gapMeV > 0.0 && out.gapRatio >= 3.53,
              "the gap ratio is at or above the BCS 3.53, never below it");
    }

    // -- Aluminium: weak coupling, and the mu* sensitivity ------------------
    //
    // lambda = 0.43, omega_log = 296 K, measured T_c = 1.18 K. Al needs
    // mu* ~ 0.12 to land; at 0.10 it overshoots and at 0.14 it undershoots by
    // a factor of two either way. That is not a defect of this code — it is
    // the exponential mu* sensitivity the module warns about, and it is
    // asserted here so the warning cannot be quietly dropped.
    std::printf("Aluminium (weak coupling, T_c = 1.18 K measured):\n");
    {
        SuperconductingInput in;
        in.lambda = 0.43;
        in.omegaLogEv = kelvinToEv(296.0);
        in.muStar = 0.12;
        const auto out = estimateSuperconductingTc(in);
        std::printf("    T_c(mu* = 0.12) = %.3f K\n", out.tcAllenDynesK);
        check(out.ok, "aluminium evaluates");
        check(std::abs(out.tcAllenDynesK - 1.18) < 0.4,
              "T_c lands within 0.4 K of the measured 1.18 K at mu* = 0.12");

        SuperconductingInput low = in;
        low.muStar = 0.10;
        SuperconductingInput high = in;
        high.muStar = 0.14;
        const double tcLow = estimateSuperconductingTc(low).tcAllenDynesK;
        const double tcHigh = estimateSuperconductingTc(high).tcAllenDynesK;
        std::printf("    mu* 0.10 -> %.3f K, 0.14 -> %.3f K (ratio %.1fx)\n",
                    tcLow, tcHigh, tcLow / tcHigh);
        check(tcLow > 2.0 * tcHigh,
              "moving mu* from 0.10 to 0.14 changes T_c by more than a "
              "factor of two — the exponential sensitivity that makes a "
              "single quoted T_c misleading");
        check(!out.warnings.empty(),
              "which the result warns about rather than leaving to be "
              "discovered");
    }

    // -- The mu* sweep -------------------------------------------------------
    //
    // The answer to "what mu* should I use?" is "report a range", so the
    // range is always produced and is checked to be monotonic: raising the
    // Coulomb repulsion can only lower T_c, and a sweep that did not fall
    // would mean a sign error somewhere in the exponent.
    std::printf("The mu* sweep:\n");
    {
        SuperconductingInput in;
        in.lambda = 0.43;
        in.omegaLogEv = kelvinToEv(296.0);
        const auto out = estimateSuperconductingTc(in);
        check(out.tcVsMuStar.size() >= 5,
              "a T_c-vs-mu* curve is produced on every run, because a single "
              "T_c implies a precision that mu* does not permit");
        bool falling = true;
        for (std::size_t i = 1; i < out.tcVsMuStar.size(); ++i)
            if (out.tcVsMuStar[i].second > out.tcVsMuStar[i - 1].second)
                falling = false;
        check(falling,
              "and falls monotonically with mu* — more Coulomb repulsion can "
              "only lower T_c");
        std::printf("    mu* %.2f -> %.3f K, mu* %.2f -> %.3f K\n",
                    out.tcVsMuStar.front().first,
                    out.tcVsMuStar.front().second,
                    out.tcVsMuStar.back().first,
                    out.tcVsMuStar.back().second);
    }

    // -- Morel-Anderson ------------------------------------------------------
    {
        using calango::core::morelAndersonMuStar;
        // mu = 0.4, W = 11 eV, omega_log = 25 meV: ln(440) = 6.087,
        // mu* = 0.4/(1 + 0.4*6.087) = 0.4/3.435 = 0.1165.
        const double mu = morelAndersonMuStar(0.4, 11.0, 0.025);
        const double expected = 0.4 / (1.0 + 0.4 * std::log(11.0 / 0.025));
        check(std::abs(mu - expected) < 1e-12,
              "mu* = mu/(1 + mu ln(W/omega)) exactly");
        check(mu > 0.10 && mu < 0.13,
              "and a bare mu of 0.4 on a typical metal lands in the "
              "conventional 0.10-0.13 window — which is WHY that window is "
              "conventional, not a coincidence");
        check(morelAndersonMuStar(0.4, 0.01, 0.025) == 0.0,
              "an electronic scale below the phonon scale is refused: there "
              "is no retardation to exploit and the formula is meaningless");
        check(morelAndersonMuStar(0.0, 11.0, 0.025) == 0.0,
              "and a zero bare mu gives nothing rather than dividing");
    }

    // -- Not a superconductor -----------------------------------------------
    //
    // The interesting failure. When lambda <= mu*(1+0.62 lambda) the exponent
    // is singular; returning T_c = 0 would be indistinguishable from a
    // converged calculation of a very low T_c.
    std::printf("The non-superconducting case:\n");
    {
        SuperconductingInput in;
        in.lambda = 0.10;
        in.omegaLogEv = kelvinToEv(300.0);
        in.muStar = 0.13;
        const auto out = estimateSuperconductingTc(in);
        check(!out.ok,
              "lambda below mu*(1 + 0.62 lambda) is refused, not returned as "
              "T_c = 0 that reads like a converged tiny number");
        check(out.tcAllenDynesK == 0.0, "with no T_c reported at all");
        bool explained = false;
        for (const std::string& warning : out.warnings)
            if (warning.find("Coulomb repulsion") != std::string::npos)
                explained = true;
        check(explained,
              "and the reason named: the screened repulsion beats the "
              "phonon attraction");
    }

    // -- Degenerate inputs ---------------------------------------------------
    {
        SuperconductingInput in;
        in.lambda = 0.0;
        in.omegaLogEv = kelvinToEv(300.0);
        check(!estimateSuperconductingTc(in).ok,
              "zero coupling is refused");
        in.lambda = 1.0;
        in.omegaLogEv = 0.0;
        check(!estimateSuperconductingTc(in).ok,
              "and so is a spectrum with no weight — there is no scale for "
              "T_c to be set by");
    }

    // -- McMillan's own form, when a Debye temperature is available ---------
    {
        SuperconductingInput in;
        in.lambda = 1.0;
        in.omegaLogEv = kelvinToEv(200.0);
        in.debyeTemperatureK = 300.0;
        in.muStar = 0.10;
        const auto out = estimateSuperconductingTc(in);
        const double denominator = 1.0 - 0.10 * 1.62;
        const double expected =
            300.0 / 1.45 * std::exp(-1.04 * 2.0 / denominator);
        check(std::abs(out.tcMcMillanK - expected) < 1e-9 * expected,
              "the McMillan form uses theta_D/1.45, reported beside the "
              "Allen-Dynes value because the older literature quotes it");

        SuperconductingInput without = in;
        without.debyeTemperatureK = 0.0;
        check(estimateSuperconductingTc(without).tcMcMillanK == 0.0,
              "and is skipped entirely without a Debye temperature rather "
              "than invented from omega_log");
    }

    if (failures == 0) {
        std::printf("\nAll superconductivity checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d superconductivity check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
