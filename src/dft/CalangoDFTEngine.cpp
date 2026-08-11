#include "dft/CalangoDFTEngine.hpp"

#include "core/Structure.hpp"

#include <set>

namespace calango::dft {

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
    case XcFunctional::LdaPz:  break;
    }
    return "LDA-PZ";
}

CalangoDFTEngine::CalangoDFTEngine(Parameters parameters)
    : parameters_(std::move(parameters))
{
    basis_.setGrid(RadialGrid(parameters_.radialShells > 0
                                  ? static_cast<std::size_t>(
                                        parameters_.radialShells * 8)
                                  : 400));
}

void CalangoDFTEngine::setParameters(Parameters parameters)
{
    parameters_ = std::move(parameters);
}

std::vector<std::string> CalangoDFTEngine::unimplementedSteps()
{
    return {
        "numerical atomic-orbital basis generation (free-atom radial solve "
        "plus confinement)",
        "the multicentre integration grid (atom-centred radial x Lebedev "
        "spheres with a smooth nuclear partition)",
        "overlap and Hamiltonian assembly on that grid",
        "the generalised eigenproblem and orbital occupations",
        "the exchange-correlation potential and energy",
    };
}

CalangoDFTEngine::Result CalangoDFTEngine::run(const core::Structure& structure)
{
    Result result;
    if (structure.empty()) {
        result.outcome = Outcome::invalid("the structure has no atoms");
        return result;
    }

    // Validated first, refused second. These checks are the ones a working
    // engine would still make, so they are worth having now: they are what a
    // caller writes its error handling against, and they cannot be added later
    // without changing behaviour somebody has come to rely on.
    std::set<int> species;
    for (const core::Atom& atom : structure.atoms()) {
        if (atom.atomicNumber < 1 || atom.atomicNumber > 118) {
            result.outcome =
                Outcome::invalid("the structure contains an atom whose atomic "
                                 "number is outside 1-118");
            return result;
        }
        species.insert(atom.atomicNumber);
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

    result.log.push_back(
        "Calango DFT: all-electron, numerical atomic orbitals");
    result.log.push_back(std::string("exchange-correlation: ")
                         + toString(parameters_.xc));
    result.log.push_back("species: " + std::to_string(species.size())
                         + ", atoms: " + std::to_string(structure.size()));

    // Reported BEFORE any partial work. A pipeline that generated a grid, then
    // failed at assembly, would leave a half-populated Result that reads like
    // a calculation which got most of the way — and the one thing this engine
    // must never do is look like it computed something it did not.
    std::string missing;
    for (const std::string& step : unimplementedSteps())
        missing += "\n  - " + step;
    result.outcome = Outcome::notImplemented(
        "the native DFT engine is a scaffold: its interfaces and its "
        "self-contained numerics (radial mesh, quadrature, radial Poisson, "
        "density mixing) are implemented and tested, but the following are "
        "not, so no energy is produced:" + missing);
    return result;
}

} // namespace calango::dft
