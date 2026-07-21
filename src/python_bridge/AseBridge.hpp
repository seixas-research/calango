#pragma once

#include "core/KPath.hpp"
#include "core/Structure.hpp"

#include <pybind11/pybind11.h>

#include <memory>
#include <string>
#include <vector>

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

    /// Write a multi-frame trajectory via ase.io.write. `format` is an ASE
    /// format name ("extxyz", "xyz", "traj", "proteindatabank" for PDB
    /// multi-model, ...); empty = infer from extension.
    static void writeTrajectory(
        const std::vector<std::shared_ptr<core::Structure>>& frames,
        const std::string& path, const std::string& format = {});

    /// All frames of a trajectory / multi-frame file (ase.io.read index=":").
    /// `format` is an explicit ASE format hint for ambiguous extensions
    /// (e.g. "lammps-data"); empty lets ASE infer from name/content.
    static std::vector<core::Structure> readTrajectory(const std::string& path,
                                                       const std::string& format = {});

    /// (nx, ny, nz) repetition via ase.Atoms.repeat — requires a defined
    /// cell along the repeated directions.
    static core::Structure makeSupercell(const core::Structure& structure,
                                         int nx, int ny, int nz);

    /// Cleave a surface slab via ase.build.surface: (h k l) Miller indices,
    /// number of layers, vacuum padding in Å on each side.
    static core::Structure makeSlab(const core::Structure& structure,
                                    int h, int k, int l, int layers, double vacuum);

    // -- Nanomaterial builders (ase.build wrappers) ------------------------

    /// Periodic graphene sheet (nx × ny cells, lattice constant a in Å).
    static core::Structure buildGraphene(double a, int nx, int ny, double vacuum);

    /// Graphene nanoribbon; `zigzag` selects the edge type, `saturated`
    /// hydrogen-terminates the edges. width/length in ribbon unit cells.
    static core::Structure buildNanoribbon(int width, int length, bool zigzag,
                                           bool saturated, double vacuum);

    /// Carbon nanotube with chiral indices (n, m) and `length` unit cells.
    static core::Structure buildNanotube(int n, int m, int length, double bond,
                                         double vacuum);

    /// TMD monolayer via ase.build.mx2 (e.g. "MoS2", phase "2H" or "1T").
    static core::Structure buildMx2(const std::string& formula, const std::string& phase,
                                    double a, double thickness, int nx, int ny,
                                    double vacuum);

    /// High-symmetry k-points and ASE's suggested band path for the
    /// structure's Bravais lattice (via ase Cell.bandpath()). The path
    /// string concatenates labels, ','-separated per segment ("GXWKG,UX").
    struct BandPathInfo {
        std::vector<core::KPathPoint> specialPoints;
        std::string suggestedPath;
    };
    static BandPathInfo bandPathInfo(const core::Structure& structure);

    /// core::Structure -> ase.Atoms (positions, symbols, cell, pbc).
    static pybind11::object toAtoms(const core::Structure& structure);

    /// ase.Atoms -> core::Structure.
    static core::Structure fromAtoms(const pybind11::handle& atoms);
};

} // namespace calango::pybridge
