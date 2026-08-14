#include "dft/HamiltonianAssembler.hpp"
#include "dft/Constants.hpp"

#include "dft/XcFunctional.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

namespace calango::dft {
namespace {

/// Highest angular momentum kept in the multipole expansion of the difference
/// density. l = 4 is well past where the bonding rearrangement of a
/// tetrahedral or octahedral site carries weight, and the cost is linear in
/// (l+1)².
constexpr int kMultipoleL = 4;

double distance(const std::array<double, 3>& a, const std::array<double, 3>& b)
{
    const double dx = a[0] - b[0];
    const double dy = a[1] - b[1];
    const double dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

void SymmetricMatrix::resize(std::size_t n)
{
    dimension = n;
    values.assign(n * n, 0.0);
}

double SymmetricMatrix::at(std::size_t i, std::size_t j) const
{
    if (i >= dimension || j >= dimension)
        return 0.0;
    return values[i * dimension + j];
}

void SymmetricMatrix::set(std::size_t i, std::size_t j, double value)
{
    if (i >= dimension || j >= dimension)
        return;
    values[i * dimension + j] = value;
    values[j * dimension + i] = value;
}

HamiltonianAssembler::HamiltonianAssembler(const NAOBasisSet& basis,
                                           Parameters parameters)
    : basis_(basis)
    , parameters_(std::move(parameters))
{
}

Outcome HamiltonianAssembler::prepare(
    const std::vector<Atom>& atoms,
    const std::vector<std::array<double, 3>>& lattice,
    const IntegrationGrid& grid)
{
    atoms_ = atoms;
    lattice_ = lattice;
    if (atoms_.empty())
        return Outcome::invalid("assembler: no atoms");
    if (!lattice_.empty() && lattice_.size() != 3)
        return Outcome::invalid("assembler: a cell needs three lattice vectors");
    if (grid.size() == 0)
        return Outcome::invalid("assembler: the integration grid is empty");

    std::vector<int> numbers;
    numbers.reserve(atoms_.size());
    for (const Atom& atom : atoms_)
        numbers.push_back(atom.atomicNumber);
    functions_ = basis_.enumerate(numbers);
    if (functions_.empty())
        return Outcome::invalid(
            "assembler: the basis has no functions for one of the species");

    gridSize_ = grid.size();
    weights_.resize(gridSize_);
    positions_.resize(gridSize_);
    pointAtom_.resize(gridSize_);
    pointShell_.resize(gridSize_);
    pointDirection_.resize(gridSize_);
    for (std::size_t g = 0; g < gridSize_; ++g) {
        const GridPoint& point = grid.points()[g];
        weights_[g] = point.weight;
        positions_[g] = point.position;
        pointAtom_[g] = point.atom;
        pointShell_[g] = point.shell;
        pointDirection_[g] = point.direction;
    }
    shellRadii_ = grid.shellRadii();
    shellWeights_ = grid.shellWeights();
    directions_ = grid.directions();

    // --- Lattice images ---------------------------------------------------
    // Far enough that every basis function reaching any grid point is
    // included. The reach is one basis cutoff plus the radius of the grid
    // itself, because a grid point can sit a full sphere radius from its own
    // atom before an orbital on a neighbour reaches it.
    double basisCutoff = 0.0;
    double densityReach = 0.0;
    for (const int z : basis_.species()) {
        const SpeciesBasis* species = basis_.forSpecies(z);
        if (species != nullptr) {
            basisCutoff = std::max(basisCutoff, species->maxCutoffBohr());
            densityReach = std::max(densityReach, species->densityCutoffBohr);
        }
    }
    double gridRadius = 0.0;
    for (const double radius : shellRadii_)
        gridRadius = std::max(gridRadius, radius);
    // The free-atom density and its neutral-atom potential are exponential
    // tails rather than the basis functions' hard cutoff, so the image list is
    // sized by the longer of the two — one list, reaching far enough for
    // whichever quantity needs it most.
    const double potentialReach = std::max(basisCutoff, densityReach)
        + gridRadius;

    images_.clear();
    imageIndices_.clear();
    std::array<int, 3> repeats{{0, 0, 0}};
    if (!lattice_.empty()) {
        for (int axis = 0; axis < 3; ++axis) {
            const auto& v = lattice_[static_cast<std::size_t>(axis)];
            const double length =
                std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (length < 1.0e-8)
                return Outcome::invalid("assembler: a lattice vector is zero");
            repeats[static_cast<std::size_t>(axis)] =
                static_cast<int>(std::ceil(potentialReach / length));
        }
    }
    for (int i = -repeats[0]; i <= repeats[0]; ++i)
        for (int j = -repeats[1]; j <= repeats[1]; ++j)
            for (int k = -repeats[2]; k <= repeats[2]; ++k) {
                std::array<double, 3> shift{{0.0, 0.0, 0.0}};
                if (!lattice_.empty())
                    for (int c = 0; c < 3; ++c)
                        shift[static_cast<std::size_t>(c)] =
                            i * lattice_[0][static_cast<std::size_t>(c)]
                            + j * lattice_[1][static_cast<std::size_t>(c)]
                            + k * lattice_[2][static_cast<std::size_t>(c)];
                images_.push_back(shift);
                imageIndices_.push_back({i, j, k});
            }

    // --- Tabulate every basis function at every grid point it reaches ------
    // Done once. From here the whole SCF is loops over this table: a matrix
    // element is a weighted sum over the points where both functions are
    // nonzero, and the k-dependence is a phase applied to a stored real value.
    contributions_.clear();
    contributionGradients_.clear();
    // Gradients are tabulated when the FUNCTIONAL needs them or when the
    // forces do: the Pulay term is built from ∇φ regardless of the
    // exchange-correlation choice.
    const bool wantGradients =
        Xc::needsGradients(parameters_.xc) || parameters_.computeForces;
    std::vector<double> gradientValues;
    offsets_.assign(gridSize_ + 1, 0);
    std::vector<BasisFunctionIndex> reachable;
    std::vector<std::array<double, 3>> displacements;
    std::vector<double> values;
    std::vector<double> kineticValues;
    std::vector<std::uint32_t> reachableFunction;
    std::vector<std::uint32_t> reachableImage;
    for (std::size_t g = 0; g < gridSize_; ++g) {
        offsets_[g] = contributions_.size();
        reachable.clear();
        displacements.clear();
        reachableFunction.clear();
        reachableImage.clear();
        for (std::size_t t = 0; t < images_.size(); ++t) {
            for (std::size_t f = 0; f < functions_.size(); ++f) {
                const BasisFunctionIndex& index = functions_[f];
                const SpeciesBasis* species =
                    basis_.forSpecies(index.atomicNumber);
                if (species == nullptr)
                    continue;
                const double cutoff =
                    species->functions[index.radial].cutoffBohr;
                std::array<double, 3> displacement{{0.0, 0.0, 0.0}};
                double squared = 0.0;
                for (int c = 0; c < 3; ++c) {
                    const double d = positions_[g][static_cast<std::size_t>(c)]
                        - atoms_[index.atom].position[static_cast<std::size_t>(c)]
                        - images_[t][static_cast<std::size_t>(c)];
                    displacement[static_cast<std::size_t>(c)] = d;
                    squared += d * d;
                }
                if (squared >= cutoff * cutoff)
                    continue;
                reachable.push_back(index);
                displacements.push_back(displacement);
                reachableFunction.push_back(static_cast<std::uint32_t>(f));
                reachableImage.push_back(static_cast<std::uint32_t>(t));
            }
        }
        if (wantGradients)
            basis_.evaluateWithGradients(reachable, displacements, values,
                                         kineticValues, gradientValues);
        else
            basis_.evaluate(reachable, displacements, values, kineticValues);
        for (std::size_t n = 0; n < values.size(); ++n) {
            if (values[n] == 0.0 && kineticValues[n] == 0.0)
                continue;
            contributions_.push_back({reachableFunction[n], reachableImage[n],
                                      values[n], kineticValues[n]});
            if (wantGradients)
                for (int c = 0; c < 3; ++c)
                    contributionGradients_.push_back(
                        gradientValues[n * 3 + static_cast<std::size_t>(c)]);
        }
    }
    offsets_[gridSize_] = contributions_.size();

    // --- Superposed free-atom density and neutral-atom potential ----------
    atomicDensity_.assign(gridSize_, 0.0);
    atomicDensityGradient_.clear();
    if (wantGradients)
        atomicDensityGradient_.assign(gridSize_ * 3, 0.0);
    neutralAtomPotential_.assign(gridSize_, 0.0);
    const RadialGrid& radial = basis_.grid();

    // dρ_free/dr per species, tabulated once. Five-point in the mesh index
    // over the analytic Jacobian, matching how the basis functions' own
    // derivatives are built.
    std::map<int, std::vector<double>> densitySlope;
    if (wantGradients) {
        for (const int z : basis_.species()) {
            const SpeciesBasis* species = basis_.forSpecies(z);
            if (species == nullptr)
                continue;
            std::vector<double> slope(radial.size(), 0.0);
            for (std::size_t i = 2; i + 2 < radial.size(); ++i)
                slope[i] = (-species->freeAtomDensity[i + 2]
                            + 8.0 * species->freeAtomDensity[i + 1]
                            - 8.0 * species->freeAtomDensity[i - 1]
                            + species->freeAtomDensity[i - 2])
                    / (12.0 * radial.drdi()[i]);
            densitySlope[z] = std::move(slope);
        }
    }
    for (std::size_t g = 0; g < gridSize_; ++g) {
        for (std::size_t t = 0; t < images_.size(); ++t) {
            for (std::size_t a = 0; a < atoms_.size(); ++a) {
                const SpeciesBasis* species =
                    basis_.forSpecies(atoms_[a].atomicNumber);
                if (species == nullptr)
                    continue;
                std::array<double, 3> centre{{0.0, 0.0, 0.0}};
                for (int c = 0; c < 3; ++c)
                    centre[static_cast<std::size_t>(c)] =
                        atoms_[a].position[static_cast<std::size_t>(c)]
                        + images_[t][static_cast<std::size_t>(c)];
                const double r = distance(positions_[g], centre);
                if (r >= species->densityCutoffBohr)
                    continue;
                atomicDensity_[g] += std::max(
                    0.0, radial.interpolate(species->freeAtomDensity, r));
                if (wantGradients && r > 1.0e-12) {
                    const auto slope =
                        densitySlope.find(atoms_[a].atomicNumber);
                    if (slope != densitySlope.end()) {
                        const double dr =
                            radial.interpolate(slope->second, r);
                        for (int c = 0; c < 3; ++c) {
                            const std::size_t o = static_cast<std::size_t>(c);
                            atomicDensityGradient_[g * 3 + o] += dr
                                * (positions_[g][o] - centre[o]) / r;
                        }
                    }
                }
            }
        }
    }

    // The neutral-atom potential. The nuclear 1/r is analytic and is NOT
    // interpolated: what gets tabulated per species is the SCREENED remainder
    // v^NA + Z/r, which is smooth through the origin, and the pole is
    // subtracted back at each point. Interpolating v^NA itself would be
    // fitting a cubic across a pole, a few thousand hartree wide, at the one
    // place the density is largest.
    std::map<int, std::vector<double>> screenedBySpecies;
    std::map<int, double> cutoffBySpecies;
    std::map<int, std::vector<double>> cumulativeBySpecies;
    for (const int z : basis_.species()) {
        const SpeciesBasis* species = basis_.forSpecies(z);
        if (species == nullptr)
            continue;
        std::vector<double> screened(radial.size(), 0.0);
        for (std::size_t i = 0; i < radial.size(); ++i) {
            const double r = radial.r()[i];
            screened[i] = species->neutralAtomPotential[i]
                + (r > 0.0 ? z / r : 0.0);
        }
        // r = 0 is the Hartree potential of the free atom there, which the
        // radial solver already produced as a finite number.
        screened[0] = screened[1] + (screened[1] - screened[2]);
        // C(t) = ∫₀^t v_B(t′)t′ dt′, with the integrand written as
        // screened(t)·t − Z so the nuclear pole cancels analytically instead
        // of being sampled.
        std::vector<double> moment(radial.size(), 0.0);
        for (std::size_t i = 0; i < radial.size(); ++i) {
            const double r = radial.r()[i];
            moment[i] = r < species->densityCutoffBohr
                ? screened[i] * r - z
                : 0.0;
        }
        cumulativeBySpecies[z] = radial.cumulative(moment);
        screenedBySpecies[z] = std::move(screened);
        cutoffBySpecies[z] = species->densityCutoffBohr;
    }
    std::fill(neutralAtomPotential_.begin(), neutralAtomPotential_.end(), 0.0);
    for (std::size_t g = 0; g < gridSize_; ++g) {
        for (std::size_t t = 0; t < images_.size(); ++t) {
            for (std::size_t a = 0; a < atoms_.size(); ++a) {
                const int z = atoms_[a].atomicNumber;
                const auto it = screenedBySpecies.find(z);
                if (it == screenedBySpecies.end())
                    continue;
                std::array<double, 3> centre{{0.0, 0.0, 0.0}};
                for (int c = 0; c < 3; ++c)
                    centre[static_cast<std::size_t>(c)] =
                        atoms_[a].position[static_cast<std::size_t>(c)]
                        + images_[t][static_cast<std::size_t>(c)];
                const double r = distance(positions_[g], centre);
                if (r >= cutoffBySpecies[z])
                    continue;
                const double screened = radial.interpolate(it->second, r);
                neutralAtomPotential_[g] +=
                    screened - (r > 1.0e-12 ? z / r : 0.0);
            }
        }
    }

    // --- Reference electrostatic energy -----------------------------------
    // Σ_A (the free atom's own electrostatic energy) + ½ Σ over neutral-atom
    // PAIRS. Both pieces are finite and short-ranged precisely because every
    // participant is neutral; the nuclear self-energies, which are not, cancel
    // out of the difference between this and the true energy rather than being
    // "regularised".
    referenceEnergy_ = 0.0;
    for (std::size_t a = 0; a < atoms_.size(); ++a) {
        const int z = atoms_[a].atomicNumber;
        const SpeciesBasis* species = basis_.forSpecies(z);
        if (species == nullptr)
            continue;
        // Self: E_H[ρ_A] + ∫ρ_A(−Z/r).
        const std::vector<double> hartree =
            radial.hartreePotential(species->freeAtomDensity);
        std::vector<double> integrand(radial.size(), 0.0);
        for (std::size_t i = 0; i < radial.size(); ++i) {
            const double r = radial.r()[i];
            integrand[i] = species->freeAtomDensity[i]
                * (0.5 * hartree[i] - (r > 0.0 ? z / r : 0.0));
        }
        referenceEnergy_ += kFourPi * radial.integrateSpherical(integrand);

        // Pairs: ∫ρ_A v_B^NA − Z_A v_B^NA(R_AB), over every other centre.
        for (std::size_t t = 0; t < images_.size(); ++t) {
            for (std::size_t b = 0; b < atoms_.size(); ++b) {
                const bool same = (b == a) && (images_[t][0] == 0.0)
                    && (images_[t][1] == 0.0) && (images_[t][2] == 0.0);
                if (same)
                    continue;
                const int zb = atoms_[b].atomicNumber;
                const auto it = screenedBySpecies.find(zb);
                if (it == screenedBySpecies.end())
                    continue;
                std::array<double, 3> centre{{0.0, 0.0, 0.0}};
                for (int c = 0; c < 3; ++c)
                    centre[static_cast<std::size_t>(c)] =
                        atoms_[b].position[static_cast<std::size_t>(c)]
                        + images_[t][static_cast<std::size_t>(c)];
                const double separation =
                    distance(atoms_[a].position, centre);
                if (separation >= species->densityCutoffBohr
                        + cutoffBySpecies[zb])
                    continue;
                // ∫ρ_A(r) v_B(r) over all space, EXACTLY, as a
                // one-dimensional integral.
                //
                // This was previously a quadrature on atom A's own spherical
                // grid, on the stated grounds that "v_B is smooth across it".
                // It is not: v_B^NA = v_H[ρ_B] − Z_B/r carries the NUCLEAR
                // POLE OF B, and a grid centred on A cannot resolve a
                // singularity sitting off-centre — which is the whole reason
                // the Becke partition exists. The error was invisible for one
                // atom (there are no pairs) and grew as atoms came closer:
                // measured on H₂ it left the total energy drifting by 0.1 eV
                // with grid refinement, against 0.001 eV for a single atom,
                // and that drift is what made every force meaningless.
                //
                // Both factors are spherical about their own centres, so the
                // angular average of v_B over a sphere of radius r about A,
                // with the centres d apart, has a closed form:
                //
                //     ⟨v_B⟩(r) = 1/(2rd) ∫_{|r−d|}^{r+d} v_B(t)·t dt
                //
                // and v_B(t)·t = screened(t)·t − Z_B is SMOOTH THROUGH THE
                // ORIGIN — the pole is removed analytically rather than
                // sampled. What is left is
                //
                //     ∫ρ_A v_B = (2π/d) ∫ ρ_A(r)·r·[C(r+d) − C(|r−d|)] dr,
                //     C(t) = ∫₀^t v_B(t′) t′ dt′,
                //
                // on the fine radial mesh, with no three-dimensional grid
                // involved at all.
                const auto cumulative = cumulativeBySpecies.find(zb);
                if (cumulative == cumulativeBySpecies.end())
                    continue;
                const std::vector<double>& tail = cumulative->second;
                const auto atRadius = [&radial, &tail](double t) {
                    if (t <= 0.0)
                        return 0.0;
                    if (t >= radial.outerRadius())
                        return tail.back(); // v_B has died; C is constant
                    return radial.interpolate(tail, t);
                };
                double overlap = 0.0;
                if (separation > 1.0e-8) {
                    std::vector<double> integrand(radial.size(), 0.0);
                    for (std::size_t i = 0; i < radial.size(); ++i) {
                        const double r = radial.r()[i];
                        integrand[i] = species->freeAtomDensity[i] * r
                            * (atRadius(r + separation)
                               - atRadius(std::abs(r - separation)));
                    }
                    overlap = 2.0 * kPi / separation
                        * radial.integrate(integrand);
                }
                const double atNucleus = separation < cutoffBySpecies[zb]
                    ? radial.interpolate(it->second, separation)
                        - zb / separation
                    : 0.0;
                referenceEnergy_ += 0.5 * (overlap - z * atNucleus);
            }
        }
    }
    return Outcome::success();
}

std::vector<std::complex<double>> HamiltonianAssembler::imagePhases(
    const std::array<double, 3>& kFractional) const
{
    // e^{ik·T} with k in fractional coordinates and T in integer lattice
    // multiples is e^{2πi(k·n)} — no reciprocal lattice vectors needed, which
    // removes the commonest sign-and-factor mistake in a k-point loop.
    std::vector<std::complex<double>> phases(images_.size(),
                                             std::complex<double>(1.0, 0.0));
    if (!lattice_.empty()) {
        for (std::size_t t = 0; t < images_.size(); ++t) {
            const double argument = kTwoPi
                * (kFractional[0] * imageIndices_[t][0]
                   + kFractional[1] * imageIndices_[t][1]
                   + kFractional[2] * imageIndices_[t][2]);
            phases[t] = std::complex<double>(std::cos(argument),
                                             std::sin(argument));
        }
    }
    return phases;
}

void HamiltonianAssembler::blochSums(
    const std::array<double, 3>& kFractional,
    std::vector<std::complex<double>>& values,
    std::vector<std::complex<double>>* gradients) const
{
    values.assign(gridSize_ * functions_.size(), {});
    const bool wantGradients =
        gradients != nullptr && !contributionGradients_.empty();
    if (gradients != nullptr)
        gradients->assign(gridSize_ * functions_.size() * 3, {});
    const std::vector<std::complex<double>> phases = imagePhases(kFractional);
    for (std::size_t g = 0; g < gridSize_; ++g) {
        for (std::size_t n = offsets_[g]; n < offsets_[g + 1]; ++n) {
            const Contribution& contribution = contributions_[n];
            const std::complex<double> phase = phases[contribution.image];
            values[g * functions_.size() + contribution.function] +=
                phase * contribution.value;
            if (wantGradients)
                for (int c = 0; c < 3; ++c)
                    (*gradients)[(g * functions_.size()
                                  + contribution.function) * 3
                                 + static_cast<std::size_t>(c)] +=
                        phase
                        * contributionGradients_[n * 3
                                                 + static_cast<std::size_t>(c)];
        }
    }
}

Outcome HamiltonianAssembler::buildOverlapAndKinetic(
    const std::array<double, 3>& kFractional,
    std::vector<std::complex<double>>& overlap,
    std::vector<std::complex<double>>& kinetic) const
{
    const std::size_t n = functions_.size();
    if (n == 0)
        return Outcome::invalid("assembler: prepare() has not run");
    overlap.assign(n * n, {});
    kinetic.assign(n * n, {});

    // T̂φ is TABULATED, not recomputed: `prepare` stored −½∇²φ next to φ for
    // every (function, image, point), so the Bloch sum of the kinetic operator
    // is the same phase-weighted sum as the Bloch sum of the function itself.
    std::vector<std::complex<double>> chi;
    std::vector<std::complex<double>> tChi;
    blochSums(kFractional, chi);
    tChi.assign(gridSize_ * n, {});
    const std::vector<std::complex<double>> phases = imagePhases(kFractional);
    for (std::size_t g = 0; g < gridSize_; ++g) {
        for (std::size_t c = offsets_[g]; c < offsets_[g + 1]; ++c) {
            const Contribution& contribution = contributions_[c];
            tChi[g * n + contribution.function] +=
                phases[contribution.image] * contribution.kinetic;
        }
    }

    // Upper triangle only, mirrored at the end. Both matrices are Hermitian by
    // construction, so computing the lower half is repeating the same
    // quadrature with the conjugate — half the innermost loop of the whole
    // engine, for nothing.
    for (std::size_t g = 0; g < gridSize_; ++g) {
        const double weight = weights_[g];
        if (weight == 0.0)
            continue;
        for (std::size_t i = 0; i < n; ++i) {
            const std::complex<double> ci = std::conj(chi[g * n + i]);
            const std::complex<double> ti = std::conj(tChi[g * n + i]);
            if (ci == std::complex<double>(0.0, 0.0)
                && ti == std::complex<double>(0.0, 0.0))
                continue;
            for (std::size_t j = i; j < n; ++j) {
                overlap[i * n + j] += weight * ci * chi[g * n + j];
                // Symmetrised: ½(⟨φ_i|T̂φ_j⟩ + ⟨T̂φ_i|φ_j⟩). The two differ
                // because each uses its own function's ε and v_at, and the
                // average is the Hermitian operator both approximate.
                kinetic[i * n + j] += 0.5 * weight
                    * (ci * tChi[g * n + j] + ti * chi[g * n + j]);
            }
        }
    }
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < i; ++j) {
            overlap[i * n + j] = std::conj(overlap[j * n + i]);
            kinetic[i * n + j] = std::conj(kinetic[j * n + i]);
        }
    return Outcome::success();
}

Outcome HamiltonianAssembler::buildPotentialMatrix(
    const std::vector<double>& potential,
    const std::vector<double>& gradientField,
    const std::array<double, 3>& kFractional,
    std::vector<std::complex<double>>& matrix) const
{
    const std::size_t n = functions_.size();
    if (n == 0)
        return Outcome::invalid("assembler: prepare() has not run");
    if (potential.size() != gridSize_)
        return Outcome::invalid(
            "assembler: the potential does not match the integration grid");
    const bool gga = gradientField.size() == gridSize_ * 3
        && !contributionGradients_.empty();
    matrix.assign(n * n, {});
    std::vector<std::complex<double>> chi;
    std::vector<std::complex<double>> dchi;
    blochSums(kFractional, chi, gga ? &dchi : nullptr);
    for (std::size_t g = 0; g < gridSize_; ++g) {
        const double weight = weights_[g] * potential[g];
        const double w = weights_[g];
        for (std::size_t i = 0; i < n; ++i) {
            const std::complex<double> ci = std::conj(chi[g * n + i]);
            if (ci == std::complex<double>(0.0, 0.0) && !gga)
                continue;
            for (std::size_t j = i; j < n; ++j) {
                matrix[i * n + j] += weight * ci * chi[g * n + j];
                if (!gga)
                    continue;
                // V · (φ_i* ∇φ_j + φ_j ∇φ_i*), the integration-by-parts term.
                std::complex<double> term{};
                for (int c = 0; c < 3; ++c) {
                    const std::size_t o = static_cast<std::size_t>(c);
                    term += gradientField[g * 3 + o]
                        * (ci * dchi[(g * n + j) * 3 + o]
                           + chi[g * n + j]
                               * std::conj(dchi[(g * n + i) * 3 + o]));
                }
                matrix[i * n + j] += w * term;
            }
        }
    }
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < i; ++j)
            matrix[i * n + j] = std::conj(matrix[j * n + i]);
    return Outcome::success();
}

Outcome HamiltonianAssembler::buildDensity(
    const std::vector<std::vector<std::complex<double>>>& coefficients,
    const std::vector<std::vector<double>>& occupations,
    const std::vector<std::array<double, 3>>& kPoints,
    std::vector<double>& density,
    std::vector<double>* densityGradient) const
{
    const std::size_t n = functions_.size();
    density.assign(gridSize_, 0.0);
    const bool wantGradient =
        densityGradient != nullptr && !contributionGradients_.empty();
    if (densityGradient != nullptr)
        densityGradient->assign(gridSize_ * 3, 0.0);
    if (n == 0)
        return Outcome::invalid("assembler: prepare() has not run");
    if (coefficients.size() != occupations.size()
        || coefficients.size() != kPoints.size())
        return Outcome::invalid(
            "assembler: coefficients, occupations and k-points disagree");

    std::vector<std::complex<double>> chi;
    std::vector<std::complex<double>> dchi;
    for (std::size_t k = 0; k < kPoints.size(); ++k) {
        const std::size_t bands = occupations[k].size();
        if (bands == 0)
            continue;
        if (coefficients[k].size() != n * bands)
            return Outcome::invalid(
                "assembler: the coefficient block is not n x bands");
        blochSums(kPoints[k], chi, wantGradient ? &dchi : nullptr);
        for (std::size_t g = 0; g < gridSize_; ++g) {
            for (std::size_t b = 0; b < bands; ++b) {
                if (occupations[k][b] == 0.0)
                    continue;
                std::complex<double> value{};
                for (std::size_t i = 0; i < n; ++i)
                    value += coefficients[k][i * bands + b] * chi[g * n + i];
                density[g] += occupations[k][b] * std::norm(value);
                if (!wantGradient)
                    continue;
                // ∇ρ = 2 Σ f Re[ψ* ∇ψ]. The imaginary part cancels between k
                // and −k for a real Hamiltonian, which is the same fact that
                // makes time-reversal folding of the k-mesh exact.
                for (int c = 0; c < 3; ++c) {
                    const std::size_t o = static_cast<std::size_t>(c);
                    std::complex<double> derivative{};
                    for (std::size_t i = 0; i < n; ++i)
                        derivative += coefficients[k][i * bands + b]
                            * dchi[(g * n + i) * 3 + o];
                    (*densityGradient)[g * 3 + o] += 2.0 * occupations[k][b]
                        * (std::conj(value) * derivative).real();
                }
            }
        }
    }
    return Outcome::success();
}

Outcome HamiltonianAssembler::hellmannFeynmanForces(
    const std::vector<double>& density,
    std::vector<std::array<double, 3>>& forces) const
{
    forces.assign(atoms_.size(), {{0.0, 0.0, 0.0}});
    if (density.size() != gridSize_)
        return Outcome::invalid(
            "forces: the density does not match the integration grid");
    if (!lattice_.empty())
        return Outcome::notImplemented(
            "the Hellmann-Feynman force of a PERIODIC cell needs an Ewald sum "
            "for the nuclear term, which is not implemented");

    for (std::size_t a = 0; a < atoms_.size(); ++a) {
        const double z = atoms_[a].atomicNumber;
        std::array<double, 3> force{{0.0, 0.0, 0.0}};

        // Attraction to the electrons: +Z ∫ρ(r)(r−R)/|r−R|³ dr. The integrand
        // goes as 1/r² at the nucleus where the all-electron density is
        // largest, which is why this term is the hard one to converge and why
        // it is never the number a caller acts on.
        for (std::size_t g = 0; g < gridSize_; ++g) {
            std::array<double, 3> d{{0.0, 0.0, 0.0}};
            double squared = 0.0;
            for (int c = 0; c < 3; ++c) {
                const auto o = static_cast<std::size_t>(c);
                d[o] = positions_[g][o] - atoms_[a].position[o];
                squared += d[o] * d[o];
            }
            const double r = std::sqrt(squared);
            if (r < 1.0e-8)
                continue;
            const double factor = z * weights_[g] * density[g] / (r * r * r);
            for (int c = 0; c < 3; ++c)
                force[static_cast<std::size_t>(c)] +=
                    factor * d[static_cast<std::size_t>(c)];
        }

        // Repulsion from the other nuclei.
        for (std::size_t b = 0; b < atoms_.size(); ++b) {
            if (b == a)
                continue;
            std::array<double, 3> d{{0.0, 0.0, 0.0}};
            double squared = 0.0;
            for (int c = 0; c < 3; ++c) {
                const auto o = static_cast<std::size_t>(c);
                d[o] = atoms_[a].position[o] - atoms_[b].position[o];
                squared += d[o] * d[o];
            }
            const double r = std::sqrt(squared);
            if (r < 1.0e-8)
                continue;
            const double factor =
                z * atoms_[b].atomicNumber / (r * r * r);
            for (int c = 0; c < 3; ++c)
                force[static_cast<std::size_t>(c)] +=
                    factor * d[static_cast<std::size_t>(c)];
        }
        forces[a] = force;
    }
    return Outcome::success();
}

Outcome HamiltonianAssembler::pulayOverlapForces(
    const std::vector<std::vector<std::complex<double>>>& coefficients,
    const std::vector<std::vector<double>>& occupations,
    const std::vector<std::vector<double>>& eigenvalues,
    const std::vector<std::array<double, 3>>& kPoints,
    std::vector<std::array<double, 3>>& forces) const
{
    forces.assign(atoms_.size(), {{0.0, 0.0, 0.0}});
    const std::size_t n = functions_.size();
    if (n == 0)
        return Outcome::invalid("forces: prepare() has not run");
    if (contributionGradients_.empty())
        return Outcome::invalid(
            "forces: the Pulay term needs the basis gradients, which are "
            "tabulated only when the functional or the forces ask for them");

    // The energy-weighted density matrix W_ij = Σ f ε C*_i C_j, on the grid.
    // Its contraction with ∂S/∂R is the overlap part of the Pulay force.
    std::vector<std::complex<double>> chi;
    std::vector<std::complex<double>> dchi;
    for (std::size_t k = 0; k < kPoints.size(); ++k) {
        const std::size_t bands = occupations[k].size();
        if (bands == 0)
            continue;
        blochSums(kPoints[k], chi, &dchi);
        for (std::size_t g = 0; g < gridSize_; ++g) {
            for (std::size_t b = 0; b < bands; ++b) {
                const double weight =
                    occupations[k][b] * eigenvalues[k][b];
                if (weight == 0.0)
                    continue;
                // ψ and ∇ψ restricted to the functions of one atom: the
                // derivative of S with respect to R_A only touches those.
                for (std::size_t a = 0; a < atoms_.size(); ++a) {
                    std::complex<double> psiAtom{};
                    std::array<std::complex<double>, 3> gradAtom{};
                    std::complex<double> psiAll{};
                    for (std::size_t i = 0; i < n; ++i) {
                        const std::complex<double> c =
                            coefficients[k][i * bands + b];
                        psiAll += c * chi[g * n + i];
                        if (functions_[i].atom != a)
                            continue;
                        psiAtom += c * chi[g * n + i];
                        for (int comp = 0; comp < 3; ++comp)
                            gradAtom[static_cast<std::size_t>(comp)] +=
                                c * dchi[(g * n + i) * 3
                                         + static_cast<std::size_t>(comp)];
                    }
                    (void)psiAtom;
                    // ∂φ/∂R_A = −∇φ for a function centred on A, so the
                    // contribution is +2 Re[ψ* ∇ψ_A] weighted by fε.
                    for (int comp = 0; comp < 3; ++comp) {
                        const auto o = static_cast<std::size_t>(comp);
                        forces[a][o] += 2.0 * weight * weights_[g]
                            * (std::conj(psiAll) * gradAtom[o]).real();
                    }
                }
            }
        }
    }
    return Outcome::success();
}

Outcome HamiltonianAssembler::buildEffectivePotential(
    const std::vector<double>& density,
    const std::vector<double>& densityGradient, std::vector<double>& effective,
    std::vector<double>& gradientField, PotentialEnergies& energies) const
{
    energies = {};
    effective.assign(gridSize_, 0.0);
    gradientField.clear();
    if (density.size() != gridSize_)
        return Outcome::invalid(
            "assembler: the density does not match the integration grid");
    if (!Xc::supports(parameters_.xc))
        return Outcome::notImplemented(
            std::string("the assembler has no implementation of ")
            + toString(parameters_.xc));

    // --- Difference density ----------------------------------------------
    // δρ = ρ − Σ free atoms. It integrates to zero over the cell, which is
    // what lets its potential be built centre by centre without a lattice
    // sum of monopoles.
    std::vector<double> difference(gridSize_, 0.0);
    for (std::size_t g = 0; g < gridSize_; ++g)
        difference[g] = density[g] - atomicDensity_[g];

    // --- Multipole expansion of δρ around each atom ------------------------
    // Each grid point already belongs to one atom with a Becke weight folded
    // into its quadrature weight, so the partition is done: the points of
    // atom A, gathered by shell, ARE w_A·δρ sampled on a spherical grid.
    const std::size_t shells = shellRadii_.size();
    const std::size_t directions = directions_.size();
    const std::size_t harmonics = AngularGrid::harmonicCount(kMultipoleL);
    std::vector<double> harmonicTable(directions * harmonics, 0.0);
    {
        std::vector<double> row;
        for (std::size_t d = 0; d < directions; ++d) {
            AngularGrid::realSphericalHarmonics(directions_[d].x,
                                                directions_[d].y,
                                                directions_[d].z, kMultipoleL,
                                                row);
            for (std::size_t h = 0; h < harmonics; ++h)
                harmonicTable[d * harmonics + h] = row[h];
        }
    }

    // Potential of each atom's own δρ, as V_lm(r) on the shells.
    std::vector<double> radialPotential(atoms_.size() * shells * harmonics,
                                        0.0);
    std::vector<double> multipoles(atoms_.size() * harmonics, 0.0);
    for (std::size_t a = 0; a < atoms_.size(); ++a) {
        // c_lm(r) = ∫ δρ_A(r, Ω) Y_lm dΩ, and the angular weights sum to one,
        // so the surface integral carries a 4π.
        std::vector<double> component(shells * harmonics, 0.0);
        for (std::size_t s = 0; s < shells; ++s) {
            for (std::size_t d = 0; d < directions; ++d) {
                const std::size_t g = (a * shells + s) * directions + d;
                // The Becke weight is inside weights_[g]; dividing it back out
                // of the quadrature weight recovers w_A(r)·δρ(r) itself, which
                // is what the expansion needs.
                const double quadrature =
                    shellWeights_[s] * directions_[d].weight * kFourPi;
                const double partition =
                    quadrature > 0.0 ? weights_[g] / quadrature : 0.0;
                const double value = partition * difference[g];
                for (std::size_t h = 0; h < harmonics; ++h)
                    component[s * harmonics + h] += kFourPi
                        * directions_[d].weight * value
                        * harmonicTable[d * harmonics + h];
            }
        }
        // Radial Poisson for each (l, m):
        //   V_lm(r) = 4π/(2l+1) [ r^{-(l+1)} ∫₀^r c r'^{l+2} dr'
        //                         + r^l ∫_r^∞ c r'^{1-l} dr' ]
        // The shell weights already carry r², so the powers below are the
        // remainder.
        for (int l = 0; l <= kMultipoleL; ++l) {
            for (int m = -l; m <= l; ++m) {
                const auto h = static_cast<std::size_t>(l * l + l + m);
                std::vector<double> inner(shells, 0.0);
                std::vector<double> outer(shells, 0.0);
                double running = 0.0;
                for (std::size_t s = 0; s < shells; ++s) {
                    running += shellWeights_[s]
                        * std::pow(shellRadii_[s], static_cast<double>(l))
                        * component[s * harmonics + h];
                    inner[s] = running;
                }
                running = 0.0;
                for (std::size_t s = shells; s-- > 0;) {
                    running += shellWeights_[s]
                        * std::pow(shellRadii_[s], -1.0 - l)
                        * component[s * harmonics + h];
                    outer[s] = running;
                }
                const double prefactor = kFourPi / (2.0 * l + 1.0);
                for (std::size_t s = 0; s < shells; ++s) {
                    const double r = shellRadii_[s];
                    radialPotential[(a * shells + s) * harmonics + h] =
                        prefactor
                        * (inner[s] / std::pow(r, l + 1.0)
                           + std::pow(r, static_cast<double>(l)) * outer[s]);
                }
                multipoles[a * harmonics + h] = inner[shells - 1];
                if (l == 0)
                    energies.largestAtomicMonopole = std::max(
                        energies.largestAtomicMonopole,
                        // c₀₀ = ∫δρ dΩ · Y₀₀, so the charge is the moment
                        // times √(4π).
                        std::abs(inner[shells - 1]) * std::sqrt(kFourPi));
            }
        }
    }

    // --- Assemble v_es on the grid ----------------------------------------
    // Σ_{A,T} v_A^NA is precomputed and constant; the δρ part is summed over
    // the same images, interpolated in ln r between shells and continued by
    // its multipole tail beyond the outermost one.
    std::vector<double> hartreeDifference(gridSize_, 0.0);
    std::vector<double> row;
    const double outerShell = shellRadii_.back();
    // δρ_A lives entirely inside atom A's sphere, so its potential outside is
    // a pure multipole tail. Cut at one and a half sphere radii, where the
    // l ≥ 1 terms are already below 10⁻⁶ hartree.
    //
    // The MONOPOLE tail is the one that does not fall off fast enough to
    // truncate safely, and it is dropped here. That is exact when each atom's
    // δρ is neutral — which symmetry guarantees for a crystal whose atoms are
    // all equivalent, silicon among them — and an approximation otherwise. The
    // monopoles are computed anyway, and a large one is reported rather than
    // absorbed.
    const double electrostaticCutoff = 1.5 * outerShell;
    for (std::size_t g = 0; g < gridSize_; ++g) {
        double total = 0.0;
        for (std::size_t t = 0; t < images_.size(); ++t) {
            for (std::size_t a = 0; a < atoms_.size(); ++a) {
                std::array<double, 3> centre{{0.0, 0.0, 0.0}};
                for (int c = 0; c < 3; ++c)
                    centre[static_cast<std::size_t>(c)] =
                        atoms_[a].position[static_cast<std::size_t>(c)]
                        + images_[t][static_cast<std::size_t>(c)];
                std::array<double, 3> d{
                    {positions_[g][0] - centre[0], positions_[g][1] - centre[1],
                     positions_[g][2] - centre[2]}};
                const double squared =
                    d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
                if (squared > electrostaticCutoff * electrostaticCutoff)
                    continue;
                const double r = std::sqrt(squared);
                AngularGrid::realSphericalHarmonics(d[0], d[1], d[2],
                                                    kMultipoleL, row);
                for (int l = 0; l <= kMultipoleL; ++l) {
                    for (int m = -l; m <= l; ++m) {
                        const auto h = static_cast<std::size_t>(l * l + l + m);
                        double value = 0.0;
                        if (r >= outerShell) {
                            // Outside the sphere the potential is exactly the
                            // multipole field — no extrapolation involved.
                            value = kFourPi / (2.0 * l + 1.0)
                                * multipoles[a * harmonics + h]
                                / std::pow(r, l + 1.0);
                        } else {
                            // Linear in ln r between shells: the radial mesh is
                            // geometric, so ln r is where it is uniform.
                            const auto upper = std::lower_bound(
                                shellRadii_.begin(), shellRadii_.end(), r);
                            std::size_t s1 = static_cast<std::size_t>(
                                upper - shellRadii_.begin());
                            if (s1 == 0)
                                s1 = 1;
                            if (s1 >= shells)
                                s1 = shells - 1;
                            const std::size_t s0 = s1 - 1;
                            const double t0 = std::log(shellRadii_[s0]);
                            const double t1 = std::log(shellRadii_[s1]);
                            const double x = (std::log(std::max(r, 1.0e-12))
                                              - t0)
                                / (t1 - t0);
                            const double v0 =
                                radialPotential[(a * shells + s0) * harmonics
                                                + h];
                            const double v1 =
                                radialPotential[(a * shells + s1) * harmonics
                                                + h];
                            value = v0 + (v1 - v0) * std::clamp(x, 0.0, 1.0);
                        }
                        total += value * row[h];
                    }
                }
            }
        }
        hartreeDifference[g] = total;
    }

    // --- Exchange-correlation and the energies -----------------------------
    // sigma = |grad rho|^2 is what a gradient functional is written in; it is
    // all zeros for a local one, and Xc::evaluateGrid ignores it there.
    const bool gga = Xc::needsGradients(parameters_.xc);
    std::vector<double> sigma;
    if (gga) {
        if (densityGradient.size() != gridSize_ * 3)
            return Outcome::invalid(
                "assembler: a gradient functional needs the density gradient, "
                "which was not supplied");
        sigma.assign(gridSize_, 0.0);
        for (std::size_t g = 0; g < gridSize_; ++g)
            for (int c = 0; c < 3; ++c) {
                const double d =
                    densityGradient[g * 3 + static_cast<std::size_t>(c)];
                sigma[g] += d * d;
            }
    }
    std::vector<double> xcPotential;
    std::vector<double> dfdsigma;
    const double xcEnergy = Xc::evaluateGrid(density, sigma, weights_,
                                             parameters_.xc, xcPotential,
                                             dfdsigma);
    if (gga) {
        // The vector field the matrix element needs: V = 2(∂f/∂σ)∇ρ.
        gradientField.assign(gridSize_ * 3, 0.0);
        for (std::size_t g = 0; g < gridSize_; ++g)
            for (int c = 0; c < 3; ++c) {
                const std::size_t o = static_cast<std::size_t>(c);
                gradientField[g * 3 + o] =
                    2.0 * dfdsigma[g] * densityGradient[g * 3 + o];
            }
    }

    double electrostatic = referenceEnergy_;
    double trace = 0.0;
    for (std::size_t g = 0; g < gridSize_; ++g) {
        const double electrostaticPotential =
            neutralAtomPotential_[g] + hartreeDifference[g];
        effective[g] = electrostaticPotential + xcPotential[g];
        // E_es = E_ref + ∫δρ V^NA + ½∫δρ v_H[δρ]. Each term is finite on its
        // own because every charge distribution in it is neutral.
        electrostatic += weights_[g]
            * (difference[g] * neutralAtomPotential_[g]
               + 0.5 * difference[g] * hartreeDifference[g]);
        trace += weights_[g] * density[g] * effective[g];
    }
    energies.electrostatic = electrostatic;
    energies.exchangeCorrelation = xcEnergy;
    energies.potentialTrace = trace;
    return Outcome::success();
}

} // namespace calango::dft
