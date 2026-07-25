#include "core/HydrogenBonds.hpp"

#include "core/PeriodicImages.hpp"

#include <algorithm>
#include <cmath>

namespace calango::core {

namespace {

constexpr int kHydrogenZ = 1;

bool contains(const std::vector<int>& list, int value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

} // namespace

std::vector<HydrogenBond> detectHydrogenBonds(
    const Structure& structure, const HydrogenBondOptions& options)
{
    std::vector<HydrogenBond> bonds;
    const auto& atoms = structure.atoms();
    if (atoms.empty())
        return bonds;

    const bool periodic = structure.cell().isDefined();
    const auto& cellVectors = structure.cell().vectors();
    const auto range = periodic
        ? imageRange(structure.cell(), options.maxDonorAcceptor)
        : std::array<int, 3>{0, 0, 0};

    // Shortest vector from `from` to `to` over the periodic images, together
    // with the shift that produced it (needed so the drawn line goes to the
    // image actually bonded rather than back across the whole cell).
    const auto minimumImage = [&](const Vec3& from, const Vec3& to,
                                  Vec3& shiftOut) {
        Vec3 best = to - from;
        double bestNorm = best.norm();
        shiftOut = {};
        if (!periodic)
            return best;
        for (int ia = -range[0]; ia <= range[0]; ++ia) {
            for (int ib = -range[1]; ib <= range[1]; ++ib) {
                for (int ic = -range[2]; ic <= range[2]; ++ic) {
                    if (ia == 0 && ib == 0 && ic == 0)
                        continue;
                    const Vec3 shift = cellVectors[0] * static_cast<double>(ia)
                        + cellVectors[1] * static_cast<double>(ib)
                        + cellVectors[2] * static_cast<double>(ic);
                    const Vec3 candidate = to + shift - from;
                    const double norm = candidate.norm();
                    if (norm < bestNorm) {
                        bestNorm = norm;
                        best = candidate;
                        shiftOut = shift;
                    }
                }
            }
        }
        return best;
    };

    // -- Pair each hydrogen with its covalent donor -------------------------
    // A hydrogen bonded to carbon is not a donor: C–H is not polar enough.
    // Restricting to the configured donor elements is what keeps a protein or
    // a hydrocarbon from lighting up with hundreds of false contacts.
    struct DonorHydrogen {
        int donor;
        int hydrogen;
    };
    std::vector<DonorHydrogen> donorHydrogens;
    for (std::size_t h = 0; h < atoms.size(); ++h) {
        if (atoms[h].atomicNumber != kHydrogenZ)
            continue;
        int bestDonor = -1;
        double bestDistance = options.maxDonorHydrogen;
        for (std::size_t d = 0; d < atoms.size(); ++d) {
            if (!contains(options.donorElements, atoms[d].atomicNumber))
                continue;
            Vec3 shift;
            const double distance =
                minimumImage(atoms[d].position, atoms[h].position, shift).norm();
            if (distance < bestDistance) {
                bestDistance = distance;
                bestDonor = static_cast<int>(d);
            }
        }
        if (bestDonor >= 0)
            donorHydrogens.push_back({bestDonor, static_cast<int>(h)});
    }

    // -- Test each D–H against every acceptor -------------------------------
    for (const auto& [donor, hydrogen] : donorHydrogens) {
        const Vec3 donorPos = atoms[static_cast<std::size_t>(donor)].position;
        const Vec3 hydrogenPos =
            atoms[static_cast<std::size_t>(hydrogen)].position;
        for (std::size_t a = 0; a < atoms.size(); ++a) {
            const int acceptor = static_cast<int>(a);
            if (acceptor == donor)
                continue; // the donor cannot accept from its own hydrogen
            if (!contains(options.acceptorElements, atoms[a].atomicNumber))
                continue;

            Vec3 shift;
            const Vec3 daVector = minimumImage(donorPos, atoms[a].position, shift);
            const double distanceDA = daVector.norm();
            if (distanceDA > options.maxDonorAcceptor || distanceDA < 1e-6)
                continue;

            // Angle at the hydrogen: the vectors H->D and H->A. A real
            // hydrogen bond is near-linear (180 deg), so the cutoff is a lower
            // bound on this angle.
            const Vec3 acceptorPos = atoms[a].position + shift;
            const Vec3 hd = donorPos - hydrogenPos;
            const Vec3 ha = acceptorPos - hydrogenPos;
            const double distanceHA = ha.norm();
            const double hdNorm = hd.norm();
            if (distanceHA < 1e-6 || hdNorm < 1e-6)
                continue;
            // The H must lie between D and A: if the acceptor is closer to the
            // donor than the hydrogen is, this is not a D-H...A arrangement.
            if (distanceHA > distanceDA)
                continue;
            const double cosine =
                std::clamp(hd.dot(ha) / (hdNorm * distanceHA), -1.0, 1.0);
            const double angle = std::acos(cosine) * 180.0 / M_PI;
            if (angle < options.minAngle)
                continue;

            bonds.push_back({donor, hydrogen, acceptor, shift, distanceDA,
                             distanceHA, angle});
        }
    }
    return bonds;
}

} // namespace calango::core
