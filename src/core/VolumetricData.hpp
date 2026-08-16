#pragma once

#include "core/Atom.hpp"
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
    /// The source file's own atom list (Cartesian Å, same convention as
    /// origin/spanA/spanB/spanC), kept ONLY so a converted .h5 stays
    /// self-contained enough to reconstruct the file it came from — nothing
    /// in the isosurface/grid-sampling API above reads it. Populated by
    /// loadCube() and loadChgcar(); loadXsf() leaves it empty (this loader
    /// never parses XSF's separate atoms block, only its DATAGRID_3D). An
    /// atom whose species could not be resolved (VASP4 CHGCAR with no
    /// symbol line) is recorded with atomicNumber 0.
    std::vector<Atom> atoms;
    /// "cube" / "chgcar" / "xsf" / empty — which loader produced this, so an
    /// HDF5 conversion can record what to reconstruct. Set by every loader,
    /// including loadHdf5() (which restores whatever format the ORIGINAL
    /// file — before conversion — was in, not "hdf5" itself).
    std::string sourceFormat;

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
    /// CHGCAR/LOCPOT/PARCHG, XCrySDen .xsf 3D data grids, or Calango's own
    /// compressed HDF5 container (.h5/.hdf5 — see saveHdf5()).
    /// Throws std::runtime_error with a readable message on failure.
    static VolumetricData load(const std::string& path);

    /// Write this grid to `path` as a chunked, gzip+shuffle compressed HDF5
    /// container — see docs/sphinx/source/reference/hdf5_density.md for the
    /// on-disk layout. Self-contained: `VolumetricData::load(path)` on the
    /// result reproduces this object's grid, cell, atoms and label exactly
    /// (values bitwise, since HDF5's own IEEE-754 float64 datatype needs no
    /// text round-trip). Throws std::runtime_error on failure; never leaves
    /// a partially-written file at `path` (writes to a temporary sibling and
    /// renames it into place on success).
    void saveHdf5(const std::string& path) const;

    /// Read `sourcePath` (any format load() understands, including an
    /// existing .h5) and write it back out as a compressed HDF5 container at
    /// `destPath` — the ONE conversion path both the calculator setup pages'
    /// "HDF5 compression" option and the Dump Charge Densities node call, so
    /// the two can never disagree about the container's layout. Returns
    /// false and fills `error` on failure (source unreadable, destination
    /// unwritable); does not touch `sourcePath`.
    static bool convertToHdf5(const std::string& sourcePath,
                              const std::string& destPath, std::string* error);

private:
    static VolumetricData loadCube(const std::string& path);
    static VolumetricData loadChgcar(const std::string& path);
    static VolumetricData loadXsf(const std::string& path);
    static VolumetricData loadHdf5(const std::string& path);
};

} // namespace calango::core
