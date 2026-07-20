#pragma once

#include "core/Structure.hpp"

#include <pybind11/pybind11.h>

#include <string>

namespace calango::pybridge {

/// Stateless conversion layer between core::Structure and ase.Atoms.
///
/// All file I/O deliberately goes through ase.io — one code path handles
/// XYZ, extended XYZ, CIF, POSCAR/CONTCAR and every other format ASE knows.
/// All functions throw std::runtime_error (with the Python traceback text)
/// on failure and must be called with PythonEngine alive, on the GUI thread.
class AseBridge {
public:
    /// Load any ASE-readable file into the core model.
    static core::Structure readStructure(const std::string& path);

    /// Write the structure via ase.io.write. `format` is an ASE format name
    /// ("extxyz", "cif", "vasp", ...); empty = infer from extension.
    static void writeStructure(const core::Structure& structure,
                               const std::string& path,
                               const std::string& format = {});

    /// (nx, ny, nz) repetition via ase.Atoms.repeat — requires a defined
    /// cell along the repeated directions.
    static core::Structure makeSupercell(const core::Structure& structure,
                                         int nx, int ny, int nz);

    /// core::Structure -> ase.Atoms (positions, symbols, cell, pbc).
    static pybind11::object toAtoms(const core::Structure& structure);

    /// ase.Atoms -> core::Structure.
    static core::Structure fromAtoms(const pybind11::handle& atoms);
};

} // namespace calango::pybridge
