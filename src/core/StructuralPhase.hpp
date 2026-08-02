#pragma once

#include "core/Structure.hpp"

#include <array>
#include <vector>

namespace calango::core {

/// The local crystal structure an atom's neighbour topology matches.
///
/// This is a LOCAL, per-atom label, not a phase of the whole cell: in a
/// nanoparticle the core comes out fcc while the {111} facets read as hcp, and
/// in a deformed metal the stacking faults are exactly the hcp-labelled planes
/// inside an fcc grain. That is the point of the analysis — the interesting
/// objects (twins, faults, grain boundaries, the amorphous fraction of a melt)
/// are the places where the label CHANGES.
///
/// Enum order is the display order and the color-table order; values are
/// persisted in the project file, so entries are appended, never inserted.
enum class StructuralPhase {
    /// Nothing matched: surfaces, defect cores, liquids and glasses all land
    /// here. Not a failure of the analysis — "other" is a real answer, and in a
    /// melt it is the correct one for nearly every atom.
    Other,
    Fcc,
    Hcp,
    Bcc,
    /// A 13-atom icosahedral shell. Falls out of the same signature test for
    /// free, and it is the structure small metal clusters actually adopt, so
    /// suppressing it would mislabel them "other".
    Icosahedral,
    /// Cubic diamond (the Si / Ge / C-diamond lattice, ABC stacking).
    CubicDiamond,
    /// Hexagonal diamond, a.k.a. lonsdaleite (AB stacking) — the diamond
    /// analogue of hcp, and what a diamond stacking fault reads as.
    HexagonalDiamond,
};

/// Number of entries in StructuralPhase, for color tables and histograms.
inline constexpr int kStructuralPhaseCount = 7;

/// Short label ("FCC", "Cubic diamond", …). Not translated: it is also written
/// into exported data files and into the generated analysis text, where a
/// locale-dependent structure name would be a bug.
const char* toString(StructuralPhase phase);

struct StructuralPhaseOptions {
    /// How far the neighbour search reaches, as a multiple of the pair's summed
    /// covalent radii.
    ///
    /// It has to comfortably clear the SECOND coordination shell, because both
    /// the bcc test (whose 6 second neighbours are part of the 14 it needs) and
    /// the diamond test (which classifies an atom by its 12 second neighbours)
    /// live there. 1.9 covers the bcc second shell (a, vs. a nearest-neighbour
    /// distance of 0.87 a) and the diamond one (0.71 a vs. 0.43 a) with room
    /// for thermal disorder, without dragging in so many atoms that the
    /// per-atom sort dominates the run time.
    ///
    /// Everything past this point is ADAPTIVE: the classifier derives its own
    /// cutoff per atom from that atom's own neighbour distances, so a single
    /// global radius does not have to be right for every phase in the cell.
    /// This number only has to be big enough.
    double searchScale = 1.9;

    /// Run the second-shell diamond test on atoms that come out four-fold
    /// coordinated. Costs one extra CNA pass over those atoms only; off is for
    /// pure-metal trajectories where no diamond phase can appear.
    bool detectDiamond = true;
};

struct StructuralPhaseResult {
    /// One label per atom, index-aligned with structure.atoms().
    std::vector<StructuralPhase> phases;
    /// How many atoms carry each label, indexed by StructuralPhase.
    std::array<int, kStructuralPhaseCount> counts{};

    int count(StructuralPhase phase) const
    {
        return counts[static_cast<std::size_t>(phase)];
    }
};

/// Classify every atom by its neighbour topology (adaptive common-neighbour
/// analysis).
///
/// The method is CNA in the adaptive form of Stukowski, *Modelling Simul.
/// Mater. Sci. Eng.* **20**, 045021 (2012). Each atom is described by the
/// signature of its bonded neighbour pairs — for every neighbour, how many
/// neighbours the two have in common, how many bonds those share, and how long
/// the longest chain of those bonds is. Ideal lattices give exact signatures:
///
///   fcc          12 neighbours, all (4,2,1)
///   hcp          12 neighbours,  6 x (4,2,1) + 6 x (4,2,2)
///   bcc          14 neighbours,  6 x (4,4,4) + 8 x (6,6,6)
///   icosahedral  12 neighbours, all (5,5,5)
///
/// What makes it ADAPTIVE — and what makes it usable on a finite-temperature
/// snapshot at all — is that the bond cutoff is not a parameter. It is derived
/// per atom from that atom's own neighbour distances, placed midway between the
/// shell it must include and the one it must exclude. A single global cutoff
/// cannot do this: the value that resolves fcc's first shell from its second
/// falls in the middle of bcc's first-plus-second shell, so a cell containing
/// both phases (which is exactly what a martensitic transformation is) has no
/// correct global choice.
///
/// Diamond is not a CNA structure — a four-fold-coordinated atom has almost no
/// common neighbours — so it is identified the standard indirect way (Maras et
/// al., *Comput. Phys. Commun.* **205**, 13 (2016)): the twelve SECOND
/// neighbours of a cubic-diamond atom sit on an fcc shell, and of a hexagonal
/// diamond atom on an hcp one. So the same signature test is run against the
/// second shell, and its answer is translated.
///
/// Periodic images are enumerated explicitly, so a primitive cell classifies
/// correctly (an atom in a one-atom fcc cell reports fcc, from twelve
/// neighbours that are all images of itself).
StructuralPhaseResult identifyStructuralPhases(
    const Structure& structure, const StructuralPhaseOptions& options = {});

} // namespace calango::core
