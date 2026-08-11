#include "dft/CalangoDFTEngine.hpp"

#include "core/Structure.hpp"
#include "dft/IntegrationGrid.hpp"
#include "dft/KPointGrid.hpp"
#include "dft/LinearAlgebra.hpp"
#include "dft/SCFSolver.hpp"
#include "dft/XcFunctional.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace calango::dft {
namespace {

constexpr double kHartreeToEv = 27.211386245988;
constexpr double kBohrPerAngstrom = 1.8897261254578281;

std::string formatted(const char* format, double a, double b = 0.0,
                      double c = 0.0)
{
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), format, a, b, c);
    return buffer;
}

} // namespace

const char* toString(Status status)
{
    switch (status) {
    case Status::Ok:               return "ok";
    case Status::NotImplemented:   return "not implemented";
    case Status::InvalidInput:     return "invalid input";
    case Status::NotConverged:     return "not converged";
    case Status::NumericalFailure: return "numerical failure";
    }
    return "unknown";
}

const char* toString(XcFunctional functional)
{
    switch (functional) {
    case XcFunctional::GgaPbe: return "PBE";
    case XcFunctional::LdaPz:  return "LDA-PZ";
    case XcFunctional::LdaVwn: return "LDA-VWN";
    case XcFunctional::LdaPw:  break;
    }
    return "LDA-PW92";
}

CalangoDFTEngine::CalangoDFTEngine(Parameters parameters)
    : parameters_(std::move(parameters))
{
    // The radial mesh the basis and the atomic solve live on. It is not the
    // integration grid: this one has thousands of points because it has to
    // resolve a 1s orbital, and it is one-dimensional so that costs nothing.
    basis_.setGrid(RadialGrid(2001, 50.0, 1.0e-6));
}

void CalangoDFTEngine::setParameters(Parameters parameters)
{
    parameters_ = std::move(parameters);
}

std::vector<std::string> CalangoDFTEngine::unimplementedSteps()
{
    return {
        "symmetrisation of the density, without which point-group folding of "
        "the k-mesh (kSymmetry=2) is inexact by ~0.1 eV; time-reversal "
        "folding is exact and is the default",
        "spin polarisation, which the spherical atomic reference needs before "
        "a cohesive energy can be compared with a published one",
        "the ANALYTIC force: the Hellmann-Feynman term and the overlap part "
        "of Pulay are implemented, the Hamiltonian part of Pulay and the "
        "integration-grid weight derivatives are not. Forces and geometry "
        "relaxation therefore run on finite differences of the energy, which "
        "are correct but cost 6N self-consistency cycles per step",
        "cell relaxation: the stress tensor is computed, nothing drives the "
        "lattice with it yet",
        "hybrid and meta-GGA functionals; the menu is LDA and PBE",
        "an externally validated total energy for PERIODIC systems: the "
        "electron count, self-consistency and band structure are checked, the "
        "absolute energy is not",
    };
}

CalangoDFTEngine::Result CalangoDFTEngine::run(const core::Structure& structure)
{
    Result result;
    if (structure.empty()) {
        result.outcome = Outcome::invalid("the structure has no atoms");
        return result;
    }

    std::set<int> speciesPresent;
    for (const core::Atom& atom : structure.atoms()) {
        if (atom.atomicNumber < 1 || atom.atomicNumber > 118) {
            result.outcome = Outcome::invalid(
                "the structure contains an atom whose atomic number is outside "
                "1-118");
            return result;
        }
        speciesPresent.insert(atom.atomicNumber);
    }
    if (parameters_.maxIterations < 1) {
        result.outcome =
            Outcome::invalid("the iteration limit must be at least 1");
        return result;
    }
    if (!(parameters_.confinementRadiusA > 0.0)) {
        result.outcome =
            Outcome::invalid("the confinement radius must be positive");
        return result;
    }
    if (!Xc::supports(parameters_.xc)) {
        result.outcome = Outcome::notImplemented(
            std::string("no implementation of ") + toString(parameters_.xc));
        return result;
    }

    const bool periodic = structure.cell().isDefined();
    result.log.push_back("Calango DFT: all-electron, numerical atomic orbitals");
    result.log.push_back(std::string("exchange-correlation: ")
                         + toString(parameters_.xc));
    if (Xc::needsGradients(parameters_.xc))
        result.log.push_back(
            "gradient functional: the basis and the free-atom reference come "
            "from an LDA-PW92 atom (the radial solver is LDA only); the "
            "crystal is solved self-consistently in the functional above");
    result.log.push_back(std::string(periodic ? "periodic" : "finite")
                         + " system, " + std::to_string(structure.size())
                         + " atoms, " + std::to_string(speciesPresent.size())
                         + " species");

    // --- 1. Basis ---------------------------------------------------------
    std::vector<Species> speciesList;
    for (const int z : speciesPresent) {
        Species species;
        species.atomicNumber = z;
        species.referenceElectrons = z;
        speciesList.push_back(species);
    }
    const Outcome basisOutcome =
        basis_.generate(speciesList, parameters_, parameters_.basisTiers);
    if (!basisOutcome.ok()) {
        result.outcome = basisOutcome;
        return result;
    }
    for (const int z : speciesPresent) {
        const SpeciesBasis* species = basis_.forSpecies(z);
        if (species != nullptr) {
            std::string labels;
            for (const RadialFunction& function : species->functions)
                labels += (labels.empty() ? std::string() : " ") + function.label;
            result.log.push_back(
                "Z=" + std::to_string(z) + ", tier "
                + std::to_string(parameters_.basisTiers) + ": "
                + std::to_string(species->functionCount())
                + " basis functions [" + labels + "], cutoff "
                + formatted("%.2f bohr", species->maxCutoffBohr()));
        }
    }

    // --- 2. Geometry and integration grid ---------------------------------
    std::vector<HamiltonianAssembler::Atom> atoms;
    std::vector<IntegrationGrid::Atom> gridAtoms;
    double totalElectrons = 0.0;
    for (const core::Atom& atom : structure.atoms()) {
        std::array<double, 3> position{
            {atom.position.x * kBohrPerAngstrom,
             atom.position.y * kBohrPerAngstrom,
             atom.position.z * kBohrPerAngstrom}};
        atoms.push_back({atom.atomicNumber, position});
        gridAtoms.push_back({atom.atomicNumber, position});
        totalElectrons += atom.atomicNumber;
    }
    std::vector<std::array<double, 3>> lattice;
    if (periodic) {
        for (const core::Vec3& v : structure.cell().vectors())
            lattice.push_back({{v.x * kBohrPerAngstrom, v.y * kBohrPerAngstrom,
                                v.z * kBohrPerAngstrom}});
    }

    // The grid reaches one confinement radius past the furthest basis
    // function, so that every point where any orbital is nonzero carries
    // quadrature weight.
    double cutoff = 0.0;
    for (const int z : speciesPresent) {
        const SpeciesBasis* species = basis_.forSpecies(z);
        if (species != nullptr)
            cutoff = std::max(cutoff, species->maxCutoffBohr());
    }
    IntegrationGrid grid;
    const Outcome gridOutcome =
        grid.build(gridAtoms, lattice, parameters_.radialShells,
                   parameters_.angularPoints, cutoff * 1.5);
    if (!gridOutcome.ok()) {
        result.outcome = gridOutcome;
        return result;
    }
    result.gridPoints = grid.size();
    result.log.push_back(
        std::to_string(grid.size()) + " integration points ("
        + std::to_string(grid.shellRadii().size()) + " shells x "
        + std::to_string(grid.directions().size()) + " directions x "
        + std::to_string(structure.size()) + " atoms)");

    HamiltonianAssembler assembler(basis_, parameters_);
    const Outcome prepareOutcome = assembler.prepare(atoms, lattice, grid);
    if (!prepareOutcome.ok()) {
        result.outcome = prepareOutcome;
        return result;
    }
    const std::size_t dimension = assembler.dimension();
    result.basisFunctions = dimension;
    if (dimension * 2 < static_cast<std::size_t>(totalElectrons)) {
        result.outcome = Outcome::invalid(
            "the basis has fewer states than there are electrons to put in "
            "them: " + std::to_string(dimension) + " functions for "
            + std::to_string(static_cast<int>(totalElectrons)) + " electrons");
        return result;
    }

    // --- 3. k-points ------------------------------------------------------
    // A Gamma-centred Monkhorst-Pack mesh, reduced only by time reversal
    // (k and -k give the same eigenvalues for a real potential). No point
    // group reduction: it would have to agree with the grid's own symmetry
    // handling, and disagreeing symmetry treatments are a class of bug that
    // shows up as a density with the wrong point group.
    std::vector<std::array<double, 3>> kPoints;
    std::vector<double> kWeights;
    {
        std::vector<KPointGrid::Atom> symmetryAtoms;
        symmetryAtoms.reserve(atoms.size());
        for (const HamiltonianAssembler::Atom& atom : atoms)
            symmetryAtoms.push_back({atom.atomicNumber, atom.position});
        const KPointGrid::Symmetry mode =
            parameters_.kSymmetry >= 2 ? KPointGrid::Symmetry::PointGroup
            : parameters_.kSymmetry == 1 ? KPointGrid::Symmetry::TimeReversal
                                         : KPointGrid::Symmetry::None;
        KPointGrid mesh;
        const Outcome meshOutcome =
            mesh.build(parameters_.kGrid, lattice, symmetryAtoms, mode);
        if (!meshOutcome.ok()) {
            result.outcome = meshOutcome;
            return result;
        }
        for (const KPoint& point : mesh.points()) {
            kPoints.push_back(point.fractional);
            kWeights.push_back(point.weight);
        }
        if (periodic && mode == KPointGrid::Symmetry::PointGroup)
            result.log.push_back(
                "CAUTION: point-group folding of the k-mesh is NOT exact for "
                "the density. Summing w_k|psi_k|^2 over a wedge is not the "
                "full-zone sum, because orbit members contribute "
                "|psi_k(R^-1 r)|^2; the density must be symmetrised and is "
                "not. Measured on silicon at 4x4x4: 0.09 eV per cell, and the "
                "gap moves by 0.2 eV. Exact for eigenvalues, so it is usable "
                "for a band structure on a converged density; kSymmetry=1 "
                "(time reversal) is exact for the SCF and is the default.");
        if (periodic)
            result.log.push_back(
                "Brillouin zone: " + std::to_string(mesh.size()) + " of "
                + std::to_string(mesh.fullMeshSize()) + " mesh points, "
                + std::to_string(mesh.crystalOperationCount())
                + " crystal point-group operation(s), "
                + std::to_string(mesh.operations().size())
                + " compatible with this mesh");
    }

    result.log.push_back(std::to_string(kPoints.size()) + " k-point(s), "
                         + std::to_string(dimension) + " basis functions");

    // --- 4. Self-consistency ---------------------------------------------
    std::vector<double> density = assembler.superposedAtomicDensity();
    std::vector<double> densityGradientStart =
        assembler.superposedAtomicDensityGradient();
    const double startingElectrons = grid.integrate(density);
    result.log.push_back(formatted(
        "superposed free atoms integrate to %.6f electrons (expected %.1f)",
        startingElectrons, totalElectrons));

    DensityMixer mixer(parameters_);
    std::vector<double> densityGradient = densityGradientStart;
    std::vector<double> effective;
    std::vector<double> gradientField;
    PotentialEnergies inputEnergies;
    std::vector<std::complex<double>> overlap;
    std::vector<std::complex<double>> kinetic;
    std::vector<std::complex<double>> potentialMatrix;
    std::vector<std::complex<double>> hamiltonian;
    double previousEnergy = 0.0;
    bool converged = false;

    for (int iteration = 1; iteration <= parameters_.maxIterations;
         ++iteration) {
        result.scfIterations = iteration;
        const Outcome potentialOutcome = assembler.buildEffectivePotential(
            density, densityGradient, effective, gradientField, inputEnergies);
        if (!potentialOutcome.ok()) {
            result.outcome = potentialOutcome;
            return result;
        }

        // --- Diagonalise at every k-point ---------------------------------
        std::vector<std::vector<std::complex<double>>> vectors(kPoints.size());
        std::vector<std::vector<double>> values(kPoints.size());
        std::size_t discardedTotal = 0;
        for (std::size_t k = 0; k < kPoints.size(); ++k) {
            const Outcome kineticOutcome =
                assembler.buildOverlapAndKinetic(kPoints[k], overlap, kinetic);
            if (!kineticOutcome.ok()) {
                result.outcome = kineticOutcome;
                return result;
            }
            const Outcome matrixOutcome = assembler.buildPotentialMatrix(
                effective, gradientField, kPoints[k], potentialMatrix);
            if (!matrixOutcome.ok()) {
                result.outcome = matrixOutcome;
                return result;
            }
            hamiltonian.assign(dimension * dimension, {});
            for (std::size_t i = 0; i < hamiltonian.size(); ++i)
                hamiltonian[i] = kinetic[i] + potentialMatrix[i];
            std::size_t discarded = 0;
            const Outcome eigenOutcome = linalg::solveGeneralizedHermitian(
                hamiltonian, overlap, dimension, values[k], vectors[k],
                &discarded);
            if (!eigenOutcome.ok()) {
                result.outcome = eigenOutcome;
                return result;
            }
            discardedTotal += discarded;
        }
        if (discardedTotal > 0 && iteration == 1)
            result.log.push_back(
                std::to_string(discardedTotal)
                + " near-linearly-dependent basis direction(s) removed by "
                  "canonical orthogonalisation");

        // --- Occupations: aufbau over every state at every k ---------------
        // Sorted globally rather than per k-point, which is what makes a
        // metal's Fermi level come out right — filling each k-point to the
        // same band index would put electrons above E_F at one k and holes
        // below it at another.
        struct State {
            std::size_t k;
            std::size_t band;
            double energy;
        };
        std::vector<State> states;
        for (std::size_t k = 0; k < values.size(); ++k)
            for (std::size_t b = 0; b < values[k].size(); ++b)
                states.push_back({k, b, values[k][b]});
        std::sort(states.begin(), states.end(),
                  [](const State& a, const State& b) {
                      return a.energy < b.energy;
                  });
        if (states.empty()) {
            result.outcome = {Status::NumericalFailure,
                              "the eigensolver returned no states"};
            return result;
        }

        // Fermi level by bisection on the electron count. A Fermi
        // distribution rather than a hard fill because degenerate levels have
        // to share: silicon's 3p² is two electrons in a threefold degenerate
        // level, and giving both to one arbitrarily chosen eigenvector makes
        // the density depend on which vector the eigensolver returned first.
        const double width =
            std::max(parameters_.smearingWidthEv / kHartreeToEv, 1.0e-8);
        double low = states.front().energy - 10.0;
        double high = states.back().energy + 10.0;
        const auto electronsAt = [&states, &kWeights, width](double mu) {
            double count = 0.0;
            for (const State& state : states) {
                const double x = (state.energy - mu) / width;
                const double f = x > 40.0    ? 0.0
                    : x < -40.0              ? 1.0
                                             : 1.0 / (1.0 + std::exp(x));
                count += 2.0 * kWeights[state.k] * f;
            }
            return count;
        };
        for (int step = 0; step < 200; ++step) {
            const double middle = 0.5 * (low + high);
            if (electronsAt(middle) < totalElectrons)
                low = middle;
            else
                high = middle;
        }
        const double fermi = 0.5 * (low + high);

        std::vector<std::vector<double>> occupations(kPoints.size());
        for (std::size_t k = 0; k < kPoints.size(); ++k)
            occupations[k].assign(values[k].size(), 0.0);
        double bandEnergy = 0.0;
        double homo = -1.0e300;
        double lumo = 1.0e300;
        bool haveLumo = false;
        for (const State& state : states) {
            const double x = (state.energy - fermi) / width;
            const double f = x > 40.0 ? 0.0
                : x < -40.0           ? 1.0
                                      : 1.0 / (1.0 + std::exp(x));
            // Two electrons per state, weighted by the k-point: this is a
            // spin-degenerate calculation, and spin polarisation is on the
            // unimplemented list rather than silently assumed away.
            const double filled = 2.0 * kWeights[state.k] * f;
            occupations[state.k][state.band] = filled;
            bandEnergy += filled * state.energy;
            // HOMO and LUMO are read off the occupations rather than off a
            // band index, so a partly filled level reports as what it is.
            if (f > 0.5)
                homo = std::max(homo, state.energy);
            else {
                lumo = std::min(lumo, state.energy);
                haveLumo = true;
            }
        }
        if (homo == -1.0e300)
            homo = states.front().energy;

        // --- New density ---------------------------------------------------
        std::vector<double> newDensity;
        std::vector<double> newGradient;
        const Outcome densityOutcome = assembler.buildDensity(
            vectors, occupations, kPoints, newDensity,
            assembler.hasGradients() ? &newGradient : nullptr);
        if (!densityOutcome.ok()) {
            result.outcome = densityOutcome;
            return result;
        }

        // --- Energies ------------------------------------------------------
        // During the loop the electrostatic and exchange-correlation energies
        // come from the INPUT density — the Harris-Foulkes functional — and
        // only the trace uses the output. That is not an approximation to the
        // answer: after convergence the exact Kohn-Sham functional is
        // evaluated once on the converged density, below. It halves the work,
        // because building the electrostatic potential is the most expensive
        // step in an iteration and the loop would otherwise do it twice.
        const PotentialEnergies& outputEnergies = inputEnergies;
        // T_s straight from the kinetic matrix, not as the band energy minus
        // a potential trace. The trace form assumes v_eff is a local
        // multiplier, which a gradient functional's is not — its matrix
        // element carries a ∇φ term that no ∫ρv can reproduce. Computing the
        // expectation value of T̂ directly is exact for both families.
        double kineticEnergy = 0.0;
        for (std::size_t k = 0; k < kPoints.size(); ++k) {
            const Outcome kineticOutcome =
                assembler.buildOverlapAndKinetic(kPoints[k], overlap, kinetic);
            if (!kineticOutcome.ok()) {
                result.outcome = kineticOutcome;
                return result;
            }
            const std::size_t bands = occupations[k].size();
            for (std::size_t b = 0; b < bands; ++b) {
                if (occupations[k][b] == 0.0)
                    continue;
                std::complex<double> sum{};
                for (std::size_t i = 0; i < dimension; ++i)
                    for (std::size_t j = 0; j < dimension; ++j)
                        sum += std::conj(vectors[k][i * bands + b])
                            * kinetic[i * dimension + j]
                            * vectors[k][j * bands + b];
                kineticEnergy += occupations[k][b] * sum.real();
            }
        }
        const double total = kineticEnergy + outputEnergies.electrostatic
            + outputEnergies.exchangeCorrelation;

        const double residual =
            DensityMixer::residualNorm(density, newDensity, [&grid] {
                std::vector<double> w(grid.size(), 0.0);
                for (std::size_t g = 0; g < grid.size(); ++g)
                    w[g] = grid.points()[g].weight;
                return w;
            }());
        const double change = std::abs(total - previousEnergy);

        result.energy.total = total * kHartreeToEv;
        result.energy.kinetic = kineticEnergy * kHartreeToEv;
        result.energy.electrostatic =
            outputEnergies.electrostatic * kHartreeToEv;
        result.energy.exchangeCorrelation =
            outputEnergies.exchangeCorrelation * kHartreeToEv;
        result.energy.bandStructure = bandEnergy * kHartreeToEv;
        result.finalResidual = residual;
        result.integratedElectrons = grid.integrate(newDensity);
        result.homoEv = homo * kHartreeToEv;
        result.lumoEv = haveLumo ? lumo * kHartreeToEv : 0.0;
        result.gapEv = haveLumo ? (lumo - homo) * kHartreeToEv : 0.0;
        result.bands.clear();
        for (std::size_t k = 0; k < kPoints.size(); ++k) {
            BandStructure band;
            band.kFractional = kPoints[k];
            band.weight = kWeights[k];
            band.eigenvalues = values[k];
            for (double& value : band.eigenvalues)
                value *= kHartreeToEv;
            band.occupations = occupations[k];
            result.bands.push_back(std::move(band));
        }
        result.log.push_back(formatted(
            "iteration %.0f: E = %.8f eV, density residual %.3e electrons",
            static_cast<double>(iteration), total * kHartreeToEv, residual));

        if (iteration > 1
            && residual < parameters_.densityToleranceElectrons
            && change * kHartreeToEv < parameters_.energyToleranceEv) {
            converged = true;
            // The exact Kohn-Sham energy on the converged density, once.
            // Inside the loop the electrostatics is evaluated on the input
            // density; here it is evaluated on the output, which is what the
            // functional actually asks for. The two differ by less than the
            // convergence threshold by definition of having converged — this
            // is a matter of reporting the quantity that was claimed, not of
            // changing the answer.
            std::vector<double> finalPotential;
            std::vector<double> finalField;
            PotentialEnergies finalEnergies;
            const Outcome finalOutcome = assembler.buildEffectivePotential(
                newDensity, newGradient, finalPotential, finalField,
                finalEnergies);
            if (!finalOutcome.ok()) {
                result.outcome = finalOutcome;
                return result;
            }
            const double finalKinetic = kineticEnergy;
            const double finalTotal = finalKinetic + finalEnergies.electrostatic
                + finalEnergies.exchangeCorrelation;
            result.energy.total = finalTotal * kHartreeToEv;
            result.energy.kinetic = finalKinetic * kHartreeToEv;
            result.energy.electrostatic =
                finalEnergies.electrostatic * kHartreeToEv;
            result.energy.exchangeCorrelation =
                finalEnergies.exchangeCorrelation * kHartreeToEv;
            // The condition under which the truncated multipole sum is exact,
            // reported rather than assumed. A crystal whose atoms are all
            // equivalent has no net charge on any of them by symmetry; one
            // that does needs an Ewald sum this engine has not been given.
            // The analytic force decomposition, on the converged density.
            if (parameters_.computeForces) {
                std::vector<std::array<double, 3>> hf;
                const Outcome hfOutcome =
                    assembler.hellmannFeynmanForces(newDensity, hf);
                if (hfOutcome.ok()) {
                    std::vector<std::vector<double>> bandEnergies(
                        kPoints.size());
                    for (std::size_t k = 0; k < kPoints.size(); ++k)
                        bandEnergies[k] = values[k];
                    std::vector<std::array<double, 3>> pulay;
                    const Outcome pulayOutcome = assembler.pulayOverlapForces(
                        vectors, occupations, bandEnergies, kPoints, pulay);
                    if (pulayOutcome.ok()) {
                        // hartree/bohr → eV/Å.
                        constexpr double kConvert =
                            kHartreeToEv * kBohrPerAngstrom;
                        result.hellmannFeynman.assign(hf.size(),
                                                      {{0.0, 0.0, 0.0}});
                        result.pulayForce.assign(pulay.size(),
                                                 {{0.0, 0.0, 0.0}});
                        for (std::size_t a = 0; a < hf.size(); ++a)
                            for (int c = 0; c < 3; ++c) {
                                const auto o = static_cast<std::size_t>(c);
                                result.hellmannFeynman[a][o] =
                                    hf[a][o] * kConvert;
                                result.pulayForce[a][o] =
                                    pulay[a][o] * kConvert;
                            }
                    }
                }
            }
            if (finalEnergies.largestAtomicMonopole > 1.0e-3)
                result.log.push_back(formatted(
                    "CAUTION: an atom carries %.4f electrons of net charge in "
                    "the difference density; the electrostatics truncates the "
                    "monopole tail and is exact only while this is negligible",
                    finalEnergies.largestAtomicMonopole));
            break;
        }
        previousEnergy = total;
        density = mixer.mix(density, newDensity);
        densityGradient = newGradient;
    }

    result.outcome = converged
        ? Outcome::success()
        : Outcome{Status::NotConverged,
                  "the SCF reached " + std::to_string(result.scfIterations)
                      + " iterations with a density residual of "
                      + std::to_string(result.finalResidual) + " electrons"};
    return result;
}

} // namespace calango::dft
