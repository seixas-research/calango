#pragma once

#include <array>

/// Slater-Koster angular transformation for an s,p Cartesian orbital basis.
///
/// SOURCE: Slater & Koster, Phys. Rev. 94, 1498 (1954), Table I. This is the
/// standard two-center integral table reproduced identically in essentially
/// every tight-binding reference (e.g. Harrison, "Electronic Structure and
/// the Properties of Solids"); s,p orbitals only, no d — see DftbBasis.hpp
/// for the scope statement (d-shell columns are parsed by
/// SlaterKosterFile.hpp so a heavier-element file round-trips exactly, but
/// are not yet assembled into a Hamiltonian).
///
/// THE BOND-DIRECTION / FILE-LOOKUP CONVENTION (read before touching this
/// file or SlaterKosterTable.hpp).
///
/// The .skf format's own spec (see SlaterKosterFile.hpp's citation) defines
/// H_ab0's two orbitals as: a on the atom AT THE ORIGIN ("atom alpha", the
/// FIRST-named species in "X-Y.skf"), b on the atom DISPLACED by r0
/// ("atom beta", the SECOND-named species), with r0 = pos(beta) - pos(alpha)
/// chosen to align the bond with the integral's own quantisation axis. Only
/// s<=p<=d-ordered channel NAMES exist (Hsp0, never Hps0) — the label order
/// is about angular momentum, not about which atom is which.
///
/// So for a bond from atom1 (species X) to atom2 (species Y), with
/// r0 = pos(atom2) - pos(atom1):
///
///   H_{s(atom1), p_i(atom2)} = +l_i * Hspsigma(X-Y.skf)   -- read DIRECTLY
///   H_{p_i(atom1), s(atom2)} = -l_i * Hspsigma(Y-X.skf)   -- READ REVERSED
///
/// The second line is NOT just a sign-flip of the first for a HETERONUCLEAR
/// pair: X-Y.skf's Hsp0 tabulates <s-shaped-like-X | p-shaped-like-Y>, which
/// is a physically different quantity from <p-shaped-like-X | s-shaped-like-Y>
/// (different atomic orbital shapes on each side) — the second genuinely
/// needs Y-X.skf's own Hsp0 column, evaluated with l_i still taken along
/// atom1->atom2 (equivalently, -l_i along atom2->atom1, which is Y-X.skf's
/// own r0 direction — the two minus signs are why the coefficient above is
/// -l_i and not +l_i). This is why `SpIntegrals` below carries the forward
/// and reversed sp-sigma values SEPARATELY rather than deriving one from the
/// other by a sign flip — for a HOMONUCLEAR pair X-Y.skf and Y-X.skf are the
/// SAME file, so the two happen to be numerically equal and the general
/// formula collapses to the textbook antisymmetric relation H_ps(r0) =
/// -H_sp(r0) as a special case, not as the rule this function assumes.
///
/// This is why SlaterKosterTable.hpp indexes BOTH orderings of every
/// heteronuclear pair rather than one — see its own class comment.
namespace calango::dftb {

/// The 5 two-center SK integral values (interpolated at one distance |r0|)
/// this s,p transform needs, ALREADY looked up in both directions — see the
/// class comment above for exactly why sp needs two independent values
/// rather than one plus a sign flip.
struct SpIntegrals {
    double ssSigma = 0.0;
    /// Hsp0 (or Ssp0) of "element(atom1)-element(atom2).skf" — s(atom1)
    /// FIRST-named, p(atom2) second. Used for the s(atom1)-p(atom2) block
    /// entries, with a +cosine coefficient.
    double spSigmaForward = 0.0;
    /// Hsp0 (or Ssp0) of "element(atom2)-element(atom1).skf" — the REVERSED
    /// file. Used for the p(atom1)-s(atom2) block entries, with a -cosine
    /// coefficient. Equal to `spSigmaForward` for a homonuclear pair (the
    /// same file either way) — this is what makes the textbook
    /// H_ps(r0) = -H_sp(r0) relation the homonuclear special case rather
    /// than the general rule.
    double spSigmaReverse = 0.0;
    double ppSigma = 0.0;
    double ppPi = 0.0;
};

/// The full 4x4 two-center block H_{mu(atom1), nu(atom2)} (or S, matching
/// whatever `integrals` holds), in the canonical orbital order
/// [s, px, py, pz] for BOTH atoms — row = atom1's orbital, column = atom2's
/// orbital, row-major (block[row * 4 + col]).
///
/// `l, m, n` are the direction cosines of r0 = pos(atom2) - pos(atom1)
/// (l^2 + m^2 + n^2 == 1 for a nonzero bond vector). The caller selects which
/// rows/columns are actually populated in the assembled Hamiltonian
/// according to each atom's active shells (DftbBasis.hpp) — a pure-s atom
/// (hydrogen in most parameter sets) simply never reads rows/columns 1..3
/// of its own block.
std::array<double, 16> skBlock(double l, double m, double n,
                                const SpIntegrals& integrals);

} // namespace calango::dftb
