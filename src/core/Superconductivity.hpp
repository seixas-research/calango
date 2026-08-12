#pragma once

#include <string>
#include <utility>
#include <vector>

namespace calango::core {

/// Superconducting transition temperature from electron-phonon coupling.
///
/// This is what alpha^2F is computed FOR, historically: McMillan solved the
/// Eliashberg equations numerically for a range of couplings and fitted the
/// result to a closed form, and Allen and Dynes later corrected it where the
/// fit fails. Everything here is that closed form — no Eliashberg equations
/// are solved, which is the accuracy ceiling and is stated as such below.
///
/// Takes only moments of the spectrum, not the spectrum itself. That is a
/// property of the theory, not a simplification made here: to this order T_c
/// depends on alpha^2F through lambda, omega_log and (weakly) omega_2 alone.
struct SuperconductingInput {
    /// Mass-enhancement coupling constant.
    double lambda = 0.0;
    /// Logarithmic average phonon frequency, eV.
    double omegaLogEv = 0.0;
    /// Second moment sqrt(<omega^2>), eV. Only enters the f2 correction; if
    /// zero, f2 is skipped and reported as 1.
    double omegaBar2Ev = 0.0;
    /// Morel-Anderson Coulomb pseudopotential.
    ///
    /// EMPIRICAL. It is not computed by any part of this program and cannot
    /// be: it screens the direct Coulomb repulsion and is conventionally
    /// taken as 0.10-0.15 for sp metals, higher for transition metals. T_c
    /// depends on it exponentially — see the warning the implementation emits.
    double muStar = 0.10;
    /// Debye temperature in K, for the McMillan form only. If zero, the
    /// McMillan estimate is skipped (Allen-Dynes does not need it).
    double debyeTemperatureK = 0.0;
};

struct SuperconductingResult {
    bool ok = false;
    /// The mu* actually used. Recorded because T_c depends on it
    /// exponentially and is meaningless without it — a stored T_c with no
    /// mu* beside it cannot be interpreted or reproduced.
    double muStar = 0.0;
    /// McMillan 1968: T_c = (theta_D/1.45) exp(-1.04(1+l)/(l - mu*(1+0.62l))).
    /// Zero if no Debye temperature was supplied.
    double tcMcMillanK = 0.0;
    /// Allen-Dynes 1975 with omega_log/1.2 in place of theta_D/1.45. The
    /// better of the two: omega_log is a property of the computed spectrum,
    /// whereas theta_D is a fit to a different measurement entirely.
    double tcAllenDynesK = 0.0;
    /// Allen-Dynes including the strong-coupling (f1) and spectral-shape (f2)
    /// corrections. This is the number to quote.
    double tcAllenDynesCorrectedK = 0.0;
    double f1 = 1.0;
    double f2 = 1.0;
    /// BCS weak-coupling gap 2*Delta = 3.53 k_B T_c, in meV. An estimate: the
    /// ratio rises above 3.53 with coupling, which is why it is reported
    /// beside T_c rather than instead of it.
    double gapMeV = 0.0;
    /// 2*Delta/(k_B T_c) actually used — 3.53, or the strong-coupling
    /// correction where lambda is large enough for it to matter.
    double gapRatio = 0.0;
    /// T_c over a range of mu*, as (mu*, T_c) pairs.
    ///
    /// Reported ALWAYS, because mu* cannot be computed here and a single T_c
    /// implies a precision that does not exist. A careful result is quoted as
    /// a range over this curve, which is what the literature does when it is
    /// being honest about the parameter.
    std::vector<std::pair<double, double>> tcVsMuStar;
    std::vector<std::string> warnings;
};

/// Morel-Anderson: the bare Coulomb pseudopotential mu, renormalized down by
/// retardation into mu*.
///
///     mu* = mu / (1 + mu * ln(E_electronic / omega_phonon))
///
/// The electrons repel instantaneously while the phonon attraction is
/// retarded, and that logarithm is the whole reason mu* lands near 0.1 when
/// mu is 0.3-0.5. Offered as a CONVERTER, not an estimator: the log is
/// computable from quantities this program has (the occupied bandwidth and
/// omega_log), but `bareMu` is not, so the user supplies it.
///
/// Returns 0 if the arguments do not admit a value.
double morelAndersonMuStar(double bareMu, double electronicScaleEv,
                           double phononScaleEv);

/// Evaluate the McMillan / Allen-Dynes formulas.
///
/// Returns ok = false when the material is not a superconductor in this
/// framework, i.e. when lambda <= mu*(1 + 0.62 lambda) makes the exponent
/// singular or positive. That is a real answer ("the repulsion wins"), not an
/// error, and is reported as such rather than as a T_c of zero that could be
/// mistaken for a converged calculation.
SuperconductingResult
estimateSuperconductingTc(const SuperconductingInput& input);

} // namespace calango::core
