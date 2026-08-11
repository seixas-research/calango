// Native DFT engine scaffold.
//
// Two kinds of check, and the split is the point of the test:
//
//   * The implemented numerics are verified against CLOSED-FORM answers, not
//     against another calculation. A radial quadrature that agrees with a
//     second quadrature proves nothing; one that reproduces ∫r²e^{-2r}dr = 1/4
//     to twelve digits is measured. The same for the radial Poisson solve,
//     which is checked against the analytic Hartree potential of a hydrogen
//     1s density, and for the Pulay mixer, which is checked against a linear
//     fixed point whose solution is known exactly.
//
//   * The unimplemented steps are verified to REFUSE. That is not busywork: a
//     scaffold whose stubs return default-constructed results is a scaffold
//     that will one day be wired into a UI and report an energy of zero as
//     though it meant it. Every entry point is asserted to report
//     NotImplemented, and to leave its output untouched.

#include "core/AseScriptGenerator.hpp"
#include "core/Structure.hpp"
#include "dft/CalangoDFTEngine.hpp"
#include "dft/HamiltonianAssembler.hpp"
#include "dft/NAOBasisSet.hpp"
#include "dft/RadialGrid.hpp"
#include "dft/SCFSolver.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace calango::dft;

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

void checkClose(double got, double expected, double tolerance,
                const std::string& what)
{
    const bool ok = std::abs(got - expected) <= tolerance;
    std::printf("  %-4s %s", ok ? "ok" : "FAIL", what.c_str());
    if (!ok)
        std::printf("  (got %.12g, expected %.12g)", got, expected);
    std::printf("\n");
    if (!ok)
        ++failures;
}

} // namespace

int main()
{
    std::printf("Radial mesh and quadrature:\n");
    {
        const RadialGrid grid(801, 40.0, 1.0e-5);
        check(grid.size() == 801, "an odd point count is kept as given");
        check(grid.r().front() == 0.0,
              "the mesh starts EXACTLY at the nucleus, where the cusp is");
        checkClose(grid.outerRadius(), 40.0, 1.0e-9,
                   "and reaches the requested outer radius");
        check(grid.r()[1] < grid.r()[2] - grid.r()[1],
              "spacing grows with r — dense at the core, coarse in the tail");

        const RadialGrid even(800, 40.0, 1.0e-5);
        check(even.size() == 801,
              "an even count is rounded up: Simpson needs an even number of "
              "intervals");

        // ∫₀^∞ r² e^{-2r} dr = 2!/2³ = 1/4. The integrand spans nine orders of
        // magnitude across the mesh, which is what the compensated summation
        // is for.
        std::vector<double> exponential(grid.size());
        for (std::size_t i = 0; i < grid.size(); ++i)
            exponential[i] = std::exp(-2.0 * grid.r()[i]);
        checkClose(grid.integrateSpherical(exponential), 0.25, 1.0e-10,
                   "∫r²e^{-2r}dr = 1/4");

        // ∫₀^∞ r⁴ e^{-r} dr = 4! = 24 — a slower decay, so the OUTER end of
        // the mesh is what is being tested here rather than the inner.
        std::vector<double> slower(grid.size());
        for (std::size_t i = 0; i < grid.size(); ++i) {
            const double r = grid.r()[i];
            slower[i] = r * r * std::exp(-r);
        }
        checkClose(grid.integrateSpherical(slower), 24.0, 1.0e-6,
                   "∫r⁴e^{-r}dr = 24");

        // The hydrogen 1s density, normalised: ∫ρ dV = 1 electron.
        std::vector<double> density(grid.size());
        for (std::size_t i = 0; i < grid.size(); ++i)
            density[i] = std::exp(-2.0 * grid.r()[i]) / M_PI;
        checkClose(4.0 * M_PI * grid.integrateSpherical(density), 1.0, 1.0e-10,
                   "the hydrogen 1s density integrates to one electron");

        check(grid.integrate({1.0, 2.0}) == 0.0,
              "a length mismatch integrates to zero rather than reading out "
              "of bounds");
    }

    std::printf("Interpolation:\n");
    {
        const RadialGrid grid(401, 20.0, 1.0e-4);
        std::vector<double> values(grid.size());
        for (std::size_t i = 0; i < grid.size(); ++i)
            values[i] = std::exp(-grid.r()[i]);
        checkClose(grid.interpolate(values, 1.0), std::exp(-1.0), 1.0e-8,
                   "cubic interpolation reproduces e^{-r} at r = 1");
        checkClose(grid.interpolate(values, 0.001), std::exp(-0.001), 1.0e-8,
                   "and close to the nucleus, where the mesh is finest");
        check(grid.interpolate(values, 25.0) == 0.0,
              "beyond the mesh it is exactly zero — a confined basis function "
              "IS zero there");
    }

    std::printf("Radial Poisson solve:\n");
    {
        // The Hartree potential of the hydrogen 1s density has a closed form:
        //     V(r) = 1/r − e^{-2r}(1 + 1/r)
        // finite at the origin (V(0) = 1) and going as 1/r far away. Checking
        // both limits matters: the small-r behaviour is where the 0/0 in the
        // enclosed-charge term lives, and the large-r behaviour is what says
        // the total charge came out right.
        const RadialGrid grid(1601, 40.0, 1.0e-6);
        std::vector<double> density(grid.size());
        for (std::size_t i = 0; i < grid.size(); ++i)
            density[i] = std::exp(-2.0 * grid.r()[i]) / M_PI;

        const std::vector<double> potential = grid.hartreePotential(density);
        check(potential.size() == grid.size(),
              "the potential is returned on the same mesh");
        check(std::isfinite(potential.front()),
              "and is finite at r = 0, where 1/r is not");
        checkClose(potential.front(), 1.0, 1.0e-5,
                   "V(0) = 1 hartree for the hydrogen 1s density");

        const auto analytic = [](double r) {
            return r > 0.0 ? 1.0 / r - std::exp(-2.0 * r) * (1.0 + 1.0 / r)
                           : 1.0;
        };
        double worst = 0.0;
        for (std::size_t i = 0; i < grid.size(); ++i) {
            const double r = grid.r()[i];
            if (r < 1.0e-3 || r > 20.0)
                continue; // the tail is 1e-18; a relative test there is noise
            const double reference = analytic(r);
            if (std::abs(reference) < 1.0e-12)
                continue;
            worst = std::max(
                worst, std::abs(potential[i] - reference) / std::abs(reference));
        }
        check(worst < 1.0e-6,
              "and matches 1/r − e^{-2r}(1 + 1/r) everywhere between "
              "10^-3 and 20 bohr (worst relative error "
                  + std::to_string(worst) + ")");

        check(grid.hartreePotential({1.0, 2.0}).size() == grid.size(),
              "a malformed density gives a zero potential of the right "
              "length, not a short one");
    }

    std::printf("Density mixing:\n");
    {
        // A linear fixed point with a known answer and a deliberately awful
        // gain: x_out = A x_in + b with A having eigenvalues near -1, which is
        // exactly the charge-sloshing regime that makes plain feedback
        // diverge. Linear mixing crawls; Pulay should not.
        const std::size_t n = 6;
        const auto step = [n](const std::vector<double>& in) {
            std::vector<double> out(n, 0.0);
            for (std::size_t i = 0; i < n; ++i) {
                out[i] = 1.0 + 0.9 * in[(i + 1) % n] - 0.85 * in[i];
            }
            return out;
        };

        const auto iterate = [&](int history, int maxIterations) {
            Parameters parameters;
            parameters.mixingFraction = 0.3;
            parameters.mixingHistory = history;
            DensityMixer mixer(parameters);
            std::vector<double> x(n, 0.0);
            for (int iteration = 0; iteration < maxIterations; ++iteration) {
                const std::vector<double> out = step(x);
                if (DensityMixer::residualNorm(x, out) < 1.0e-10)
                    return iteration;
                x = mixer.mix(x, out);
            }
            return maxIterations;
        };

        const int linear = iterate(0, 5000);
        const int pulay = iterate(8, 5000);
        check(linear < 5000, "linear mixing converges on the test problem");
        check(pulay < 5000, "so does Pulay");
        check(pulay < linear / 2,
              "and Pulay gets there in under half the iterations ("
                  + std::to_string(pulay) + " vs " + std::to_string(linear)
                  + ")");

        Parameters parameters;
        DensityMixer mixer(parameters);
        const std::vector<double> a{1.0, 2.0};
        check(mixer.mix(a, {1.0}).size() == a.size(),
              "a length mismatch returns the input unchanged rather than a "
              "truncated mixture");
        mixer.mix(a, {1.5, 2.5});
        check(mixer.historySize() == 1, "the history records one entry");
        mixer.reset();
        check(mixer.historySize() == 0,
              "and reset clears it — extrapolating across a changed "
              "Hamiltonian is worse than not extrapolating");
    }

    std::printf("Self-consistency loop:\n");
    {
        // The loop is driven by a callback, so its convergence logic can be
        // tested without any of the physics existing.
        struct Context {
            int calls = 0;
        };
        Context context;
        const auto step = [](const std::vector<double>& in,
                             std::vector<double>& out, EnergyBreakdown& energy,
                             void* raw) {
            auto* ctx = static_cast<Context*>(raw);
            ++ctx->calls;
            out.assign(in.size(), 0.0);
            for (std::size_t i = 0; i < in.size(); ++i)
                out[i] = 0.5 * in[i] + 1.0; // fixed point at 2
            double sum = 0.0;
            for (const double v : out)
                sum += v;
            energy.total = -sum;
            return Outcome::success();
        };

        Parameters parameters;
        parameters.maxIterations = 200;
        SCFSolver solver(parameters);
        SCFSolver::Report report =
            solver.run(std::vector<double>(4, 0.0), step, &context);
        check(report.outcome.ok(), "a well-behaved fixed point converges");
        check(report.iterations > 1,
              "and never in one iteration — the first has no previous energy "
              "to compare against");
        check(report.residualHistory.size()
                  == static_cast<std::size_t>(report.iterations),
              "with one residual recorded per iteration, for the plot");
        check(report.residualHistory.back() <= report.residualHistory.front(),
              "and the residual fell");

        // A step that never settles must be reported as NotConverged, not as
        // a success with a bad number. The map below has NO fixed point — it
        // walks away by 1 every call — which is a sharper test than a tight
        // tolerance on a convergent map, because Pulay solves a linear fixed
        // point essentially exactly and would meet almost any tolerance.
        const auto runaway = [](const std::vector<double>& in,
                                std::vector<double>& out,
                                EnergyBreakdown& energy, void* raw) {
            auto* ctx = static_cast<Context*>(raw);
            ++ctx->calls;
            out.assign(in.size(), 0.0);
            for (std::size_t i = 0; i < in.size(); ++i)
                out[i] = in[i] + 1.0;
            energy.total = static_cast<double>(ctx->calls);
            return Outcome::success();
        };
        Parameters impatient;
        impatient.maxIterations = 5;
        SCFSolver strict(impatient);
        Context second;
        SCFSolver::Report unconverged =
            strict.run(std::vector<double>(4, 0.0), runaway, &second);
        check(unconverged.outcome.status == Status::NotConverged,
              "an unreachable tolerance reports NotConverged");
        check(unconverged.iterations == 5, "after exactly the iteration limit");

        SCFSolver::Report noStep =
            solver.run(std::vector<double>(4, 0.0), nullptr, nullptr);
        check(noStep.outcome.status == Status::InvalidInput,
              "a missing step function is rejected");
        SCFSolver::Report noDensity = solver.run({}, step, &context);
        check(noDensity.outcome.status == Status::InvalidInput,
              "and so is an empty initial density");
    }

    std::printf("Basis set:\n");
    {
        NAOBasisSet basis(RadialGrid(401, 20.0, 1.0e-4));
        check(basis.forSpecies(6) == nullptr,
              "an absent species has no basis");

        SpeciesBasis carbon;
        carbon.species = {6, 6.0};
        carbon.tierOffsets = {0};
        carbon.functions.push_back({0, 1, std::vector<double>(401, 0.0), 5.0,
                                    -10.0, "1s"});
        carbon.functions.push_back({0, 2, std::vector<double>(401, 0.0), 5.0,
                                    -0.5, "2s"});
        carbon.functions.push_back({1, 2, std::vector<double>(401, 0.0), 5.0,
                                    -0.2, "2p"});
        check(carbon.functionCount() == 1 + 1 + 3,
              "each radial function carries 2l+1 basis functions — an s is "
              "one, a p is three");
        basis.setSpeciesBasis(carbon);
        check(basis.forSpecies(6) != nullptr, "and can be installed directly");
        check(basis.species() == std::vector<int>{6},
              "the species list reports it");
        check(basis.totalFunctions({{6, 2}}) == 10,
              "two carbons carry twice the functions");
        check(basis.totalFunctions({{6, 2}, {8, 1}}) == 10,
              "a species with no basis contributes nothing to the count");

        // Generation refuses, and the validation in front of it runs first.
        const Outcome empty = basis.generate({}, {}, 1);
        check(empty.status == Status::InvalidInput,
              "generating a basis for no species is rejected");
        Parameters bad;
        bad.confinementRadiusA = -1.0;
        check(basis.generate({{6, 6.0}}, bad, 1).status == Status::InvalidInput,
              "as is a non-positive confinement radius");
        const Outcome real = basis.generate({{6, 6.0}}, {}, 2);
        check(real.status == Status::NotImplemented,
              "and a valid request reports NotImplemented");
        check(!real.message.empty(),
              "saying what generation would involve, rather than failing "
              "silently");
    }

    std::printf("Hamiltonian assembly refuses rather than returning zeros:\n");
    {
        NAOBasisSet basis(RadialGrid(201, 20.0, 1.0e-4));
        SpeciesBasis hydrogen;
        hydrogen.species = {1, 1.0};
        hydrogen.tierOffsets = {0};
        hydrogen.functions.push_back({0, 1, std::vector<double>(201, 0.0), 5.0,
                                      -0.5, "1s"});
        basis.setSpeciesBasis(hydrogen);

        calango::core::Structure h2;
        calango::core::Atom atom;
        atom.atomicNumber = 1;
        atom.position = {0.0, 0.0, 0.0};
        h2.addAtom(atom);
        atom.position = {0.74, 0.0, 0.0};
        h2.addAtom(atom);

        HamiltonianAssembler assembler(basis, {});
        check(assembler.dimension(h2) == 2,
              "two hydrogens in a single-zeta basis give a 2x2 problem");

        calango::core::Structure withCarbon = h2;
        atom.atomicNumber = 6;
        withCarbon.addAtom(atom);
        check(assembler.dimension(withCarbon) == 0,
              "and a species without a basis gives 0, not a matrix that is "
              "quietly too small");

        SymmetricMatrix overlap;
        const Outcome s = assembler.buildOverlap(h2, overlap);
        check(s.status == Status::NotImplemented,
              "the overlap build reports NotImplemented");
        check(overlap.empty(),
              "and leaves the matrix UNTOUCHED — a zero overlap is a singular "
              "eigenproblem, and the failure would surface far from here");

        SymmetricMatrix hamiltonian;
        check(assembler.buildHamiltonian(h2, {1.0, 2.0}, hamiltonian).status
                  == Status::NotImplemented,
              "so does the Hamiltonian build");
        check(hamiltonian.empty(), "leaving that matrix untouched too");
        check(assembler.buildHamiltonian(h2, {}, hamiltonian).status
                  == Status::InvalidInput,
              "and an empty potential is rejected before that");

        std::vector<double> density;
        check(assembler.buildDensity(h2, {1.0}, {2.0}, density).status
                  == Status::NotImplemented,
              "and so does the density build");
        check(density.empty(), "with no density invented");
    }

    std::printf("Engine facade:\n");
    {
        CalangoDFTEngine engine;
        calango::core::Structure empty;
        check(engine.run(empty).outcome.status == Status::InvalidInput,
              "an empty structure is rejected");

        calango::core::Structure water;
        calango::core::Atom atom;
        atom.atomicNumber = 8;
        atom.position = {0.0, 0.0, 0.0};
        water.addAtom(atom);
        atom.atomicNumber = 1;
        atom.position = {0.96, 0.0, 0.0};
        water.addAtom(atom);
        atom.position = {-0.24, 0.93, 0.0};
        water.addAtom(atom);

        const CalangoDFTEngine::Result result = engine.run(water);
        check(result.outcome.status == Status::NotImplemented,
              "and a valid one reports NotImplemented");
        check(result.energy.total == 0.0,
              "with NO energy invented — a scaffold that reports a plausible "
              "number is worse than one that says it cannot");
        check(!result.log.empty(),
              "but it still says what it was asked to do");
        check(result.outcome.message.find("basis") != std::string::npos,
              "and the message enumerates what is missing");

        Parameters broken;
        broken.maxIterations = 0;
        engine.setParameters(broken);
        check(engine.run(water).outcome.status == Status::InvalidInput,
              "parameters are validated before anything else");

        check(!CalangoDFTEngine::unimplementedSteps().empty(),
              "the missing steps are queryable, so a UI cannot drift from "
              "them");
    }

    std::printf("Calculator integration:\n");
    {
        using calango::core::CalculatorKind;
        using calango::core::CalculatorFamily;
        check(calango::core::calculatorFamily(CalculatorKind::CalangoDft)
                  == CalculatorFamily::AbInitio,
              "the built-in engine is classified as ab initio — that "
              "it runs in process says nothing about the family");

        // The one thing that must never happen: a run on this engine reaching
        // the ASE script generator. There is no Python calculator to build, so
        // a generated script would hand ASE an `atoms` with no calculator and
        // fail at get_potential_energy() with a message naming neither cause.
        calango::core::CalculatorConfig config;
        config.calculator = CalculatorKind::CalangoDft;
        const std::string script =
            calango::core::AseScriptGenerator::generate(config,
                                                        "structure.extxyz");
        check(script.find("raise RuntimeError") != std::string::npos,
              "a script generated for it refuses loudly rather than running "
              "without a calculator");
        check(script.find("runs inside the application") != std::string::npos,
              "and says why, so the mis-route is diagnosable from the script");
        check(script.find("atoms.calc =") == std::string::npos
                  || script.find("raise RuntimeError")
                      < script.find("atoms.calc ="),
              "with nothing pretending to be a calculator before it");
    }

    std::printf(failures == 0 ? "\nAll DFT engine checks passed.\n"
                              : "\n%d DFT engine check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
