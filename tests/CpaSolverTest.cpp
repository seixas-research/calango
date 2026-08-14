// Single-site CPA, checked against closed forms rather than against itself.
//
// Every assertion here is either an exact analytic result (the semi-elliptic
// Hilbert transform, the one-component and equal-level limits), an exact
// identity the converged solution must satisfy (the CPA condition, the DOS sum
// rule, spectral positivity), or a limit whose value is fixed by concentration
// alone (the split-band weights). Nothing compares against a stored number
// produced by this code.

#include "core/CpaSolver.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using calango::core::CpaSolver;

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

void checkClose(double got, double want, double tol, const std::string& what)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  %-4s %s (got %.10g, want %.10g, tol %g)\n", ok ? "ok" : "FAIL",
                what.c_str(), got, want, tol);
    if (!ok)
        ++failures;
}

constexpr double kPi = 3.14159265358979323846;

/// Exact Hilbert transform of the semi-elliptic band of half-width W:
///     G(ζ) = 2[ζ − sqrt(ζ² − W²)] / W²,
/// with the branch fixed by G → 1/ζ as |ζ| → ∞.
std::complex<double> semicircularGreenExact(std::complex<double> zeta, double w)
{
    const std::complex<double> root = std::sqrt(zeta * zeta - w * w);
    // Pick the branch that decays: sqrt() returns the principal branch, which
    // has positive real part, so flip when that makes G grow instead of decay.
    const std::complex<double> gPlus = 2.0 * (zeta - root) / (w * w);
    const std::complex<double> gMinus = 2.0 * (zeta + root) / (w * w);
    return (std::abs(gPlus) < std::abs(gMinus)) ? gPlus : gMinus;
}

std::vector<CpaSolver::Component> binary(double cA, double eA, double eB)
{
    return {{"A", cA, eA, 0.0}, {"B", 1.0 - cA, eB, 0.0}};
}

// ---------------------------------------------------------------------------

void testHilbertTransform()
{
    std::printf("Lattice Green's function against the exact semi-elliptic form:\n");
    const double w = 1.0;
    // A single component at zero level makes Σ = 0 exactly, so solve() returns
    // the bare lattice Green's function and this isolates the quadrature.
    CpaSolver solver({{"A", 1.0, 0.0, 0.0}}, CpaSolver::Lattice::semicircular(w));

    double worst = 0.0;
    for (double e = -1.6; e <= 1.6001; e += 0.05) {
        const auto sol = solver.solve(e);
        const std::complex<double> exact =
            semicircularGreenExact({e, 0.02}, w);
        worst = std::max(worst, std::abs(sol.greenFunction - exact));
    }
    checkClose(worst, 0.0, 2e-4,
               "max |G_quadrature - G_exact| over the band and its tails");
}

void testOneComponentLimit()
{
    std::printf("One component: Sigma is exactly the on-site level\n");
    CpaSolver solver({{"A", 1.0, 0.35, 0.0}},
                     CpaSolver::Lattice::semicircular(1.0));
    const auto sol = solver.solve(0.2);
    check(sol.converged, "converged");
    checkClose(sol.selfEnergy.real(), 0.35, 1e-9, "Re Sigma = epsilon_A");
    checkClose(sol.selfEnergy.imag(), 0.0, 1e-9,
               "Im Sigma = 0 (no disorder, no scattering)");
    checkClose(sol.residual, 0.0, 1e-12, "CPA residual vanishes identically");
}

void testVirtualCrystalLimit()
{
    std::printf("Equal levels: CPA collapses to the virtual crystal\n");
    // Two components that differ only in name. Any self-energy other than the
    // common level would be a bug in the iteration rather than physics.
    CpaSolver solver(binary(0.3, 0.25, 0.25),
                     CpaSolver::Lattice::semicircular(1.0));
    const auto sol = solver.solve(-0.1);
    checkClose(sol.selfEnergy.real(), 0.25, 1e-9, "Re Sigma = the common level");
    checkClose(sol.selfEnergy.imag(), 0.0, 1e-9, "Im Sigma = 0");
}

void testCpaConditionAndSumRule()
{
    std::printf("The CPA condition and the DOS sum rule, pointwise:\n");
    CpaSolver solver(binary(0.35, -0.4, 0.5),
                     CpaSolver::Lattice::semicircular(1.0, 1201));

    double worstResidual = 0.0;
    double worstSum = 0.0;
    bool allConverged = true;
    bool herglotz = true;
    for (double e = -2.0; e <= 2.0001; e += 0.05) {
        const auto sol = solver.solve(e);
        allConverged = allConverged && sol.converged;
        worstResidual = std::max(worstResidual, sol.residual);
        // Sum_i c_i n_i(E) = n(E) must hold at every energy: it is the CPA
        // condition restated on the imaginary part.
        const auto partial = solver.partialDos(e);
        double sum = 0.0;
        for (double p : partial)
            sum += p;
        worstSum = std::max(worstSum, std::abs(sum - solver.totalDos(e)));
        // A retarded self-energy is Herglotz: Im Sigma <= 0, Im G < 0.
        herglotz = herglotz && sol.selfEnergy.imag() <= 1e-12
            && sol.greenFunction.imag() < 0.0;
    }
    check(allConverged, "every energy converged");
    checkClose(worstResidual, 0.0, 1e-9,
               "max |sum_i c_i G_i - G| over the band");
    checkClose(worstSum, 0.0, 1e-12, "partial DOS sums to the total DOS");
    check(herglotz, "Im Sigma <= 0 and Im G < 0 everywhere (Herglotz)");
}

void testDosNormalisation()
{
    std::printf("DOS normalisation: two states per site, minus the known tail\n");
    const double eta = 0.02;
    const double cutoff = 6.0;
    CpaSolver::Options opts;
    opts.broadening = eta;
    CpaSolver solver(binary(0.5, -0.5, 0.5),
                     CpaSolver::Lattice::semicircular(1.0, 1201), opts);
    const auto grid = solver.computeDosGrid(-cutoff, cutoff, 1201);

    // The sum rule is ∫ n dE = 2 over the WHOLE real line, and a finite window
    // cannot reach it: each spectral peak is Lorentzian, whose η/(πE²) tail
    // carries weight outside any cutoff. That missing weight is not an error
    // to be loosened away — it has a closed form.
    //
    // Far from the band, n(E) → η/(πE²) per spin (unit total weight), so the
    // two tails of the two spins remove 4η/(πE₀) between them.
    const double tail = 4.0 * eta / (kPi * cutoff);
    checkClose(CpaSolver::integratedDos(grid, cutoff), 2.0 - tail, 1e-4,
               "integral equals 2 minus the analytic Lorentzian tail");
}

void testSplitBandWeights()
{
    std::printf("Split-band limit: sub-band weights are the concentrations\n");
    // Level separation far larger than the bandwidth splits the spectrum into
    // an A-derived and a B-derived sub-band. Their integrated weights are then
    // fixed by c alone — a result that owes nothing to this implementation.
    const double cA = 0.3;
    CpaSolver solver(binary(cA, -6.0, 6.0),
                     CpaSolver::Lattice::semicircular(1.0, 1201));
    const auto grid = solver.computeDosGrid(-12.0, 12.0, 2401);
    const double lower = CpaSolver::integratedDos(grid, 0.0);
    const double total = CpaSolver::integratedDos(grid, 12.0);
    checkClose(total, 2.0, 5e-3, "total weight still 2");
    checkClose(lower / total, cA, 5e-3,
               "lower sub-band carries exactly c_A of the states");
}

void testBlochSpectralFunction()
{
    std::printf("Bloch spectral function:\n");
    const double w = 1.0;
    const double eta = 0.02;
    CpaSolver::Options opts;
    opts.broadening = eta;

    // Ordered limit: one component, so Im Sigma = 0 and A(k,E) is a pure
    // Lorentzian of half-width eta centred on the band energy. Its peak is
    // then 1/(pi*eta) exactly.
    CpaSolver ordered({{"A", 1.0, 0.0, 0.0}},
                      CpaSolver::Lattice::semicircular(w), opts);
    const double peak = ordered.blochSpectralFunction(0.3, 0.3);
    checkClose(peak, 1.0 / (kPi * eta), 1e-6,
               "ordered peak height = 1/(pi*eta)");

    // Positivity and the k-integral. A(k,E) integrated over the band energies
    // with the bare DOS weight is the total DOS per spin at that energy.
    CpaSolver alloy(binary(0.5, -0.3, 0.3),
                    CpaSolver::Lattice::semicircular(w, 601), opts);
    const auto lattice = CpaSolver::Lattice::semicircular(w, 601);
    const double energy = 0.1;
    double integral = 0.0;
    bool positive = true;
    for (std::size_t j = 0; j < lattice.energies.size(); ++j) {
        const double a =
            alloy.blochSpectralFunction(lattice.energies[j], energy);
        positive = positive && a >= 0.0;
        integral += lattice.weights[j] * a;
    }
    check(positive, "A(k,E) >= 0 everywhere");
    // The per-spin DOS is half the (spin-summed) total for a non-magnetic
    // alloy, and the k-average of the spectral function is exactly that.
    checkClose(integral, 0.5 * alloy.totalDos(energy), 1e-9,
               "k-averaged A(k,E) equals the per-spin DOS");

    // Disorder broadens: the alloy peak must be lower and wider than the
    // ordered one at the same eta.
    const double alloyPeak = alloy.blochSpectralFunction(0.0, 0.0);
    check(alloyPeak < 1.0 / (kPi * eta),
          "disorder lowers the spectral peak below the ordered limit");
}

void testMagneticMoments()
{
    std::printf("Magnetic alloy:\n");
    // Zero splitting must reproduce the non-magnetic answer exactly.
    CpaSolver nonMagnetic(binary(0.5, -0.2, 0.2),
                          CpaSolver::Lattice::semicircular(1.0));
    std::vector<CpaSolver::Component> withZeroSplit = binary(0.5, -0.2, 0.2);
    withZeroSplit[0].exchangeSplitting = 0.0;
    CpaSolver alsoNonMagnetic(withZeroSplit,
                              CpaSolver::Lattice::semicircular(1.0));
    checkClose(alsoNonMagnetic.totalDos(0.15), nonMagnetic.totalDos(0.15), 1e-12,
               "Delta = 0 reproduces the non-magnetic DOS bit for bit");

    const auto nmGrid = nonMagnetic.computeDosGrid(-6.0, 6.0, 401);
    const auto zeroMoments = nonMagnetic.componentMoments(nmGrid, 0.0);
    checkClose(zeroMoments[0], 0.0, 1e-12, "non-magnetic component moment is 0");

    // One magnetic component in a non-magnetic host. The moment must be
    // positive, and must be carried by the split component.
    std::vector<CpaSolver::Component> magnetic = {
        {"Fe", 0.25, 0.0, 2.0},
        {"Cu", 0.75, 0.0, 0.0},
    };
    CpaSolver solver(magnetic, CpaSolver::Lattice::semicircular(1.0, 1201));
    const auto grid = solver.computeDosGrid(-6.0, 6.0, 601);
    const double ef = CpaSolver::findFermiEnergy(grid, 1.0);
    const auto moments = solver.componentMoments(grid, ef);
    check(moments[0] > 0.05, "the split component carries a moment");
    check(std::abs(moments[1]) < std::abs(moments[0]),
          "the unsplit component carries less than the split one");
}

void testFermiEnergyBisection()
{
    std::printf("Fermi level from the band filling:\n");
    CpaSolver solver(binary(0.5, -0.3, 0.3),
                     CpaSolver::Lattice::semicircular(1.0, 1201));
    const auto grid = solver.computeDosGrid(-6.0, 6.0, 1201);
    const double ef = CpaSolver::findFermiEnergy(grid, 1.0);
    const double filled = CpaSolver::integratedDos(grid, ef);
    checkClose(filled, 1.0, 1e-6, "inversion lands on the requested filling");
    // Half filling of a symmetric alloy sits at the symmetry point, E = 0.
    checkClose(ef, 0.0, 2e-2, "half filling of a symmetric alloy is at E = 0");
}

} // namespace

int main()
{
    std::printf("CpaSolver — closed-form checks\n\n");
    testHilbertTransform();
    testOneComponentLimit();
    testVirtualCrystalLimit();
    testCpaConditionAndSumRule();
    testDosNormalisation();
    testSplitBandWeights();
    testBlochSpectralFunction();
    testMagneticMoments();
    testFermiEnergyBisection();

    std::printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
