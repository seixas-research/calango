#include "dft/NAOBasisSet.hpp"

#include "dft/IntegrationGrid.hpp"

#include <algorithm>
#include <cmath>

namespace calango::dft {
namespace {

constexpr double kBohrPerAngstrom = 1.8897261254578281;

/// The fraction of an orbital's norm left OUTSIDE the split radius.
///
/// 0.15 is the long-standing default of the split-valence construction and
/// the tier-3 value halves it, which places the second split further out and
/// gives a function that refines a different part of the orbital. Neither is
/// fitted here; they are the standard values, and the test that matters is
/// that the total energy falls when a tier is added.
constexpr double kSplitNormTier2 = 0.15;
constexpr double kSplitNormTier3 = 0.075;

/// du/dr at a mesh point, by a central difference in the INDEX divided by the
/// analytic Jacobian. The mesh is uniform in the index, so this is a genuine
/// O(h²) derivative rather than a difference of unequally spaced points.
double derivativeAt(const RadialGrid& grid, const std::vector<double>& u,
                    std::size_t i)
{
    if (i == 0 || i + 1 >= u.size())
        return 0.0;
    return 0.5 * (u[i + 1] - u[i - 1]) / grid.drdi()[i];
}

/// Build the second-zeta partner of `source` by the split-valence
/// construction, or return false when the split radius lands somewhere the
/// mesh cannot resolve.
///
/// Inside r_s the orbital is replaced by a smooth polynomial and the new
/// function is the difference, so it is exactly zero for r > r_s:
///
///     u_new(r) = u(r) − r^{l+1}(a − br² + cr⁴)   for r < r_s,   0 beyond.
///
/// THREE parameters, matching value, slope AND CURVATURE at r_s. The obvious
/// two-parameter form r^l(a − br²) matches only value and slope, which makes
/// the function C¹ — and that is not enough. −½∇²φ then has a JUMP at r_s,
/// and while the matrix element ∫φ∇²φ is still perfectly well defined, the
/// multicentre integration grid has a few tens of radial shells and cannot
/// integrate a discontinuity. The error lands on kinetic matrix elements
/// worth tens of hartree, and a variational calculation will find and exploit
/// it: measured on silicon, the two-parameter form left the free atom
/// correctly variational while pushing the crystal 7 eV per atom too low, and
/// the cohesive energy from 7 eV to 14. Matching the curvature as well makes
/// u_new, u_new′ and u_new″ all vanish at r_s, so −½∇²φ goes smoothly to zero
/// and there is nothing left for the quadrature to miss.
///
/// The kinetic energy stays analytic, which is the reason to prefer this
/// construction over anything fitted. For F = r^l(a − br² + cr⁴),
///
///     −½∇²(F·Y_lm) = [ b(2l+3) − 2c(2l+5)r² ] · r^l · Y_lm
///
/// exactly. No numerical second derivative is taken anywhere: the source
/// function's own curvature comes from inverting its stored kinetic array,
///
///     u″ = l(l+1)u/r² − 2·(kinetic array),
///
/// which is the definition of that array rearranged, and therefore works for
/// any function carrying one — including a split of a split.
bool buildSplitFunction(const RadialGrid& grid, const RadialFunction& source,
                        double splitNorm, RadialFunction& result)
{
    const std::size_t n = grid.size();
    if (source.u.size() != n || source.kineticU.size() != n)
        return false;

    // Where does `splitNorm` of the norm lie beyond r? The orbital is
    // normalised, so the enclosed norm is what has to reach 1 − splitNorm.
    std::vector<double> square(n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        square[i] = source.u[i] * source.u[i];
    const std::vector<double> enclosed = grid.cumulative(square);
    const double target = (1.0 - splitNorm) * enclosed.back();
    std::size_t split = 0;
    for (std::size_t i = 1; i < n; ++i) {
        if (grid.r()[i] > source.cutoffBohr)
            break;
        if (enclosed[i] >= target) {
            split = i;
            break;
        }
    }
    // Too close to the nucleus to describe, or effectively the whole orbital:
    // either way there is no useful function to build.
    if (split < 8 || split + 4 >= n || grid.r()[split] <= 1.0e-3)
        return false;

    const double rs = grid.r()[split];
    const double l = source.l;
    const double u = source.u[split];
    const double du = derivativeAt(grid, source.u, split);
    const double ddu = l * (l + 1.0) * u / (rs * rs)
        - 2.0 * source.kineticU[split];

    // R and its first two derivatives at r_s, from u = rR.
    const double bigF = u / rs;
    const double bigG = du / rs - u / (rs * rs);
    const double bigH =
        ddu / rs - 2.0 * du / (rs * rs) + 2.0 * u / (rs * rs * rs);

    // Peel off the r^l prefactor: R = r^l·f, so f and its derivatives at r_s
    // follow from R's by the product rule.
    const double power = std::pow(rs, l);
    const double f0 = bigF / power;
    const double f1 = bigG / power - l * f0 / rs;
    const double f2 = bigH / power - 2.0 * l * f1 / rs
        - l * (l - 1.0) * f0 / (rs * rs);

    // f = a − br² + cr⁴ evaluated at r_s gives three equations in a, b, c.
    const double c = (f2 - f1 / rs) / (8.0 * rs * rs);
    const double b = (12.0 * c * rs * rs - f2) / 2.0;
    const double a = f0 + b * rs * rs - c * rs * rs * rs * rs;

    result = source;
    result.eigenvalue = 0.0;
    result.cutoffBohr = rs;
    result.label = source.label + "+";
    result.u.assign(n, 0.0);
    result.kineticU.assign(n, 0.0);
    for (std::size_t i = 0; i < split; ++i) {
        const double r = grid.r()[i];
        const double rl1 = std::pow(r, l + 1.0);
        result.u[i] = source.u[i] - rl1 * (a - b * r * r
                                           + c * r * r * r * r);
        result.kineticU[i] = source.kineticU[i]
            - rl1 * (b * (2.0 * l + 3.0) - 2.0 * c * (2.0 * l + 5.0) * r * r);
    }

    // Normalise. A split that produced almost nothing — because the orbital
    // was already close to the polynomial inside r_s — is not a basis
    // function, it is noise about to be handed to an eigensolver.
    std::vector<double> newSquare(n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        newSquare[i] = result.u[i] * result.u[i];
    const double norm = grid.integrate(newSquare);
    if (!(norm > 1.0e-8))
        return false;
    const double scale = 1.0 / std::sqrt(norm);
    for (std::size_t i = 0; i < n; ++i) {
        result.u[i] *= scale;
        result.kineticU[i] *= scale;
    }
    return true;
}

/// The kinetic array of a function that IS an eigenfunction: (ε − v_at)·u.
///
/// v_at = v_electronic − Z/r, and the 1/r is put in analytically rather than
/// interpolated. The result is smooth through the origin — it behaves as Z·r^l
/// — so the first mesh point is filled by extrapolation rather than by
/// evaluating 0/0 there.
std::vector<double> eigenfunctionKinetic(const RadialGrid& grid,
                                         const std::vector<double>& electronic,
                                         double z, double eigenvalue,
                                         const std::vector<double>& u)
{
    std::vector<double> kinetic(grid.size(), 0.0);
    for (std::size_t i = 1; i < grid.size(); ++i) {
        const double r = grid.r()[i];
        const double atomic = electronic[i] - z / r;
        kinetic[i] = (eigenvalue - atomic) * u[i];
    }
    if (grid.size() > 2)
        kinetic[0] = 2.0 * kinetic[1] - kinetic[2];
    return kinetic;
}

} // namespace

int SpeciesBasis::functionCount() const
{
    int total = 0;
    for (const RadialFunction& function : functions)
        total += function.functionCount();
    return total;
}

double SpeciesBasis::maxCutoffBohr() const
{
    double largest = 0.0;
    for (const RadialFunction& function : functions)
        largest = std::max(largest, function.cutoffBohr);
    return largest;
}

const SpeciesBasis* NAOBasisSet::forSpecies(int atomicNumber) const
{
    const auto it = byAtomicNumber_.find(atomicNumber);
    return it == byAtomicNumber_.end() ? nullptr : &it->second;
}

void NAOBasisSet::setSpeciesBasis(SpeciesBasis basis)
{
    const int key = basis.species.atomicNumber;
    byAtomicNumber_[key] = std::move(basis);
}

std::vector<int> NAOBasisSet::species() const
{
    std::vector<int> result;
    result.reserve(byAtomicNumber_.size());
    for (const auto& entry : byAtomicNumber_)
        result.push_back(entry.first);
    return result;
}

std::size_t NAOBasisSet::totalFunctions(const std::map<int, int>& counts) const
{
    std::size_t total = 0;
    for (const auto& entry : counts) {
        const SpeciesBasis* basis = forSpecies(entry.first);
        if (basis == nullptr)
            return 0;
        total += static_cast<std::size_t>(basis->functionCount()) * entry.second;
    }
    return total;
}

Outcome NAOBasisSet::generate(const std::vector<Species>& speciesList,
                              const Parameters& parameters, int tiers)
{
    if (speciesList.empty())
        return Outcome::invalid("basis generation: no species requested");
    if (tiers < 1)
        return Outcome::invalid("basis generation: tiers must be at least 1");
    if (tiers > 3)
        return Outcome::notImplemented(
            "basis tiers beyond the third (triple-zeta plus double "
            "polarisation)");
    if (grid_.size() < 64)
        return Outcome::invalid(
            "basis generation: the radial mesh is too coarse");

    const double cutoff = parameters.confinementRadiusA * kBohrPerAngstrom;
    // Clamped rather than rejected: a width wider than the cutoff itself has
    // no onset to start from, and a caller asking for one has made an ordinary
    // units mistake, not a physics choice.
    const double width = std::clamp(
        parameters.confinementWidthA * kBohrPerAngstrom, 0.0, 0.8 * cutoff);
    if (!(cutoff > 1.0))
        return Outcome::invalid(
            "basis generation: the confinement radius is below 1 bohr");
    if (cutoff >= grid_.outerRadius())
        return Outcome::invalid(
            "basis generation: the confinement radius is outside the radial "
            "mesh");

    AtomicSolver solver(grid_, parameters);
    for (const Species& species : speciesList) {
        const int z = species.atomicNumber;
        if (z < 1)
            return Outcome::invalid("basis generation: invalid atomic number");

        // The FREE atom first. Two things come from it that the confined
        // solution cannot give: the density the crystal starts from, and the
        // neutral-atom potential the electrostatics is written as a
        // difference from. Both must be the potential of an atom that is
        // actually neutral out to infinity, which an atom in a box is not.
        const AtomicResult free = solver.solve(z);
        if (!free.outcome.ok())
            return free.outcome;

        // Then the CONFINED atom, whose orbitals are the basis. Solved in a
        // sphere with u(r_cut) = 0, so every function is exactly zero outside
        // it. The confinement raises each eigenvalue — that is the price of
        // locality, and it is why the cutoff is a convergence parameter and
        // not a detail.
        const AtomicResult confined = solver.solveConfiguration(
            z, AtomicSolver::groundStateConfiguration(z), cutoff, width);
        if (!confined.outcome.ok())
            return confined.outcome;

        SpeciesBasis basis;
        basis.species = species;
        if (basis.species.referenceElectrons <= 0.0)
            basis.species.referenceElectrons = z;
        basis.tierOffsets = {0};
        // v_at for the kinetic identity is the FULL potential the orbitals
        // solve: Hartree plus exchange-correlation plus the confining barrier.
        // Leaving the barrier out here is the same bug as using a hard wall —
        // −½∇²φ = (ε − v_at)φ would then be false exactly where the orbital is
        // being bent to zero.
        basis.atomicPotential = confined.electronicPotential;
        for (std::size_t i = 0; i < grid_.size()
             && i < confined.confiningPotential.size();
             ++i)
            basis.atomicPotential[i] += confined.confiningPotential[i];
        basis.freeAtomDensity = free.density;

        // v^NA(r) = −Z/r + v_H[ρ_free](r): the potential of the nucleus plus
        // its own electrons. Neutral, so it decays exponentially instead of
        // as 1/r, and a lattice sum of it converges absolutely.
        const std::vector<double> hartree =
            grid_.hartreePotential(free.density);
        basis.neutralAtomPotential.assign(grid_.size(), 0.0);
        for (std::size_t i = 0; i < grid_.size(); ++i) {
            const double r = grid_.r()[i];
            basis.neutralAtomPotential[i] =
                hartree[i] - (r > 0.0 ? z / r : 0.0);
        }

        // Where the free atom stops mattering. Both the density and the
        // neutral-atom potential are tested, because they decay at different
        // rates — v^NA is the difference of two things that individually go as
        // 1/r, so it reaches its floor later than the density does.
        basis.densityCutoffBohr = grid_.outerRadius();
        for (std::size_t i = grid_.size(); i-- > 1;) {
            if (std::abs(basis.freeAtomDensity[i]) > 1.0e-10
                || std::abs(basis.neutralAtomPotential[i]) > 1.0e-8) {
                basis.densityCutoffBohr =
                    std::min(grid_.r()[i] + 1.0, grid_.outerRadius());
                break;
            }
        }

        // --- Tier 1: the confined atom's occupied orbitals ----------------
        for (const AtomicShell& shell : confined.shells) {
            RadialFunction function;
            function.l = shell.l;
            function.principal = shell.principal;
            function.eigenvalue = shell.eigenvalue;
            function.label = shell.label;
            function.cutoffBohr = cutoff;
            function.u = shell.u;
            // Enforce the confinement rather than trusting it: the solver
            // imposes u(r_cut) = 0 as a boundary condition, but the array
            // beyond it holds whatever the inward integration left there.
            for (std::size_t i = 0; i < grid_.size(); ++i)
                if (grid_.r()[i] > cutoff)
                    function.u[i] = 0.0;
            function.kineticU =
                eigenfunctionKinetic(grid_, basis.atomicPotential, z,
                                     shell.eigenvalue, function.u);
            basis.functions.push_back(std::move(function));
        }
        if (basis.functions.empty())
            return {Status::NumericalFailure,
                    "the confined atom produced no orbitals"};

        // Which shells are VALENCE: the outermost occupied one in each l
        // channel. Only these are split. Splitting a core orbital adds a
        // function that no bond can use and an overlap eigenvalue that the
        // eigensolver then has to discard — cost with no freedom bought.
        std::map<int, std::size_t> valenceOfL;
        for (std::size_t k = 0; k < basis.functions.size(); ++k) {
            const RadialFunction& function = basis.functions[k];
            const auto it = valenceOfL.find(function.l);
            if (it == valenceOfL.end()
                || basis.functions[it->second].principal < function.principal)
                valenceOfL[function.l] = k;
        }
        int highestOccupiedL = 0;
        for (const auto& entry : valenceOfL)
            highestOccupiedL = std::max(highestOccupiedL, entry.first);

        // --- Tier 2: a second zeta per valence channel, plus polarisation --
        if (tiers >= 2) {
            basis.tierOffsets.push_back(basis.functions.size());
            std::vector<RadialFunction> added;
            for (const auto& entry : valenceOfL) {
                RadialFunction split;
                if (buildSplitFunction(grid_, basis.functions[entry.second],
                                       kSplitNormTier2, split))
                    added.push_back(std::move(split));
            }
            // The polarisation shell: the lowest state of the first angular
            // momentum the atom does not occupy, in the CONFINED atom's own
            // potential. Lowest means node-free, so n = l + 1.
            AtomicShell polarisation;
            polarisation.l = highestOccupiedL + 1;
            polarisation.principal = polarisation.l + 1;
            polarisation.label =
                AtomicSolver::shellLabel(polarisation.principal, polarisation.l);
            const Outcome outcome = solver.solveOrbital(
                z, basis.atomicPotential, cutoff, polarisation);
            if (outcome.ok()) {
                RadialFunction function;
                function.l = polarisation.l;
                function.principal = polarisation.principal;
                function.eigenvalue = polarisation.eigenvalue;
                function.label = polarisation.label;
                function.cutoffBohr = cutoff;
                function.u = polarisation.u;
                for (std::size_t i = 0; i < grid_.size(); ++i)
                    if (grid_.r()[i] > cutoff)
                        function.u[i] = 0.0;
                function.kineticU =
                    eigenfunctionKinetic(grid_, basis.atomicPotential, z,
                                         polarisation.eigenvalue, function.u);
                added.push_back(std::move(function));
            }
            for (RadialFunction& function : added)
                basis.functions.push_back(std::move(function));
        }

        // --- Tier 3: a third zeta, and a second one on the polarisation ----
        if (tiers >= 3) {
            const std::size_t tier2Begin = basis.tierOffsets.back();
            basis.tierOffsets.push_back(basis.functions.size());
            std::vector<RadialFunction> added;
            for (const auto& entry : valenceOfL) {
                RadialFunction split;
                if (buildSplitFunction(grid_, basis.functions[entry.second],
                                       kSplitNormTier3, split)) {
                    split.label += "+";
                    added.push_back(std::move(split));
                }
            }
            // Split whatever tier 2 added at the polarisation momentum. Found
            // by angular momentum rather than by position, so this still does
            // the right thing if the polarisation solve failed and the tier is
            // one function shorter than expected.
            for (std::size_t k = tier2Begin; k < basis.tierOffsets.back(); ++k) {
                if (basis.functions[k].l != highestOccupiedL + 1)
                    continue;
                RadialFunction split;
                if (buildSplitFunction(grid_, basis.functions[k],
                                       kSplitNormTier2, split))
                    added.push_back(std::move(split));
            }
            for (RadialFunction& function : added)
                basis.functions.push_back(std::move(function));
        }

        // du/dr for every function, once. A FIVE-POINT stencil in the mesh
        // index divided by the analytic Jacobian: the mesh is uniform in the
        // index, so this is a genuine O(h⁴) derivative.
        //
        // The extra order is not gratuitous. A split-valence function is
        // already the difference of two similar quantities, so its derivative
        // loses digits to cancellation before any stencil error is added; with
        // the three-point form ∇φ came out about a percent off for exactly
        // those functions and a few parts in ten thousand for the rest.
        for (RadialFunction& function : basis.functions) {
            function.du.assign(grid_.size(), 0.0);
            const std::size_t n = grid_.size();
            for (std::size_t i = 2; i + 2 < n; ++i)
                function.du[i] = (-function.u[i + 2] + 8.0 * function.u[i + 1]
                                  - 8.0 * function.u[i - 1] + function.u[i - 2])
                    / (12.0 * grid_.drdi()[i]);
            if (n > 4) {
                for (std::size_t i : {std::size_t{1}, n - 2})
                    function.du[i] =
                        0.5 * (function.u[i + 1] - function.u[i - 1])
                        / grid_.drdi()[i];
                function.du[0] = 2.0 * function.du[1] - function.du[2];
            }
        }

        setSpeciesBasis(std::move(basis));
    }
    return Outcome::success();
}

std::vector<BasisFunctionIndex> NAOBasisSet::enumerate(
    const std::vector<int>& atomicNumbers) const
{
    std::vector<BasisFunctionIndex> functions;
    for (std::size_t atom = 0; atom < atomicNumbers.size(); ++atom) {
        const SpeciesBasis* basis = forSpecies(atomicNumbers[atom]);
        if (basis == nullptr)
            return {};
        for (std::size_t k = 0; k < basis->functions.size(); ++k) {
            const RadialFunction& function = basis->functions[k];
            for (int m = -function.l; m <= function.l; ++m)
                functions.push_back({atom, atomicNumbers[atom], k, function.l,
                                     m});
        }
    }
    return functions;
}

void NAOBasisSet::evaluate(
    const std::vector<BasisFunctionIndex>& functions,
    const std::vector<std::array<double, 3>>& displacements,
    std::vector<double>& values, std::vector<double>& kineticValues) const
{
    values.assign(functions.size(), 0.0);
    kineticValues.assign(functions.size(), 0.0);
    if (displacements.size() != functions.size())
        return;
    std::vector<double> harmonics;
    int lastLMax = -1;
    std::array<double, 3> lastDisplacement{{1.0e300, 0.0, 0.0}};
    for (std::size_t i = 0; i < functions.size(); ++i) {
        const BasisFunctionIndex& index = functions[i];
        const SpeciesBasis* basis = forSpecies(index.atomicNumber);
        if (basis == nullptr || index.radial >= basis->functions.size())
            continue;
        const RadialFunction& function = basis->functions[index.radial];
        const std::array<double, 3>& d = displacements[i];
        const double radius =
            std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (radius >= function.cutoffBohr)
            continue; // exactly zero outside the confinement sphere
        // Harmonics are recomputed only when the direction changes. The
        // enumeration is atom-major and every function on one atom is
        // evaluated at the same displacement, so this collapses (2l+1)·n_l
        // evaluations into one per atom per point.
        if (function.l > lastLMax || d[0] != lastDisplacement[0]
            || d[1] != lastDisplacement[1] || d[2] != lastDisplacement[2]) {
            lastLMax = std::max(function.l, 4);
            lastDisplacement = d;
            AngularGrid::realSphericalHarmonics(d[0], d[1], d[2], lastLMax,
                                                harmonics);
        }
        if (radius <= 1.0e-12)
            continue;
        // R(r) = u(r)/r, and likewise the kinetic array carries the r. Both
        // are interpolated as u-like quantities and divided once, which keeps
        // the r^l behaviour at the origin inside the smoother function.
        const auto harmonic =
            static_cast<std::size_t>(index.l * index.l + index.l + index.m);
        if (harmonic >= harmonics.size())
            continue;
        const double angular = harmonics[harmonic];
        values[i] = grid_.interpolate(function.u, radius) / radius * angular;
        kineticValues[i] =
            grid_.interpolate(function.kineticU, radius) / radius * angular;
    }
}

void NAOBasisSet::evaluateWithGradients(
    const std::vector<BasisFunctionIndex>& functions,
    const std::vector<std::array<double, 3>>& displacements,
    std::vector<double>& values, std::vector<double>& kineticValues,
    std::vector<double>& gradients) const
{
    values.assign(functions.size(), 0.0);
    kineticValues.assign(functions.size(), 0.0);
    gradients.assign(functions.size() * 3, 0.0);
    if (displacements.size() != functions.size())
        return;

    std::vector<double> solid;
    std::vector<double> solidGradients;
    std::array<double, 3> lastDisplacement{{1.0e300, 0.0, 0.0}};
    bool haveHarmonics = false;
    for (std::size_t i = 0; i < functions.size(); ++i) {
        const BasisFunctionIndex& index = functions[i];
        const SpeciesBasis* basis = forSpecies(index.atomicNumber);
        if (basis == nullptr || index.radial >= basis->functions.size())
            continue;
        const RadialFunction& function = basis->functions[index.radial];
        const std::array<double, 3>& d = displacements[i];
        const double radius =
            std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (radius >= function.cutoffBohr || radius <= 1.0e-12)
            continue;
        if (!haveHarmonics || d[0] != lastDisplacement[0]
            || d[1] != lastDisplacement[1] || d[2] != lastDisplacement[2]) {
            lastDisplacement = d;
            haveHarmonics = true;
            AngularGrid::realSolidHarmonics(d[0], d[1], d[2], 4, solid,
                                            solidGradients);
        }
        const auto harmonic =
            static_cast<std::size_t>(index.l * index.l + index.l + index.m);
        if (harmonic * 3 + 2 >= solidGradients.size())
            continue;

        const double u = grid_.interpolate(function.u, radius);
        const double du = grid_.interpolate(function.du, radius);
        const double r = radius;
        const double bigR = u / r;                 // R(r)
        const double dR = du / r - u / (r * r);    // R'(r)
        const double power = std::pow(r, static_cast<double>(index.l));

        // φ = R·Y = (R/r^l)·S. Value from the solid harmonic divided back by
        // r^l, so exactly one convention is in play.
        values[i] = bigR * solid[harmonic] / power;
        kineticValues[i] =
            grid_.interpolate(function.kineticU, radius) / r
            * (solid[harmonic] / power);

        // ∇φ = (R′ − ℓR/r)·(r̂)·Y + (R/r^ℓ)·∇S.
        const double radialPart = (dR - index.l * bigR / r)
            * (solid[harmonic] / power);
        for (int c = 0; c < 3; ++c)
            gradients[i * 3 + static_cast<std::size_t>(c)] =
                radialPart * d[static_cast<std::size_t>(c)] / r
                + bigR / power
                    * solidGradients[harmonic * 3 + static_cast<std::size_t>(c)];
    }
}

} // namespace calango::dft
