#pragma once

#include "core/Structure.hpp"
#include "dft/DftTypes.hpp"
#include "dftb/DftbBasis.hpp"
#include "dftb/DftbHamiltonian.hpp"
#include "dftb/SlaterKosterTable.hpp"

#include <array>
#include <complex>
#include <vector>

/// The self-consistency loop: non-SCC (DFTB0, one shot) when
/// `settings.sccEnabled` is false, SCC-DFTB (DFTB2) when true — see
/// DftbGamma.hpp for the second-order term this iterates on.
///
/// SCOPE: non-spin-polarized (each eigenstate holds up to 2 electrons,
/// spin-up and spin-down always paired) — spin-polarized DFTB is FUTURE.md.
namespace calango::dftb {

/// One Brillouin-zone sampling point: fractional coordinates + BZ weight
/// (weights across the whole set must sum to 1 — a molecule's single
/// Gamma point, weight 1, is the degenerate case).
struct DftbKPoint {
    std::array<double, 3> fractional{{0.0, 0.0, 0.0}};
    double weight = 1.0;
};

struct DftbScfSettings {
    bool sccEnabled = true;
    double sccToleranceElectrons = 1.0e-5; ///< max |dQ change| between iterations
    int maxSccIterations = 100;
    /// Fermi filling temperature. A tiny internal floor (1e-6 Hartree, far
    /// below anything physically meaningful) is used even when this is
    /// exactly 0, purely so the Fermi-level bisection never divides by
    /// zero at an exact degeneracy — see CalangoDFTEngine's own
    /// `smearingWidthEv` doc comment for why a hard zero-width aufbau is
    /// not just an edge case but a genuine non-convergence trap.
    double fillingTemperatureHartree = 0.0;
    /// Simple linear-mixing fraction of the new charges. Also the mixing
    /// used for the FIRST SCC iteration even when Anderson mixing is on
    /// (there is no history yet to extrapolate from).
    double mixingParameter = 0.2;
    /// Depth-1 Anderson (Pulay-style least-squares) mixing acceleration on
    /// top of the simple mixing above, after the first iteration. Falls
    /// back to simple mixing on any step whose residual history is
    /// numerically degenerate (a near-zero denominator), so it can never
    /// make convergence WORSE than plain linear mixing.
    bool useAndersonMixing = true;
};

struct DftbScfKPointResult {
    std::array<double, 3> fractional{{0.0, 0.0, 0.0}};
    double weight = 1.0;
    std::vector<double> eigenvaluesHartree; ///< ascending
    /// dimension x dimension, row-major, one eigenvector per COLUMN.
    std::vector<std::complex<double>> eigenvectors;
    std::vector<double> occupations; ///< 0..2 per state, this k's own weight
                                      ///< NOT yet folded in (kWeight is
                                      ///< separate — see occupations2() note)
};

struct DftbScfResult {
    bool converged = false;
    int iterations = 0;
    double maxChargeResidual = 0.0;
    std::vector<double> deltaQ; ///< converged Mulliken charge fluctuations
    double fermiEnergyHartree = 0.0;

    double bandStructureEnergyHartree = 0.0; ///< sum f_i * eps_i
    /// -0.5 * sum_AB gamma_AB * dQ_A * dQ_B — the SCC double-counting
    /// correction (zero for a non-SCC run).
    double coulombEnergyHartree = 0.0;
    double repulsiveEnergyHartree = 0.0;
    double totalEnergyHartree = 0.0;

    std::vector<DftbScfKPointResult> kpoints;
};

class DftbScf {
public:
    dft::Outcome run(const core::Structure& structure,
                      const SlaterKosterTable& table, const DftbBasis& basis,
                      const DftbHamiltonianBuilder& hamiltonian,
                      const std::vector<DftbKPoint>& kpoints,
                      const DftbScfSettings& settings, DftbScfResult& out);
};

} // namespace calango::dftb
