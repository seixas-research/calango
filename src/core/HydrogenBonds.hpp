#pragma once

#include "core/Structure.hpp"
#include "core/Vec3.hpp"

#include <vector>

namespace calango::core {

/// A detected hydrogen bond D–H···A.
///
/// The hydrogen is covalently bound to the donor D and points at the acceptor
/// A. Only the H···A contact is drawn — the D–H part is already an ordinary
/// covalent bond.
struct HydrogenBond {
    int donor = -1;     ///< atom index of D (N, O, F, …)
    int hydrogen = -1;  ///< atom index of H
    int acceptor = -1;  ///< atom index of A
    /// Cartesian shift applied to the acceptor to reach the periodic image
    /// actually involved. Zero for contacts inside the cell.
    Vec3 acceptorOffset{};
    double distanceDA = 0.0;  ///< D···A separation (Å)
    double distanceHA = 0.0;  ///< H···A separation (Å)
    double angleDHA = 0.0;    ///< D–H···A angle (degrees)
};

/// Geometric criteria for hydrogen-bond perception. The defaults are the
/// widely used "loose" IUPAC-style geometric cutoffs, which is what most
/// visualization tools show by default.
struct HydrogenBondOptions {
    /// Maximum donor-acceptor separation. Beyond ~3.5 Å the interaction is
    /// negligible for the usual N/O donors.
    double maxDonorAcceptor = 3.5;
    /// Minimum D–H···A angle in degrees. A hydrogen bond is close to linear;
    /// admitting bent geometries turns every nearby polar pair into a "bond".
    double minAngle = 120.0;
    /// Covalent-bond cutoff used to decide which hydrogens belong to which
    /// donor: H is bound to D when d(D,H) < this.
    double maxDonorHydrogen = 1.25;
    /// Atomic numbers accepted as donors and as acceptors. Defaults to the
    /// electronegative first- and second-row elements (N, O, F, S, Cl).
    std::vector<int> donorElements{7, 8, 9, 16, 17};
    std::vector<int> acceptorElements{7, 8, 9, 16, 17};
};

/// Detect hydrogen bonds by geometry. Honors periodicity through the minimum
/// image convention when the structure has a cell, so contacts across a cell
/// boundary are found (and carry the offset needed to draw them).
///
/// O(N²) over the donor/acceptor subsets, which is small in practice: only
/// electronegative atoms and their hydrogens participate.
std::vector<HydrogenBond> detectHydrogenBonds(
    const Structure& structure, const HydrogenBondOptions& options = {});

} // namespace calango::core
