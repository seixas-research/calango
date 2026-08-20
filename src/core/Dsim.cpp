#include "core/Dsim.hpp"

#include "core/PhysicalConstants.hpp"

#include <cmath>

namespace calango::core {

MMatrix computeMMatrix(const EnergyMatrix& energyMatrix, double dilution)
{
    const std::size_t n = energyMatrix.size();
    MMatrix m(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j)
                continue; // diagonal stays exactly 0
            m[i][j] = energyMatrix[i][j]
                - (dilution * energyMatrix[i][i] + (1.0 - dilution) * energyMatrix[j][j]);
        }
    }
    return m;
}

double enthalpyOfMixing(const MMatrix& mMatrix, const std::vector<double>& composition,
                        double epsilon)
{
    const std::size_t n = mMatrix.size();
    double total = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            const double xi = composition[i];
            const double xj = composition[j];
            const double omega = (mMatrix[j][i] * xi + mMatrix[i][j] * xj) / (xi + xj + epsilon);
            total += omega * xi * xj;
        }
    }
    return total;
}

double configurationalEntropyEvPerAtomK(const std::vector<double>& composition)
{
    double s = 0.0;
    for (double x : composition) {
        if (x > 0.0)
            s -= x * std::log(x);
    }
    return kBoltzmannEvPerK * s;
}

double gibbsFreeEnergyOfMixingEvPerAtom(const MMatrix& mMatrix,
                                        const std::vector<double>& composition,
                                        double temperatureK)
{
    return enthalpyOfMixing(mMatrix, composition)
        - temperatureK * configurationalEntropyEvPerAtomK(composition);
}

DsimBinaryResult solveDsimBinary(const std::string& speciesA, const std::string& speciesB,
                                 int supercellAtomCount, double energyPureATotalEv,
                                 double energyPureBTotalEv, double energyBInATotalEv,
                                 double energyAInBTotalEv, int compositionPoints)
{
    DsimBinaryResult result;
    result.speciesA = speciesA;
    result.speciesB = speciesB;
    result.supercellAtomCount = supercellAtomCount;
    result.dilution = supercellAtomCount > 0 ? 1.0 / static_cast<double>(supercellAtomCount) : 0.0;
    result.energyPureATotalEv = energyPureATotalEv;
    result.energyPureBTotalEv = energyPureBTotalEv;
    result.energyBInATotalEv = energyBInATotalEv;
    result.energyAInBTotalEv = energyAInBTotalEv;

    // Species index 0 = A (host at x=0), 1 = B (solute at x=0). The energy
    // matrix's off-diagonal entries are exactly the two impurity supercells
    // this run measured: [B][A] = B diluted in host A, [A][B] = A diluted
    // in host B (Eq. 9-10, TOTAL-energy convention — see Dsim.hpp).
    const EnergyMatrix energyMatrix{
        {energyPureATotalEv, energyAInBTotalEv},
        {energyBInATotalEv, energyPureBTotalEv},
    };
    const MMatrix m = computeMMatrix(energyMatrix, result.dilution);

    result.mBInA = m[1][0]; // M_2[1]: solute B (index 1), host A (index 0)
    result.mAInB = m[0][1]; // M_1[2]: solute A (index 0), host B (index 1)
    result.dHdxAt0 = result.mBInA;   //  dDeltaH/dx |_{x=0} (Eq. 8)
    result.dHdxAt1 = -result.mAInB;  // -dDeltaH/dx |_{x=1} (Eq. 8)

    const int points = compositionPoints < 2 ? 2 : compositionPoints;
    result.curve.reserve(static_cast<std::size_t>(points));
    for (int i = 0; i < points; ++i) {
        const double x = static_cast<double>(i) / static_cast<double>(points - 1);
        DsimCurvePoint point;
        point.x = x;
        // Eq. 7 directly (equivalent to enthalpyOfMixing(m, {1-x, x}), spelled
        // out to make the x=0/x=1 exact-zero endpoints obvious by construction
        // rather than relying on the epsilon-guarded general formula there).
        point.enthalpyEvPerAtom = result.mBInA * x * (1.0 - x) * (1.0 - x)
            + result.mAInB * x * x * (1.0 - x);
        point.enthalpyKjPerMol = point.enthalpyEvPerAtom * kEvToKjPerMol;
        result.curve.push_back(point);
    }

    return result;
}

namespace {

/// All non-negative integer n-tuples summing to `resolution` (stars-and-
/// bars), converted to fractions — the recursive-enumeration equivalent of
/// oncapintada's combinations_with_replacement + bincount construction.
/// Enumeration order is unspecified (not needed for a plotting grid).
void enumerateCompositions(int slotsLeft, int remaining, std::vector<int>& current,
                           int resolution, std::vector<std::vector<double>>& out)
{
    if (slotsLeft == 1) {
        current.push_back(remaining);
        std::vector<double> row(current.size());
        for (std::size_t i = 0; i < current.size(); ++i)
            row[i] = static_cast<double>(current[i]) / static_cast<double>(resolution);
        out.push_back(std::move(row));
        current.pop_back();
        return;
    }
    for (int k = 0; k <= remaining; ++k) {
        current.push_back(k);
        enumerateCompositions(slotsLeft - 1, remaining - k, current, resolution, out);
        current.pop_back();
    }
}

} // namespace

std::vector<std::vector<double>> simplexGrid(int nComponents, int resolution)
{
    std::vector<std::vector<double>> out;
    if (nComponents < 1 || resolution < 1)
        return out;
    std::vector<int> current;
    current.reserve(static_cast<std::size_t>(nComponents));
    enumerateCompositions(nComponents, resolution, current, resolution, out);
    return out;
}

DsimMulticomponentResult solveDsimMulticomponent(const std::vector<std::string>& species,
                                                 const EnergyMatrix& energyMatrix,
                                                 int supercellAtomCount, int resolution)
{
    DsimMulticomponentResult result;
    result.species = species;
    result.supercellAtomCount = supercellAtomCount;
    result.dilution = supercellAtomCount > 0 ? 1.0 / static_cast<double>(supercellAtomCount) : 0.0;
    result.energyMatrix = energyMatrix;
    result.mMatrix = computeMMatrix(energyMatrix, result.dilution);

    const std::size_t n = species.size();
    const int points = resolution < 2 ? 2 : resolution;

    // N=2: the same binary curve DsimBinaryResult carries (Eq. 7).
    if (n == 2) {
        result.mBInA = result.mMatrix[1][0];
        result.mAInB = result.mMatrix[0][1];
        result.binaryCurve.reserve(static_cast<std::size_t>(points));
        for (int i = 0; i < points; ++i) {
            const double x = static_cast<double>(i) / static_cast<double>(points - 1);
            DsimCurvePoint point;
            point.x = x;
            point.enthalpyEvPerAtom = enthalpyOfMixing(result.mMatrix, {1.0 - x, x});
            point.enthalpyKjPerMol = point.enthalpyEvPerAtom * kEvToKjPerMol;
            result.binaryCurve.push_back(point);
        }
    }

    // N=3: DeltaH_mix over the whole composition triangle (Eq. 4+6).
    if (n == 3) {
        for (const auto& row : simplexGrid(3, points)) {
            DsimMulticomponentResult::TernaryGridPoint gp;
            gp.xB = row[1];
            gp.xC = row[2];
            gp.enthalpyEvPerAtom = enthalpyOfMixing(result.mMatrix, row);
            gp.enthalpyKjPerMol = gp.enthalpyEvPerAtom * kEvToKjPerMol;
            result.ternaryGrid.push_back(gp);
        }
    }

    // Every N>=2: pairwise binary sub-curves (species i vs. j, every other
    // species held at 0 — the general formula's OTHER pairwise terms
    // vanish exactly when a third species' fraction is 0, so this is Eq. 7
    // for that pair alone, not an approximation).
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            DsimMulticomponentResult::PairwiseCurve pc;
            pc.speciesI = static_cast<int>(i);
            pc.speciesJ = static_cast<int>(j);
            pc.curve.reserve(static_cast<std::size_t>(points));
            for (int k = 0; k < points; ++k) {
                const double x = static_cast<double>(k) / static_cast<double>(points - 1);
                std::vector<double> composition(n, 0.0);
                composition[i] = 1.0 - x;
                composition[j] = x;
                DsimCurvePoint point;
                point.x = x;
                point.enthalpyEvPerAtom = enthalpyOfMixing(result.mMatrix, composition);
                point.enthalpyKjPerMol = point.enthalpyEvPerAtom * kEvToKjPerMol;
                pc.curve.push_back(point);
            }
            result.pairwiseCurves.push_back(std::move(pc));
        }
    }

    return result;
}

} // namespace calango::core
