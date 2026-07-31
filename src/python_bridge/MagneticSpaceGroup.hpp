#pragma once

#include "core/Structure.hpp"

#include <string>
#include <vector>

namespace calango::pybridge {

/// Magnetic space group (MSG) determination from the atomic coordinates plus
/// the magnetic moments.
///
/// The classification follows the Belov-Neronova-Smirnova (BNS) scheme as laid
/// out in Watanabe, Po & Vishwanath, "Structure and topology of band structures
/// in the 1651 magnetic space groups", Sci. Adv. 4, eaat8685 (2018). There are
/// 1651 MSGs. Writing the full group as M = G + A, with G the UNITARY
/// operations and A the ANTIUNITARY ones (each a spatial operation combined
/// with time reversal T), the four BNS types are:
///
///   Type I   — A is empty. M = G, an ordinary space group. Every symmetry is
///              unitary; the magnetic order breaks time reversal outright.
///              230 of them, one per space group.
///   Type II  — A = T·G, the "grey" groups. T alone is a symmetry, which is
///              only possible when every moment vanishes: these are the
///              NON-MAGNETIC structures. 230 of them.
///   Type III — A = T·g₀·G with g₀ a spatial operation that is NOT a pure
///              translation. A halving subgroup of the parent space group
///              stays unitary; the rest survives only in combination with T.
///              674 of them.
///   Type IV  — A = T·g₀·G with g₀ a pure TRANSLATION — an anti-translation.
///              The magnetic unit cell is a supercell of the crystallographic
///              one; this is the classic two-sublattice antiferromagnet.
///              517 of them.
///
/// An MSG is labelled `S.L` in BNS notation, S being one of the 230 ordinary
/// space groups and L an index distinguishing its magnetic descendants — the
/// notation the paper uses throughout (its MSG 2.4, 209.51, 227.131, …).
///
/// GUI-thread only (embedded interpreter); requires spglib ≥ 2.0 and numpy.
class MagneticSpaceGroup {
public:
    /// Where the moments come from. A structure can carry both a converged
    /// result and the guess it started from, and they are different data: the
    /// guess says what was ASKED for, the result says what the calculation
    /// FOUND, and an ordering that collapsed during the SCF is exactly the
    /// case where the two disagree and the difference matters.
    enum class MomentSource {
        Auto,     ///< computed moments if present, else the initial ones
        Computed, ///< `magmoms` — the converged result
        Initial,  ///< `initial_magmoms` — the seeded guess
    };

    /// One symmetry operation of the magnetic group: the spatial part in the
    /// fractional basis, plus whether it is combined with time reversal.
    struct Operation {
        int rotation[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        double translation[3] = {0.0, 0.0, 0.0};
        /// True for the ANTIUNITARY operations, the elements of A above.
        bool timeReversal = false;
    };

    struct Result {
        // -- BNS / OG identification ---------------------------------------
        /// BNS label `S.L`, e.g. "221.97". The identifier the tables are
        /// keyed by, and the one the paper cites.
        std::string bnsNumber;
        /// Opechowski-Guccione label, e.g. "229.6.1643". The other convention
        /// in the literature; listed so a reader can cross-reference either.
        std::string ogNumber;
        /// Position in the enumeration of all 1651 MSGs (spglib's UNI number).
        int uniNumber = 0;
        int litvinNumber = 0;
        /// BNS type, 1-4 (the paper's types I-IV).
        int type = 0;
        /// The parent space-group number S of the BNS label.
        int parentNumber = 0;
        std::string parentSymbol;

        // -- Underlying crystallography (moments ignored) ------------------
        //
        // Reported alongside so the two can be COMPARED. The magnetic order
        // can only lower the symmetry, and how far it lowers it is the
        // physically interesting number — the operations in this group but not
        // in the unitary part above are precisely those the magnetism broke.
        std::string crystalSpaceGroup;
        int crystalSpaceGroupNumber = 0;
        std::string crystalPointGroup;
        /// Point group of the unitary subgroup G — the operations that survive
        /// WITHOUT needing time reversal.
        std::string unitaryPointGroup;

        // -- Group order ----------------------------------------------------
        int operations = 0;            ///< |M|
        int unitaryOperations = 0;     ///< |G|
        int antiunitaryOperations = 0; ///< |A|
        std::vector<Operation> symmetryOperations;
        /// For a type-IV group: the anti-translation, i.e. the fractional
        /// translation that is a symmetry only when combined with time
        /// reversal. This is the vector that doubles the magnetic cell.
        bool hasAntiTranslation = false;
        double antiTranslation[3] = {0.0, 0.0, 0.0};

        // -- The magnetic configuration analysed ---------------------------
        std::string momentSource;   ///< "magmoms", "initial_magmoms", "manual"
        bool collinear = true;
        double totalMoment = 0.0;   ///< |Σ mᵢ| in μB per cell
        double absoluteMoment = 0.0;///< Σ |mᵢ| in μB per cell
        /// "Non-magnetic", "Ferromagnetic", "Ferrimagnetic",
        /// "Antiferromagnetic" — read off the two sums above.
        std::string ordering;
        /// Symmetry-equivalence class of each atom UNDER THE MAGNETIC GROUP,
        /// index-aligned with the structure's atoms. Two atoms of the same
        /// element in different classes here are the two sublattices of an
        /// antiferromagnet.
        std::vector<int> equivalentAtoms;
        int uniqueSites = 0;

        std::string error; ///< empty on success
    };

    /// Per-atom moment vectors carried by the structure. Collinear results are
    /// one number per atom (the calculation quantized the spin along z), and
    /// are promoted to (0, 0, m) — Structure::resolvedVectorField does the
    /// same for the renderer, so what is analysed is what is drawn.
    /// `sourceName` receives the array the moments actually came from.
    static std::vector<core::Vec3> momentsFor(const core::Structure& structure,
                                              MomentSource source,
                                              std::string* sourceName = nullptr);

    /// `moments` must be one vector per atom. `magSymprec` is the tolerance on
    /// the moments themselves (μB); it is separate from the positional
    /// `symprec` because a moment converged to 1.98 μB against a neighbour's
    /// −2.02 μB is one antiferromagnet, not two inequivalent sites.
    static Result analyze(const core::Structure& structure,
                          const std::vector<core::Vec3>& moments,
                          double symprec = 1e-4, double magSymprec = 1e-3);
};

} // namespace calango::pybridge
