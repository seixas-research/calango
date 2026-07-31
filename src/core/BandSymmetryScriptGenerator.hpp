#pragma once

#include <string>

namespace calango::core {

/// Symmetry classification of electronic bands at the high-symmetry points of
/// a band path: which irreducible representation of the little group each band
/// (or degenerate multiplet) realizes.
///
/// This is the analysis of Kogan & Nazarov, "Symmetry classification of energy
/// bands in graphene", Phys. Rev. B 85, 115418 (2012), generalized to an
/// arbitrary crystal. The method is the textbook one and has three stages:
///
///   1. spglib supplies the space-group operations {R|t} of the cell. At a
///      wave vector k the LITTLE GROUP is the subset that leaves k invariant
///      modulo a reciprocal-lattice vector, R⁻ᵀk = k + G₀. At a general k that
///      is only the identity; at the symmetry POINTS of the zone (Γ, K, M, …)
///      it can be as large as the whole point group, which is exactly why the
///      classification is worth making there and nowhere else.
///
///   2. The character of a degenerate multiplet under {R|t} is the trace
///      χ(R) = Σ_n ⟨ψ_nk | {R|t} | ψ_nk⟩, evaluated in the plane-wave
///      representation of the Kohn-Sham states. Writing ψ = Σ_G c_G
///      e^{2πi(k+G)·x}, the operation maps the coefficient at G to G' = G₀ +
///      R⁻ᵀG and multiplies it by e^{-2πi(k+G')·t}, so the whole trace is one
///      permutation of the FFT coefficient array plus a phase — exact for any
///      grid, any translation and any (including nonsymmorphic) operation.
///
///   3. The characters are reduced against the character table of the little
///      co-group, computed numerically from the class-sum algebra (Burnside)
///      rather than looked up, and each multiplet is labelled with the Mulliken
///      symbols of the irreps it contains. This is the same machinery the
///      Symmetry dialog uses for the Γ-point factor-group analysis, so a band
///      label and a phonon label out of this application mean the same thing.
///
/// Nonsymmorphic caveat, stated honestly rather than papered over: at a zone
/// BOUNDARY point of a nonsymmorphic group the little-group representations are
/// PROJECTIVE, and the ordinary point-group table cannot label them. The
/// generated script detects this — the reduction leaves non-integer
/// multiplicities — and marks the point `projective`, reporting the raw
/// characters instead of inventing a Mulliken symbol for them.
struct BandSymmetryConfig {
    /// spglib symmetry-finding tolerance (Å) for the space-group operations.
    double symprec = 1e-4;
    /// Bands closer than this are treated as one degenerate multiplet and get
    /// a single, shared character. It has to be a real window rather than an
    /// exact test: two states that are degenerate by symmetry still come out
    /// of a diagonalization a few µeV apart, and splitting them yields two
    /// meaningless one-dimensional characters instead of one correct
    /// two-dimensional one.
    double degeneracyEv = 0.02;
    /// Bands above this many eV from the Fermi level are skipped. The states
    /// worth classifying are the ones near the gap; classifying 200 empty
    /// bands costs an FFT permutation each and says nothing.
    double windowEv = 25.0;
    /// Also classify a generic point of each symmetry LINE (the midpoint of
    /// every path segment), not only the high-symmetry points at its ends.
    ///
    /// This is what makes the COMPATIBILITY RELATIONS visible: an irrep at a
    /// point has to decompose into the irreps of the lines running out of it,
    /// and that decomposition is the whole content of Kogan & Nazarov's
    /// Table III. It is also how a band is followed through a crossing — two
    /// bands that touch but carry different line irreps cannot hybridize, and
    /// only the line labels say so. One extra k-point per segment.
    bool classifyLines = true;
};

/// Python block appended to the Electronic Structure script (GPAW backend).
/// Expects `band_calc` (the fixed-density calculator evaluated along the path),
/// `bs` (the ASE BandStructure) and `efermi` to be in scope, and writes
/// `band_symmetry.json` next to `bands.json`.
std::string generateBandSymmetryBlock(const BandSymmetryConfig& config);

} // namespace calango::core
