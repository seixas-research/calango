#pragma once

#include <string>

namespace calango::core {

/// Parameters for a Wannier-interpolated Fermi surface.
///
/// The Fermi surface is the constant-energy sheet E_n(k) = E_F. Resolving it
/// directly from a DFT k-mesh is impractical — it is a surface in 3D, so a
/// legible one needs a grid an SCF could never afford. Wannier interpolation
/// is what makes it tractable: the localized Wannier Hamiltonian H(R) is
/// short-ranged, so E_n(k) can be evaluated at any k for the cost of
/// diagonalizing a small matrix, and a 40³ grid becomes minutes rather than
/// months.
struct FermiSurfaceConfig {
    /// ABSOLUTE path to the MLWF job directory (holding wannier.json and the
    /// path to the wavefunctions it localized).
    std::string mlwfDir;

    /// Samples along b1, b2 and b3 — independently.
    ///
    /// Not one number, because reciprocal cells are rarely cubic. A slab's
    /// short real-space c gives a long b3, and sampling it as densely as the
    /// two in-plane directions spends most of the (multiplicative) cost
    /// resolving a direction the physics is flat along. Anisotropic sampling
    /// is how a layered material's surface is resolved at all at a tractable
    /// point count.
    int gridSamples[3] = {32, 32, 32};

    /// Fermi level offset, eV. 0 is the calculation's own E_F; scanning it is
    /// how a rigid-band doping study is done, and how nested features are
    /// found.
    double energyOffsetEv = 0.0;

    /// Number of Wannier localization iterations before interpolating. The
    /// interpolation is only as good as the localization: a poorly localized
    /// H(R) is long-ranged and its interpolated bands ring between the
    /// computed k-points.
    int maxIterations = 50;
};

/// Turns a FermiSurfaceConfig into a standalone script that rebuilds the MLWF
/// localization, interpolates the band energies onto a Γ-centred 3D grid
/// spanning one reciprocal cell, and writes `fermi_surface.json` for the
/// viewer to extract the E = E_F isosurface from.
///
/// The grid spans the reciprocal UNIT CELL (fractional −1/2 … 1/2), not the
/// first Brillouin zone: the two cover the same volume and tile the same way,
/// but the parallelepiped is what a regular grid can be laid on. Clipping to
/// the Wigner-Seitz cell is the viewer's job, where the sheet already exists
/// as triangles.
std::string generateFermiSurfaceScript(const FermiSurfaceConfig& cfg);

} // namespace calango::core
