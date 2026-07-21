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

/// A k-path as continuous sections: consecutive points within a section
/// are connected; sections are separated by a discontinuity ("break"),
/// e.g. Γ→X→W | K→L. Sections with fewer than two points are ignored by
/// the exporters.
using KPathSegments = std::vector<std::vector<KPathPoint>>;

/// VASP KPOINTS file in line mode ("Reciprocal"), one block per leg;
/// section breaks simply start the next block at the new point.
/// `divisionsPerSegment` is the number of k-points along each line.
std::string toVaspKpoints(const KPathSegments& sections, int divisionsPerSegment);

/// Quantum ESPRESSO K_POINTS card, `crystal_b` variant: each row carries
/// the number of points to the next vertex; a section's last row gets 1,
/// which is also how discontinuities are conventionally encoded.
std::string toQeKpointsCard(const KPathSegments& sections, int pointsPerSegment);

/// CASTEP SPECTRAL_KPOINT_PATH block (.cell); discontinuities emit the
/// `break` keyword between sections.
std::string toCastepPath(const KPathSegments& sections);

/// SIESTA BandLines block (BandLinesScale ReciprocalLatticeVectors); each
/// section restarts with a count of 1.
std::string toSiestaBandLines(const KPathSegments& sections, int pointsPerSegment);

/// Standalone ASE/Python script reconstructing the path (special-point
/// dictionary + "," -separated path string) via cell.bandpath().
std::string toAsePythonScript(const KPathSegments& sections, int totalPoints);

} // namespace calango::core
