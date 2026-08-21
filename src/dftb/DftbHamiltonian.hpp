#pragma once

#include "core/Structure.hpp"
#include "dft/DftTypes.hpp"
#include "dftb/DftbBasis.hpp"
#include "dftb/SlaterKosterTable.hpp"

#include <array>
#include <complex>
#include <vector>

/// Real-space two-center pair construction and Bloch summation, H(k)/S(k)
/// for an s,p Slater-Koster basis.
///
/// UNITS: internally Hartree/Bohr throughout (matching both the .skf format
/// itself and calango::dft — see SlaterKosterFile.hpp); `build()` is the one
/// place Structure's Angstrom Cartesian positions get converted.
namespace calango::dftb {

/// One real-space two-center contribution: atom `atomI`'s orbitals (rows)
/// against atom `atomJ`'s orbitals in periodic image `image` (columns).
/// `atomI == atomJ` with `image == {0,0,0}` (the on-site block) is NEVER
/// represented here — see DftbHamiltonianBuilder::onsite* below instead.
/// `atomI == atomJ` with a NONZERO image (an atom interacting with its own
/// periodic copy) is a normal, homonuclear two-center pair like any other.
struct DftbPairBlock {
    int atomI = 0;
    int atomJ = 0;
    std::array<int, 3> image{{0, 0, 0}};
    double distanceBohr = 0.0;
    /// [s,px,py,pz](atomI) x [s,px,py,pz](atomJ), row-major, Hartree.
    std::array<double, 16> h{};
    /// Same layout, dimensionless (overlap).
    std::array<double, 16> s{};
    /// pos(atomJ, in this image) - pos(atomI), Cartesian bohr — the force
    /// evaluator differentiates SlaterKosterFile's radial functions along
    /// this direction.
    core::Vec3 bondVectorBohr;
};

class DftbHamiltonianBuilder {
public:
    /// Enumerate every real-space pair within the loaded parameter set's
    /// tabulated range, for one fixed geometry. `basis` must already be
    /// built from the SAME atomic-number list, in the SAME atom order, as
    /// `structure.atoms()`.
    dft::Outcome build(const core::Structure& structure,
                        const SlaterKosterTable& table,
                        const DftbBasis& basis);

    const std::vector<DftbPairBlock>& pairs() const { return pairs_; }
    int dimension() const { return dimension_; }
    const DftbBasis* basis() const { return basis_; }

    /// H(k) and S(k), `dimension() x dimension()`, row-major, Hermitian by
    /// construction (see the .cpp for the transpose identity this relies
    /// on — DftbTest.cpp checks it numerically too).
    ///
    /// `kFrac` is fractional reciprocal coordinates (matching
    /// dft::KPointGrid's own convention): the Bloch phase for image
    /// (n1, n2, n3) is exp(i * 2*pi * (kFrac . (n1, n2, n3))), which needs
    /// no reciprocal-lattice-vector bookkeeping at all — the standard,
    /// simplest correct convention. A molecule (no atomI==atomJ-only-image
    /// pairs beyond the trivial on-site block; `pairs()` never carries a
    /// nonzero image when the structure is non-periodic) ignores `kFrac`
    /// entirely and returns the identical real matrix for any k — the
    /// Gamma-only case the task calls for.
    ///
    /// `atomicShiftHartree`, one entry per atom (empty means all-zero, the
    /// non-SCC case), is added into H via the standard SCC-DFTB second-order
    /// term: H1_mu(A),nu(B) = 0.5 * S_mu,nu * (shift_A + shift_B) — including
    /// the on-site block, where mu==nu on the SAME atom A gives the familiar
    /// H1_AA = shift_A (S_mu,mu == 1 there).
    void blochMatrices(const std::array<double, 3>& kFrac,
                        std::vector<std::complex<double>>& hOut,
                        std::vector<std::complex<double>>& sOut,
                        const std::vector<double>& atomicShiftHartree = {})
        const;

private:
    std::vector<DftbPairBlock> pairs_;
    /// One entry per orbital (dimension_ long), Hartree — Es/Ep read off
    /// each atom's own homonuclear on-site line, filled once by build().
    std::vector<double> onsiteEnergyHartree_;
    const DftbBasis* basis_ = nullptr;
    int dimension_ = 0;
};

} // namespace calango::dftb
