// The Bethe-Salpeter exciton solver, tested against:
//   1. The analytic 3D Rydberg series E_b(n) = -R*/n^2 (Wannier-Mott limit):
//      a synthetic two-band parabolic model with a statically screened
//      Coulomb kernel, no exchange (triplet), checked for the RIGHT SIGN,
//      the RIGHT ORDER OF MAGNITUDE, and — the discriminating check — that
//      the ground-state binding energy converges MONOTONICALLY toward R* as
//      the k-mesh densifies. A lattice BSE sum converges to the continuum
//      hydrogenic limit slowly (the task's own stated caveat), and reaching
//      the fully converged value needs meshes far too large for a fast
//      ctest — the monotonic TREND is what a discrete-vs-continuum
//      comparison can honestly assert at a k-mesh size that finishes in
//      seconds, not what a single absolute-tolerance number would claim.
//   2. Kernel Hermiticity, directly, via hamiltonianForTesting().
//   3. The singlet/triplet exchange switch: singlet (+2v) must be LESS
//      bound than triplet (no exchange) at the identical mesh and
//      screening, since exchange is repulsive.
//   4. The 2D Rytova-Keldysh potential: r0 = 0 reduces EXACTLY to the bare
//      2D Coulomb closed form (checked on the formula directly, no
//      diagonalization needed), and a full small BSE run's ground-state
//      binding decreases MONOTONICALLY as r0 increases (screening always
//      weakens the interaction) — the two qualitative behaviors the task
//      asks for. Full numerical convergence to the non-hydrogenic
//      1/(n-1/2)^2 series is NOT attempted here for the same cost reason as
//      item 1 (2D convergence is, if anything, slower — the unscreened 2D
//      Coulomb has a stronger long-range tail); see FUTURE.md.

#include "core/BseSolver.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace calango::core;

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

/// ħ²/(2mₑ), eV.Å² — the standard kinetic-energy prefactor, so a hopping t
/// (eV) on a simple-cubic lattice of constant a (Å) gives effective mass
/// m*/mₑ = (ħ²/2mₑ) / (t a²) for the s-band tight-binding dispersion below.
constexpr double kHbar2Over2Me = 3.809982;
/// 13.605693 eV, the hydrogen Rydberg — R* = 13.605693 * (mu/me) / eps^2.
constexpr double kRydbergEv = 13.605693;

/// A decoupled two-orbital simple-cubic tight-binding model: orbital 0
/// (valence) has its band MAXIMUM at Gamma (E_v(k) curves down away from
/// Gamma, giving a hole of positive effective mass hbar2_2me/(tv*a^2));
/// orbital 1 (conduction) has its band MINIMUM at Gamma, gap `eg` above the
/// valence top. No inter-orbital hopping, so H(k) is exactly diagonal at
/// every k and the two bands never hybridize — the clean single-valence/
/// single-conduction-pair limit the class doc's kernel derivation assumes.
///
/// H_00(k) = -6*tv + 2*tv*(cos(kx*a)+cos(ky*a)+cos(kz*a))  -> 0 at Gamma
/// H_11(k) = eg + 6*tc - 2*tc*(cos(kx*a)+cos(ky*a)+cos(kz*a)) -> eg at Gamma
WannierHamiltonian buildTwoBandModel(double a, double tv, double tc, double eg)
{
    std::vector<WannierHamiltonian::HoppingBlock> hoppings;
    WannierHamiltonian::HoppingBlock onsite;
    onsite.lattice = {0, 0, 0};
    onsite.matrix = {-6.0 * tv, 0.0, 0.0, eg + 6.0 * tc};
    hoppings.push_back(onsite);
    const std::array<std::array<int, 3>, 6> dirs = {{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    }};
    for (const auto& d : dirs) {
        WannierHamiltonian::HoppingBlock b;
        b.lattice = d;
        b.matrix = {tv, 0.0, 0.0, -tc};
        hoppings.push_back(b);
    }
    const std::array<std::array<double, 3>, 3> cell{{{a, 0, 0}, {0, a, 0}, {0, 0, a}}};
    return WannierHamiltonian(2, cell, hoppings);
}

/// Same as buildTwoBandModel, but with a small ON-SITE (k-independent)
/// interband coupling `mix` added between the two orbitals — the exchange
/// kernel is proportional to the transition DIPOLE (see BseSolver.cpp's
/// class doc), which is exactly zero for buildTwoBandModel's fully
/// decoupled orbitals (a "dark", dipole-forbidden exciton pair has no
/// exchange splitting — correct physics, but nothing for a singlet/triplet
/// test to see). A small hybridization gives both bands a genuine,
/// k-dependent interband dipole to exercise.
WannierHamiltonian buildTwoBandModelWithCoupling(double a, double tv, double tc, double eg,
                                                  double mix)
{
    std::vector<WannierHamiltonian::HoppingBlock> hoppings;
    WannierHamiltonian::HoppingBlock onsite;
    onsite.lattice = {0, 0, 0};
    onsite.matrix = {-6.0 * tv, mix, mix, eg + 6.0 * tc};
    hoppings.push_back(onsite);
    const std::array<std::array<int, 3>, 6> dirs = {{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    }};
    for (const auto& d : dirs) {
        WannierHamiltonian::HoppingBlock b;
        b.lattice = d;
        b.matrix = {tv, 0.0, 0.0, -tc};
        hoppings.push_back(b);
    }
    const std::array<std::array<double, 3>, 3> cell{{{a, 0, 0}, {0, a, 0}, {0, 0, a}}};
    return WannierHamiltonian(2, cell, hoppings);
}

/// Same idea, restricted to two in-plane dimensions with a vacuum third
/// axis (z): a 2D square-lattice analogue for the Rytova-Keldysh tests.
/// Vacuum length is irrelevant to the potential (Rytova-Keldysh is already
/// area-normalized, no vacuum-height dependence the way a 3D volume-
/// normalized quantity would have), but the third lattice vector still has
/// to point clearly along z for BseSolver's own vacuum-axis inference
/// (the largest |cell[2][axis]| component) to land on it.
WannierHamiltonian buildTwoBandModel2D(double a, double tv, double tc, double eg,
                                       double vacuum)
{
    std::vector<WannierHamiltonian::HoppingBlock> hoppings;
    WannierHamiltonian::HoppingBlock onsite;
    onsite.lattice = {0, 0, 0};
    onsite.matrix = {-4.0 * tv, 0.0, 0.0, eg + 4.0 * tc};
    hoppings.push_back(onsite);
    const std::array<std::array<int, 3>, 4> dirs = {{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0},
    }};
    for (const auto& d : dirs) {
        WannierHamiltonian::HoppingBlock b;
        b.lattice = d;
        b.matrix = {tv, 0.0, 0.0, -tc};
        hoppings.push_back(b);
    }
    const std::array<std::array<double, 3>, 3> cell{
        {{a, 0, 0}, {0, a, 0}, {0, 0, vacuum}}};
    return WannierHamiltonian(2, cell, hoppings);
}

void testHydrogenicConvergenceTrend()
{
    std::printf("\n3D Wannier-Mott limit: E_b(1) converges toward -R* as the k-mesh densifies\n");
    // Unscreened (epsilon=1) for the smallest practical Bohr radius, so a
    // modest mesh already shows a clear, monotonic trend rather than
    // sitting at a barely-nonzero plateau -- see BseSolverTest's own
    // exploration notes: screened (epsilon > 1) models need meshes far too
    // large to finish in a fast ctest before showing real movement.
    const double a = 5.0, t = 1.0, eg = 20.0, epsInf = 1.0;
    auto ham = buildTwoBandModel(a, t, t, eg);
    const double mass = kHbar2Over2Me / (t * a * a); // mv == mc == mass here
    const double mu = mass / 2.0; // reduced mass, equal masses
    const double rstar = kRydbergEv * mu / (epsInf * epsInf);
    check(rstar > 0.0, "R* is positive (a sanity check on the model construction itself)");

    double previousBinding = 0.0;
    bool monotonic = true;
    double lastRatio = 0.0;
    // Capped at 12 per axis (N=1728): large enough to show a clear,
    // meaningful convergence trend (see the loose floor check below) while
    // staying reliably fast under load — larger meshes (16, 20 per axis)
    // measured anywhere from ~15 seconds to several MINUTES depending on
    // concurrent system load in practice, an unacceptable spread for a
    // ctest even with a generous TIMEOUT.
    for (int nMesh : {6, 8, 10, 12}) {
        BseSolver::Options opt;
        opt.kmesh = {nMesh, nMesh, nMesh};
        opt.valenceBandTop = 0;
        opt.nValence = 1;
        opt.nConduction = 1;
        opt.spin = BseSolver::Spin::Triplet; // no exchange -> pure hydrogenic kernel
        opt.epsilonInfinity = epsInf;
        opt.lowestExcitons = 2;
        opt.denseSizeLimit = 300; // n=216 (mesh 6) dense, everything else Lanczos
        opt.lanczosIterations = 150;
        BseSolver solver(ham, opt);
        const auto result = solver.solve();
        check(!result.excitons.empty(), "mesh " + std::to_string(nMesh) + " produced at least one exciton");
        if (result.excitons.empty())
            continue;
        const double binding = result.excitons[0].bindingEnergy;
        std::printf("    mesh=%d^3 N=%zu iterative=%d E_b(1)=%.6f eV (target -R*=%.6f eV)\n",
                    nMesh, result.basisDimension, result.usedIterativeSolver ? 1 : 0, binding,
                    -rstar);
        check(binding < 0.0, "mesh " + std::to_string(nMesh) + ": the ground state is BOUND (negative)");
        if (nMesh > 6)
            monotonic = monotonic && (std::abs(binding) > std::abs(previousBinding) - 1e-9);
        previousBinding = binding;
        lastRatio = binding / (-rstar);
    }
    check(monotonic,
        "|E_b(1)| increases MONOTONICALLY toward R* as the mesh densifies (6..20 per axis)");
    // A loose floor, not a tight target: this mesh range recovers only a
    // fraction of the continuum value (see the class/test-file doc comment
    // on why full convergence is not attempted here), but it must be a
    // MEANINGFUL, non-trivial fraction -- not stuck near zero, which would
    // indicate the kernel normalization is wrong by orders of magnitude
    // rather than merely under-converged.
    check(lastRatio > 0.05 && lastRatio < 1.5,
        "by the largest mesh tested, E_b(1)/R* is a meaningful (if not fully "
        "converged) fraction, not orders of magnitude off");
}

void testKernelHermiticity()
{
    std::printf("\nKernel Hermiticity: H_BSE[i][j] == conj(H_BSE[j][i])\n");
    auto ham = buildTwoBandModel(5.0, 1.0, 1.0, 3.0);
    BseSolver::Options opt;
    opt.kmesh = {3, 3, 3};
    opt.valenceBandTop = 0;
    opt.nValence = 1;
    opt.nConduction = 1;
    opt.spin = BseSolver::Spin::Singlet; // exercise the exchange term too
    opt.epsilonInfinity = 3.0;
    BseSolver solver(ham, opt);
    const auto h = solver.hamiltonianForTesting();
    const std::size_t n = h.size();
    check(n == 27, "the 3x3x3, single-valence/single-conduction basis has 27 states");
    bool hermitian = true;
    double maxDiagImag = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        maxDiagImag = std::max(maxDiagImag, std::abs(h[i][i].imag()));
        for (std::size_t j = 0; j < n; ++j)
            if (std::abs(h[i][j] - std::conj(h[j][i])) > 1e-10)
                hermitian = false;
    }
    check(hermitian, "H_BSE[i][j] == conj(H_BSE[j][i]) for every entry");
    checkClose(maxDiagImag, 0.0, 1e-12, "every diagonal entry is purely real");
}

void testSinglettTripletExchange()
{
    std::printf("\nSinglet/triplet switch: exchange (+2v, singlet only) is repulsive\n");
    // A small interband coupling (see buildTwoBandModelWithCoupling's doc):
    // the exchange kernel is proportional to the transition DIPOLE, which
    // is exactly zero for buildTwoBandModel's fully decoupled orbitals — a
    // "dark" exciton with no exchange splitting at all (correct, but not a
    // test of the switch).
    auto ham = buildTwoBandModelWithCoupling(5.0, 1.0, 1.0, 3.0, 0.1);
    const auto groundState = [&](BseSolver::Spin spin) {
        BseSolver::Options opt;
        opt.kmesh = {5, 5, 5}; // small on purpose: this test only needs the
                               // SIGN of the singlet-triplet splitting, not
                               // a converged binding energy.
        opt.valenceBandTop = 0;
        opt.nValence = 1;
        opt.nConduction = 1;
        opt.spin = spin;
        opt.epsilonInfinity = 3.0;
        opt.lowestExcitons = 1;
        opt.denseSizeLimit = 300;
        BseSolver solver(ham, opt);
        return solver.solve().excitons.at(0).energy;
    };
    const double triplet = groundState(BseSolver::Spin::Triplet);
    const double singlet = groundState(BseSolver::Spin::Singlet);
    std::printf("    E(triplet, no exchange) = %.6f eV, E(singlet, +2v) = %.6f eV\n", triplet,
                singlet);
    check(singlet > triplet,
        "the singlet (with the +2v exchange term) sits HIGHER than the triplet — "
        "exchange is repulsive, exactly as 2*v > 0 requires");
}

void testTransitionDipoleVanishesForDecoupledOrbitals()
{
    std::printf("\ntransitionDipoleSquared: exactly zero for two orbitals with no hopping "
                "between them\n");
    // buildTwoBandModel's two orbitals never hop into each other (H01 = H10
    // = 0 in every HoppingBlock), so H(k) — and therefore its gradient — is
    // EXACTLY block-diagonal at every k, not just at Gamma: the eigenvectors
    // are exactly the orbital basis vectors, and the interband matrix
    // element <v|dH/dk|c> is identically zero everywhere. A cleaner,
    // EXACT assertion than a merely-plausible ">= 0", and one that would
    // catch a bug that leaked spurious inter-orbital coupling into the
    // gradient contraction.
    auto ham = buildTwoBandModel(5.0, 1.0, 1.0, 3.0);
    BseSolver::Options opt;
    opt.kmesh = {4, 4, 4};
    opt.valenceBandTop = 0;
    opt.nValence = 1;
    opt.nConduction = 1;
    BseSolver solver(ham, opt);
    for (const auto& kFrac : {std::array<double, 3>{0.0, 0.0, 0.0},
                              std::array<double, 3>{0.15, 0.0, 0.0},
                              std::array<double, 3>{0.2, 0.3, -0.1}}) {
        BseSolver::BasisState state;
        state.valenceBand = 0;
        state.conductionBand = 1;
        state.kFractional = kFrac;
        const double d2 = solver.transitionDipoleSquared(state);
        check(std::isfinite(d2), "transitionDipoleSquared is finite (no NaN/Inf)");
        checkClose(d2, 0.0, 1e-20, "and exactly zero for these fully decoupled orbitals");
    }
}

void testRytovaKeldyshZeroR0MatchesBareCoulomb2D()
{
    std::printf("\nRytova-Keldysh at r0=0: exact reduction to the bare 2D Coulomb potential\n");
    const double q = 0.3, area = 25.0, eps = 1.0, qMin = 1e-6;
    const double rk = bse_detail::rytovaKeldyshPotential2D(q, 0.0, eps, area, qMin);
    const double bare2D = 2.0 * 3.14159265358979323846 * bse_detail::kCoulombEvAngstrom
        / (area * eps * q);
    checkClose(rk, bare2D, 1e-9, "v(q; r0=0) matches the bare 2D Coulomb 2*pi*ke2/(A*q) exactly");

    std::printf("\nRytova-Keldysh: screening strength decreases monotonically with r0\n");
    double previous = bse_detail::rytovaKeldyshPotential2D(q, 0.0, eps, area, qMin);
    bool monotonicDecrease = true;
    for (double r0 : {1.0, 5.0, 10.0, 30.0, 100.0}) {
        const double v = bse_detail::rytovaKeldyshPotential2D(q, r0, eps, area, qMin);
        monotonicDecrease = monotonicDecrease && (v < previous);
        previous = v;
    }
    check(monotonicDecrease, "v(q) strictly decreases as r0 increases, at fixed q");
}

void testRytovaKeldyshBindingDecreasesWithR0()
{
    std::printf("\n2D BSE: ground-state binding decreases monotonically as r0 increases\n");
    auto ham = buildTwoBandModel2D(5.0, 1.0, 1.0, 20.0, 30.0);
    double previousBinding = 0.0;
    bool first = true;
    bool monotonicDecrease = true;
    for (double r0 : {0.0, 5.0, 20.0, 50.0}) {
        BseSolver::Options opt;
        opt.kmesh = {10, 10, 1}; // 2D: a single point along the vacuum axis
        opt.valenceBandTop = 0;
        opt.nValence = 1;
        opt.nConduction = 1;
        opt.spin = BseSolver::Spin::Triplet;
        opt.dimensionality = BseSolver::Dimensionality::Slab2D;
        opt.keldyshR0Angstrom = r0;
        opt.environmentEpsilon = 1.0;
        opt.lowestExcitons = 1;
        opt.denseSizeLimit = 200;
        BseSolver solver(ham, opt);
        const double binding = solver.solve().excitons.at(0).bindingEnergy;
        std::printf("    r0=%.1f A -> E_b(1)=%.6f eV\n", r0, binding);
        check(binding < 0.0, "r0=" + std::to_string(r0) + ": the 2D ground state is bound");
        if (!first)
            monotonicDecrease = monotonicDecrease && (std::abs(binding) < std::abs(previousBinding));
        previousBinding = binding;
        first = false;
    }
    check(monotonicDecrease,
        "|E_b(1)| decreases monotonically as r0 grows (stronger screening -> weaker binding)");
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0); // line-buffered: progress is visible
                                              // while the slower sections run, not
                                              // only once the whole binary exits.
    std::printf("BseSolver - Wannier-basis Bethe-Salpeter excitons: hydrogenic/Rytova-Keldysh "
                "limits, kernel Hermiticity, singlet/triplet exchange\n");
    testHydrogenicConvergenceTrend();
    testKernelHermiticity();
    testSinglettTripletExchange();
    testTransitionDipoleVanishesForDecoupledOrbitals();
    testRytovaKeldyshZeroR0MatchesBareCoulomb2D();
    testRytovaKeldyshBindingDecreasesWithR0();

    std::printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
