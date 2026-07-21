#pragma once

#include "core/Vec3.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// A 3D scalar field on a regular (possibly triclinic) grid: charge
/// densities, electrostatic potentials, wavefunctions, ELF, ...
///
/// Storage is C-order with z fastest: value(ix, iy, iz) =
/// values[(ix * ny + iy) * nz + iz]. Loaders normalize every input
/// format to this layout.
class VolumetricData {
public:
    int nx = 0, ny = 0, nz = 0;
    Vec3 origin{};                ///< Cartesian origin of grid point (0,0,0)
    Vec3 spanA{}, spanB{}, spanC{}; ///< full spanning vectors of the grid box
    std::vector<double> values;   ///< nx*ny*nz scalars, z fastest
    std::string label;            ///< field name (file stem / block title)

    bool empty() const { return values.empty(); }
    double minValue() const;
    double maxValue() const;

    double at(int ix, int iy, int iz) const
    {
        return values[(static_cast<std::size_t>(ix) * ny + iy) * nz + iz];
    }

    /// Cartesian position of a grid node (indices may be fractional).
    Vec3 position(double ix, double iy, double iz) const;

    /// Trilinear interpolation at fractional grid coordinates, clamped to
    /// the box (use periodic() for wrap-around sampling of crystal grids).
    double sample(double ix, double iy, double iz) const;
    /// Trilinear interpolation with periodic wrapping in all directions.
    double samplePeriodic(double ix, double iy, double iz) const;

    /// Load a volumetric file, auto-detected from the extension/content:
    /// Gaussian .cube (also written by Quantum ESPRESSO's pp.x), VASP
    /// CHGCAR/LOCPOT/PARCHG, or XCrySDen .xsf 3D data grids.
    /// Throws std::runtime_error with a readable message on failure.
    static VolumetricData load(const std::string& path);

private:
    static VolumetricData loadCube(const std::string& path);
    static VolumetricData loadChgcar(const std::string& path);
    static VolumetricData loadXsf(const std::string& path);
};

} // namespace calango::core
