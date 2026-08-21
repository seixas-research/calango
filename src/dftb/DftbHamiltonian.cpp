#include "dftb/DftbHamiltonian.hpp"

#include "core/PeriodicImages.hpp"
#include "dft/Constants.hpp"
#include "dftb/SlaterKosterTransform.hpp"

#include <cmath>

namespace calango::dftb {

namespace {

/// The farthest distance (bohr) any loaded two-center integral is nonzero
/// for the ORDERED pair (z1, z2) — the table's own grid extent.
double pairCutoffBohr(const SlaterKosterTable& table, int z1, int z2)
{
    const SlaterKosterFile* file = table.pair(z1, z2);
    if (!file || file->table.empty())
        return 0.0;
    return static_cast<double>(file->table.size()) * file->gridDistanceBohr;
}

} // namespace

dft::Outcome DftbHamiltonianBuilder::build(const core::Structure& structure,
                                            const SlaterKosterTable& table,
                                            const DftbBasis& basis)
{
    pairs_.clear();
    basis_ = &basis;
    dimension_ = basis.totalOrbitals;

    const auto& atoms = structure.atoms();
    if (atoms.size() != basis.atoms.size())
        return dft::Outcome::invalid(
            "DftbBasis was built for a different atom count than this "
            "structure");

    // On-site energies: Es for every atom, plus Ep*3 (px,py,pz share the
    // free atom's single p-shell energy) for atoms with a p shell.
    onsiteEnergyHartree_.assign(static_cast<std::size_t>(dimension_), 0.0);
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        const auto* shells = table.onsite(atoms[i].atomicNumber);
        if (!shells)
            return dft::Outcome::invalid(
                "no on-site data for an atom already accepted by DftbBasis "
                "— table changed between build() calls?");
        const AtomOrbitals& ao = basis.atoms[i];
        onsiteEnergyHartree_[static_cast<std::size_t>(ao.firstOrbital)] =
            (*shells)[0].energyHartree; // s
        for (int p = 0; p < 3 && ao.hasP; ++p)
            onsiteEnergyHartree_[static_cast<std::size_t>(ao.firstOrbital + 1 + p)] =
                (*shells)[1].energyHartree; // p (px=py=pz for a free atom)
    }

    // The overall cutoff: the largest of every pair type actually present,
    // so the periodic-image search radius is never too short for any one
    // element pair.
    double cutoffBohr = 0.0;
    for (std::size_t i = 0; i < atoms.size(); ++i)
        for (std::size_t j = 0; j < atoms.size(); ++j)
            cutoffBohr = std::max(
                cutoffBohr, pairCutoffBohr(table, atoms[i].atomicNumber,
                                            atoms[j].atomicNumber));
    if (cutoffBohr <= 0.0)
        return dft::Outcome::invalid(
            "no two-center integral range found — every loaded .skf pair "
            "has an empty table");

    const auto& cell = structure.cell();
    const std::array<bool, 3> pbc = cell.pbc();
    const bool periodic = pbc[0] || pbc[1] || pbc[2];

    // Lattice vectors in bohr, for building integer-image translations.
    std::array<core::Vec3, 3> latticeBohr{};
    std::array<int, 3> range{{0, 0, 0}};
    if (periodic) {
        for (int a = 0; a < 3; ++a)
            latticeBohr[static_cast<std::size_t>(a)] =
                cell.vectors()[static_cast<std::size_t>(a)]
                * dft::kBohrPerAngstrom;
        core::UnitCell bohrCell(latticeBohr[0], latticeBohr[1], latticeBohr[2],
                                 pbc);
        range = core::imageRange(bohrCell, cutoffBohr);
    }
    const int n1max = pbc[0] ? range[0] : 0;
    const int n2max = pbc[1] ? range[1] : 0;
    const int n3max = pbc[2] ? range[2] : 0;

    std::vector<core::Vec3> positionsBohr;
    positionsBohr.reserve(atoms.size());
    for (const auto& atom : atoms)
        positionsBohr.push_back(atom.position * dft::kBohrPerAngstrom);

    for (std::size_t i = 0; i < atoms.size(); ++i) {
        const int zi = atoms[i].atomicNumber;
        for (std::size_t j = 0; j < atoms.size(); ++j) {
            const int zj = atoms[j].atomicNumber;
            const SlaterKosterFile* forward = table.pair(zi, zj);
            const SlaterKosterFile* reverse = table.pair(zj, zi);
            if (!forward || !reverse)
                continue; // reported by the table's own missingPairs()

            for (int n1 = -n1max; n1 <= n1max; ++n1) {
                for (int n2 = -n2max; n2 <= n2max; ++n2) {
                    for (int n3 = -n3max; n3 <= n3max; ++n3) {
                        if (i == j && n1 == 0 && n2 == 0 && n3 == 0)
                            continue; // the on-site block, handled separately

                        const core::Vec3 translation =
                            latticeBohr[0] * static_cast<double>(n1)
                            + latticeBohr[1] * static_cast<double>(n2)
                            + latticeBohr[2] * static_cast<double>(n3);
                        const core::Vec3 r0 = positionsBohr[j] + translation
                            - positionsBohr[i];
                        const double d = r0.norm();
                        if (d < 1.0e-6)
                            continue; // coincident atoms — not a valid bond
                        if (d > cutoffBohr)
                            continue;

                        const double l = r0.x / d;
                        const double m = r0.y / d;
                        const double n = r0.z / d;

                        SpIntegrals hInt;
                        hInt.ssSigma =
                            forward->integral(false, SkChannel::Sssigma, d);
                        hInt.spSigmaForward =
                            forward->integral(false, SkChannel::Spsigma, d);
                        hInt.spSigmaReverse =
                            reverse->integral(false, SkChannel::Spsigma, d);
                        hInt.ppSigma =
                            forward->integral(false, SkChannel::Ppsigma, d);
                        hInt.ppPi =
                            forward->integral(false, SkChannel::Pppi, d);

                        SpIntegrals sInt;
                        sInt.ssSigma =
                            forward->integral(true, SkChannel::Sssigma, d);
                        sInt.spSigmaForward =
                            forward->integral(true, SkChannel::Spsigma, d);
                        sInt.spSigmaReverse =
                            reverse->integral(true, SkChannel::Spsigma, d);
                        sInt.ppSigma =
                            forward->integral(true, SkChannel::Ppsigma, d);
                        sInt.ppPi =
                            forward->integral(true, SkChannel::Pppi, d);

                        // Both integrals vanishing means this pair is
                        // outside its OWN file's tabulated range even
                        // though it is inside the overall cutoff (a
                        // different pair type may have a longer range) —
                        // skip rather than insert a dead zero block.
                        const bool allZero = hInt.ssSigma == 0.0
                            && hInt.spSigmaForward == 0.0
                            && hInt.spSigmaReverse == 0.0
                            && hInt.ppSigma == 0.0 && hInt.ppPi == 0.0
                            && sInt.ssSigma == 0.0
                            && sInt.spSigmaForward == 0.0
                            && sInt.spSigmaReverse == 0.0
                            && sInt.ppSigma == 0.0 && sInt.ppPi == 0.0;
                        if (allZero)
                            continue;

                        DftbPairBlock block;
                        block.atomI = static_cast<int>(i);
                        block.atomJ = static_cast<int>(j);
                        block.image = {n1, n2, n3};
                        block.distanceBohr = d;
                        block.h = skBlock(l, m, n, hInt);
                        block.s = skBlock(l, m, n, sInt);
                        block.bondVectorBohr = r0;
                        pairs_.push_back(block);
                    }
                }
            }
        }
    }

    return dft::Outcome::success();
}

void DftbHamiltonianBuilder::blochMatrices(
    const std::array<double, 3>& kFrac,
    std::vector<std::complex<double>>& hOut,
    std::vector<std::complex<double>>& sOut,
    const std::vector<double>& atomicShiftHartree) const
{
    const auto n = static_cast<std::size_t>(dimension_);
    hOut.assign(n * n, {0.0, 0.0});
    sOut.assign(n * n, {0.0, 0.0});
    if (!basis_)
        return;

    const auto shiftFor = [&](int atomIndex) -> double {
        if (atomicShiftHartree.empty()
            || static_cast<std::size_t>(atomIndex)
                >= atomicShiftHartree.size())
            return 0.0;
        return atomicShiftHartree[static_cast<std::size_t>(atomIndex)];
    };

    // On-site blocks: H0 = diag(on-site energies), S = identity, plus the
    // SCC shift (H1_AA = shift_A exactly, since S_mu,mu == 1 there).
    for (std::size_t atomIndex = 0; atomIndex < basis_->atoms.size();
         ++atomIndex) {
        const AtomOrbitals& ao = basis_->atoms[atomIndex];
        const double shift = shiftFor(static_cast<int>(atomIndex));
        for (int o = 0; o < ao.orbitalCount(); ++o) {
            const auto orbital = static_cast<std::size_t>(ao.firstOrbital + o);
            const auto idx = orbital * n + orbital;
            sOut[idx] = {1.0, 0.0};
            hOut[idx] = std::complex<double>(
                onsiteEnergyHartree_[orbital] + shift, 0.0);
        }
    }

    for (const auto& pair : pairs_) {
        const AtomOrbitals& oi = basis_->atoms[static_cast<std::size_t>(pair.atomI)];
        const AtomOrbitals& oj = basis_->atoms[static_cast<std::size_t>(pair.atomJ)];
        const double phaseArg = 2.0 * dft::kPi
            * (kFrac[0] * pair.image[0] + kFrac[1] * pair.image[1]
               + kFrac[2] * pair.image[2]);
        const std::complex<double> phase(std::cos(phaseArg), std::sin(phaseArg));
        const double shiftSum =
            shiftFor(pair.atomI) + shiftFor(pair.atomJ);

        for (int row = 0; row < oi.orbitalCount(); ++row) {
            for (int col = 0; col < oj.orbitalCount(); ++col) {
                const double hVal = pair.h[static_cast<std::size_t>(row * 4 + col)];
                const double sVal = pair.s[static_cast<std::size_t>(row * 4 + col)];
                const auto idx =
                    static_cast<std::size_t>(oi.firstOrbital + row) * n
                    + static_cast<std::size_t>(oj.firstOrbital + col);
                // SCC second-order term: H1 = 0.5 * S * (shift_i + shift_j).
                hOut[idx] += phase * (hVal + 0.5 * shiftSum * sVal);
                sOut[idx] += phase * sVal;
            }
        }
    }
}

} // namespace calango::dftb
