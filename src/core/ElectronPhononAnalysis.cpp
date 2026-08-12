#include "core/ElectronPhononAnalysis.hpp"

#include "core/TetrahedronBz.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace calango::core {

namespace {

constexpr double kBoltzmannEvPerK = 8.617333262e-5;
constexpr double kHbarEvFs = 0.6582119569;
constexpr double kPi = 3.14159265358979323846;
constexpr double kHbarEvS = 6.582119569e-16;
constexpr double kVacuumPermittivity = 8.8541878128e-12; // F/m

/// Invert the 3x3 reciprocal matrix. Returns false if it is singular.
bool invert3x3(const std::array<std::array<double, 3>, 3>& in,
               std::array<std::array<double, 3>, 3>& out)
{
    const double det =
        in[0][0] * (in[1][1] * in[2][2] - in[1][2] * in[2][1])
        - in[0][1] * (in[1][0] * in[2][2] - in[1][2] * in[2][0])
        + in[0][2] * (in[1][0] * in[2][1] - in[1][1] * in[2][0]);
    if (std::abs(det) < 1e-30)
        return false;
    const double inv = 1.0 / det;
    out[0][0] = (in[1][1] * in[2][2] - in[1][2] * in[2][1]) * inv;
    out[0][1] = (in[0][2] * in[2][1] - in[0][1] * in[2][2]) * inv;
    out[0][2] = (in[0][1] * in[1][2] - in[0][2] * in[1][1]) * inv;
    out[1][0] = (in[1][2] * in[2][0] - in[1][0] * in[2][2]) * inv;
    out[1][1] = (in[0][0] * in[2][2] - in[0][2] * in[2][0]) * inv;
    out[1][2] = (in[0][2] * in[1][0] - in[0][0] * in[1][2]) * inv;
    out[2][0] = (in[1][0] * in[2][1] - in[1][1] * in[2][0]) * inv;
    out[2][1] = (in[0][1] * in[2][0] - in[0][0] * in[2][1]) * inv;
    out[2][2] = (in[0][0] * in[1][1] - in[0][1] * in[1][0]) * inv;
    return true;
}

} // namespace

ElectronPhononResult analyzeElectronPhonon(const ElectronPhononInput& input)
{
    ElectronPhononResult result;
    result.temperatureK = input.temperatureK;

    const std::size_t nk = input.kPointCount();
    const int nb = input.bands;
    const int ns = std::max(1, input.spins);
    const int nq = input.qCount;
    const int nm = input.modes;
    if (nk == 0 || nb <= 0 || nq <= 0 || nm <= 0) {
        result.warnings.push_back("Empty input: nothing to analyse.");
        return result;
    }
    if (input.eigenvalues.size() < static_cast<std::size_t>(ns) * nk * nb
        || input.kPlusQ.size() < static_cast<std::size_t>(nq) * nk
        || input.phononFrequenciesEv.size()
            < static_cast<std::size_t>(nq) * nm
        || input.gSquaredEv2.size()
            < static_cast<std::size_t>(ns) * nq * nk * nm * nb * nb) {
        result.warnings.push_back(
            "Input arrays are shorter than the declared mesh dimensions.");
        return result;
    }

    const TetrahedronBz bz(input.kGrid, input.reciprocal);
    const double fermi = input.fermiLevelEv;

    // -- N(E_F) -------------------------------------------------------------
    // One band at a time, accumulated: the same routine, and the same
    // tetrahedra, that weight the pairs below. Per spin, which is the
    // normalization alpha^2F is defined with.
    std::vector<double> band(nk, 0.0);
    {
        std::vector<double> dosWeights(nk, 0.0);
        for (int s = 0; s < ns; ++s)
            for (int b = 0; b < nb; ++b) {
                for (std::size_t k = 0; k < nk; ++k)
                    band[k] = input.eigenvalues[(s * nk + k) * nb + b];
                bz.accumulateDeltaWeights(band, fermi, dosWeights);
            }
        double total = 0.0;
        for (const double w : dosWeights)
            total += w;
        result.dosAtFermi = total / ns;
    }
    if (result.dosAtFermi <= 1e-12) {
        result.warnings.push_back(
            "The density of states at the Fermi level is essentially zero: "
            "there is no Fermi surface for phonons to scatter electrons "
            "across. Electron-phonon coupling in this sense is a metallic "
            "property.");
        return result;
    }

    // -- Which modes are usable --------------------------------------------
    // Imaginary (negative) frequencies are a real result — the structure is
    // not at a minimum — but alpha^2F integrates 1/omega and cannot represent
    // them. Excluded by mask and counted, never turned into NaN: writing them
    // into the frequency array as NaN poisons max(), the grid, and every sum
    // downstream, which is how an earlier version reported lambda = NaN.
    std::vector<char> usable(static_cast<std::size_t>(nq) * nm, 0);
    double highest = 0.0;
    for (int iq = 0; iq < nq; ++iq)
        for (int mode = 0; mode < nm; ++mode) {
            const double omega =
                input.phononFrequenciesEv[static_cast<std::size_t>(iq) * nm
                                          + mode];
            const bool ok = std::isfinite(omega) && omega > 1e-6;
            usable[static_cast<std::size_t>(iq) * nm + mode] = ok ? 1 : 0;
            if (ok)
                highest = std::max(highest, omega);
            else
                ++result.excludedModes;
        }
    if (highest <= 0.0) {
        result.warnings.push_back(
            "Every phonon frequency is zero or imaginary, so there is no "
            "stable mode to couple to. Relax the structure first.");
        return result;
    }
    if (result.excludedModes > 0)
        result.warnings.push_back(
            std::to_string(result.excludedModes)
            + " imaginary or zero phonon frequencies were excluded; "
              "alpha^2F therefore describes only the stable modes.");

    // -- The Fermi-surface pair weights -------------------------------------
    //
    // weight_{q,nu} = 1/(N(E_F) N_q N_spin) *
    //     sum_{k,m,n} |g|^2 delta(eps_nk - E_F) delta(eps_m,k+q - E_F)
    //
    // The double delta comes from the tetrahedron method, so there is no
    // electronic smearing anywhere in this function. Both constraints are
    // evaluated on the SAME tetrahedra as N(E_F) above, which is what makes
    // the ratio meaningful rather than two independent approximations.
    // -- Band velocities, for the transport weight --------------------------
    //
    // v = grad_k eps by central differences on the regular grid. Only the
    // DIRECTION is used below, so the hbar in v = (1/hbar) grad eps cancels —
    // but the reciprocal-basis transform does not, and getting it wrong would
    // rotate every velocity and silently corrupt cos theta.
    //
    // eps is sampled at k = sum_j frac_j b_j, so d(eps)/d(frac_i) =
    // sum_a (grad_k eps)_a B_ia, i.e. grad_frac = B grad_k and therefore
    // grad_k = B^{-1} grad_frac.
    std::vector<double> velocity;
    bool haveVelocities = false;
    std::array<std::array<double, 3>, 3> inverseReciprocal{};
    if (invert3x3(input.reciprocal, inverseReciprocal)) {
        haveVelocities = true;
        velocity.assign(static_cast<std::size_t>(ns) * nk * nb * 3, 0.0);
        const int n1 = input.kGrid[0];
        const int n2 = input.kGrid[1];
        const int n3 = input.kGrid[2];
        for (int s = 0; s < ns; ++s)
            for (int b = 0; b < nb; ++b)
                for (int i = 0; i < n1; ++i)
                    for (int j = 0; j < n2; ++j)
                        for (int k = 0; k < n3; ++k) {
                            const std::size_t here = bz.index(i, j, k);
                            const auto at = [&](int a, int c, int d) {
                                return input.eigenvalues[(s * nk + bz.index(a, c, d))
                                                             * nb
                                                         + b];
                            };
                            const double gradFrac[3] = {
                                (at(i + 1, j, k) - at(i - 1, j, k)) * n1 * 0.5,
                                (at(i, j + 1, k) - at(i, j - 1, k)) * n2 * 0.5,
                                (at(i, j, k + 1) - at(i, j, k - 1)) * n3 * 0.5};
                            double* v =
                                velocity.data()
                                + ((static_cast<std::size_t>(s) * nk + here) * nb
                                   + b)
                                    * 3;
                            for (int a = 0; a < 3; ++a)
                                v[a] = inverseReciprocal[a][0] * gradFrac[0]
                                    + inverseReciprocal[a][1] * gradFrac[1]
                                    + inverseReciprocal[a][2] * gradFrac[2];
                        }
    } else {
        result.warnings.push_back(
            "The reciprocal lattice is singular, so band velocities could "
            "not be formed and no transport quantities were computed.");
    }

    result.weightPerMode.assign(static_cast<std::size_t>(nq) * nm, 0.0);
    std::vector<double> transportWeight(static_cast<std::size_t>(nq) * nm, 0.0);
    std::vector<double> bandAtK(nk, 0.0);
    std::vector<double> bandAtKq(nk, 0.0);
    std::vector<double> pairWeights(nk, 0.0);

    for (int s = 0; s < ns; ++s)
        for (int iq = 0; iq < nq; ++iq) {
            const int* map = input.kPlusQ.data() + static_cast<std::size_t>(iq) * nk;
            for (int n = 0; n < nb; ++n) {
                for (std::size_t k = 0; k < nk; ++k)
                    bandAtK[k] = input.eigenvalues[(s * nk + k) * nb + n];
                for (int m = 0; m < nb; ++m) {
                    for (std::size_t k = 0; k < nk; ++k) {
                        const std::size_t kq =
                            static_cast<std::size_t>(map[k]);
                        bandAtKq[k] =
                            input.eigenvalues[(s * nk + kq) * nb + m];
                    }
                    std::fill(pairWeights.begin(), pairWeights.end(), 0.0);
                    bz.accumulateDoubleDeltaWeights(bandAtK, bandAtKq, fermi,
                                                    pairWeights);
                    for (std::size_t k = 0; k < nk; ++k) {
                        const double w = pairWeights[k];
                        if (w == 0.0)
                            continue;
                        // The backscattering weight. Forward scattering
                        // (theta = 0) barely disturbs a current and gets zero
                        // weight; backscattering (theta = pi) reverses it and
                        // gets 2. This factor is the ENTIRE difference between
                        // lambda and lambda_tr, and it is why the transport
                        // coupling is always the smaller of the two.
                        double backscatter = 1.0;
                        if (haveVelocities) {
                            const std::size_t kq =
                                static_cast<std::size_t>(map[k]);
                            const double* v1 =
                                velocity.data()
                                + ((static_cast<std::size_t>(s) * nk + k) * nb
                                   + n)
                                    * 3;
                            const double* v2 =
                                velocity.data()
                                + ((static_cast<std::size_t>(s) * nk + kq) * nb
                                   + m)
                                    * 3;
                            const double n1 = std::sqrt(
                                v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2]);
                            const double n2 = std::sqrt(
                                v2[0] * v2[0] + v2[1] * v2[1] + v2[2] * v2[2]);
                            if (n1 > 1e-9 && n2 > 1e-9) {
                                const double dot = v1[0] * v2[0]
                                    + v1[1] * v2[1] + v1[2] * v2[2];
                                backscatter = 1.0 - dot / (n1 * n2);
                            } else {
                                // No direction to speak of. Weight 1 (the
                                // isotropic average of 1 - cos theta) and
                                // counted, because many such states mean the
                                // mesh is resolving flat regions and
                                // lambda_tr should be distrusted.
                                ++result.velocityDegenerateStates;
                            }
                        }
                        for (int mode = 0; mode < nm; ++mode) {
                            const std::size_t modeIndex =
                                static_cast<std::size_t>(iq) * nm + mode;
                            if (!usable[modeIndex])
                                continue;
                            const std::size_t g =
                                ((((static_cast<std::size_t>(s) * nq + iq) * nk
                                   + k)
                                      * nm
                                  + mode)
                                     * nb
                                 + m)
                                    * nb
                                + n;
                            result.weightPerMode[modeIndex] +=
                                w * input.gSquaredEv2[g];
                            transportWeight[modeIndex] +=
                                w * input.gSquaredEv2[g] * backscatter;
                        }
                    }
                }
            }
        }

    const double norm = result.dosAtFermi * nq * ns;
    for (double& w : result.weightPerMode)
        w /= norm;
    for (double& w : transportWeight)
        w /= norm;

    // -- lambda, from the MODE SUM -----------------------------------------
    //
    // lambda = 2 sum_{q,nu} weight / omega, not 2*integral(alpha^2F/omega).
    // The two are the same on paper, but the integral form divides the
    // SMEARED spectrum by omega, and the phonon Gaussian's tail reaches below
    // the lowest real mode: with an acoustic branch at 10 meV and a 5 meV
    // width that tail is two sigma out, and 1/omega against a grid starting
    // near zero turns it into an enormous spurious contribution. Measured on
    // fcc Al, the integral form gave lambda = 29.8 with omega_log = 0.
    double lambda = 0.0;
    double logSum = 0.0;
    double squareSum = 0.0;
    for (int iq = 0; iq < nq; ++iq)
        for (int mode = 0; mode < nm; ++mode) {
            const std::size_t index = static_cast<std::size_t>(iq) * nm + mode;
            if (!usable[index])
                continue;
            const double omega = input.phononFrequenciesEv[index];
            const double term = result.weightPerMode[index] / omega;
            lambda += term;
            logSum += term * std::log(omega);
            // <omega^2> weighted by the same alpha^2F/omega measure:
            // (w/omega) * omega^2 = w * omega.
            squareSum += result.weightPerMode[index] * omega;
        }
    lambda *= 2.0;
    result.lambda = lambda;
    result.omegaLogEv =
        lambda > 1e-12 ? std::exp(2.0 * logSum / lambda) : 0.0;
    result.omegaBar2Ev =
        lambda > 1e-12 ? std::sqrt(2.0 * squareSum / lambda) : 0.0;
    result.massEnhancement = 1.0 + lambda;

    // -- Mode-resolved coupling and phonon linewidths -----------------------
    //
    // lambda_qnu is normalized so that (1/N_q) sum_qnu lambda_qnu == lambda,
    // and the linewidth follows from the standard inversion
    //
    //     gamma_qnu = pi N(E_F) omega_qnu^2 lambda_qnu
    //
    // which, substituting lambda_qnu back, is 2 pi omega_qnu sum |g|^2 dd per
    // spin — the usual expression. Worth reporting per mode rather than only
    // in the sum: a single lambda cannot distinguish coupling concentrated in
    // one soft branch from the same total spread across the spectrum, and the
    // linewidth is measurable by inelastic neutron scattering, which makes it
    // the one output here checkable against an experiment on the same
    // material rather than against a tabulated number.
    result.lambdaPerMode.assign(static_cast<std::size_t>(nq) * nm, 0.0);
    result.linewidthsEv.assign(static_cast<std::size_t>(nq) * nm, 0.0);
    for (int iq = 0; iq < nq; ++iq)
        for (int mode = 0; mode < nm; ++mode) {
            const std::size_t index = static_cast<std::size_t>(iq) * nm + mode;
            if (!usable[index])
                continue;
            const double omega = input.phononFrequenciesEv[index];
            const double perMode =
                2.0 * nq * result.weightPerMode[index] / omega;
            result.lambdaPerMode[index] = perMode;
            result.linewidthsEv[index] =
                kPi * result.dosAtFermi * omega * omega * perMode;
        }

    // -- The spectrum, for display -----------------------------------------
    const double top = highest * 1.15 + 5.0 * input.phononSmearingEv;
    const int points = std::max(2, input.spectrumPoints);
    result.omegaEv.resize(points);
    result.alpha2F.assign(points, 0.0);
    for (int i = 0; i < points; ++i)
        result.omegaEv[i] = 1e-6 + (top - 1e-6) * i / (points - 1);
    const double sigma = std::max(1e-9, input.phononSmearingEv);
    const double gaussNorm = 1.0 / (sigma * std::sqrt(2.0 * kPi));
    for (int iq = 0; iq < nq; ++iq)
        for (int mode = 0; mode < nm; ++mode) {
            const std::size_t index = static_cast<std::size_t>(iq) * nm + mode;
            if (!usable[index])
                continue;
            const double omega = input.phononFrequenciesEv[index];
            const double weight = result.weightPerMode[index];
            if (weight == 0.0)
                continue;
            for (int i = 0; i < points; ++i) {
                const double x = (result.omegaEv[i] - omega) / sigma;
                result.alpha2F[i] += weight * gaussNorm * std::exp(-0.5 * x * x);
            }
        }

    // -- The relaxation time ------------------------------------------------
    result.scatteringRateEv =
        2.0 * kPi * result.lambda * kBoltzmannEvPerK * input.temperatureK;
    result.relaxationTimeFs = result.scatteringRateEv > 0.0
        ? kHbarEvFs / result.scatteringRateEv
        : 0.0;
    // Provisional: the transport rate below replaces this when band
    // velocities were available. See the note there — the Drude term wants
    // the TRANSPORT lifetime, not this one.
    result.drudeRateEv = 0.5 * result.scatteringRateEv;
    result.debyeTemperatureK =
        result.omegaLogEv > 0.0 ? result.omegaLogEv / kBoltzmannEvPerK : 0.0;
    if (result.debyeTemperatureK > 0.0
        && input.temperatureK < result.debyeTemperatureK / 3.0)
        result.warnings.push_back(
            "The requested temperature is below about a third of the Debye "
            "temperature, where the Bloch-Grueneisen T^5 regime takes over: "
            "hbar/tau = 2*pi*lambda*k_B*T overestimates the scattering rate "
            "there, so the reported tau is a lower bound.");

    // -- Transport: lambda_tr, tau_tr and the resistivity -------------------
    if (haveVelocities) {
        double lambdaTr = 0.0;
        for (int iq = 0; iq < nq; ++iq)
            for (int mode = 0; mode < nm; ++mode) {
                const std::size_t index =
                    static_cast<std::size_t>(iq) * nm + mode;
                if (!usable[index])
                    continue;
                lambdaTr += transportWeight[index]
                    / input.phononFrequenciesEv[index];
            }
        result.lambdaTransport = 2.0 * lambdaTr;

        result.scatteringRateTransportEv = 2.0 * kPi * result.lambdaTransport
            * kBoltzmannEvPerK * input.temperatureK;
        result.relaxationTimeTransportFs =
            result.scatteringRateTransportEv > 0.0
            ? kHbarEvFs / result.scatteringRateTransportEv
            : 0.0;

        // THE number the optics module consumes, and it is built on
        // lambda_tr rather than lambda.
        //
        // The Drude term describes how a CURRENT decays, and a current is
        // degraded by backscattering, not by every scattering event: an
        // electron deflected forward still carries nearly the same current.
        // That is exactly the (1 - cos theta) weight separating lambda_tr
        // from lambda, so using lambda here would damp the optical response
        // by the quasiparticle lifetime instead of the current lifetime.
        //
        // The two differ by tens of per cent in a simple metal and can differ
        // by a factor of several where the Fermi surface is anisotropic, and
        // nothing about the resulting spectrum looks wrong — it is simply a
        // Drude peak of the wrong width.
        //
        // Still HALF the rate, because GPAW's DielectricFunction damps as
        // omega_p^2/(omega + i*rate)^2 whereas the textbook form has
        // Gamma = hbar/tau in omega_p^2/(omega*(omega + i*Gamma)).
        result.drudeRateEv = 0.5 * result.scatteringRateTransportEv;
        result.drudeRateFromTransport = true;

        // Drude, written so that everything in it is computed rather than
        // assumed: rho = m/(n e^2 tau) and omega_p^2 = n e^2/(eps_0 m) give
        // rho = 1/(eps_0 omega_p^2 tau_tr). omega_p comes from the optics or
        // k-point convergence module; without it rho is SKIPPED, not guessed,
        // because rho goes as 1/omega_p^2 and an invented omega_p would
        // produce something that looks like a measurement.
        if (input.plasmaFrequencyEv > 0.0
            && result.relaxationTimeTransportFs > 0.0) {
            const double omegaP = input.plasmaFrequencyEv / kHbarEvS; // rad/s
            const double tauSeconds =
                result.relaxationTimeTransportFs * 1e-15;
            const double ohmMetre =
                1.0 / (kVacuumPermittivity * omegaP * omegaP * tauSeconds);
            result.resistivityMicroOhmCm = ohmMetre * 1e8;
        } else if (input.plasmaFrequencyEv <= 0.0) {
            result.warnings.push_back(
                "No plasma frequency was supplied, so the resistivity was "
                "not computed. Take hbar*omega_p from the Optics or K-point "
                "Convergence module: rho goes as 1/omega_p^2, so it cannot "
                "be estimated without it.");
        }

        // The spectrum, transport-weighted, on the same grid.
        result.alpha2FTransport.assign(result.omegaEv.size(), 0.0);
        const double sigma = std::max(1e-9, input.phononSmearingEv);
        const double gaussNorm = 1.0 / (sigma * std::sqrt(2.0 * kPi));
        for (int iq = 0; iq < nq; ++iq)
            for (int mode = 0; mode < nm; ++mode) {
                const std::size_t index =
                    static_cast<std::size_t>(iq) * nm + mode;
                if (!usable[index] || transportWeight[index] == 0.0)
                    continue;
                const double omega = input.phononFrequenciesEv[index];
                for (std::size_t i = 0; i < result.omegaEv.size(); ++i) {
                    const double x = (result.omegaEv[i] - omega) / sigma;
                    result.alpha2FTransport[i] += transportWeight[index]
                        * gaussNorm * std::exp(-0.5 * x * x);
                }
            }

        // The only bound that actually holds. 1 - cos(theta) lies in [0, 2],
        // so 0 <= lambda_tr <= 2 lambda — and BOTH lambda_tr < lambda and
        // lambda_tr > lambda are physical: forward scattering dominates for
        // small q (lambda_tr -> 0 as q -> 0, since 1 - cos = q^2/2k_F^2 for a
        // free-electron sphere), and backscattering dominates as q approaches
        // 2k_F, where 1 - cos -> 2. An earlier version of this code warned
        // that lambda_tr > lambda "cannot happen"; that was simply wrong, and
        // it fires routinely on a coarse q-mesh whose q are a large fraction
        // of the zone.
        if (result.lambdaTransport > 2.0 * result.lambda * (1.0 + 1e-9))
            result.warnings.push_back(
                "lambda_tr exceeds 2*lambda, which the bound "
                "1 - cos(theta) <= 2 forbids. The band velocities are "
                "suspect — check the reciprocal lattice vectors.");
        else if (result.lambdaTransport > result.lambda)
            result.warnings.push_back(
                "lambda_tr > lambda: scattering on this mesh is "
                "backscattering-dominated, which happens when the q-mesh "
                "reaches a large fraction of 2k_F. Physical, but it means the "
                "q-sampling is coarse relative to the Fermi surface, so "
                "rho(T) is correspondingly under-resolved.");
    }

    // -- The retardation logarithm ------------------------------------------
    // Half of Morel-Anderson, and the half that is computable here: the
    // electronic scale is the occupied bandwidth, the phonon scale is
    // omega_log. The bare mu is not available, so mu* is not derived from
    // this — but the ratio is what makes mu* material-dependent, and a user
    // with a bare mu in mind can convert.
    {
        double lowest = input.eigenvalues.empty()
            ? fermi
            : *std::min_element(input.eigenvalues.begin(),
                                input.eigenvalues.end());
        result.occupiedBandwidthEv = fermi - lowest;
        if (result.occupiedBandwidthEv > 0.0 && result.omegaLogEv > 0.0)
            result.retardationLog =
                std::log(result.occupiedBandwidthEv / result.omegaLogEv);
    }

    // -- What the coupling is FOR -------------------------------------------
    // T_c is the property alpha^2F was invented to predict, so it is computed
    // here rather than left to a separate call that could be forgotten. Its
    // own refusal (repulsion beats attraction) does not make the
    // electron-phonon result invalid, so it does not touch result.ok.
    SuperconductingInput superconducting;
    superconducting.lambda = result.lambda;
    superconducting.omegaLogEv = result.omegaLogEv;
    superconducting.omegaBar2Ev = result.omegaBar2Ev;
    superconducting.muStar = input.muStar;
    superconducting.debyeTemperatureK = result.debyeTemperatureK;
    result.superconductivity = estimateSuperconductingTc(superconducting);

    if (!result.drudeRateFromTransport)
        result.warnings.push_back(
            "The Drude rate reported here is built on lambda rather than "
            "lambda_tr, because band velocities were not available. The "
            "optical Drude term wants the TRANSPORT lifetime — a current is "
            "degraded by backscattering, not by every scattering event — so "
            "this value damps the response by the quasiparticle lifetime "
            "instead, and the resulting Drude peak has the wrong width "
            "without looking wrong.");

    result.ok = true;
    return result;
}

} // namespace calango::core
