#pragma once

#include "dft/DftTypes.hpp"
#include "dft/HamiltonianAssembler.hpp"
#include "dft/NAOBasisSet.hpp"

#include <array>
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
/// The order of operations is fixed by the physics:
///
///   1. Solve every species' free atom on a radial mesh, then solve it again
///      inside a sphere. The free solution gives the starting density and the
///      neutral-atom potential; the confined one gives the basis.
///   2. Build the multicentre integration grid — atom-centred radial shells
///      times angular points, partitioned smoothly between atoms and their
///      periodic images.
///   3. Superpose free-atom densities as the initial guess. It is a good one:
///      most of the density of a solid IS its atoms, and the difference is
///      what the SCF has to find.
///   4. Iterate: density → electrostatic potential (neutral-atom reference
///      plus the multipole field of the difference) → exchange-correlation
///      potential → H(k) and S(k) → generalised eigenproblem → occupations →
///      new density.
///   5. Report the energy decomposition.
///
/// What is VALIDATED, and against what, because an all-electron total energy
/// that has not been checked against something is a number with no standing:
///
///   * the radial solver, against the analytic hydrogenic eigenvalues
///     (`dft_atomic_solver`) and against GPAW's own all-electron atom;
///   * the angular quadratures, against the exact orthonormality of the
///     spherical harmonics (`dft_grids`);
///   * the eigensolver, against LAPACK and against analytic spectra
///     (`dft_linalg`);
///   * the whole three-dimensional pipeline, by running a single ATOM through
///     it and requiring the answer the one-dimensional solver already gives
///     (`dft_engine`).
///
/// What is NOT independently validated is the absolute total energy of a
/// PERIODIC system: there is no all-electron reference for it in this
/// project's toolchain, and a pseudopotential code's total energy is not
/// comparable. The periodic path is checked on the quantities that can be
/// checked — the integrated electron count, self-consistency, the band gap
/// and the invariance of the answer under translating the cell — and the
/// energy it reports is labelled accordingly.
class CalangoDFTEngine {
public:
    explicit CalangoDFTEngine(Parameters parameters = {});

    struct BandStructure {
        std::array<double, 3> kFractional{{0.0, 0.0, 0.0}};
        double weight = 0.0;
        std::vector<double> eigenvalues; ///< hartree, ascending
        std::vector<double> occupations;
    };

    struct Result {
        Outcome outcome;
        EnergyBreakdown energy;
        int scfIterations = 0;
        double finalResidual = 0.0;
        /// Electrons found by integrating the converged density over the
        /// grid. The single sharpest check on the whole machinery: it must
        /// equal the number of electrons put in, and any deviation is a grid
        /// or basis failure that would otherwise show up only as a wrong
        /// energy.
        double integratedElectrons = 0.0;
        double homoEv = 0.0;
        double lumoEv = 0.0;
        double gapEv = 0.0;
        std::size_t basisFunctions = 0;
        std::size_t gridPoints = 0;
        std::vector<BandStructure> bands;
        /// The electrostatic force on each bare nucleus, eV/Å. Filled only
        /// when Parameters::computeForces is set AND the system is finite.
        std::vector<std::array<double, 3>> hellmannFeynman;
        /// The overlap part of the Pulay force, eV/Å — the contribution that
        /// exists because the basis moves with the nuclei. PARTIAL: see
        /// ForceCalculator.
        std::vector<std::array<double, 3>> pulayForce;
        /// One line per iteration, for the log and the convergence plot.
        std::vector<std::string> log;
    };

    /// Run a ground-state calculation. A structure with a cell is treated as
    /// periodic, one without as a finite cluster.
    Result run(const core::Structure& structure);

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
