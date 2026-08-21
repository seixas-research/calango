#include "dftb/DftbScf.hpp"

#include "dft/LinearAlgebra.hpp"
#include "dftb/DftbGamma.hpp"
#include "dftb/DftbMulliken.hpp"

#include <algorithm>
#include <cmath>

namespace calango::dftb {

namespace {

double fermiDirac(double energyHartree, double fermiEnergyHartree,
                   double temperatureHartree)
{
    const double x = (energyHartree - fermiEnergyHartree) / temperatureHartree;
    if (x > 40.0)
        return 0.0;
    if (x < -40.0)
        return 1.0;
    return 1.0 / (std::exp(x) + 1.0);
}

/// Per-atom Hubbard U for the gamma functional: the highest OCCUPIED
/// shell's U (p if the element has a p shell in this parameter set,
/// otherwise s) — the standard SCC-DFTB convention (the frontier shell is
/// the one whose charge fluctuation the second-order term should respond
/// to).
std::vector<double> hubbardUPerAtom(const DftbBasis& basis,
                                     const SlaterKosterTable& table)
{
    std::vector<double> u(basis.atoms.size(), 0.0);
    for (std::size_t a = 0; a < basis.atoms.size(); ++a) {
        const auto* shells = table.onsite(basis.atoms[a].atomicNumber);
        if (!shells)
            continue;
        u[a] = basis.atoms[a].hasP ? (*shells)[1].hubbardUHartree
                                    : (*shells)[0].hubbardUHartree;
    }
    return u;
}

} // namespace

dft::Outcome DftbScf::run(const core::Structure& structure,
                           const SlaterKosterTable& table,
                           const DftbBasis& basis,
                           const DftbHamiltonianBuilder& hamiltonian,
                           const std::vector<DftbKPoint>& kpoints,
                           const DftbScfSettings& settings,
                           DftbScfResult& out)
{
    out = DftbScfResult{};
    if (kpoints.empty())
        return dft::Outcome::invalid("at least one k-point is required "
                                      "(Gamma, weight 1, for a molecule)");
    double weightSum = 0.0;
    for (const auto& kp : kpoints)
        weightSum += kp.weight;
    if (std::fabs(weightSum - 1.0) > 1.0e-6)
        return dft::Outcome::invalid("k-point weights must sum to 1");

    const double targetElectrons = totalValenceElectrons(basis, table);
    if (targetElectrons <= 0.0)
        return dft::Outcome::invalid(
            "the structure has no valence electrons according to the "
            "loaded parameter set");

    const double temperature =
        std::max(settings.fillingTemperatureHartree, 1.0e-6);
    const auto hubbardU = hubbardUPerAtom(basis, table);

    DftbEwaldSum ewald;
    if (settings.sccEnabled) {
        const auto ewaldOutcome = ewald.build(structure, hubbardU);
        if (!ewaldOutcome.ok())
            return ewaldOutcome;
    }

    const auto natoms = basis.atoms.size();
    std::vector<double> deltaQ(natoms, 0.0);
    std::vector<double> deltaQPrev, residualPrev; // Anderson history (depth 1)

    const int maxIterations = settings.sccEnabled ? settings.maxSccIterations : 1;
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        const std::vector<double> shift =
            settings.sccEnabled ? ewald.potentialShift(deltaQ)
                                 : std::vector<double>();

        std::vector<DftbScfKPointResult> kResults;
        kResults.reserve(kpoints.size());
        // Kept alongside kResults (same index) so the Mulliken pass below
        // can reuse each k-point's S(k) instead of rebuilding it.
        std::vector<std::vector<std::complex<double>>> overlapPerK;
        overlapPerK.reserve(kpoints.size());
        double minEigen = 1.0e300, maxEigen = -1.0e300;
        for (const auto& kp : kpoints) {
            std::vector<std::complex<double>> h, s;
            hamiltonian.blochMatrices(kp.fractional, h, s, shift);
            std::vector<double> eigenvalues;
            std::vector<std::complex<double>> eigenvectors;
            const auto eigenOutcome = dft::linalg::solveGeneralizedHermitian(
                h, s, static_cast<std::size_t>(hamiltonian.dimension()),
                eigenvalues, eigenvectors);
            if (!eigenOutcome.ok())
                return eigenOutcome;
            if (!eigenvalues.empty()) {
                minEigen = std::min(minEigen, eigenvalues.front());
                maxEigen = std::max(maxEigen, eigenvalues.back());
            }
            DftbScfKPointResult kr;
            kr.fractional = kp.fractional;
            kr.weight = kp.weight;
            kr.eigenvaluesHartree = std::move(eigenvalues);
            kr.eigenvectors = std::move(eigenvectors);
            kResults.push_back(std::move(kr));
            overlapPerK.push_back(std::move(s));
        }

        // Global Fermi-level bisection: 2 electrons per state (spin-
        // unpolarized), summed over every k-point with its BZ weight.
        const auto electronCount = [&](double ef) {
            double total = 0.0;
            for (const auto& kr : kResults) {
                double perK = 0.0;
                for (double e : kr.eigenvaluesHartree)
                    perK += 2.0 * fermiDirac(e, ef, temperature);
                total += kr.weight * perK;
            }
            return total;
        };
        double lo = minEigen - 1.0, hi = maxEigen + 1.0;
        for (int b = 0; b < 100; ++b) {
            const double mid = 0.5 * (lo + hi);
            if (electronCount(mid) < targetElectrons)
                lo = mid;
            else
                hi = mid;
        }
        const double fermiEnergy = 0.5 * (lo + hi);

        for (auto& kr : kResults) {
            kr.occupations.resize(kr.eigenvaluesHartree.size());
            for (std::size_t i = 0; i < kr.eigenvaluesHartree.size(); ++i)
                kr.occupations[i] =
                    2.0 * fermiDirac(kr.eigenvaluesHartree[i], fermiEnergy,
                                      temperature);
        }

        std::vector<double> population(
            static_cast<std::size_t>(hamiltonian.dimension()), 0.0);
        for (std::size_t k = 0; k < kResults.size(); ++k) {
            const auto& kr = kResults[k];
            std::vector<double> weighted = kr.occupations;
            for (double& o : weighted)
                o *= kr.weight;
            accumulateMullikenPopulation(kr.eigenvectors, overlapPerK[k],
                                          hamiltonian.dimension(), weighted,
                                          population);
        }
        const std::vector<double> deltaQNew =
            mullikenChargeFluctuation(population, basis, table);

        // Always kept up to date with the LATEST iteration's k-point
        // results, converged or not — a caller doing post-processing (or
        // this function's own final energetics below) needs something to
        // work with even from a best-effort, non-converged run.
        out.fermiEnergyHartree = fermiEnergy;
        out.kpoints = std::move(kResults);

        if (!settings.sccEnabled) {
            // DFTB0: a single shot, no mixing/convergence loop — the
            // Mulliken charges above are informational, not iterated.
            out.converged = true;
            out.iterations = 1;
            out.deltaQ = deltaQNew;
            break;
        }

        double maxResidual = 0.0;
        std::vector<double> residual(natoms);
        for (std::size_t a = 0; a < natoms; ++a) {
            residual[a] = deltaQNew[a] - deltaQ[a];
            maxResidual = std::max(maxResidual, std::fabs(residual[a]));
        }
        out.maxChargeResidual = maxResidual;
        out.iterations = iteration + 1;

        if (maxResidual < settings.sccToleranceElectrons) {
            out.converged = true;
            out.deltaQ = deltaQNew;
            break;
        }

        // Mix: simple linear mixing, with depth-1 Anderson acceleration
        // once a one-step history exists.
        std::vector<double> mixed(natoms);
        bool usedAnderson = false;
        if (settings.useAndersonMixing && !residualPrev.empty()) {
            double num = 0.0, den = 0.0;
            for (std::size_t a = 0; a < natoms; ++a) {
                const double dR = residual[a] - residualPrev[a];
                num += residual[a] * dR;
                den += dR * dR;
            }
            if (den > 1.0e-14) {
                const double beta = num / den;
                if (beta > -5.0 && beta < 5.0) { // reject a wild extrapolation
                    for (std::size_t a = 0; a < natoms; ++a) {
                        const double simpleNew =
                            deltaQ[a] + settings.mixingParameter * residual[a];
                        const double simplePrev = deltaQPrev[a]
                            + settings.mixingParameter * residualPrev[a];
                        mixed[a] = (1.0 - beta) * simpleNew + beta * simplePrev;
                    }
                    usedAnderson = true;
                }
            }
        }
        if (!usedAnderson) {
            for (std::size_t a = 0; a < natoms; ++a)
                mixed[a] = deltaQ[a] + settings.mixingParameter * residual[a];
        }

        deltaQPrev = deltaQ;
        residualPrev = residual;
        deltaQ = mixed;
    }

    if (!out.converged && settings.sccEnabled) {
        // Report the best-effort state honestly rather than nothing.
        out.deltaQ = deltaQ;
    }

    // Final energetics, evaluated once more at the converged (or
    // best-effort) charge state for a clean, self-consistent set of
    // numbers rather than reusing a mid-loop shift array.
    double bandEnergy = 0.0;
    for (const auto& kr : out.kpoints.empty()
             ? std::vector<DftbScfKPointResult>{}
             : out.kpoints) {
        for (std::size_t i = 0; i < kr.eigenvaluesHartree.size(); ++i)
            bandEnergy += kr.weight * kr.occupations[i] * kr.eigenvaluesHartree[i];
    }
    out.bandStructureEnergyHartree = bandEnergy;

    double coulombEnergy = 0.0;
    if (settings.sccEnabled && !out.deltaQ.empty()) {
        const auto finalShift = ewald.potentialShift(out.deltaQ);
        for (std::size_t a = 0; a < natoms; ++a)
            coulombEnergy += out.deltaQ[a] * finalShift[a];
        coulombEnergy *= -0.5;
    }
    out.coulombEnergyHartree = coulombEnergy;

    double repulsiveEnergy = 0.0;
    for (const auto& pair : hamiltonian.pairs()) {
        const int zi = basis.atoms[static_cast<std::size_t>(pair.atomI)].atomicNumber;
        const int zj = basis.atoms[static_cast<std::size_t>(pair.atomJ)].atomicNumber;
        if (const auto* file = table.pair(zi, zj))
            repulsiveEnergy += file->repulsiveEnergyHartree(pair.distanceBohr);
    }
    out.repulsiveEnergyHartree = 0.5 * repulsiveEnergy; // each bond counted twice

    out.totalEnergyHartree =
        out.bandStructureEnergyHartree + out.coulombEnergyHartree
        + out.repulsiveEnergyHartree;

    return dft::Outcome::success();
}

} // namespace calango::dftb
