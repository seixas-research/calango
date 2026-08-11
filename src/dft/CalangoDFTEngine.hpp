#pragma once

#include "dft/DftTypes.hpp"
#include "dft/HamiltonianAssembler.hpp"
#include "dft/NAOBasisSet.hpp"
#include "dft/SCFSolver.hpp"

#include <string>
#include <vector>

namespace calango::core {
class Structure;
}

namespace calango::dft {

/// The engine's public face: hand it a structure and parameters, get energies
/// back.
///
/// Everything else in this module is an implementation detail of this call.
/// The order of operations is fixed by the physics and is worth stating,
/// because it is what the unimplemented pieces have to slot into:
///
///   1. Generate (or load) a numerical atomic-orbital basis for every species
///      present, by solving each free atom self-consistently on a radial mesh
///      and confining the result.
///   2. Build the multicentre integration grid — atom-centred radial shells
///      times Lebedev angular points, partitioned smoothly between atoms.
///   3. Superpose free-atom densities as the initial guess. It is a good one:
///      most of the density of a molecule IS its atoms, and starting from it
///      saves several iterations over starting from nothing.
///   4. Iterate: density → electrostatic potential (radial Poisson per centre
///      plus the multipole field of the rest) → exchange-correlation potential
///      → Hamiltonian → generalised eigenproblem → occupations → new density.
///   5. Report the energy decomposition, having checked it against the sum of
///      occupied eigenvalues.
///
/// STATUS: steps 1, 2 and 4's assembly are not implemented. `run()` therefore
/// reports NotImplemented, and reports it BEFORE doing anything, so nothing
/// can mistake a partial pipeline for a result. What is implemented and
/// exercised by the tests is the radial mesh with its quadrature and Poisson
/// solve (RadialGrid) and the self-consistency loop with Pulay mixing
/// (SCFSolver) — the two pieces that are self-contained enough to be verified
/// against known answers rather than against another calculation.
class CalangoDFTEngine {
public:
    explicit CalangoDFTEngine(Parameters parameters = {});

    struct Result {
        Outcome outcome;
        EnergyBreakdown energy;
        int scfIterations = 0;
        double finalResidual = 0.0;
        /// One line per iteration, for the log and the convergence plot.
        std::vector<std::string> log;
    };

    /// Run a ground-state calculation.
    Result run(const core::Structure& structure);

    /// The basis, exposed so one can be installed directly while generation is
    /// unimplemented — that is what lets the assembler and the SCF loop be
    /// developed against a basis of known provenance.
    NAOBasisSet& basis() { return basis_; }
    const NAOBasisSet& basis() const { return basis_; }

    const Parameters& parameters() const { return parameters_; }
    void setParameters(Parameters parameters);

    /// Everything the engine cannot yet do, as a list a caller can show.
    ///
    /// Deliberately part of the API rather than a comment: a UI that offers
    /// this engine has to be able to say what it will and will not compute,
    /// and reading that off a hard-coded list in the dialog would let the two
    /// drift the moment a piece lands.
    static std::vector<std::string> unimplementedSteps();

private:
    Parameters parameters_;
    NAOBasisSet basis_;
};

} // namespace calango::dft
