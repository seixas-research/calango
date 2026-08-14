#include "dft/XcFunctional.hpp"
#include "dft/Constants.hpp"

#include <cmath>

namespace calango::dft {
namespace {

/// r_s, the radius of the sphere holding one electron: (3/4πρ)^{1/3}. Every
/// correlation parameterisation is written in it rather than in ρ because the
/// electron gas data was computed at fixed r_s.
inline double wignerSeitzRadius(double density)
{
    return std::cbrt(3.0 / (4.0 * kPi * density));
}

/// Vosko-Wilk-Nusair correlation, formula V, paramagnetic (unpolarised) fit.
///
///   ε_c = A { ln(x²/X) + (2b/Q) atan(Q/(2x+b))
///             − (b x₀/X₀) [ ln((x−x₀)²/X) + (2(b+2x₀)/Q) atan(Q/(2x+b)) ] }
///
/// with x = √r_s, X(x) = x² + bx + c, X₀ = X(x₀), Q = √(4c − b²).
///
/// The potential comes from v_c = ε_c − (r_s/3) dε_c/dr_s, and the derivative
/// is taken analytically rather than by finite differences: the two atan
/// terms differentiate to the same simple −Q/(2X) because (2x+b)² + Q² = 4X
/// identically, which collapses what looks like a messy derivative into three
/// rational terms.
XcResult vwnCorrelation(double density)
{
    constexpr double a = 0.0310907;
    constexpr double x0 = -0.10498;
    constexpr double b = 3.72744;
    constexpr double c = 12.9352;

    const double rs = wignerSeitzRadius(density);
    const double x = std::sqrt(rs);
    const double bigX = x * x + b * x + c;
    const double bigX0 = x0 * x0 + b * x0 + c;
    const double q = std::sqrt(4.0 * c - b * b);
    const double atanTerm = std::atan(q / (2.0 * x + b));

    const double epsilon = a
        * (std::log(x * x / bigX) + 2.0 * b / q * atanTerm
           - b * x0 / bigX0
               * (std::log((x - x0) * (x - x0) / bigX)
                  + 2.0 * (b + 2.0 * x0) / q * atanTerm));

    const double dXdx = 2.0 * x + b;
    const double dEpsilonDx = a
        * (2.0 / x - dXdx / bigX - b / bigX
           - b * x0 / bigX0
               * (2.0 / (x - x0) - dXdx / bigX - (b + 2.0 * x0) / bigX));

    // v_c = ε_c − (r_s/3) dε_c/dr_s and dε_c/dr_s = dε_c/dx · 1/(2x) with
    // r_s = x², so the whole prefactor is x/6.
    return {epsilon, epsilon - x / 6.0 * dEpsilonDx};
}

/// Perdew-Zunger 1981 correlation, unpolarised. Two branches meeting at
/// r_s = 1: a Padé form for the low-density side fitted to the Monte Carlo
/// data, and the high-density RPA expansion below it.
XcResult pzCorrelation(double density)
{
    const double rs = wignerSeitzRadius(density);
    if (rs >= 1.0) {
        constexpr double gamma = -0.1423;
        constexpr double beta1 = 1.0529;
        constexpr double beta2 = 0.3334;
        const double sqrtRs = std::sqrt(rs);
        const double denominator = 1.0 + beta1 * sqrtRs + beta2 * rs;
        const double epsilon = gamma / denominator;
        const double numerator =
            1.0 + 7.0 / 6.0 * beta1 * sqrtRs + 4.0 / 3.0 * beta2 * rs;
        return {epsilon, epsilon * numerator / denominator};
    }
    constexpr double a = 0.0311;
    constexpr double b = -0.048;
    constexpr double c = 0.0020;
    constexpr double d = -0.0116;
    const double logRs = std::log(rs);
    const double epsilon = a * logRs + b + c * rs * logRs + d * rs;
    const double potential = a * logRs + (b - a / 3.0)
        + 2.0 / 3.0 * c * rs * logRs + (2.0 * d - c) / 3.0 * rs;
    return {epsilon, potential};
}

/// Perdew-Wang 1992 correlation, unpolarised.
///
///     ε_c = −2A(1 + α₁ r_s) ln[ 1 + 1/(2A(β₁√r_s + β₂r_s + β₃r_s^{3/2}
///                                          + β₄r_s²)) ]
///
/// One analytic expression over the whole density range — no branch, no join —
/// which is why it has become what "LDA" means without qualification in every
/// modern plane-wave code. That is the practical reason it is the default
/// here: it is the only LDA whose numbers can be checked against another
/// program to more digits than the fits differ by.
XcResult pw92Correlation(double density)
{
    constexpr double a = 0.031091;
    constexpr double alpha1 = 0.21370;
    constexpr double beta1 = 7.5957;
    constexpr double beta2 = 3.5876;
    constexpr double beta3 = 1.6382;
    constexpr double beta4 = 0.49294;

    const double rs = wignerSeitzRadius(density);
    const double x = std::sqrt(rs);
    const double q0 = -2.0 * a * (1.0 + alpha1 * rs);
    const double q1 =
        2.0 * a * (beta1 * x + beta2 * rs + beta3 * rs * x + beta4 * rs * rs);
    const double q1prime = 2.0 * a
        * (0.5 * beta1 / x + beta2 + 1.5 * beta3 * x + 2.0 * beta4 * rs);
    const double logTerm = std::log(1.0 + 1.0 / q1);

    const double epsilon = q0 * logTerm;
    // d/dr_s ln(1 + 1/Q₁) = −Q₁′ / (Q₁(Q₁+1)) — the derivative of the log
    // collapses to that once the 1/Q₁² of the inner derivative cancels one
    // power against the (1 + 1/Q₁) of the outer one.
    const double derivative =
        -2.0 * a * alpha1 * logTerm - q0 * q1prime / (q1 * (q1 + 1.0));
    return {epsilon, epsilon - rs / 3.0 * derivative};
}

} // namespace

bool Lda::supports(XcFunctional functional)
{
    switch (functional) {
    case XcFunctional::LdaPw:
    case XcFunctional::LdaVwn:
    case XcFunctional::LdaPz:
        return true;
    case XcFunctional::GgaPbe:
        return false;
    }
    return false;
}

XcResult Lda::exchange(double density)
{
    if (density <= 0.0)
        return {};
    // ε_x = −(3/4)(3/π)^{1/3} ρ^{1/3}, and v_x = (4/3) ε_x exactly, because
    // ρ ε_x scales as ρ^{4/3}. That factor of 4/3 is the cheapest available
    // check that an exchange implementation is right.
    const double factor = -0.75 * std::cbrt(3.0 / kPi);
    const double epsilon = factor * std::cbrt(density);
    return {epsilon, 4.0 / 3.0 * epsilon};
}

XcResult Lda::correlation(double density, XcFunctional functional)
{
    if (density <= 0.0)
        return {};
    switch (functional) {
    case XcFunctional::LdaPz:
        return pzCorrelation(density);
    case XcFunctional::LdaVwn:
        return vwnCorrelation(density);
    case XcFunctional::LdaPw:
        return pw92Correlation(density);
    case XcFunctional::GgaPbe:
        return {};
    }
    return {};
}

XcResult Lda::evaluate(double density, XcFunctional functional)
{
    if (density <= 0.0 || !supports(functional))
        return {};
    const XcResult x = exchange(density);
    const XcResult c = correlation(density, functional);
    return {x.energyPerElectron + c.energyPerElectron,
            x.potential + c.potential};
}

namespace {

/// PBE exchange. The enhancement factor multiplying the uniform-gas exchange
/// energy density,
///
///     F_x(s) = 1 + κ − κ/(1 + μs²/κ),
///     s = |∇ρ| / (2 k_F ρ),   k_F = (3π²ρ)^{1/3},
///
/// with the constants of Perdew, Burke and Ernzerhof: κ = 0.804 fixed by the
/// Lieb-Oxford bound, μ = βπ²/3 chosen so the gradient expansion of the
/// LINEAR RESPONSE is recovered rather than that of the exchange energy.
///
/// Everything is written in t = s² so that no square root of σ appears: the
/// derivatives stay rational, and σ = 0 is an ordinary point rather than a
/// place where ∂s/∂σ diverges.
XcPoint pbeExchange(double density, double sigma)
{
    constexpr double kappa = 0.804;
    constexpr double beta = 0.06672455060314922;
    const double mu = beta * kPi * kPi / 3.0;

    // f = A ρ^{4/3} F(t), t = σ / (C ρ^{8/3}).
    const double a = -0.75 * std::cbrt(3.0 / kPi);
    const double c = 4.0 * std::pow(3.0 * kPi * kPi, 2.0 / 3.0);
    const double t = sigma / (c * std::pow(density, 8.0 / 3.0));
    const double denominator = 1.0 + mu * t / kappa;
    const double f = 1.0 + kappa - kappa / denominator;
    const double dfdt = mu / (denominator * denominator);

    XcPoint result;
    const double rho13 = std::cbrt(density);
    result.energyDensity = a * rho13 * density * f;
    // ∂t/∂ρ = −(8/3)t/ρ, so the ρ derivative collects into one bracket.
    result.dfdrho = a * rho13 * (4.0 / 3.0 * f - 8.0 / 3.0 * t * dfdt);
    result.dfdsigma = a * dfdt / (c * std::pow(density, 4.0 / 3.0));
    return result;
}

/// PBE correlation: the PW92 uniform-gas result plus the gradient term
///
///     H = γ ln[ 1 + (β/γ) T (1 + AT) / (1 + AT + A²T²) ],
///     T = t², t = |∇ρ|/(2 k_s ρ),   k_s = √(4k_F/π),
///     A = (β/γ) / [ exp(−ε_c^unif/γ) − 1 ].
///
/// A depends on the density through ε_c, so ∂f/∂ρ has a term through A that
/// is easy to forget; it is included and the whole thing is checked against a
/// finite difference of the energy in `dft_engine`.
XcPoint pbeCorrelation(double density, double sigma)
{
    constexpr double beta = 0.06672455060314922;
    const double gamma = (1.0 - std::log(2.0)) / (kPi * kPi);

    const XcResult local = pw92Correlation(density);
    const double rho43 = std::pow(density, 4.0 / 3.0);
    // 4 k_s² ρ² = D ρ^{7/3} with k_s² = 4k_F/π.
    const double d = 16.0 * std::cbrt(3.0 * kPi * kPi) / kPi;
    const double bigT = sigma / (d * std::pow(density, 7.0 / 3.0));

    // A → ∞ as ε_c → 0⁻, which happens in the low-density tail. There H → 0
    // as well, so the limit is finite; taking it explicitly avoids an
    // intermediate infinity.
    const double exponential = std::exp(-local.energyPerElectron / gamma);
    if (!(exponential - 1.0 > 1.0e-14)) {
        XcPoint result;
        result.energyDensity = density * local.energyPerElectron;
        result.dfdrho = local.potential;
        return result;
    }
    const double bigA = (beta / gamma) / (exponential - 1.0);

    const double numerator = (beta / gamma) * bigT * (1.0 + bigA * bigT);
    const double denominator =
        1.0 + bigA * bigT + bigA * bigA * bigT * bigT;
    const double ratio = numerator / denominator;
    const double w = 1.0 + ratio;
    const double h = gamma * std::log(w);

    // ∂H/∂T and ∂H/∂A, both through the same quotient.
    const double dNdT = (beta / gamma) * (1.0 + 2.0 * bigA * bigT);
    const double dDdT = bigA + 2.0 * bigA * bigA * bigT;
    const double dRatioDT =
        (dNdT * denominator - numerator * dDdT) / (denominator * denominator);
    const double dHdT = gamma / w * dRatioDT;

    const double dNdA = (beta / gamma) * bigT * bigT;
    const double dDdA = bigT + 2.0 * bigA * bigT * bigT;
    const double dRatioDA =
        (dNdA * denominator - numerator * dDdA) / (denominator * denominator);
    const double dHdA = gamma / w * dRatioDA;
    // dA/dε_c, from A = (β/γ)/(e^{−ε_c/γ} − 1).
    const double dAdEpsilon = (beta / gamma) * (exponential / gamma)
        / ((exponential - 1.0) * (exponential - 1.0));
    // ρ dε_c/dρ = v_c − ε_c, since v_c = d(ρε_c)/dρ. No extra derivative of
    // the PW92 fit is needed.
    const double rhoDEpsilon = local.potential - local.energyPerElectron;

    XcPoint result;
    result.energyDensity = density * (local.energyPerElectron + h);
    result.dfdrho = local.potential + h - 7.0 / 3.0 * bigT * dHdT
        + dHdA * dAdEpsilon * rhoDEpsilon;
    result.dfdsigma = dHdT / (d * rho43);
    return result;
}

} // namespace

bool Xc::supports(XcFunctional functional)
{
    return functional == XcFunctional::GgaPbe || Lda::supports(functional);
}

bool Xc::needsGradients(XcFunctional functional)
{
    return functional == XcFunctional::GgaPbe;
}

XcPoint Xc::evaluate(double density, double sigma, XcFunctional functional)
{
    // Below this the functional contributes nothing an integration weight can
    // see, and every expression here has a ρ^{-4/3} in it somewhere.
    if (density <= 1.0e-12)
        return {};
    if (functional != XcFunctional::GgaPbe) {
        const XcResult local = Lda::evaluate(density, functional);
        return {density * local.energyPerElectron, local.potential, 0.0};
    }
    const double clamped = std::max(sigma, 0.0);
    const XcPoint exchange = pbeExchange(density, clamped);
    const XcPoint correlation = pbeCorrelation(density, clamped);
    return {exchange.energyDensity + correlation.energyDensity,
            exchange.dfdrho + correlation.dfdrho,
            exchange.dfdsigma + correlation.dfdsigma};
}

double Xc::evaluateGrid(const std::vector<double>& density,
                        const std::vector<double>& sigma,
                        const std::vector<double>& weights,
                        XcFunctional functional, std::vector<double>& dfdrho,
                        std::vector<double>& dfdsigma)
{
    dfdrho.assign(density.size(), 0.0);
    dfdsigma.assign(density.size(), 0.0);
    if (weights.size() != density.size())
        return 0.0;
    const bool gradients = needsGradients(functional);
    double energy = 0.0;
    for (std::size_t i = 0; i < density.size(); ++i) {
        const double s = gradients && i < sigma.size() ? sigma[i] : 0.0;
        const XcPoint point = evaluate(density[i], s, functional);
        dfdrho[i] = point.dfdrho;
        dfdsigma[i] = point.dfdsigma;
        energy += weights[i] * point.energyDensity;
    }
    return energy;
}

double Lda::evaluateGrid(const std::vector<double>& density,
                         const std::vector<double>& weights,
                         XcFunctional functional,
                         std::vector<double>& potential)
{
    potential.assign(density.size(), 0.0);
    if (weights.size() != density.size())
        return 0.0;
    double energy = 0.0;
    for (std::size_t i = 0; i < density.size(); ++i) {
        const XcResult result = evaluate(density[i], functional);
        potential[i] = result.potential;
        energy += weights[i] * density[i] * result.energyPerElectron;
    }
    return energy;
}

} // namespace calango::dft
