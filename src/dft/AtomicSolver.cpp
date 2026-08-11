#include "dft/AtomicSolver.hpp"

#include "dft/SCFSolver.hpp"
#include "dft/XcFunctional.hpp"

#include <algorithm>
#include <cmath>

namespace calango::dft {
namespace {

constexpr double kFourPi = 12.566370614359172;

/// State carried through the radial integration: u and its derivative with
/// respect to r. Integrating the pair rather than applying Numerov to u alone
/// costs one extra array and buys the derivative at the matching point for
/// free, which is what the join needs.
struct RadialState {
    double u = 0.0;
    double p = 0.0; ///< du/dr
};

/// The radial equation's coefficient,
///     F(r) = l(l+1)/r² + 2( v_el(r) − Z/r − ε ),
/// evaluated at a fractional mesh index. Both singular pieces are analytic
/// here — the centrifugal barrier and the nuclear attraction — so the only
/// interpolated quantity is the smooth electronic potential.
struct RadialEquation {
    const RadialGrid* grid = nullptr;
    const std::vector<double>* electronicPotential = nullptr;
    double z = 0.0;
    int l = 0;
    double energy = 0.0;

    double f(double index) const
    {
        const double r = grid->radiusAt(index);
        if (r <= 0.0)
            return 0.0;
        const double v = grid->interpolateIndex(*electronicPotential, index);
        const double centrifugal =
            l > 0 ? static_cast<double>(l * (l + 1)) / (r * r) : 0.0;
        return centrifugal + 2.0 * (v - z / r - energy);
    }
};

/// One classical Runge-Kutta step of the pair (u, du/dr) in the mesh index.
///
/// The step is taken in `i`, where the mesh is uniform and h = 1, and the
/// Jacobian dr/di appears in the right-hand side. That is the whole reason
/// for the logarithmic mesh: a fixed-step integrator with a fourth-order
/// error term is fourth-order EVERYWHERE, from the 10⁻⁸ bohr spacing at the
/// nucleus to the 0.3 bohr spacing in the tail, without a single adaptive
/// decision.
RadialState rungeKuttaStep(const RadialEquation& equation, double index,
                           double direction, const RadialState& state)
{
    const auto derivative = [&equation](double at, const RadialState& s) {
        const double jacobian = equation.grid->jacobianAt(at);
        return RadialState{jacobian * s.p, jacobian * equation.f(at) * s.u};
    };
    const double h = direction;
    const RadialState k1 = derivative(index, state);
    const RadialState k2 = derivative(index + 0.5 * h,
                                      {state.u + 0.5 * h * k1.u,
                                       state.p + 0.5 * h * k1.p});
    const RadialState k3 = derivative(index + 0.5 * h,
                                      {state.u + 0.5 * h * k2.u,
                                       state.p + 0.5 * h * k2.p});
    const RadialState k4 =
        derivative(index + h, {state.u + h * k3.u, state.p + h * k3.p});
    return {state.u + h / 6.0 * (k1.u + 2.0 * k2.u + 2.0 * k3.u + k4.u),
            state.p + h / 6.0 * (k1.p + 2.0 * k2.p + 2.0 * k3.p + k4.p)};
}

/// Integrate outward from the nucleus to `lastIndex`, counting nodes.
///
/// Returns the node count; `u` holds the (unnormalised, repeatedly rescaled)
/// solution. Rescaling is not optional: a 1s solution of a Z = 14 atom spans
/// forty orders of magnitude between the mesh's first point and its last, and
/// without it the tail is an overflow rather than a number.
int integrateOutward(const RadialEquation& equation, std::size_t lastIndex,
                     std::vector<double>& u, std::vector<double>& p)
{
    const RadialGrid& grid = *equation.grid;
    const std::size_t n = grid.size();
    u.assign(n, 0.0);
    p.assign(n, 0.0);
    const int l = equation.l;

    // Series start at the first mesh point off the nucleus:
    //     u ≈ r^{l+1} (1 − Z r/(l+1)),
    // which is the exact leading behaviour of the regular solution. Starting
    // from u(0) = 0 alone would leave the amplitude undetermined; starting
    // from the series fixes it and gets the cusp right at the same time.
    const double r1 = grid.r()[1];
    const double correction = 1.0 - equation.z * r1 / (l + 1.0);
    u[1] = std::pow(r1, l + 1.0) * correction;
    p[1] = (l + 1.0) * std::pow(r1, static_cast<double>(l)) * correction;
    if (u[1] == 0.0) {
        // Underflow for a high l on a very fine mesh: rescale the start so the
        // integration carries meaningful digits.
        u[1] = 1.0e-30;
        p[1] = (l + 1.0) / r1 * u[1];
    }

    int nodes = 0;
    RadialState state{u[1], p[1]};
    for (std::size_t i = 1; i < lastIndex; ++i) {
        state = rungeKuttaStep(equation, static_cast<double>(i), 1.0, state);
        u[i + 1] = state.u;
        p[i + 1] = state.p;
        if (u[i] != 0.0 && u[i + 1] * u[i] < 0.0)
            ++nodes;
        const double magnitude = std::abs(state.u);
        if (magnitude > 1.0e60) {
            const double scale = 1.0e-60;
            for (std::size_t k = 0; k <= i + 1; ++k) {
                u[k] *= scale;
                p[k] *= scale;
            }
            state.u *= scale;
            state.p *= scale;
        }
    }
    return nodes;
}

/// Integrate inward from `startIndex` down to `stopIndex`, starting from the
/// boundary condition (u₀, p₀).
///
/// The two boundary conditions this is called with are the two the problem
/// has: the decaying exponential of a free atom, and u = 0 at the wall of a
/// confined one. Both need the inward leg for the same reason — beyond the
/// classical turning point the outward solution is the GROWING one, and at
/// the eigenvalue its physical coefficient is zero, so what outward
/// integration actually carries out there is rounding error amplified by
/// e^{κΔr}. In a 6 bohr box that is a factor of 10²⁹ of pure noise.
void integrateInward(const RadialEquation& equation, std::size_t startIndex,
                     std::size_t stopIndex, double u0, double p0,
                     std::vector<double>& u, std::vector<double>& p)
{
    const RadialGrid& grid = *equation.grid;
    u.assign(grid.size(), 0.0);
    p.assign(grid.size(), 0.0);
    // Amplitude is arbitrary — the join rescales it — so the start carries the
    // shape of the solution, not its size.
    u[startIndex] = u0;
    p[startIndex] = p0;
    RadialState state{u[startIndex], p[startIndex]};
    for (std::size_t i = startIndex; i > stopIndex; --i) {
        state = rungeKuttaStep(equation, static_cast<double>(i), -1.0, state);
        u[i - 1] = state.u;
        p[i - 1] = state.p;
        if (std::abs(state.u) > 1.0e60) {
            const double scale = 1.0e-60;
            for (std::size_t k = i - 1; k <= startIndex; ++k) {
                u[k] *= scale;
                p[k] *= scale;
            }
            state.u *= scale;
            state.p *= scale;
        }
    }
}

/// Outermost classical turning point: the largest index where F changes sign.
/// Beyond it the solution is exponential and outward integration is dominated
/// by the growing branch, which is why the wavefunction is joined here.
std::size_t turningPoint(const RadialEquation& equation, std::size_t lastIndex)
{
    for (std::size_t i = lastIndex; i > 1; --i) {
        if (equation.f(static_cast<double>(i)) < 0.0)
            return i;
    }
    return lastIndex / 2;
}

} // namespace

AtomicSolver::AtomicSolver(RadialGrid grid, Parameters parameters)
    : grid_(std::move(grid))
    , parameters_(std::move(parameters))
{
}

std::string AtomicSolver::shellLabel(int principal, int l)
{
    static const char* kOrbitals = "spdfghi";
    const char letter =
        (l >= 0 && l < 7) ? kOrbitals[l] : '?';
    return std::to_string(principal) + std::string(1, letter);
}

std::vector<AtomicShell> AtomicSolver::groundStateConfiguration(int z)
{
    // Madelung order: increasing n + l, and increasing n within a group. It
    // reproduces the periodic table for every element this engine claims,
    // including the 4s-before-3d inversion; the handful of transition metals
    // whose true configuration violates it (Cr, Cu, …) differ by moving one
    // electron between two shells that are degenerate to within the accuracy
    // of a spherical LDA atom anyway.
    struct Shell {
        int n;
        int l;
    };
    std::vector<Shell> order;
    for (int sum = 1; sum <= 8; ++sum) {
        for (int l = 0; l <= 6; ++l) {
            const int n = sum - l;
            if (n >= l + 1 && n <= 8)
                order.push_back({n, l});
        }
    }
    std::stable_sort(order.begin(), order.end(),
                     [](const Shell& a, const Shell& b) {
                         if (a.n + a.l != b.n + b.l)
                             return a.n + a.l < b.n + b.l;
                         return a.n < b.n;
                     });

    std::vector<AtomicShell> shells;
    int remaining = z;
    for (const Shell& shell : order) {
        if (remaining <= 0)
            break;
        const int capacity = 2 * (2 * shell.l + 1);
        AtomicShell entry;
        entry.principal = shell.n;
        entry.l = shell.l;
        entry.occupation = std::min(remaining, capacity);
        entry.label = shellLabel(shell.n, shell.l);
        remaining -= static_cast<int>(entry.occupation);
        shells.push_back(std::move(entry));
    }
    return shells;
}

AtomicResult AtomicSolver::solve(int z) const
{
    return solveConfiguration(z, groundStateConfiguration(z), 0.0, 0.0);
}

std::vector<double> AtomicSolver::confiningPotential(double radiusBohr,
                                                     double widthBohr) const
{
    std::vector<double> potential(grid_.size(), 0.0);
    if (!(radiusBohr > 0.0) || !(widthBohr > 0.0))
        return potential; // a hard wall has no barrier to tabulate
    const double onset = radiusBohr - widthBohr;
    if (!(onset > 0.0))
        return potential;

    // Junquera, Paz, Sánchez-Portal and Artacho, Phys. Rev. B 64, 235111
    // (2001): zero inside the onset, divergent at the cutoff, and every
    // derivative continuous where it switches on — which is the property that
    // matters here, because this potential is what the stored kinetic array
    // is built from.
    //
    //     v(r) = V₀ · exp[ −(r_c − r_i)/(r − r_i) ] / (r_c − r)
    //
    // CAPPED, and the cap is not cosmetic. The radial integrator takes a fixed
    // step in the mesh index, so the local step in r near the cutoff is a few
    // hundredths of a bohr; an uncapped barrier makes √(2v)·Δr large enough
    // that Runge-Kutta leaves its stability region and the eigenvalue search
    // stops converging. A hundred hartree already damps a valence orbital by
    // e^{−14} per bohr, which is nothing left to miss.
    constexpr double kScaleHartree = 20.0;
    constexpr double kCapHartree = 100.0;
    for (std::size_t i = 0; i < grid_.size(); ++i) {
        const double r = grid_.r()[i];
        if (r <= onset)
            continue;
        if (r >= radiusBohr) {
            potential[i] = kCapHartree;
            continue;
        }
        const double value = kScaleHartree
            * std::exp(-widthBohr / (r - onset)) / (radiusBohr - r);
        potential[i] = std::min(value, kCapHartree);
    }
    return potential;
}

Outcome AtomicSolver::solveOrbital(double z,
                                   const std::vector<double>& electronicPotential,
                                   double confinementRadiusBohr,
                                   AtomicShell& shell) const
{
    const std::size_t n = grid_.size();
    if (n < 16 || electronicPotential.size() != n)
        return Outcome::invalid(
            "solveOrbital: the potential does not match the radial mesh");
    if (shell.l < 0 || shell.principal <= shell.l)
        return Outcome::invalid("solveOrbital: n must exceed l");

    const auto& r = grid_.r();
    std::size_t lastIndex = n - 1;
    if (confinementRadiusBohr > 0.0) {
        const auto it =
            std::lower_bound(r.begin(), r.end(), confinementRadiusBohr);
        if (it == r.end() || it - r.begin() < 8)
            return Outcome::invalid(
                "the confinement radius is outside the radial mesh");
        lastIndex = static_cast<std::size_t>(it - r.begin());
    }

    RadialEquation equation;
    equation.grid = &grid_;
    equation.electronicPotential = &electronicPotential;
    equation.z = z;
    equation.l = shell.l;
    const int targetNodes = shell.principal - shell.l - 1;

    std::vector<double> uOut;
    std::vector<double> pOut;
    std::vector<double> uIn;
    std::vector<double> pIn;

    // Bracket, then bisect on the NODE COUNT. The count is a non-decreasing
    // step function of ε that steps by one at each eigenvalue, so this cannot
    // land on a neighbouring state however close the two are — which a
    // matching-condition root find, with its non-monotone defect, certainly
    // can.
    double low = -2.0 * z * z - 1.0;
    double high = 0.0;
    {
        equation.energy = high;
        int nodes = integrateOutward(equation, lastIndex, uOut, pOut);
        // A confined atom's higher states are pushed above zero by the wall,
        // so the upper bracket has to be searched for rather than assumed.
        double probe = 1.0;
        while (nodes <= targetNodes && probe < 1.0e6) {
            high = probe;
            equation.energy = high;
            nodes = integrateOutward(equation, lastIndex, uOut, pOut);
            probe *= 4.0;
        }
        if (nodes <= targetNodes)
            return {Status::NumericalFailure,
                    "no state with " + std::to_string(targetNodes)
                        + " nodes exists for " + shell.label
                        + " in this potential"};
    }
    for (int step = 0; step < 200; ++step) {
        const double middle = 0.5 * (low + high);
        if (high - low <= 1.0e-13 * (std::abs(middle) + 1.0))
            break;
        equation.energy = middle;
        const int nodes = integrateOutward(equation, lastIndex, uOut, pOut);
        if (nodes <= targetNodes)
            low = middle;
        else
            high = middle;
    }
    shell.eigenvalue = 0.5 * (low + high);
    equation.energy = shell.eigenvalue;

    // --- The wavefunction itself ------------------------------------------
    // Rebuilt by joining an outward solution to an inward one at the classical
    // turning point. Outward integration alone is fine for counting nodes but
    // useless as a wavefunction beyond that point: there the growing solution
    // dominates, and at the eigenvalue its physical coefficient is zero, so
    // what it actually carries is rounding error amplified by e^{κΔr}.
    const std::size_t match =
        std::min(std::max(turningPoint(equation, lastIndex), std::size_t{4}),
                 lastIndex - 2);
    // Outward only as far as the join. Running it to the end of the mesh and
    // keeping the result was the first version's bug: the rescaling that stops
    // the divergent tail overflowing multiplies the CORE down with it, and
    // after five rescales a 1s orbital is a row of denormals whose norm
    // underflows to zero.
    integrateOutward(equation, match, uOut, pOut);
    const double kappa = std::sqrt(std::max(-2.0 * shell.eigenvalue, 1.0e-8));
    std::size_t start = lastIndex;
    double startU = 0.0;
    double startP = -1.0e-20; // hard wall: u(r_cut) = 0
    if (confinementRadiusBohr <= 0.0) {
        // Free atom: start where the state has decayed by e^{-40}. Any further
        // out is an exponential of a number the tail cannot hold, and
        // contributes nothing to the density.
        const double reach = r[match] + 40.0 / kappa;
        while (start > match + 2 && r[start] > reach)
            --start;
        startU = 1.0e-20;
        startP = -kappa * startU;
    }
    integrateInward(equation, start, match, startU, startP, uIn, pIn);

    std::vector<double> u(n, 0.0);
    for (std::size_t i = 0; i <= match; ++i)
        u[i] = uOut[i];
    if (uIn[match] != 0.0) {
        const double scale = uOut[match] / uIn[match];
        for (std::size_t i = match; i <= start; ++i)
            u[i] = uIn[i] * scale;
    }

    // ∫u²dr = 1. The r² of the volume element is already inside u² because
    // u = rR, which is the reason for using it.
    std::vector<double> square(n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        square[i] = u[i] * u[i];
    const double norm = grid_.integrate(square);
    if (!(norm > 0.0))
        return {Status::NumericalFailure,
                "the radial solution for " + shell.label + " has zero norm"};
    // Sign convention: positive near the origin, so two runs of the same
    // calculation produce the same coefficients rather than the same density.
    const double sign = u[std::min(match, n - 1)] < 0.0 ? -1.0 : 1.0;
    const double scale = sign / std::sqrt(norm);
    for (std::size_t i = 0; i < n; ++i)
        u[i] *= scale;
    shell.u = std::move(u);
    return Outcome::success();
}

AtomicResult AtomicSolver::solveConfiguration(
    int z, std::vector<AtomicShell> occupations,
    double confinementRadiusBohr, double confinementWidthBohr) const
{
    AtomicResult result;
    const std::size_t n = grid_.size();
    if (z < 1 || n < 16) {
        result.outcome =
            Outcome::invalid("atomic solver: atomic number or mesh is invalid");
        return result;
    }
    // The radial solver is LDA-only, and a GGA request is served with
    // LDA-PW92 rather than refused. That is not a silent substitution of one
    // answer for another: what comes out of here is the BASIS and the
    // free-atom reference density, not an energy. Generating numerical
    // orbitals from an LDA atom and then running the crystal with a gradient
    // functional is ordinary practice — the basis is a variational space, and
    // the self-consistency in the functional actually requested is what
    // determines the result. The engine says so in its log.
    const XcFunctional radialXc = Lda::supports(parameters_.xc)
        ? parameters_.xc
        : XcFunctional::LdaPw;

    // Confinement is what turns the free atom's exponential tails into the
    // strictly finite-range functions a local basis needs, and exactly zero is
    // what makes the overlap sparse — an "almost zero" tail leaves every pair
    // of atoms in the crystal weakly coupled and the sparsity notional.
    //
    // It is applied as a POTENTIAL added to the one the orbitals are solved
    // in, not as a wall at the boundary. See Parameters::confinementWidthA:
    // a wall leaves u′(r_cut) ≠ 0, which puts a delta function in ∇²φ that the
    // stored kinetic array cannot represent, and the resulting error hides
    // entirely in inter-atomic matrix elements.
    const auto& r = grid_.r();
    const std::vector<double> confining =
        confiningPotential(confinementRadiusBohr, confinementWidthBohr);
    std::vector<double> confinedPotential(n, 0.0);
    if (confinementRadiusBohr > 0.0) {
        const auto it =
            std::lower_bound(r.begin(), r.end(), confinementRadiusBohr);
        if (it == r.end() || it - r.begin() < 8) {
            result.outcome = Outcome::invalid(
                "the confinement radius is outside the radial mesh");
            return result;
        }
    }

    const double zReal = z;

    // Thomas-Fermi (Moliere) screening as the starting potential. Starting
    // from a bare nucleus instead costs a dozen iterations and can converge to
    // the wrong occupation ordering on the way; starting from something with
    // roughly the right screening length does not.
    std::vector<double> electronic(n, 0.0);
    {
        const double scale = 0.8853 / std::cbrt(zReal);
        for (std::size_t i = 0; i < n; ++i) {
            const double x = r[i] / scale;
            const double phi = 0.10 * std::exp(-6.0 * x)
                + 0.55 * std::exp(-1.2 * x) + 0.35 * std::exp(-0.3 * x);
            // v_el = −Z/r (φ − 1) = (Z/r)(1 − φ), finite at the origin because
            // 1 − φ vanishes linearly there.
            electronic[i] =
                r[i] > 0.0 ? zReal * (1.0 - phi) / r[i] : zReal * 1.365 / scale;
        }
    }

    DensityMixer mixer(parameters_);
    std::vector<double> density(n, 0.0);
    std::vector<double> previousDensity(n, 0.0);
    double previousEnergy = 0.0;
    std::vector<double> uOut;
    std::vector<double> pOut;
    std::vector<double> uIn;
    std::vector<double> pIn;

    // Quadrature weights for the density residual: the mixer's convergence
    // measure has to be ∫|Δρ|dV, an electron count, and not a sum over mesh
    // points, which would be dominated by the thousand points inside the core.
    std::vector<double> volumeWeights(n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        volumeWeights[i] = kFourPi * grid_.weights()[i] * r[i] * r[i];

    bool converged = false;
    for (int iteration = 1; iteration <= parameters_.maxIterations;
         ++iteration) {
        result.iterations = iteration;

        // --- Eigenstates in the current potential -------------------------
        // The barrier goes in HERE, alongside the Hartree and
        // exchange-correlation terms, so the orbitals are eigenstates of a
        // potential that is finite everywhere and the identity the kinetic
        // array rests on holds at every radius.
        for (std::size_t i = 0; i < n; ++i)
            confinedPotential[i] = electronic[i] + confining[i];
        double bandStructure = 0.0;
        for (AtomicShell& shell : occupations) {
            const Outcome outcome = solveOrbital(zReal, confinedPotential,
                                                 confinementRadiusBohr, shell);
            if (!outcome.ok()) {
                result.outcome = outcome;
                return result;
            }
            bandStructure += shell.occupation * shell.eigenvalue;
        }

        // --- Density from the occupied orbitals ---------------------------
        std::vector<double> newDensity(n, 0.0);
        for (const AtomicShell& shell : occupations) {
            if (shell.occupation <= 0.0)
                continue;
            for (std::size_t i = 1; i < n; ++i)
                newDensity[i] += shell.occupation * shell.u[i] * shell.u[i]
                    / (kFourPi * r[i] * r[i]);
        }
        // ρ(0) is finite — only l = 0 survives there, and u ~ r for it — but
        // the expression above is 0/0 at the first mesh point. Extrapolated
        // linearly in r from the two next points, which is exact to the order
        // the density is smooth in.
        newDensity[0] = newDensity[1]
            + (newDensity[1] - newDensity[2]) * (r[1] - r[0]) / (r[2] - r[1]);
        newDensity[0] = std::max(newDensity[0], 0.0);

        // --- Potential and energy from that density -----------------------
        const std::vector<double> hartree = grid_.hartreePotential(newDensity);
        std::vector<double> xcPotential;
        const double xcEnergy =
            Lda::evaluateGrid(newDensity, volumeWeights, radialXc,
                              xcPotential);
        std::vector<double> newElectronic(n, 0.0);
        for (std::size_t i = 0; i < n; ++i)
            newElectronic[i] = hartree[i] + xcPotential[i];

        double hartreeEnergy = 0.0;
        double externalEnergy = 0.0;
        double potentialTrace = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            hartreeEnergy += 0.5 * volumeWeights[i] * newDensity[i] * hartree[i];
            if (r[i] > 0.0)
                externalEnergy -=
                    volumeWeights[i] * newDensity[i] * zReal / r[i];
            // ∫ρ v_eff with the potential the ORBITALS were found in — the
            // input potential, not the output one. Getting this wrong is the
            // classic way to produce a total energy that converges to a
            // slightly wrong value while every other diagnostic looks healthy.
            potentialTrace += volumeWeights[i] * newDensity[i]
                * (electronic[i] - (r[i] > 0.0 ? zReal / r[i] : 0.0));
        }
        const double kinetic = bandStructure - potentialTrace;
        const double total =
            kinetic + externalEnergy + hartreeEnergy + xcEnergy;

        const double residual = DensityMixer::residualNorm(
            previousDensity, newDensity, volumeWeights);
        const double energyChange = std::abs(total - previousEnergy);

        result.shells = occupations;
        result.density = newDensity;
        result.electronicPotential = newElectronic;
        result.confiningPotential = confining;
        result.totalEnergy = total;
        result.kinetic = kinetic;
        result.externalEnergy = externalEnergy;
        result.hartreeEnergy = hartreeEnergy;
        result.xcEnergy = xcEnergy;
        result.bandStructure = bandStructure;
        result.finalResidual = residual;

        // Both tests, from the second iteration on. The energy is variational
        // and therefore second-order in the density error, so it settles while
        // the density is still moving — a loop that stops on energy alone
        // stops early and reports a density nobody converged.
        if (iteration > 1 && residual < parameters_.densityToleranceElectrons
            && energyChange < parameters_.energyToleranceEv / 27.211386245988) {
            converged = true;
            break;
        }

        previousDensity = newDensity;
        previousEnergy = total;
        electronic = mixer.mix(electronic, newElectronic);
        density = newDensity;
    }

    result.outcome = converged
        ? Outcome::success()
        : Outcome{Status::NotConverged,
                  "the atomic SCF reached " + std::to_string(result.iterations)
                      + " iterations with a density residual of "
                      + std::to_string(result.finalResidual) + " electrons"};
    return result;
}

} // namespace calango::dft
