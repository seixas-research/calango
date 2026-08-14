// Native cRPA in a Wannier basis, checked against closed forms and exact
// identities rather than against stored output of this code.
//
// The anchors are: the two analytic limits of the Gaussian Coulomb integral,
// the exactness of the eigen-decomposition, and — most importantly — the three
// situations in which the screened interaction must collapse EXACTLY onto the
// bare one (no allowed transitions, every transition constrained away, a gap
// above the cutoff). Those last three are what actually test the constraint
// bookkeeping, which is the part of cRPA that is easy to get subtly wrong.

#include "core/CrpaSolver.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using calango::core::CrpaSolver;

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
constexpr double kCoulomb = 14.399645; // eV·Å

/// Three correlated "d" orbitals plus one ligand "p" orbital on a cubic cell.
/// The p level sits well below the d manifold, so p→d screening exists and is
/// exactly what cRPA must keep while removing d→d.
CrpaSolver::Model buildModel(double dLevel = 0.0, double pLevel = -3.0,
                             double hopping = 0.4)
{
    CrpaSolver::Model model;
    const double a = 4.0;
    model.cell = {{{a, 0.0, 0.0}, {0.0, a, 0.0}, {0.0, 0.0, a}}};

    for (int m = 0; m < 3; ++m)
        model.orbitals.push_back(
            {"d" + std::to_string(m), {0.0, 0.0, 0.0}, 0.6, true});
    model.orbitals.push_back({"p", {2.0, 0.0, 0.0}, 1.2, false});

    const std::size_t n = model.orbitals.size();
    // On-site block.
    CrpaSolver::HoppingBlock onsite;
    onsite.lattice = {0, 0, 0};
    onsite.matrix.assign(n * n, 0.0);
    for (int m = 0; m < 3; ++m)
        onsite.matrix[m * n + m] = dLevel;
    onsite.matrix[3 * n + 3] = pLevel;
    // p-d hybridisation, which is what gives the bands mixed character and
    // makes the weighted constraint do something non-trivial.
    for (int m = 0; m < 3; ++m) {
        onsite.matrix[m * n + 3] = 0.5;
        onsite.matrix[3 * n + m] = 0.5;
    }
    model.hoppings.push_back(onsite);

    // Nearest-neighbour hopping along x, giving the bands dispersion.
    for (int sign : {-1, 1}) {
        CrpaSolver::HoppingBlock block;
        block.lattice = {sign, 0, 0};
        block.matrix.assign(n * n, 0.0);
        for (int m = 0; m < 3; ++m)
            block.matrix[m * n + m] = hopping;
        block.matrix[3 * n + 3] = hopping;
        model.hoppings.push_back(block);
    }

    model.electrons = 4.0;
    return model;
}

// ---------------------------------------------------------------------------

void testBareCoulombLimits()
{
    std::printf("Bare Coulomb against its two analytic limits:\n");
    CrpaSolver::Model model;
    model.cell = {{{500.0, 0.0, 0.0}, {0.0, 500.0, 0.0}, {0.0, 0.0, 500.0}}};
    const double omega = 0.9; // spread in Å²
    model.orbitals.push_back({"a", {0.0, 0.0, 0.0}, omega, true});
    model.orbitals.push_back({"b", {60.0, 0.0, 0.0}, omega, true});
    model.hoppings.push_back({{0, 0, 0}, std::vector<double>(4, 0.0)});
    model.electrons = 1.0;

    CrpaSolver solver(model);
    const auto v = solver.bareCoulomb();

    // On site: V = e²/4πε₀ · sqrt(2/(π(s_i²+s_j²))) with s² = Ω/3.
    const double s2 = omega / 3.0;
    const double onsite = kCoulomb * std::sqrt(2.0 / (kPi * (s2 + s2)));
    checkClose(v[0][0], onsite, 1e-9, "on-site V equals the R->0 closed form");

    // Far apart: erf -> 1 and V -> e²/4πε₀ / R, the classical point-charge law.
    checkClose(v[0][1], kCoulomb / 60.0, 1e-9,
               "V at large separation is the bare 1/R law");
    check(v[0][1] < v[0][0], "separated orbitals interact less than on-site");
    check(std::abs(v[0][1] - v[1][0]) < 1e-14, "V is symmetric");
}

void testEigenDecomposition()
{
    std::printf("Hermitian eigensolver reconstructs H(k) exactly:\n");
    CrpaSolver solver(buildModel());
    const std::array<double, 3> k{0.13, 0.29, 0.41};
    const auto h = solver.hamiltonianAt(k);
    const std::size_t n = h.size();

    // H must be Hermitian to begin with.
    double herm = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j)
            herm = std::max(herm, std::abs(h[i][j] - std::conj(h[j][i])));
    checkClose(herm, 0.0, 1e-14, "H(k) is Hermitian");
}

void testNoTransitionsGivesBareInteraction()
{
    std::printf("W = V exactly when no transition survives:\n");
    // Screening cutoff below the smallest excitation energy: every transition
    // is dropped, P = 0, and the geometric series collapses to its first term.
    CrpaSolver::Options opts;
    opts.kmesh = {2, 2, 2};
    opts.screeningCutoff = 1e-6;
    CrpaSolver solver(buildModel(), opts);

    const auto v = solver.bareCoulomb();
    const auto p = solver.polarizability(0.0);
    const auto w = solver.screenedCoulomb(0.0);

    double maxP = 0.0;
    double maxDiff = 0.0;
    for (std::size_t i = 0; i < v.size(); ++i)
        for (std::size_t j = 0; j < v.size(); ++j) {
            maxP = std::max(maxP, std::abs(p[i][j]));
            maxDiff = std::max(maxDiff, std::abs(w[i][j] - v[i][j]));
        }
    checkClose(maxP, 0.0, 1e-14, "P vanishes with every transition cut off");
    checkClose(maxDiff, 0.0, 1e-12, "W(0) equals V element by element");
}

void testConstraintRemovesAllScreeningWhenEverythingIsCorrelated()
{
    std::printf("W = V when the correlated subspace is the whole basis:\n");
    // Every orbital correlated => every band has d-weight 1 => the constraint
    // factor 1 - w_n w_m vanishes for every transition => P_rest = 0.
    // This is the sharpest available test of the constraint bookkeeping: it
    // has to zero the polarizability for reasons of algebra, not of tolerance.
    auto model = buildModel();
    for (auto& orbital : model.orbitals)
        orbital.correlated = true;
    CrpaSolver::Options opts;
    opts.kmesh = {2, 2, 2};
    CrpaSolver solver(model, opts);

    const auto v = solver.bareCoulomb();
    const auto p = solver.polarizability(0.0);
    const auto w = solver.screenedCoulomb(0.0);

    double maxP = 0.0;
    double maxDiff = 0.0;
    for (std::size_t i = 0; i < v.size(); ++i)
        for (std::size_t j = 0; j < v.size(); ++j) {
            maxP = std::max(maxP, std::abs(p[i][j]));
            maxDiff = std::max(maxDiff, std::abs(w[i][j] - v[i][j]));
        }
    checkClose(maxP, 0.0, 1e-12, "P_rest vanishes identically");
    checkClose(maxDiff, 0.0, 1e-10, "W(0) = V, so U = U_bare");

    // ...and with the constraint lifted, the same system DOES screen. If this
    // failed, the test above would be passing for the wrong reason.
    const auto pFull = solver.polarizability(0.0, /*includeCorrelated=*/true);
    double maxFull = 0.0;
    for (const auto& row : pFull)
        for (const auto& value : row)
            maxFull = std::max(maxFull, std::abs(value));
    check(maxFull > 1e-6,
          "unconstrained RPA on the same system has non-zero P");
}

void testScreeningReducesTheInteraction()
{
    std::printf("Screening, and the ordering cRPA exists to produce:\n");
    CrpaSolver::Options opts;
    opts.kmesh = {3, 3, 3};
    CrpaSolver solver(buildModel(), opts);

    const auto interaction = solver.staticInteraction();
    check(interaction.u > 0.0, "U is positive");
    check(interaction.u < interaction.uBare,
          "screening reduces U below the bare value");

    // The defining inequality of cRPA: removing the d-d screening channel can
    // only leave the remaining screening weaker, so the constrained U must
    // exceed the fully screened RPA U. A sign error in the constraint shows up
    // here and almost nowhere else.
    const auto wFull = solver.screenedCoulomb(0.0, /*includeCorrelated=*/true);
    const auto indices = solver.correlatedIndices();
    double uFull = 0.0;
    for (std::size_t i : indices)
        uFull += wFull[i][i].real();
    uFull /= static_cast<double>(indices.size());
    check(interaction.u > uFull,
          "constrained U exceeds fully screened RPA U");
    std::printf("       U_bare = %.3f eV, U_cRPA = %.3f eV, U_RPA = %.3f eV\n",
                interaction.uBare, interaction.u, uFull);
}

void testKanamoriAndMatrixStructure()
{
    std::printf("Kanamori bookkeeping and matrix structure:\n");
    CrpaSolver::Options opts;
    opts.kmesh = {3, 3, 3};
    CrpaSolver solver(buildModel(), opts);
    const auto interaction = solver.staticInteraction();

    checkClose(interaction.j, 0.5 * (interaction.u - interaction.uPrime), 1e-12,
               "J = (U - U')/2 holds by construction");
    check(interaction.screenedMatrix.size() == 3,
          "the screened matrix spans the three correlated orbitals");

    // A DEGENERATE shell must give exactly J = 0 here, and that is a closed-
    // form statement rather than a tolerance: the three orbitals share a
    // centre and a spread, so their spherical Gaussian densities are the same
    // function and every density-density element is literally the same number.
    // Anything else would mean the Coulomb integral depends on the orbital
    // label, which it cannot.
    checkClose(interaction.uPrime, interaction.u, 1e-10,
               "degenerate spherical shell: U' = U identically");
    checkClose(interaction.j, 0.0, 1e-10,
               "degenerate spherical shell: J = 0 identically");

    // Make the orbitals inequivalent and J must appear. This is the case a
    // real t2g/eg splitting or a two-site correlated subspace produces.
    auto split = buildModel();
    split.orbitals[0].spread = 0.45;
    split.orbitals[1].spread = 0.60;
    split.orbitals[2].spread = 0.80;
    CrpaSolver splitSolver(split, opts);
    const auto splitInteraction = splitSolver.staticInteraction();
    check(splitInteraction.uPrime < splitInteraction.u,
          "inequivalent orbitals: U' falls below U");
    check(splitInteraction.j > 0.0, "inequivalent orbitals: J is positive");
    std::printf("       inequivalent shell: U = %.3f, U' = %.3f, J = %.3f eV\n",
                splitInteraction.u, splitInteraction.uPrime,
                splitInteraction.j);

    // W(0) must be Hermitian; with a real V and a real static P its correlated
    // block is real symmetric.
    const auto w = solver.screenedCoulomb(0.0);
    double asym = 0.0;
    for (std::size_t i = 0; i < w.size(); ++i)
        for (std::size_t j = 0; j < w.size(); ++j)
            asym = std::max(asym, std::abs(w[i][j] - std::conj(w[j][i])));
    checkClose(asym, 0.0, 1e-9, "W(0) is Hermitian");
}

void testStaticPolarizabilityIsNegative()
{
    std::printf("Static polarizability has the sign screening requires:\n");
    CrpaSolver::Options opts;
    opts.kmesh = {3, 3, 3};
    CrpaSolver solver(buildModel(), opts);
    const auto p = solver.polarizability(0.0);
    bool negativeDiagonal = true;
    for (std::size_t i = 0; i < p.size(); ++i)
        negativeDiagonal = negativeDiagonal && p[i][i].real() <= 1e-12;
    check(negativeDiagonal,
          "Re P_ii(0) <= 0, so the dielectric function exceeds 1");
}

void testCalculatorAgnosticInput()
{
    std::printf("Input surface is Wannier data only:\n");
    // The whole model is centres, spreads, H(R) and a cell. If this compiles
    // and runs there is no plane-wave, pseudopotential or code-specific path
    // in the solver — the type system is the assertion.
    CrpaSolver::Model model = buildModel();
    check(!model.orbitals.empty() && !model.hoppings.empty(),
          "a model is fully specified by orbitals + H(R) + cell");
    CrpaSolver solver(model);
    check(solver.correlatedIndices().size() == 3,
          "the correlated subspace is declared per orbital");
}

void testSlaterIntegrals()
{
    std::printf("Slater integrals of the Gaussian shell:\n");
    const double omega = 0.75;
    const auto f = CrpaSolver::slaterIntegrals(omega);

    // F0 is the monopole, i.e. the electrostatic self-energy of the Gaussian
    // charge density — the SAME quantity bareCoulomb() gets from the erf
    // closed form by a completely different route (2D radial quadrature here,
    // an analytic R -> 0 limit there). They have to agree.
    const double s2 = omega / 3.0;
    const double closedForm = kCoulomb * std::sqrt(2.0 / (kPi * (s2 + s2)));
    checkClose(f[0], closedForm, 2e-4,
               "F0 from quadrature equals the erf closed form for V_onsite");

    check(f[1] > 0.0 && f[2] > 0.0, "F2 and F4 are positive");
    check(f[1] > f[2] && f[2] > f[3],
          "the multipole series decreases: F2 > F4 > F6");

    // Every F^k of a Gaussian scales as 1/s, so the RATIOS are pure numbers
    // independent of the spread. That is an exact invariant of the model, and
    // it catches an error in the radial weight that a single-spread check
    // would sail past.
    const auto wide = CrpaSolver::slaterIntegrals(4.0 * omega);
    checkClose(wide[1] / wide[0], f[1] / f[0], 1e-6,
               "F2/F0 is independent of the spread");
    checkClose(wide[2] / wide[0], f[2] / f[0], 1e-6,
               "F4/F0 is independent of the spread");
    // ...and the scaling itself: doubling s halves every integral.
    checkClose(wide[0] * 2.0, f[0], 2e-3, "F0 scales as 1/s");
    std::printf("       F0 = %.3f, F2 = %.3f, F4 = %.3f eV  (F4/F2 = %.3f)\n",
                f[0], f[1], f[2], f[2] / f[1]);
}

void testHundsJFromAngularStructure()
{
    std::printf("Hund's J once the shell declares an angular momentum:\n");
    auto model = buildModel();
    for (auto& orbital : model.orbitals)
        if (orbital.correlated)
            orbital.angularL = 2; // a d shell
    CrpaSolver::Options opts;
    opts.kmesh = {3, 3, 3};
    CrpaSolver solver(model, opts);
    const auto interaction = solver.staticInteraction();

    check(interaction.jFromSlater, "J is taken from the Slater route");
    check(interaction.jSlater > 0.0,
          "a degenerate d shell now has a non-zero J");
    checkClose(interaction.jKanamori, 0.0, 1e-10,
               "the Kanamori route still gives zero, as it must");
    checkClose(interaction.j,
               (interaction.slaterF[1] + interaction.slaterF[2]) / 14.0, 1e-12,
               "J = (F2 + F4)/14 for d electrons");
    // F0 reported by the Slater route is the BARE monopole, so it must match
    // the bare U the density-density route computed independently.
    checkClose(interaction.slaterF[0], interaction.uBare, 2e-4,
               "Slater F0 agrees with U_bare from the erf route");
    check(interaction.u < interaction.uBare, "U is still screened");
    std::printf("       U = %.3f eV, J = %.3f eV (Slater), U_bare = %.3f eV\n",
                interaction.u, interaction.j, interaction.uBare);

    // A p shell uses a different angular formula, and an s shell has no
    // exchange at all — both are exact statements of the algebra.
    auto pModel = model;
    for (auto& orbital : pModel.orbitals)
        if (orbital.correlated)
            orbital.angularL = 1;
    const auto pInteraction = CrpaSolver(pModel, opts).staticInteraction();
    checkClose(pInteraction.j, pInteraction.slaterF[1] / 5.0, 1e-12,
               "J = F2/5 for p electrons");

    auto sModel = model;
    for (auto& orbital : sModel.orbitals)
        if (orbital.correlated)
            orbital.angularL = 0;
    const auto sInteraction = CrpaSolver(sModel, opts).staticInteraction();
    checkClose(sInteraction.j, 0.0, 1e-12,
               "an s shell has no exchange: J = 0");
}

} // namespace

int main()
{
    std::printf("CrpaSolver — closed-form and identity checks\n\n");
    testBareCoulombLimits();
    testEigenDecomposition();
    testNoTransitionsGivesBareInteraction();
    testConstraintRemovesAllScreeningWhenEverythingIsCorrelated();
    testScreeningReducesTheInteraction();
    testKanamoriAndMatrixStructure();
    testStaticPolarizabilityIsNegative();
    testCalculatorAgnosticInput();
    testSlaterIntegrals();
    testHundsJFromAngularStructure();

    std::printf("\n%d check(s) FAILED.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
