#pragma once

#include "core/BandUnfolding.hpp"
#include "core/Structure.hpp"
#include "dft/DftTypes.hpp"
#include "dftb/DftbBasis.hpp"

#include <array>
#include <complex>
#include <vector>

/// Effective Band Structure: the DFTB-side Popescu-Zunger spectral weight,
/// for a supercell built and diagonalized in an LCAO (Slater-Koster) basis.
///
/// core::BandUnfolding.hpp already owns the scheme's GEOMETRY and
/// BROADENING (supercell/primitive matrix, K = fold(k), the Gaussian
/// spectral function) — its own doc says plainly that the projection
/// WEIGHTS come from whichever engine exposes its wavefunction coefficients,
/// and that side just consumes them. This module is that missing weight
/// computation for the LCAO case (GPAW/QE/SIESTA instead read plane-wave
/// coefficients — see UnfoldingScriptGenerator.cpp).
///
/// THE FORMULA. For a supercell that is N exact copies of a primitive cell
/// at lattice translations T_1..T_N (n indexing which copy), define, for
/// each orbital type alpha that exists once per primitive cell (e.g.
/// "carbon pz"), the primitive Bloch sum
///
///     |k, alpha> = (1/sqrt(N)) * sum_n exp(i k . T_n) |n, alpha>
///
/// where |n, alpha> is that SAME orbital in copy n. The projection of one
/// supercell eigenstate |Km> (LCAO coefficients c, dimension = total
/// orbitals) onto primitive k is
///
///     P_Km(k) = sum_alpha | <k,alpha | Km> |^2
///     <k,alpha | Km> = (1/sqrt(N)) sum_n exp(-i k . T_n) sum_nu S_{(n,alpha),nu} c_nu(Km)
///
/// i.e. exactly the Mulliken-style S*c contraction this module already uses
/// for charges and PDOS (DftbMulliken.hpp), phased and summed over the N
/// primitive images before the modulus-squared — this is where "the overlap
/// matrix enters the projection" for a non-orthogonal basis, per the task
/// this module was written to satisfy.
///
/// NORMALIZATION / VALIDATION. Summed over every primitive k' that folds
/// onto the SAME supercell K, sum_k' P_Km(k') = 1 exactly for ANY state
/// (a property of the S-weighted resolution of identity the primitive Bloch
/// sums form, not something assumed about the geometry being defect-free)
/// — DftbTest.cpp's "partition identity" check verifies this numerically,
/// mirroring the same invariant the existing plane-wave unfolding code
/// documents relying on for its own three-bug history.
namespace calango::dftb {

/// The atom-to-primitive-image assignment a supercell needs before any
/// weight can be computed. `primitiveSublatticeIndex[i]` is which atom of
/// the primitive cell's own atom list supercell atom `i` corresponds to;
/// `imageTranslationBohr[i]` is that atom's own T_n (Cartesian bohr, the
/// lattice translation from its ideal primitive-image position). Built once
/// per geometry via nearest-image matching — see build()'s own doc for why
/// that is what makes a DISPLACED/defective supercell still unfoldable.
struct DftbUnfoldingMap {
    core::SupercellMatrix matrix;
    std::vector<int> primitiveSublatticeIndex;
    /// T_n as an INTEGER combination of the primitive real-space lattice
    /// vectors (n1, n2, n3) — this, not the Cartesian translation, is what
    /// the Bloch phase needs: k . T_n = 2*pi * (kPrimitiveFrac . (n1,n2,n3))
    /// exactly (b_i . a_j = 2*pi*delta_ij), with no reciprocal-lattice
    /// vectors to build at all — the same trick
    /// DftbHamiltonianBuilder::blochMatrices() already uses for the
    /// two-center Bloch sum.
    std::vector<std::array<int, 3>> imageIndex;
    /// Number of primitive images actually present (== |det M|, checked
    /// against the atom count as a sanity cross-check).
    int imageCount = 0;

    /// Deduce M from the two cells (core::deduceSupercellMatrix) and assign
    /// every supercell atom to its nearest ideal primitive-image site —
    /// nearest, not exact, so a relaxed or DEFECTIVE supercell (a vacancy,
    /// a substitution, a small displacement) still unfolds; a site with no
    /// atom within a generous tolerance is left unassigned (index -1) and
    /// silently contributes nothing to any orbital group's Bloch sum, which
    /// is the physically right thing for a vacancy specifically.
    static dft::Outcome build(const core::Structure& supercell,
                               const core::Structure& primitive,
                               DftbUnfoldingMap& out, double* residual = nullptr);
};

/// P_Km(kPrimitiveFrac) for every eigenstate at supercell k-point
/// K = foldToSupercell(kPrimitiveFrac, map.matrix) — `eigenvectors`/`s` are
/// that K's own solveGeneralizedHermitian() output (dimension x dimension,
/// eigenvectors one per column) and S(K) respectively; `basis` is the
/// SUPERCELL's basis (DftbBasis::build on the full structure).
std::vector<double> dftbUnfoldingWeights(
    const std::vector<std::complex<double>>& eigenvectors,
    const std::vector<std::complex<double>>& overlapMatrix,
    const DftbBasis& basis, const DftbUnfoldingMap& map,
    const std::array<double, 3>& kPrimitiveFrac);

} // namespace calango::dftb
