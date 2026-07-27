#pragma once

#include "core/Atom.hpp"
#include "core/UnitCell.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace calango::core {

struct Bond {
    int i = 0;
    int j = 0;
    /// Cartesian shift applied to atom j's position to get the periodic
    /// image actually bonded to atom i. Zero for bonds inside the cell.
    Vec3 imageOffset{};
    /// Bond order: 1 single, 2 double, 3 triple, 4 aromatic. Orders are never
    /// auto-perceived — they are assigned manually (Bond Editor /
    /// setBondOrder) and default to single. Rendering draws order-n bonds as n
    /// parallel cylinders; aromatic renders as a double bond (two cylinders),
    /// which is the conventional depiction of a delocalized 1.5 bond.
    int order = 1;

    bool crossesBoundary() const { return imageOffset.dot(imageOffset) > 1e-12; }
};

/// Macromolecular annotation for one atom, as carried by PDB / PDBx-mmCIF
/// files: which chain and residue it belongs to and what the residue calls it.
///
/// A protein is not just a bag of atoms — the backbone connectivity that makes
/// a ribbon diagram possible, and the chain colouring that makes a complex
/// readable, are both statements about residues, not about elements. Without
/// this the 3000 carbons of a protein are indistinguishable from each other.
///
/// Empty/zero for every atom of a structure that came from a format with no
/// such notion (XYZ, POSCAR, a small-molecule CIF), which is the signal that a
/// residue-based representation has nothing to work with.
struct ResidueInfo {
    std::string chain;    ///< author chain id, e.g. "A"
    std::string residue;  ///< residue/component name, e.g. "LYS", "HOH"
    int residueSeq = 0;   ///< author residue sequence number
    std::string atomName; ///< atom name within the residue, e.g. "CA", "N"
    /// True for the α-carbon, the atom a backbone trace is drawn through.
    bool isAlphaCarbon() const { return atomName == "CA"; }
};

/// The central data model: a collection of atoms plus an optional periodic
/// cell. Pure data + geometry queries — it knows nothing about rendering,
/// Qt, files or Python (MVC "Model"). Views observe it read-only; the
/// controller (GUI actions, AseBridge) replaces or mutates it.
class Structure {
public:
    const std::vector<Atom>& atoms() const { return atoms_; }
    std::vector<Atom>& atoms() { return atoms_; }

    const UnitCell& cell() const { return cell_; }
    void setCell(const UnitCell& cell) { cell_ = cell; }

    void addAtom(const Atom& atom);
    void removeAtom(std::size_t index);
    void clear();

    bool empty() const { return atoms_.empty(); }
    std::size_t size() const { return atoms_.size(); }

    /// Hill-ordered chemical formula, e.g. "C2H6O".
    std::string chemicalFormula() const;

    Vec3 centroid() const;

    /// Radius of the bounding sphere around `center` (Å).
    double boundingRadius(const Vec3& center) const;

    /// Distance-based bond perception: a bond exists when
    /// d(i,j) < tolerance * (r_cov(i) + r_cov(j)).
    ///
    /// With a defined periodic cell, distances use the minimum-image
    /// convention, so bonds across cell boundaries are found and carry
    /// the image offset for rendering. O(N²) — fine for a few thousand
    /// atoms; a cell-list spatial index is on the roadmap (Phase 3).
    ///
    /// Manual overrides are always honored: pairs marked removed are
    /// suppressed, manually added bonds are appended (minimum-image, even
    /// beyond the cutoff). `autoDetect = false` skips perception entirely
    /// and returns only the manual bonds.
    std::vector<Bond> detectBonds(double tolerance = 1.15, bool autoDetect = true) const;

    // -- Manual bond overrides (Bond Editor) -------------------------------
    //
    // Pairs are stored index-normalized (i < j) and survive atom removal
    // (indices shift; pairs touching a removed atom are dropped).

    const std::vector<std::pair<int, int>>& addedBonds() const { return addedBonds_; }
    const std::vector<std::pair<int, int>>& removedBonds() const { return removedBonds_; }
    /// Force a bond between i and j (clears an opposing "removed" mark).
    void addBondOverride(int i, int j);
    /// Suppress the auto-detected bond between i and j (clears an opposing
    /// manual bond).
    void removeBondOverride(int i, int j);
    /// Forget any override for the pair / all overrides.
    void clearBondOverride(int i, int j);
    void clearBondOverrides();

    // -- Manual bond orders ------------------------------------------------
    //
    // Assigned per atom pair (index-normalized i < j, persisted like the
    // add/remove overrides). Applies to whichever bond — auto-detected or
    // manual — connects the pair; unlisted pairs are single bonds.

    const std::map<std::pair<int, int>, int>& bondOrders() const {
        return bondOrders_;
    }
    /// Order of the bond between i and j (1 when unassigned).
    int bondOrder(int i, int j) const;
    /// Assign order 1-4 (4 = aromatic); 1 resets the pair to the default.
    void setBondOrder(int i, int j, int order);

    // -- Per-atom scalar fields (charges, |forces|, potentials, ...) -------
    //
    // Named one-value-per-atom overlays used by the scalar color-mapping
    // modes. Fields are kept index-aligned with atoms(): addAtom() pads
    // every field with 0.0 and removeAtom() erases the matching entry.

    const std::map<std::string, std::vector<double>>& scalarFields() const {
        return scalarFields_;
    }
    /// Stores (or replaces) a field; ignored unless values.size() == size().
    void setScalarField(const std::string& name, std::vector<double> values);

    // -- Per-atom vector fields (forces, velocities, dipoles, ...) ---------
    //
    // One 3-vector per atom, index-aligned like the scalar fields; the
    // renderer draws them as arrows (Representation panel toggles).

    const std::map<std::string, std::vector<Vec3>>& vectorFields() const {
        return vectorFields_;
    }
    /// Stores (or replaces) a field; ignored unless values.size() == size().
    void setVectorField(const std::string& name, std::vector<Vec3> values);

    // -- Macromolecular annotation (PDB / PDBx-mmCIF) ----------------------
    //
    // Index-aligned with atoms() and maintained by addAtom()/removeAtom()
    // exactly like the per-atom fields above. Empty when the source format
    // carried no residue information.

    const std::vector<ResidueInfo>& residues() const { return residues_; }
    /// True when residue annotation is present and aligned — the precondition
    /// for any residue-based representation (ribbon, chain colouring).
    bool hasResidues() const
    {
        return residues_.size() == atoms_.size() && !residues_.empty();
    }
    /// Annotation of atom `index`, or a default-constructed one when the
    /// structure carries none. Never throws, so callers can ask per atom.
    const ResidueInfo& residue(std::size_t index) const;
    /// Replace the whole annotation; ignored unless values.size() == size().
    void setResidues(std::vector<ResidueInfo> values);

    // -- Reordering --------------------------------------------------------

    /// Permute the atoms so that the atom currently at `order[k]` ends up at
    /// index k, carrying every index-aligned property with it.
    ///
    /// The whole point is that "carrying with it" is not optional and is easy
    /// to get half right. Scalar and vector fields, residue annotation, the
    /// manual bond-order map and the add/remove bond overrides are all keyed
    /// by atom index; a sort that moved only `atoms_` would leave a structure
    /// whose forces belong to the wrong atoms and whose double bond now joins
    /// two different ones — silently, and in a form that looks perfectly
    /// normal on screen.
    ///
    /// `order` must be a permutation of [0, size()); anything else is ignored
    /// rather than applied partially.
    void reorder(const std::vector<std::size_t>& order);

private:
    std::vector<Atom> atoms_;
    UnitCell cell_;
    std::map<std::pair<int, int>, int> bondOrders_;
    std::map<std::string, std::vector<double>> scalarFields_;
    std::map<std::string, std::vector<Vec3>> vectorFields_;
    std::vector<ResidueInfo> residues_;
    std::vector<std::pair<int, int>> addedBonds_;
    std::vector<std::pair<int, int>> removedBonds_;
};

} // namespace calango::core
