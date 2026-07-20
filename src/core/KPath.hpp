#pragma once

#include "core/Vec3.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// One high-symmetry point on a band-structure path, in fractional
/// coordinates of the reciprocal lattice. Labels follow ASE conventions
/// ("G" for Gamma; the UI renders it as "Γ").
struct KPathPoint {
    std::string label;
    Vec3 fractional;
};

/// VASP KPOINTS file in line mode ("Reciprocal"), one block per segment.
/// `divisionsPerSegment` is the number of k-points along each line.
std::string toVaspKpoints(const std::vector<KPathPoint>& path, int divisionsPerSegment);

/// Quantum ESPRESSO K_POINTS card, `crystal_b` variant: each row carries
/// the number of points to the next path vertex (last row weight 1).
std::string toQeKpointsCard(const std::vector<KPathPoint>& path, int pointsPerSegment);

} // namespace calango::core
