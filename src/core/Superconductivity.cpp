#include "core/Superconductivity.hpp"

#include <cmath>
// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

namespace calango::core {

namespace {

constexpr double kBoltzmannEvPerK = 8.617333262e-5;

} // namespace

SuperconductingResult
estimateSuperconductingTc(const SuperconductingInput& input)
{
    SuperconductingResult result;

    const double lambda = input.lambda;
    const double mu = input.muStar;
    result.muStar = mu;
    if (!(lambda > 0.0) || !std::isfinite(lambda)) {
        result.warnings.push_back(
            "The coupling constant is zero or not finite, so there is no "
            "electron-phonon superconductivity to estimate.");
        return result;
    }
    if (input.omegaLogEv <= 0.0) {
        result.warnings.push_back(
            "omega_log is zero: the phonon spectrum carries no weight, so "
            "there is no scale for T_c to be set by.");
        return result;
    }
    if (mu < 0.0 || mu > 0.5) {
        result.warnings.push_back(
            "mu* outside the physical range 0-0.5; 0.10-0.15 is the "
            "conventional choice for sp metals and somewhat higher for "
            "transition metals.");
    }

    // The denominator of the exponent. When it reaches zero the phonon
    // attraction no longer beats the screened Coulomb repulsion and the
    // formula has no solution — the correct answer is "not a superconductor
    // here", not a T_c that rounds to zero.
    const double denominator = lambda - mu * (1.0 + 0.62 * lambda);
    if (denominator <= 0.0) {
        result.warnings.push_back(
            "lambda <= mu*(1 + 0.62 lambda): the screened Coulomb repulsion "
            "exceeds the phonon attraction, so the McMillan/Allen-Dynes "
            "formula has no solution. This material is not a phonon-mediated "
            "superconductor at this coupling — which is a result, not a "
            "failure. Note that mu* is empirical: a smaller value would move "
            "this boundary.");
        return result;
    }

    const double exponent = -1.04 * (1.0 + lambda) / denominator;
    const double factor = std::exp(exponent);

    // McMillan's original: theta_D/1.45. Kept because it is what the older
    // literature quotes, but theta_D is a fit to a DIFFERENT measurement
    // (usually low-temperature specific heat) and is not a property of the
    // spectrum computed here, so it is the weaker of the two.
    if (input.debyeTemperatureK > 0.0)
        result.tcMcMillanK = input.debyeTemperatureK / 1.45 * factor;

    const double omegaLogK = input.omegaLogEv / kBoltzmannEvPerK;
    result.tcAllenDynesK = omegaLogK / 1.2 * factor;

    // -- Strong-coupling corrections ---------------------------------------
    //
    // Allen and Dynes found the McMillan fit systematically low for large
    // lambda, because McMillan fitted it against niobium's spectrum alone and
    // then applied it everywhere. Two multiplicative corrections repair that:
    //
    //   f1 — the strong-coupling correction, which grows with lambda;
    //   f2 — the spectral-shape correction, which is 1 for a spectrum with
    //        omega_2 == omega_log and departs from it as the spectrum
    //        broadens away from a single peak.
    //
    // Both are 1 to within a per cent in the weak-coupling limit, which is
    // why the uncorrected number is still reported alongside.
    const double lambda1 = 2.46 * (1.0 + 3.8 * mu);
    result.f1 = std::cbrt(1.0 + std::pow(lambda / lambda1, 1.5));

    if (input.omegaBar2Ev > 0.0 && input.omegaLogEv > 0.0) {
        const double ratio = input.omegaBar2Ev / input.omegaLogEv;
        const double lambda2 = 1.82 * (1.0 + 6.3 * mu) * ratio;
        result.f2 = 1.0
            + ((ratio - 1.0) * lambda * lambda)
                / (lambda * lambda + lambda2 * lambda2);
    } else {
        result.warnings.push_back(
            "No second moment omega_2 was supplied, so the Allen-Dynes "
            "spectral-shape correction f2 was skipped (taken as 1).");
    }
    result.tcAllenDynesCorrectedK = result.f1 * result.f2 * result.tcAllenDynesK;

    // -- The gap ------------------------------------------------------------
    // BCS weak coupling gives 2*Delta/k_B T_c = 3.53. Strong coupling raises
    // it; the standard interpolation adds a term in (T_c/omega_log)^2 ln(...).
    // Applied only where it is meaningful, and the ratio used is reported so
    // the number is not mistaken for a pure BCS one.
    double ratioGap = 3.53;
    if (result.tcAllenDynesCorrectedK > 0.0 && omegaLogK > 0.0) {
        const double x = result.tcAllenDynesCorrectedK / omegaLogK;
        if (x > 0.0 && x < 0.25)
            ratioGap = 3.53
                * (1.0 + 12.5 * x * x * std::log(1.0 / (2.0 * x)));
    }
    result.gapRatio = ratioGap;
    result.gapMeV = 0.5 * ratioGap * kBoltzmannEvPerK
        * result.tcAllenDynesCorrectedK * 1000.0;

    // -- What the number is worth ------------------------------------------
    //
    // These are the caveats that decide whether a computed T_c means
    // anything, so they travel with it rather than living in documentation.
    result.warnings.push_back(
        "T_c depends EXPONENTIALLY on mu*, which is empirical and not "
        "computed here. At mu* = " + std::to_string(mu).substr(0, 4)
        + ", moving it by 0.02 typically changes T_c by tens of per cent; "
          "quote a range over mu* = 0.10-0.15 rather than a single value.");
    if (lambda > 1.5)
        result.warnings.push_back(
            "lambda > 1.5 is beyond where the McMillan fit was ever tested. "
            "The Allen-Dynes f1/f2 corrections extend it, but at this "
            "coupling a direct solution of the Eliashberg equations is the "
            "defensible route and this closed form is an estimate.");
    if (lambda < 0.3)
        result.warnings.push_back(
            "lambda < 0.3 puts T_c in the regime where it is exponentially "
            "small and exponentially sensitive to every input, including the "
            "k-mesh alpha^2F was integrated on. Treat the value as an order "
            "of magnitude.");

    // -- T_c across the defensible range of mu* -----------------------------
    //
    // Always computed. mu* is empirical and this is the only honest way to
    // present a number that depends on it exponentially: the reader sees the
    // spread rather than a single value carrying invented precision.
    for (int i = 0; i <= 8; ++i) {
        const double sweepMu = 0.08 + 0.01 * i; // 0.08 to 0.16
        const double sweepDenominator =
            lambda - sweepMu * (1.0 + 0.62 * lambda);
        if (sweepDenominator <= 0.0) {
            result.tcVsMuStar.emplace_back(sweepMu, 0.0);
            continue;
        }
        const double sweepF1 =
            std::cbrt(1.0 + std::pow(lambda / (2.46 * (1.0 + 3.8 * sweepMu)),
                                     1.5));
        double sweepF2 = 1.0;
        if (input.omegaBar2Ev > 0.0) {
            const double ratio = input.omegaBar2Ev / input.omegaLogEv;
            const double l2 = 1.82 * (1.0 + 6.3 * sweepMu) * ratio;
            sweepF2 = 1.0
                + ((ratio - 1.0) * lambda * lambda)
                    / (lambda * lambda + l2 * l2);
        }
        result.tcVsMuStar.emplace_back(
            sweepMu,
            sweepF1 * sweepF2 * omegaLogK / 1.2
                * std::exp(-1.04 * (1.0 + lambda) / sweepDenominator));
    }

    result.ok = true;
    return result;
}

double morelAndersonMuStar(double bareMu, double electronicScaleEv,
                           double phononScaleEv)
{
    if (!(bareMu > 0.0) || !(electronicScaleEv > 0.0)
        || !(phononScaleEv > 0.0) || electronicScaleEv <= phononScaleEv)
        return 0.0;
    return bareMu
        / (1.0 + bareMu * std::log(electronicScaleEv / phononScaleEv));
}

} // namespace calango::core
