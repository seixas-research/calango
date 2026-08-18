#pragma once

#include <string>
#include <vector>

namespace calango::core {

/// Extended Generalized Quasichemical Approximation (EGQCA) for a binary
/// solid solution A(1-x)Bx.
///
/// Working theory: P.N. Ferreira, R. Lucrezi, I. Guilhon, M. Marques,
/// L.K. Teles, C. Heil, L.T.F. Eleno, "Ab initio modeling of superconducting
/// alloys", Materials Today Physics 48 (2024) 101547,
/// https://doi.org/10.1016/j.mtphys.2024.101547 — Section 2, "Extended
/// Generalized Quasichemical Approximation". Equation numbers in the
/// comments below (Eq. N) are that paper's own, so this implementation can
/// be audited against it directly.
///
/// The model (Sec. 2, "The GQCA Model"): the alloy is represented as an
/// ensemble of M small, statistically and energetically independent
/// supercells, each falling into one of J non-equivalent classes by
/// symmetry. Cluster j has total energy E_j, degeneracy g_j, and n_j atoms
/// of type B out of n sites. At composition x and temperature T, the
/// occurrence probability p_j of each class is the one that minimizes the
/// Gibbs mixing free energy — solved here via the closed-form Lagrange-
/// multiplier construction the paper derives (Eq. 13-15), not by iteration.
///
/// "Extended" over the original GQCA (Sec. 2, "Inclusion of vibrational
/// effects", Eq. 9-12): each cluster MAY carry a phonon density of states,
/// whose harmonic vibrational free energy (reused directly from
/// core::computePhononThermodynamics — Eq. 12 IS that function's own F(T)
/// integral) enters ΔG as ΔA, a genuinely temperature-dependent correction
/// beyond the enthalpy/entropy of plain GQCA. Omitting it on every cluster
/// reduces exactly to the original GQCA.
struct EgqcaCluster {
    /// n_j: number of B atoms in this cluster, out of
    /// EgqcaInput::sitesPerCluster total sites.
    int bAtomCount = 0;
    /// g_j (Eq. 6, 7, 13): the symmetry degeneracy of this class — how many
    /// equivalent decorations of the supercell it represents.
    int degeneracy = 1;
    /// E_j: total DFT energy of this cluster's supercell (eV). Used in
    /// H_j = E_j + P V_j (Eq. 4).
    double energyEv = 0.0;
    /// V_j (Eq. 4): unit-cell volume of this cluster's supercell (Å^3).
    /// Only read when EgqcaInput::pressureEv3 is nonzero.
    double volumeAng3 = 0.0;
    /// The paper's own cluster names ("S7", "S12", ...) for reporting.
    std::string label;

    /// Optional phonon DOS for the vibrational term (Eq. 9-12): frequency
    /// in cm^-1 (negative = imaginary, excluded — see
    /// core::computePhononThermodynamics) and states per cm^-1. Empty means
    /// this cluster's vibrational contribution is zero; see
    /// EgqcaResult::vibrationalAvailable for whether ΔA is real physics or
    /// silently absent across the whole ensemble.
    std::vector<double> phononFrequenciesCm;
    std::vector<double> phononDos;

    /// Any additional computationally-accessible property of this cluster
    /// (Eq. 17-18, "Composition-dependent properties") — a lattice
    /// parameter, a superconducting T_c, the electron-phonon coupling λ,
    /// anything ab initio or measured per cluster. See
    /// EgqcaInput::propertyName and EgqcaResult::propertyAvailable.
    double property = 0.0;
    bool hasProperty = false;
};

struct EgqcaInput {
    std::vector<EgqcaCluster> clusters;
    /// n (Eq. 4, 6, 7, 13-15): total sites per cluster/supercell — the same
    /// for every cluster, since they are all decorations of ONE supercell
    /// size. Every EgqcaCluster::bAtomCount must be in [0, sitesPerCluster].
    int sitesPerCluster = 0;

    /// H_A, H_B (Eq. 4): pure-element reference enthalpies, eV/atom.
    double referenceEnthalpyA = 0.0;
    double referenceEnthalpyB = 0.0;
    /// P (Eq. 4): pressure, eV/Å^3. Zero (the default) makes H_j = E_j —
    /// the zero-pressure case every worked example in the paper uses.
    double pressureEv3 = 0.0;

    /// Composition grid, x in A(1-x)Bx. Endpoints x=0/x=1 are handled as a
    /// deterministic pure-element limit rather than solved (Eq. 15 is
    /// singular there), so the range MAY safely include them.
    double minComposition = 0.02;
    double maxComposition = 0.98;
    int compositionSteps = 49;
    /// Temperature grid, K.
    double minTemperatureK = 100.0;
    double maxTemperatureK = 1400.0;
    int temperatureSteps = 27;

    /// Label for EgqcaCluster::property (e.g. "Tc (K)", "a (Angstrom)"),
    /// carried through to EgqcaResult purely for display.
    std::string propertyName;

    /// Root-solve controls for Eq. 15 (bisection on the unique positive
    /// root the paper proves exists).
    int maxIterations = 200;
    double tolerance = 1e-12;
};

/// State of the solid solution at one (x, T) grid point.
struct EgqcaPoint {
    double composition = 0.0;
    double temperatureK = 0.0;

    /// p_j(x, T) (Eq. 13), one per EgqcaInput::clusters, same order.
    std::vector<double> clusterProbabilities;

    /// Per-cluster quantities (Eq. 3, 8, 10, 9 each divided by M — the
    /// paper's own "meV/atom/cluster" convention, since M is an arbitrary
    /// macroscopic total that cancels out of every intensive quantity):
    double mixingEnthalpyEv = 0.0;        ///< Delta H / M (Eq. 3)
    double mixingEntropyKb = 0.0;         ///< (Delta S / M) / k_B, from Eq. 8
    double vibrationalFreeEnergyEv = 0.0; ///< Delta A / M (Eq. 10); 0 if unavailable
    double mixingFreeEnergyEv = 0.0;      ///< Delta G / M (Eq. 9)

    /// Kullback-Leibler divergence from the ideal (regular) solid solution
    /// (Eq. 16) — -> 0 means p_j has converged to the random-mixing p_j^0.
    double klDivergence = 0.0;

    /// Weighted average of EgqcaCluster::property (Eq. 17) and its standard
    /// deviation (Eq. 18). Meaningful only when EgqcaResult::propertyAvailable.
    double propertyValue = 0.0;
    double propertyUncertainty = 0.0;

    bool converged = false;
    int iterations = 0;
};

struct EgqcaResult {
    bool ok = false;
    std::string note; ///< failure reason, empty on success

    /// composition-major: point (i, t) is points[i * temperatureSteps + t].
    std::vector<EgqcaPoint> points;
    int compositionSteps = 0;
    int temperatureSteps = 0;

    /// True when every cluster carried a phonon DOS — Delta A is real
    /// physics throughout the grid, not silently zero because one cluster
    /// (routinely a dynamically unstable end-member) had none.
    bool vibrationalAvailable = false;
    /// True when every cluster carried a property value.
    bool propertyAvailable = false;
    std::string propertyName;

    std::vector<std::string> warnings;
};

/// Solve the (E)GQCA free-energy minimization over the (x, T) grid.
EgqcaResult solveEgqca(const EgqcaInput& input);

/// p_j^0(x) (Eq. 7): the cluster probability of a random, regular solid
/// solution — g_j x^{n_j} (1-x)^{n-n_j}. Exposed on its own because it is
/// the baseline every EGQCA result is compared against (Eq. 16).
double egqcaIdealClusterProbability(int degeneracy, int bAtomCount,
                                    int sitesPerCluster, double composition);

} // namespace calango::core
