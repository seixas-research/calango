#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace calango::core {

/// Stereo annotation on a bond, in the chemical-drawing sense: which end comes
/// out of the page. The narrow end of the glyph always sits on atom `a` — that
/// is the whole reason a bond carries a direction here even though the graph
/// itself is undirected.
enum class BondStereo {
    None,   ///< plain line
    Wedge,  ///< solid wedge — `b` is above the plane
    Hash,   ///< hashed wedge — `b` is below the plane
};

/// One vertex of a 2D sketch.
///
/// The position is in SKETCH UNITS, not Ångström: a standard bond is
/// `MoleculeGraph::kBondLength` long and every snapping rule is expressed
/// against that. Converting to a real geometry is MoleculeEmbed3d's job and
/// happens exactly once, at export — keeping the drawing dimensionless is what
/// lets a user stretch a bond on screen without claiming a bond length.
struct MolAtom {
    int atomicNumber = 6;
    double x = 0.0;
    double y = 0.0;
    int charge = 0;            ///< formal charge, drawn as a superscript
    int radicalElectrons = 0;  ///< unpaired electrons, drawn as dots
    /// Hydrogens the user has PINNED on this atom. -1 (the default) means
    /// "whatever the valence implies", which is what a sketcher normally
    /// wants; 0 or more overrides that. Kept as a separate concept from the
    /// implied count because "this nitrogen has no hydrogens" is a chemical
    /// statement a drawing must be able to make.
    int explicitHydrogens = -1;
};

struct MolBond {
    int a = 0;
    int b = 0;
    /// 1 single, 2 double, 3 triple. Aromatic bonds are NOT a fourth order
    /// here: the canvas draws — and this model stores — a Kekulé structure,
    /// which is both what ChemDraw draws and what makes the valence arithmetic
    /// exact. Aromaticity is PERCEIVED (see perceiveAromaticBonds) wherever it
    /// is needed, never stored.
    int order = 1;
    BondStereo stereo = BondStereo::None;

    bool touches(int atom) const { return a == atom || b == atom; }
    int other(int atom) const { return a == atom ? b : a; }
};

/// A free-text caption placed on the canvas — a label, a reaction condition, a
/// note. Not part of the chemistry: captions are never exported to 3D and
/// never affect a formula.
struct MolCaption {
    std::string text;
    double x = 0.0;
    double y = 0.0;
};

/// The 2D molecular graph the sketcher edits: the single source of truth for
/// rendering, editing, undo and export.
///
/// Deliberately Qt-free and geometry-light. It knows chemistry (valence,
/// implicit hydrogens, ring membership, connected fragments) and it knows where
/// things sit on a flat canvas; it knows nothing about painters, tools or
/// files. Every editing operation is a plain value mutation, so a snapshot of
/// the whole graph IS an undo step — which is what the dialog's undo stack
/// stores.
///
/// DISCONNECTED FRAGMENTS ARE LEGAL. A canvas holds a drawing, not a molecule;
/// several molecules side by side is the normal case in a sketch and every
/// query that could care (fragments(), formula()) is written for it.
class MoleculeGraph {
public:
    /// One bond, in sketch units. Every default geometry — ring radii, chain
    /// steps, the length a freshly dragged bond snaps to — is a multiple of
    /// this, so changing it rescales a drawing rather than distorting it.
    static constexpr double kBondLength = 1.0;

    // -- Contents -----------------------------------------------------------

    const std::vector<MolAtom>& atoms() const { return atoms_; }
    std::vector<MolAtom>& atoms() { return atoms_; }
    const std::vector<MolBond>& bonds() const { return bonds_; }
    std::vector<MolBond>& bonds() { return bonds_; }
    const std::vector<MolCaption>& captions() const { return captions_; }
    std::vector<MolCaption>& captions() { return captions_; }

    int atomCount() const { return static_cast<int>(atoms_.size()); }
    int bondCount() const { return static_cast<int>(bonds_.size()); }
    bool empty() const { return atoms_.empty() && captions_.empty(); }
    void clear();

    // -- Editing ------------------------------------------------------------

    /// Append an atom and return its index.
    int addAtom(int atomicNumber, double x, double y);
    int addAtom(const MolAtom& atom);

    /// Bond `a` to `b` at `order`, or, when the pair is ALREADY bonded, raise
    /// that bond's order to `order`. Returns the bond index, or -1 for a
    /// self-bond or an out-of-range index.
    int addBond(int a, int b, int order = 1,
                BondStereo stereo = BondStereo::None);

    /// ChemDraw's defining gesture: drawing a bond onto an existing one cycles
    /// its order 1 -> 2 -> 3 -> 1. Returns the new order, or 0 when `bond` is
    /// out of range. A stereo bond drops its stereo flag on the way, because a
    /// hashed double bond is not a thing anyone means to draw.
    int cycleBondOrder(int bond);

    /// Remove one atom and every bond that touched it. Remaining indices are
    /// COMPACTED (the atom list is a vector, not a slot map), so any index a
    /// caller is holding across this call is stale — which is why the dialog
    /// deletes through a selection set and rebuilds rather than by index.
    void removeAtom(int index);
    void removeBond(int index);
    /// Remove a whole set of atoms in one pass; indices out of range are
    /// ignored. Much safer than a loop of removeAtom() precisely because of the
    /// compaction above.
    void removeAtoms(const std::vector<int>& indices);
    void removeCaption(int index);

    // -- Queries ------------------------------------------------------------

    /// Index of the bond joining `a` and `b`, or -1.
    int bondBetween(int a, int b) const;
    /// Indices of the bonds touching `atom`.
    std::vector<int> bondsAt(int atom) const;
    /// Indices of the atoms bonded to `atom`.
    std::vector<int> neighbors(int atom) const;

    /// Sum of the orders of the bonds at `atom` — the σ+π count the valence is
    /// measured against. Hydrogens pinned via `explicitHydrogens` are NOT
    /// included; see implicitHydrogens().
    int bondOrderSum(int atom) const;

    /// Hydrogens this atom carries but does not draw.
    ///
    /// `explicitHydrogens >= 0` is returned as-is. Otherwise the smallest
    /// standard valence that still accommodates the drawn bonds is chosen and
    /// the shortfall is the hydrogen count — so a three-bonded carbon gets one
    /// hydrogen, a nitrogen with four bonds is read as a valence-5 nitrogen
    /// with none rather than as an error, and a pentavalent carbon (which has
    /// no valence that fits) gets zero.
    int implicitHydrogens(int atom) const;

    /// Total hydrogens on `atom` — implicit plus any drawn as their own
    /// vertices.
    int hydrogenCount(int atom) const;

    /// True when the drawn bonds exceed every standard valence the element
    /// has at this formal charge — a pentavalent carbon, a divalent hydrogen.
    ///
    /// Reported, never enforced: chemists sketch intermediates, transition
    /// states and deliberate nonsense, and a sketcher that refuses the stroke
    /// is a sketcher nobody finishes a mechanism in. The canvas paints these
    /// atoms in a warning colour and moves on.
    bool valenceViolated(int atom) const;

    /// Connected components, as lists of atom indices, in ascending order of
    /// their lowest member. An isolated atom is a fragment of one.
    std::vector<std::vector<int>> fragments() const;

    /// The subgraph induced by `atomIndices`, with bonds whose BOTH ends are
    /// in the set. Captions are not carried. The mapping from old index to new
    /// is the position within a sorted, deduplicated `atomIndices`.
    MoleculeGraph subgraph(const std::vector<int>& atomIndices) const;

    /// Append every atom, bond and caption of `other`, offset by (dx, dy).
    /// Returns the index the first appended atom landed at, so a caller can
    /// address what it just pasted.
    int append(const MoleculeGraph& other, double dx = 0.0, double dy = 0.0);

    /// Hill-ordered formula of the whole canvas, implicit hydrogens included —
    /// "C6H6", "C2H6O". Empty for an empty canvas.
    std::string formula() const;
    /// The same for one fragment.
    std::string formula(const std::vector<int>& atomIndices) const;

    /// Axis-aligned bounds of the atoms and captions, in sketch units. Returns
    /// false (leaving the outputs untouched) for an empty canvas.
    bool bounds(double& minX, double& minY, double& maxX, double& maxY) const;

    // -- Rings --------------------------------------------------------------

    /// The smallest ring through each bond, deduplicated — a practical
    /// stand-in for the SSSR that is enough for what this module asks of it
    /// (aromatic perception, ring-planarity restraints in the 3D embedding,
    /// and deciding where a fused template's new atoms go).
    ///
    /// Each ring is a cycle of atom indices in traversal order. Rings larger
    /// than `maxSize` are not reported; the default covers everything the
    /// template palette can draw plus the fused systems they build.
    std::vector<std::vector<int>> rings(int maxSize = 10) const;

    /// Which bonds belong to a perceived aromatic ring.
    ///
    /// The graph stores Kekulé structures, so this is derived, not read: a ring
    /// of 5 or 6 qualifies when every member contributes to a closed π system
    /// (one ring double bond, or a lone-pair heteroatom with none) and the
    /// total is 4n+2. Benzene, pyridine, pyrrole, furan and thiophene pass;
    /// cyclohexene and cyclopentadiene do not.
    ///
    /// Returns a bond-indexed flag vector. Used by SMILES export (lowercase
    /// atoms) and by the 3D embedding (one bond length for the whole ring
    /// instead of the alternating long/short one a Kekulé structure would
    /// otherwise relax to).
    std::vector<bool> perceiveAromaticBonds() const;

    // -- Geometry helpers ---------------------------------------------------

    /// Index of the atom within `radius` of (x, y), nearest first, or -1.
    int atomAt(double x, double y, double radius) const;
    /// Index of the bond whose segment passes within `radius` of (x, y), or -1.
    int bondAt(double x, double y, double radius) const;
    /// Index of the caption whose anchor is within `radius` of (x, y), or -1.
    int captionAt(double x, double y, double radius) const;

    /// Move one atom.
    void moveAtom(int index, double x, double y);
    /// Translate a set of atoms and captions.
    void translate(const std::vector<int>& atomIndices,
                   const std::vector<int>& captionIndices, double dx,
                   double dy);

private:
    std::vector<MolAtom> atoms_;
    std::vector<MolBond> bonds_;
    std::vector<MolCaption> captions_;
};

// ---------------------------------------------------------------------------
// Valence
// ---------------------------------------------------------------------------

/// The standard valences of element `z` at formal charge `q`, ascending —
/// {3} for boron, {4} for carbon, {3, 5} for nitrogen, {2, 4, 6} for sulfur.
/// Empty for an element with no useful organic valence (a metal, a noble gas),
/// which is the signal to leave the atom alone rather than guess at it.
///
/// The charge rule is the conventional one and is applied by GROUP, because
/// the two halves of the p block respond to a charge in opposite directions:
/// a group 15-17 element GAINS a valence per positive charge (ammonium N is
/// tetravalent, hydronium O trivalent) and LOSES one per negative charge
/// (hydroxide O is monovalent), while a group 13-14 element loses one per unit
/// of charge of EITHER sign — carbocation and carbanion are both trivalent —
/// except that group 13 gains one when negative, which is what makes
/// borohydride tetravalent.
std::vector<int> standardValences(int z, int charge = 0);

/// True when `z` is an element the sketcher will place implicit hydrogens on
/// at all — equivalently, standardValences(z) is non-empty.
bool hasOrganicValence(int z);

// ---------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------

/// The ring palette, in the order the sidebar shows it.
enum class RingTemplate {
    Cyclopropane,
    Cyclobutane,
    Cyclopentane,
    Cyclohexane,
    Cycloheptane,
    Cyclooctane,
    Benzene,
    Cyclopentadiene,
    Naphthalene,
};

/// Every template, in palette order.
const std::vector<RingTemplate>& ringTemplates();
/// "Cyclohexane", "Benzene" — the tooltip and the doc table read from here.
const char* ringTemplateName(RingTemplate ring);
/// Ring size, or 10 for naphthalene (two fused six-rings, 10 atoms).
int ringTemplateSize(RingTemplate ring);

/// A free-standing template, centred on (cx, cy), as its own graph.
MoleculeGraph makeRing(RingTemplate ring, double cx, double cy);

/// Stamp `ring` onto the canvas, FUSED to the existing bond `bondIndex`: the
/// two atoms of that bond become two atoms of the new ring and only the
/// remainder are created, on whichever side of the bond is emptier. Returns
/// false (changing nothing) for an out-of-range bond, or for a template that
/// cannot be fused edge-on (naphthalene, which is already a fused system —
/// it is stamped free-standing instead).
bool fuseRing(MoleculeGraph& graph, int bondIndex, RingTemplate ring);

/// Stamp `ring` free-standing at (cx, cy). Returns the index of its first
/// atom.
int stampRing(MoleculeGraph& graph, RingTemplate ring, double cx, double cy);

/// Grow a zig-zag alkyl chain of `length` bonds from `startAtom`, the first
/// step heading at `angleDeg`, alternating ±30° about it thereafter — the
/// standard drawn alkane. Returns the indices of the atoms created (empty for
/// `length <= 0`). `startAtom` itself is not created and is not in the result.
std::vector<int> growChain(MoleculeGraph& graph, int startAtom, double angleDeg,
                           int length);

// ---------------------------------------------------------------------------
// Clean-up
// ---------------------------------------------------------------------------

/// Regularize bond lengths and angles by relaxing the 2D graph — the "Tidy"
/// button, and the single most-used control in any real sketcher.
///
/// A spring system, not a layout algorithm: every bond pulls toward
/// kBondLength, every bond PAIR at a shared atom pushes toward the angle its
/// coordination implies (180° for two bonds on an sp centre, 120° for three,
/// 109.5°-drawn-as-120° otherwise), every ring pushes toward a regular polygon,
/// and non-bonded atoms of the same fragment repel weakly so a folded drawing
/// opens out. Fragments keep their relative placement — tidying a canvas of
/// three molecules must not stack them on each other.
///
/// `atomIndices` empty means the whole canvas; otherwise only those atoms move
/// (everything else is held fixed, which is what makes tidying a selection a
/// local repair rather than a redraw).
void tidy(MoleculeGraph& graph, const std::vector<int>& atomIndices = {},
          int iterations = 400);

} // namespace calango::core
