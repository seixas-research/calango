#include "core/BandGap.hpp"

#include <cmath>
#include <limits>

namespace calango::core {

BandGapInfo analyzeBandGap(
    const std::vector<std::vector<std::vector<double>>>& energies,
    double fermiLevel, double tolerance)
{
    BandGapInfo info;

    double vbm = -std::numeric_limits<double>::infinity();
    double cbm = std::numeric_limits<double>::infinity();
    bool sawValence = false;
    bool sawConduction = false;

    // Classify per BAND, not per eigenvalue. Splitting the raw eigenvalues at
    // E_F would call any metal a semiconductor whenever the k-mesh is coarse:
    // a band crossing E_F between two sampled k-points shows up as a highest
    // "occupied" and a lowest "empty" value separated by however far apart
    // those samples happen to land — an artefact of sampling density, not a
    // gap. A band that dips below E_F at one k-point and rises above it at
    // another is crossing, and the system conducts, whatever the spacing.
    for (std::size_t spin = 0; spin < energies.size(); ++spin) {
        const auto& kPoints = energies[spin];
        if (kPoints.empty())
            continue;
        std::size_t bandCount = 0;
        for (const auto& bands : kPoints)
            bandCount = std::max(bandCount, bands.size());

        for (std::size_t b = 0; b < bandCount; ++b) {
            double lowest = std::numeric_limits<double>::infinity();
            double highest = -std::numeric_limits<double>::infinity();
            std::size_t lowestK = 0;
            std::size_t highestK = 0;
            for (std::size_t k = 0; k < kPoints.size(); ++k) {
                if (b >= kPoints[k].size())
                    continue;
                const double e = kPoints[k][b];
                if (!std::isfinite(e))
                    continue;
                if (e < lowest) {
                    lowest = e;
                    lowestK = k;
                }
                if (e > highest) {
                    highest = e;
                    highestK = k;
                }
            }
            if (!std::isfinite(lowest))
                continue; // band entirely NaN on this path

            if (lowest <= fermiLevel && highest > fermiLevel) {
                // Crosses the Fermi level: the system conducts.
                info.valid = true;
                info.metallic = true;
                info.gap = 0.0;
                return info;
            }
            if (highest <= fermiLevel) { // fully occupied -> valence band
                sawValence = true;
                if (highest > vbm) {
                    vbm = highest;
                    info.vbmKPoint = highestK;
                    info.vbmSpin = spin;
                }
            } else { // fully empty -> conduction band
                sawConduction = true;
                if (lowest < cbm) {
                    cbm = lowest;
                    info.cbmKPoint = lowestK;
                    info.cbmSpin = spin;
                }
            }
        }
    }

    if (!sawValence || !sawConduction)
        return info; // valid stays false: the window does not bracket E_F
    info.valid = true;
    info.vbm = vbm;
    info.cbm = cbm;
    info.gap = cbm - vbm;

    if (info.gap <= tolerance) {
        // Bands touch without crossing (a semimetal like graphene at the
        // Dirac point, or numerical noise): no usable gap either way.
        info.metallic = true;
        info.gap = 0.0;
        return info;
    }

    info.direct = info.vbmKPoint == info.cbmKPoint;

    // Minimum vertical gap: the smallest same-k separation between an
    // occupied and an empty state. For a direct-gap material this equals the
    // fundamental gap; for an indirect one it is the optical absorption
    // onset, which is strictly larger.
    double bestDirect = std::numeric_limits<double>::infinity();
    const std::size_t kCount = energies.front().size();
    for (std::size_t k = 0; k < kCount; ++k) {
        // Spins are compared within the same k-point but across channels:
        // in a half-metal-adjacent system the optical transition need not
        // conserve the spin index in this simple analysis, and taking the
        // minimum is the conservative (smallest onset) reading.
        double highestOccupied = -std::numeric_limits<double>::infinity();
        double lowestEmpty = std::numeric_limits<double>::infinity();
        for (const auto& spinBands : energies) {
            if (k >= spinBands.size())
                continue;
            for (const double e : spinBands[k]) {
                if (!std::isfinite(e))
                    continue;
                if (e <= fermiLevel)
                    highestOccupied = std::max(highestOccupied, e);
                else
                    lowestEmpty = std::min(lowestEmpty, e);
            }
        }
        if (!std::isfinite(highestOccupied) || !std::isfinite(lowestEmpty))
            continue;
        const double vertical = lowestEmpty - highestOccupied;
        if (vertical < bestDirect) {
            bestDirect = vertical;
            info.directKPoint = k;
        }
    }
    info.directGap = std::isfinite(bestDirect) ? bestDirect : info.gap;
    return info;
}

} // namespace calango::core
