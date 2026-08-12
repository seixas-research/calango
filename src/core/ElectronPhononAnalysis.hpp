#pragma once

#include "core/Superconductivity.hpp"

#include <array>
#include <string>
#include <vector>

namespace calango::core {

/// Raw output of a `gpaw.elph` run, before any physics is read out of it.
///
/// The generated script's job ends here: it produces the matrix elements, the
/// eigenvalues, the phonon frequencies and the meshes they live on, and this
/// module turns them into alpha^2F, lambda and tau. The split is deliberate —
/// the Fermi-surface integration is the delicate part, it wants tetrahedron
/// weights (see TetrahedronBz) rather than a smearing nobody can converge,
/// and in C++ it can be checked against closed-form references on every
/// commit instead of once inside a 45-minute run.
struct ElectronPhononInput {
    /// Electron k-mesh. Eigenvalues are indexed in TetrahedronBz's linear
    /// order, i.e. ((i1 * n2) + i2) * n3 + i3.
    std::array<int, 3> kGrid{1, 1, 1};
    /// Reciprocal lattice vectors b1, b2, b3 as ROWS, inverse Angstrom.
    std::array<std::array<double, 3>, 3> reciprocal{};

    int spins = 1;
    int bands = 0;
    /// eps[(s * nk + k) * bands + band], eV.
    std::vector<double> eigenvalues;
    double fermiLevelEv = 0.0;

    /// For each q, the k-index that k + q maps to:
    /// kPlusQ[iq * nk + k]. Supplied rather than derived because the script
    /// already had to build it to index the matrix elements, and two
    /// independent constructions of the same map is one more thing that can
    /// disagree.
    std::vector<int> kPlusQ;
    int qCount = 0;

    int modes = 0;
    /// omega[iq * modes + mode], eV. Imaginary modes are conventionally
    /// stored negative and are excluded (and counted) rather than integrated.
    std::vector<double> phononFrequenciesEv;

    /// |g|^2 in eV^2, indexed
    /// (((s * qCount + iq) * nk + k) * modes + mode) * bands + m) * bands + n
    /// with m the band at k+q and n the band at k — the ordering
    /// `bloch_matrix` returns.
    std::vector<double> gSquaredEv2;

    double temperatureK = 300.0;
    /// Morel-Anderson Coulomb pseudopotential for the T_c estimate.
    ///
    /// EMPIRICAL and not computed anywhere in this program — see
    /// Superconductivity.hpp. Carried here only so that one call produces
    /// every derived property.
    double muStar = 0.10;
    /// Drude plasma frequency hbar*omega_p in eV, for the resistivity.
    ///
    /// Not computed here — it comes from the optics / k-point convergence
    /// module. Zero means "unknown", and rho(T) is then skipped rather than
    /// invented: rho depends on it as 1/omega_p^2 and a guessed omega_p would
    /// produce a resistivity that looks like a measurement.
    double plasmaFrequencyEv = 0.0;
    /// Gaussian width used ONLY to draw alpha^2F(omega). It does not enter
    /// lambda, which is summed over modes exactly — see the note in the
    /// implementation about why the integral form of lambda diverges.
    double phononSmearingEv = 0.005;
    int spectrumPoints = 400;

    std::size_t kPointCount() const
    {
        return static_cast<std::size_t>(kGrid[0]) * kGrid[1] * kGrid[2];
    }
};

struct ElectronPhononResult {
    bool ok = false;
    /// N(E_F), states/eV per unit cell per spin, by linear tetrahedron.
    double dosAtFermi = 0.0;
    /// Mass-enhancement coupling constant.
    double lambda = 0.0;
    /// Logarithmic average phonon frequency, eV.
    double omegaLogEv = 0.0;
    double temperatureK = 0.0;
    /// hbar/tau = 2 pi lambda k_B T (Allen, high temperature).
    double scatteringRateEv = 0.0;
    double relaxationTimeFs = 0.0;
    /// hbar/(2 tau) — what GPAW's DielectricFunction `rate` takes, because it
    /// damps as omega_p^2/(omega + i*rate)^2 where the textbook form has
    /// Gamma = hbar/tau. The factor of two is the standing trap, so both are
    /// reported.
    double drudeRateEv = 0.0;
    /// Occupied bandwidth E_F - min(eigenvalue), eV.
    ///
    /// Reported so that the Morel-Anderson retardation logarithm
    /// ln(E_F / omega_log) can be formed from computed quantities. mu* itself
    /// still cannot be — the bare mu is not available here — but this is the
    /// half of it that is, and it is what makes mu* material-dependent rather
    /// than universal.
    double occupiedBandwidthEv = 0.0;
    /// ln(occupied bandwidth / omega_log). Multiply a bare mu by
    /// 1/(1 + mu * this) to get mu*; see morelAndersonMuStar().
    double retardationLog = 0.0;

    /// Estimated Debye temperature from omega_log, K. The high-temperature
    /// formula above is only valid well above roughly a third of it.
    double debyeTemperatureK = 0.0;
    /// Second moment of alpha^2F, sqrt(<omega^2>), eV.
    ///
    /// Only used by the Allen-Dynes strong-coupling correction f2, which is
    /// where the shape of the spectrum (not just its log-average) starts to
    /// matter for T_c. Reported because omega_2/omega_log is itself the
    /// diagnostic for how far from a single-peak spectrum a material is.
    double omegaBar2Ev = 0.0;
    /// Band-mass enhancement 1 + lambda.
    ///
    /// The same lambda, read as what it also is: the factor by which the
    /// electron-phonon interaction renormalizes the band mass at the Fermi
    /// surface, and hence the linear specific-heat coefficient gamma.
    double massEnhancement = 0.0;

    /// Mode-resolved coupling lambda_qnu, indexed as the frequencies.
    ///
    /// Normalized so that (1/N_q) * sum over q,nu gives `lambda`. Which
    /// branches actually couple is the physically interesting output here —
    /// a single lambda hides whether the coupling is one soft mode or spread
    /// over the spectrum.
    std::vector<double> lambdaPerMode;
    /// Phonon linewidth gamma_qnu = pi N(E_F) omega^2 lambda_qnu, eV.
    ///
    /// The FWHM a phonon acquires from decaying into electron-hole pairs.
    /// Directly comparable with inelastic neutron or Raman linewidths, which
    /// makes it the one quantity in this module that can be checked against
    /// an experiment on the same material rather than against a tabulated
    /// constant.
    std::vector<double> linewidthsEv;

    /// The spectral function, for display.
    std::vector<double> omegaEv;
    std::vector<double> alpha2F;
    /// Fermi-surface weight per (q, mode), same indexing as the frequencies.
    std::vector<double> weightPerMode;

    // -- Transport ----------------------------------------------------------
    //
    // The SAME sums, reweighted by (1 - cos theta) with theta the angle
    // between the band velocities at k and k+q. That factor is the whole
    // difference between the mass-enhancement lambda and the transport one:
    // forward scattering barely changes a current, backscattering reverses
    // it, so only the latter causes resistance. lambda_tr is therefore always
    // <= lambda, and the two coincide only for isotropic scattering.
    /// Transport coupling constant.
    double lambdaTransport = 0.0;
    /// alpha^2F_tr on the same frequency grid as alpha2F.
    std::vector<double> alpha2FTransport;
    /// hbar/tau_tr = 2 pi lambda_tr k_B T, eV.
    double scatteringRateTransportEv = 0.0;
    double relaxationTimeTransportFs = 0.0;
    /// Resistivity at the requested temperature, micro-ohm centimetre.
    ///
    /// Zero when no plasma frequency was supplied. rho = 1/(eps_0 omega_p^2
    /// tau_tr), which is the Drude result written so that everything in it is
    /// something this program computes.
    double resistivityMicroOhmCm = 0.0;
    /// States whose velocity was too small to define a direction, so the
    /// backscattering factor was taken as 1. A large count means the mesh is
    /// resolving flat regions and lambda_tr should be distrusted.
    int velocityDegenerateStates = 0;

    /// T_c, the strong-coupling corrections and the gap, from lambda,
    /// omega_log and omega_2. Its own `ok` is false when the material is not
    /// a phonon-mediated superconductor at this coupling — which is a result
    /// and not a failure of the electron-phonon calculation around it.
    SuperconductingResult superconductivity;

    int excludedModes = 0;
    std::vector<std::string> warnings;
};

/// Compute alpha^2F, lambda and the electron-phonon relaxation time.
///
/// Both Fermi-surface constraints are evaluated with the linear tetrahedron
/// method, so there is no electronic smearing parameter at all — which is the
/// point. With a Gaussian, lambda on fcc Al ran 0.009 to 31 as the width was
/// varied over a factor of sixteen, with no plateau to converge to.
ElectronPhononResult analyzeElectronPhonon(const ElectronPhononInput& input);

} // namespace calango::core
