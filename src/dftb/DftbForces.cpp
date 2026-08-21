#include "dftb/DftbForces.hpp"

#include "dft/Constants.hpp"
#include "dftb/DftbHamiltonian.hpp"

namespace calango::dftb {

namespace {

/// Total SCC-DFTB energy (Hartree) with atom `atomIndex`'s position offset
/// by `deltaAngstrom` along `axis` (0=x,1=y,2=z) from `structure`'s own
/// geometry. Rebuilds the basis (element set never changes, but re-deriving
/// it is cheap) and the pair list (which DOES depend on geometry) fresh at
/// the perturbed position.
dft::Outcome perturbedEnergy(const core::Structure& structure,
                              const SlaterKosterTable& table,
                              const std::vector<DftbKPoint>& kpoints,
                              const DftbScfSettings& settings, int atomIndex,
                              int axis, double deltaAngstrom, double& energyOut)
{
    core::Structure perturbed = structure;
    core::Vec3& pos = perturbed.atoms()[static_cast<std::size_t>(atomIndex)].position;
    if (axis == 0)
        pos.x += deltaAngstrom;
    else if (axis == 1)
        pos.y += deltaAngstrom;
    else
        pos.z += deltaAngstrom;

    std::vector<int> atomicNumbers;
    atomicNumbers.reserve(perturbed.atoms().size());
    for (const auto& atom : perturbed.atoms())
        atomicNumbers.push_back(atom.atomicNumber);

    DftbBasis basis;
    const auto basisOutcome = DftbBasis::build(atomicNumbers, table, basis);
    if (!basisOutcome.ok())
        return basisOutcome;

    DftbHamiltonianBuilder builder;
    const auto buildOutcome = builder.build(perturbed, table, basis);
    if (!buildOutcome.ok())
        return buildOutcome;

    DftbScf scf;
    DftbScfResult result;
    const auto scfOutcome =
        scf.run(perturbed, table, basis, builder, kpoints, settings, result);
    if (!scfOutcome.ok())
        return scfOutcome;
    energyOut = result.totalEnergyHartree;
    return dft::Outcome::success();
}

} // namespace

dft::Outcome computeDftbForces(const core::Structure& structure,
                                const SlaterKosterTable& table,
                                const std::vector<DftbKPoint>& kpoints,
                                const DftbScfSettings& settings,
                                DftbForces& out, double displacementAngstrom)
{
    out = DftbForces{};
    out.displacementAngstrom = displacementAngstrom;
    const auto natoms = structure.atoms().size();
    out.forcesEvPerAngstrom.assign(natoms, core::Vec3{});

    for (std::size_t a = 0; a < natoms; ++a) {
        double component[3] = {};
        for (int axis = 0; axis < 3; ++axis) {
            double ePlus = 0.0, eMinus = 0.0;
            auto outcome =
                perturbedEnergy(structure, table, kpoints, settings,
                                 static_cast<int>(a), axis,
                                 displacementAngstrom, ePlus);
            if (!outcome.ok())
                return outcome;
            outcome = perturbedEnergy(structure, table, kpoints, settings,
                                       static_cast<int>(a), axis,
                                       -displacementAngstrom, eMinus);
            if (!outcome.ok())
                return outcome;
            const double dEdR = (ePlus - eMinus) / (2.0 * displacementAngstrom);
            component[axis] = -dEdR * dft::kHartreeToEv;
        }
        out.forcesEvPerAngstrom[a] = {component[0], component[1], component[2]};
    }

    return dft::Outcome::success();
}

} // namespace calango::dftb
