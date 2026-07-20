#pragma once

#include "core/Atom.hpp"
#include "core/UnitCell.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace calango::core {

struct Bond {
    int i = 0;
    int j = 0;
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

    void addAtom(const Atom& atom) { atoms_.push_back(atom); }
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
    /// O(N²) — fine for a few thousand atoms; switch to a cell-list /
    /// k-d tree spatial index for large systems. Bonds across periodic
    /// boundaries are not yet detected (TODO: minimum-image convention).
    std::vector<Bond> detectBonds(double tolerance = 1.15) const;

private:
    std::vector<Atom> atoms_;
    UnitCell cell_;
};

} // namespace calango::core
