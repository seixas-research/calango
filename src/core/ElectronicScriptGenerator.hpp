#pragma once

#include <string>

namespace calango::core {

/// Backends for electronic band-structure / PDOS jobs. FreeElectrons is
/// the always-available reference backend (empty-lattice bands, no
/// projections); GPAW adds real DFT bands plus element/orbital-resolved
/// PDOS when the package is importable; Espresso generates a pw.x
/// workflow that requires user-supplied pseudopotentials.
enum class ElectronicBackend {
    FreeElectrons,
    Gpaw,
    Espresso,
};

struct ElectronicConfig {
    ElectronicBackend backend = ElectronicBackend::FreeElectrons;
    /// High-symmetry path string ("GXWKG", "GMKG", ...); empty lets ASE
    /// suggest the path for the structure's Bravais lattice.
    std::string kpath;
    int npoints = 80;   ///< k-points along the whole path
    int nvalence = 4;   ///< electrons per cell (FreeElectrons backend)
    // -- SCF settings (GPAW / Espresso) --
    double ecutEv = 340.0;  ///< plane-wave cutoff (eV)
    int scfKpts = 4;        ///< Monkhorst-Pack k-grid (n x n x n)
    // -- PDOS (GPAW) --
    bool pdos = true;
    double pdosWidthEv = 0.1;
    int pdosPoints = 401;
};

/// Standalone run.py: reads structure.extxyz from the job directory,
/// runs the backend, and writes bands.json (+ pdos.json when available)
/// with CALANGO_PROGRESS markers for the job console.
std::string generateElectronicScript(const ElectronicConfig& config);

} // namespace calango::core
