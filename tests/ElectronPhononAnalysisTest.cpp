// alpha^2F, lambda and the relaxation time, from raw electron-phonon data.
//
// The whole chain has a closed form for one contrived-but-exact case, and
// that is what this checks rather than a previous run of the code.
//
// Take a free-electron band, ONE phonon mode at a fixed frequency w0, and a
// CONSTANT |g|^2 = g0^2. Then the pair weight of the single mode is
//
//     weight = g0^2 zeta(q) / (N(E_F) N_q)
//
// with zeta the nesting function the tetrahedron tests already pin against
// its own closed form, zeta(q) = V_cell / (16 pi^2 A^2 q). So
//
//     lambda = 2 weight / w0
//
// end to end, with no fitted quantity anywhere. Getting the normalization,
// the band sums or the 1/N(E_F) wrong all show up here as a factor.

#include "core/ElectronPhononAnalysis.hpp"
#include "core/TetrahedronBz.hpp"

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

constexpr double kPi = 3.14159265358979323846;
constexpr double kHbar2Over2m = 3.80998212; // eV * A^2
constexpr double kBoltzmann = 8.617333262e-5;
constexpr double kHbarEvFs = 0.6582119569;

} // namespace

int main()
{
    using calango::core::ElectronPhononInput;
    using calango::core::TetrahedronBz;

    // -- The model system ---------------------------------------------------
    const double a = 4.0;
    const double b = 2.0 * kPi / a;
    const int m = 24;
    const int shift = 3; // q along b1, three grid steps
    const double fermi = 1.2;
    const double omega0 = 0.025; // 25 meV, a plausible acoustic scale
    const double g0Squared = 1.0e-4; // eV^2

    ElectronPhononInput input;
    input.kGrid = {m, m, m};
    input.reciprocal = {{{b, 0.0, 0.0}, {0.0, b, 0.0}, {0.0, 0.0, b}}};
    input.spins = 1;
    input.bands = 1;
    input.qCount = 1;
    input.modes = 1;
    input.fermiLevelEv = fermi;
    input.temperatureK = 300.0;
    input.phononFrequenciesEv = {omega0};

    const std::size_t nk = input.kPointCount();
    TetrahedronBz bz(input.kGrid, input.reciprocal);
    const int half = m / 2;
    const auto fold = [m, half](int i) {
        return static_cast<double>((i + half) % m - half)
            / static_cast<double>(m);
    };
    input.eigenvalues.assign(nk, 0.0);
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < m; ++j)
            for (int k = 0; k < m; ++k) {
                const double kx = fold(i) * b;
                const double ky = fold(j) * b;
                const double kz = fold(k) * b;
                input.eigenvalues[bz.index(i, j, k)] =
                    kHbar2Over2m * (kx * kx + ky * ky + kz * kz);
            }
    // k + q, as an index map — the same thing the generated script builds to
    // index the matrix elements.
    input.kPlusQ.assign(nk, 0);
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < m; ++j)
            for (int k = 0; k < m; ++k)
                input.kPlusQ[bz.index(i, j, k)] =
                    static_cast<int>(bz.index(i + shift, j, k));
    input.gSquaredEv2.assign(nk, g0Squared);

    // -- What the closed form says ------------------------------------------
    const double q = shift * b / m;
    const double volume = a * a * a;
    const double nesting =
        volume / (16.0 * kPi * kPi * kHbar2Over2m * kHbar2Over2m * q);
    const double dosExact = (volume / (4.0 * kPi * kPi))
        * std::pow(1.0 / kHbar2Over2m, 1.5) * std::sqrt(fermi);
    const double weightExact = g0Squared * nesting / dosExact;
    const double lambdaExact = 2.0 * weightExact / omega0;

    const auto result = calango::core::analyzeElectronPhonon(input);
    check(result.ok, "the analysis runs");
    std::printf("    N(E_F) %.6f (exact %.6f), lambda %.6f (exact %.6f)\n",
                result.dosAtFermi, dosExact, result.lambda, lambdaExact);

    check(std::abs(result.dosAtFermi - dosExact) / dosExact < 0.02,
          "N(E_F) matches the closed-form free-electron DOS");
    check(std::abs(result.lambda - lambdaExact) / lambdaExact < 0.05,
          "and lambda matches 2 g0^2 zeta(q) / (N(E_F) w0) end to end — "
          "normalization, band sums and the 1/N(E_F) all included");

    // omega_log of a single mode is that mode.
    check(std::abs(result.omegaLogEv - omega0) < 1e-9,
          "omega_log of one mode is its own frequency");

    // -- The relaxation time is exactly Allen's relation --------------------
    const double rateExact =
        2.0 * kPi * result.lambda * kBoltzmann * input.temperatureK;
    check(std::abs(result.scatteringRateEv - rateExact) < 1e-12,
          "hbar/tau = 2 pi lambda k_B T exactly");
    check(std::abs(result.relaxationTimeFs - kHbarEvFs / rateExact)
              < 1e-9 * result.relaxationTimeFs,
          "and tau is hbar over it");
    // The handoff to the optics module. TWO things have to be right, and the
    // second is the one that is easy to get wrong without anything looking
    // wrong afterwards.
    //
    // (a) the factor of two: GPAW damps as omega_p^2/(omega + i*rate)^2 while
    //     the textbook form has Gamma = hbar/tau, so rate = hbar/2tau; and
    // (b) it must be built on the TRANSPORT lifetime. A Drude term describes
    //     how a CURRENT decays, and a current survives forward scattering
    //     that the quasiparticle lifetime still counts. Using lambda instead
    //     of lambda_tr gives a Drude peak of the wrong width that looks
    //     perfectly ordinary.
    check(result.drudeRateFromTransport,
          "the Drude rate is built on the transport lifetime, not the "
          "quasiparticle one");
    check(std::abs(result.drudeRateEv
                   - 0.5 * result.scatteringRateTransportEv) < 1e-15,
          "and is exactly half of hbar/tau_tr");
    check(std::abs(result.drudeRateEv - 0.5 * result.scatteringRateEv) > 1e-12,
          "which differs from half the quasiparticle rate — the two are not "
          "interchangeable, and this test would pass on either if it only "
          "checked the factor of two");

    // -- No smearing parameter anywhere -------------------------------------
    // The point of stage 3. The phonon smearing only DRAWS alpha^2F; changing
    // it by a factor of five must leave lambda untouched, because lambda is
    // summed over modes rather than integrated over the smeared spectrum.
    {
        ElectronPhononInput wide = input;
        wide.phononSmearingEv = input.phononSmearingEv * 5.0;
        const auto other = calango::core::analyzeElectronPhonon(wide);
        check(std::abs(other.lambda - result.lambda) < 1e-12,
              "lambda is independent of the phonon smearing, which only "
              "draws the spectrum");
        check(other.alpha2F != result.alpha2F,
              "though the spectrum itself does change with it");
    }

    // -- The spectrum -------------------------------------------------------
    {
        check(result.alpha2F.size() == result.omegaEv.size()
                  && result.alpha2F.size() > 10,
              "alpha^2F is on a frequency grid of its own length");
        check(std::all_of(result.alpha2F.begin(), result.alpha2F.end(),
                          [](double v) { return v >= 0.0; }),
              "and is non-negative, as a spectral function must be");
        // One mode means one peak, and it must sit at that mode.
        const auto peak = std::max_element(result.alpha2F.begin(),
                                           result.alpha2F.end());
        const double peakOmega =
            result.omegaEv[std::distance(result.alpha2F.begin(), peak)];
        check(std::abs(peakOmega - omega0) < 3.0 * input.phononSmearingEv,
              "with its single peak at the single mode's frequency");
    }

    // -- Mode-resolved coupling and linewidths ------------------------------
    //
    // These must ADD UP to the totals, which is the only thing that can go
    // wrong with them: a per-mode quantity that does not reconstruct lambda
    // is normalized wrongly and would be quoted as if it were comparable
    // with a published lambda_qnu.
    std::printf("Mode-resolved quantities:\n");
    {
        check(result.lambdaPerMode.size() == 1
                  && result.linewidthsEv.size() == 1,
              "there is one lambda_qnu and one linewidth per mode");
        double sum = 0.0;
        for (const double v : result.lambdaPerMode)
            sum += v;
        check(std::abs(sum / input.qCount - result.lambda)
                  < 1e-12 * result.lambda,
              "and (1/N_q) sum lambda_qnu reconstructs lambda exactly");
        // gamma = pi N(E_F) w^2 lambda_qnu, checked against the arithmetic
        // rather than against itself.
        const double expected = kPi * result.dosAtFermi * omega0 * omega0
            * result.lambdaPerMode[0];
        check(std::abs(result.linewidthsEv[0] - expected)
                  < 1e-12 * std::abs(expected),
              "the linewidth is pi N(E_F) omega^2 lambda_qnu");
        check(result.linewidthsEv[0] > 0.0,
              "and is positive — it is a decay rate into electron-hole "
              "pairs, measurable by inelastic neutron scattering");

        // omega_2 of a single mode is that mode, exactly as omega_log is.
        check(std::abs(result.omegaBar2Ev - omega0) < 1e-9,
              "omega_2 of one mode is its own frequency");
        check(std::abs(result.massEnhancement - (1.0 + result.lambda)) < 1e-15,
              "and the mass enhancement is 1 + lambda");
    }

    // -- Transport: an EXACT closed form ------------------------------------
    //
    // For free electrons, both scattering states lie on the Fermi sphere, so
    // |k| = |k+q| = k_F. Then
    //
    //     k . (k+q) = k_F^2 + k.q,   and   |k+q|^2 = k_F^2
    //                                 =>   k.q = -q^2/2
    //
    // so k.(k+q) = k_F^2 - q^2/2 and, since v is parallel to k,
    //
    //     1 - cos(theta) = q^2 / (2 k_F^2)      EXACTLY,
    //
    // the SAME value everywhere on the intersection ring. So lambda_tr must
    // equal lambda times that constant — a closed form for the transport
    // weight, not merely a bound. Any error in the reciprocal-basis transform
    // of the velocities rotates the vectors and breaks it.
    std::printf("Transport:\n");
    {
        const double kFermi = std::sqrt(fermi / kHbar2Over2m);
        const double expectedRatio = q * q / (2.0 * kFermi * kFermi);
        const double ratio = result.lambdaTransport / result.lambda;
        std::printf("    lambda %.6f, lambda_tr %.6f, ratio %.6f "
                    "(exact q^2/2k_F^2 = %.6f)\n",
                    result.lambda, result.lambdaTransport, ratio,
                    expectedRatio);
        check(std::abs(ratio - expectedRatio) < 0.02 * expectedRatio,
              "lambda_tr/lambda = q^2/(2 k_F^2) exactly, as it must be for a "
              "free-electron sphere — this pins the velocity directions and "
              "the reciprocal-basis transform together");
        // NOT a general bound. 1 - cos(theta) is in [0, 2], so lambda_tr can
        // exceed lambda when backscattering dominates (q approaching 2k_F).
        // Here q is small, so forward scattering dominates and lambda_tr is
        // the smaller — but the only inequality that always holds is the
        // factor of two, which is what is asserted as a bound.
        check(result.lambdaTransport < result.lambda,
              "at this small q, forward scattering dominates and lambda_tr < "
              "lambda");
        check(result.lambdaTransport <= 2.0 * result.lambda,
              "and lambda_tr <= 2 lambda always, since 1 - cos(theta) <= 2 — "
              "the only bound that holds for every q");
        check(result.velocityDegenerateStates == 0,
              "with no state left without a velocity direction");

        // tau_tr is longer than tau precisely because lambda_tr is smaller:
        // it takes more scattering events to degrade a current than to
        // renormalize a mass.
        check(result.relaxationTimeTransportFs > result.relaxationTimeFs,
              "tau_tr exceeds tau — a current survives forward scattering "
              "that the mass enhancement still counts");

        // Resistivity is skipped without omega_p rather than guessed.
        check(result.resistivityMicroOhmCm == 0.0,
              "no resistivity without a plasma frequency, since rho goes as "
              "1/omega_p^2");
        ElectronPhononInput withPlasma = input;
        withPlasma.plasmaFrequencyEv = 15.8; // free-electron Al
        const auto metal = calango::core::analyzeElectronPhonon(withPlasma);
        check(metal.resistivityMicroOhmCm > 0.0,
              "and a resistivity once one is supplied");
        // rho = 1/(eps_0 omega_p^2 tau_tr), checked against the arithmetic.
        const double omegaP = 15.8 / 6.582119569e-16;
        const double expectedRho =
            1.0e8
            / (8.8541878128e-12 * omegaP * omegaP
               * metal.relaxationTimeTransportFs * 1e-15);
        check(std::abs(metal.resistivityMicroOhmCm - expectedRho)
                  < 1e-9 * expectedRho,
              "equal to 1/(eps_0 omega_p^2 tau_tr) in micro-ohm cm");
    }

    // -- Imaginary modes ----------------------------------------------------
    std::printf("Imaginary and degenerate cases:\n");
    {
        ElectronPhononInput unstable = input;
        unstable.modes = 2;
        unstable.phononFrequenciesEv = {omega0, -0.004};
        unstable.gSquaredEv2.assign(nk * 2, g0Squared);
        const auto other = calango::core::analyzeElectronPhonon(unstable);
        check(other.ok, "a structure with an imaginary mode still analyses");
        check(other.excludedModes == 1, "with that mode excluded and counted");
        check(std::isfinite(other.lambda) && other.lambda > 0.0,
              "and lambda stays finite — the NaN this module used to report "
              "came from writing imaginary frequencies into the grid");
        check(std::abs(other.lambda - result.lambda) < 1e-9,
              "the stable mode contributes exactly as before");
        check(!other.warnings.empty(), "and the exclusion is reported");
    }

    // A gapped system has no Fermi surface for this to be about.
    {
        ElectronPhononInput gapped = input;
        gapped.fermiLevelEv = -5.0; // below the band entirely
        const auto other = calango::core::analyzeElectronPhonon(gapped);
        check(!other.ok && !other.warnings.empty(),
              "a Fermi level outside the band is refused with a reason, not "
              "returned as a zero that reads like weak coupling");
    }

    // Malformed input must not read past the end of an array.
    {
        ElectronPhononInput truncated = input;
        truncated.bands = 4; // arrays no longer match the declared shape
        const auto other = calango::core::analyzeElectronPhonon(truncated);
        check(!other.ok, "input shorter than its declared dimensions is "
                         "refused rather than read past");
    }

    if (failures == 0) {
        std::printf("\nAll electron-phonon analysis checks passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d electron-phonon analysis check(s) FAILED.\n", failures);
    return EXIT_FAILURE;
}
