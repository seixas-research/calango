#include "core/Structure.hpp"

#include "core/PeriodicImages.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace calango::core {
namespace {

/// Uniform spatial hash over the atoms, so bond detection can ask "what is
/// near this point" instead of testing every pair.
///
/// The all-pairs scan this replaces was O(n^2) — already ~300 ms on a 15 000
/// atom protein — and correct periodic bonding multiplies the work by the
/// number of lattice images (75 for a thin 2D cell), which the quadratic form
/// could not absorb.
struct SpatialGrid {
    Vec3 origin;
    double cellSize = 1.0;
    int dim[3] = {1, 1, 1};
    /// Bins as an intrusive linked list: heads[bin] is the first atom index in
    /// that bin, next[atom] the one after it, -1 terminating. One allocation
    /// each instead of a vector per bin.
    std::vector<int> heads;
    std::vector<int> next;

    void build(const std::vector<Atom>& atoms, double spacing)
    {
        const auto n = static_cast<int>(atoms.size());
        next.assign(static_cast<std::size_t>(n), -1);
        cellSize = std::max(spacing, 1e-3);

        Vec3 lo{std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max()};
        Vec3 hi{std::numeric_limits<double>::lowest(),
                std::numeric_limits<double>::lowest(),
                std::numeric_limits<double>::lowest()};
        for (const Atom& atom : atoms) {
            lo.x = std::min(lo.x, atom.position.x);
            lo.y = std::min(lo.y, atom.position.y);
            lo.z = std::min(lo.z, atom.position.z);
            hi.x = std::max(hi.x, atom.position.x);
            hi.y = std::max(hi.y, atom.position.y);
            hi.z = std::max(hi.z, atom.position.z);
        }
        origin = lo;
        const double extent[3] = {hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};

        // Bin count is capped against the ATOM count, not the bounding box: a
        // pair of atoms 1000 A apart in a vacuum box would otherwise ask for
        // tens of millions of empty bins. Coarsening the spacing keeps the
        // table proportional to the structure.
        const auto axes = [&](double spacingUsed) {
            long long total = 1;
            for (int i = 0; i < 3; ++i) {
                dim[i] = std::max(
                    1, static_cast<int>(std::floor(extent[i] / spacingUsed)) + 1);
                total *= dim[i];
            }
            return total;
        };
        const long long budget = 8LL * std::max(n, 1) + 64;
        if (axes(cellSize) > budget) {
            const double scale =
                std::cbrt(static_cast<double>(axes(cellSize)) / static_cast<double>(budget));
            cellSize *= std::max(1.0, scale);
            axes(cellSize);
        }

        heads.assign(static_cast<std::size_t>(dim[0]) * dim[1] * dim[2], -1);
        for (int i = 0; i < n; ++i) {
            const int bin = binOf(atoms[static_cast<std::size_t>(i)].position);
            next[static_cast<std::size_t>(i)] = heads[static_cast<std::size_t>(bin)];
            heads[static_cast<std::size_t>(bin)] = i;
        }
    }

    /// Grid coordinate of `p`, clamped into the table. Clamping is what makes a
    /// query from outside the bounding box (a shifted periodic image) land in
    /// the nearest edge bin rather than out of range.
    void coordOf(const Vec3& p, int out[3]) const
    {
        const double f[3] = {(p.x - origin.x) / cellSize, (p.y - origin.y) / cellSize,
                             (p.z - origin.z) / cellSize};
        for (int i = 0; i < 3; ++i) {
            const int raw = static_cast<int>(std::floor(f[i]));
            out[i] = std::clamp(raw, 0, dim[i] - 1);
        }
    }

    int binOf(const Vec3& p) const
    {
        int c[3];
        coordOf(p, c);
        return (c[2] * dim[1] + c[1]) * dim[0] + c[0];
    }

    /// Call `visit(atomIndex)` for every atom in the 3x3x3 block around `p`.
    template <typename F>
    void forEachNear(const Vec3& p, F&& visit) const
    {
        int c[3];
        coordOf(p, c);
        for (int z = std::max(0, c[2] - 1); z <= std::min(dim[2] - 1, c[2] + 1); ++z)
            for (int y = std::max(0, c[1] - 1); y <= std::min(dim[1] - 1, c[1] + 1); ++y)
                for (int x = std::max(0, c[0] - 1); x <= std::min(dim[0] - 1, c[0] + 1); ++x) {
                    const auto bin = static_cast<std::size_t>((z * dim[1] + y) * dim[0] + x);
                    for (int atom = heads[bin]; atom >= 0;
                         atom = next[static_cast<std::size_t>(atom)])
                        visit(atom);
                }
    }
};

} // namespace
} // namespace calango::core

namespace calango::core {

void Structure::addAtom(const Atom& atom)
{
    atoms_.push_back(atom);
    for (auto& [name, values] : scalarFields_) {
        (void)name;
        values.push_back(0.0);
    }
    for (auto& [name, values] : vectorFields_) {
        (void)name;
        values.push_back(Vec3{});
    }
    // Only pad the annotation when there IS one: growing it from empty would
    // make hasResidues() true for a structure that has no residues, and a
    // ribbon would then be drawn through a pile of blanks.
    if (!residues_.empty())
        residues_.emplace_back();
}

void Structure::removeAtom(std::size_t index)
{
    if (index >= atoms_.size())
        return;
    atoms_.erase(atoms_.begin() + static_cast<std::ptrdiff_t>(index));
    for (auto& [name, values] : scalarFields_) {
        (void)name;
        if (index < values.size())
            values.erase(values.begin() + static_cast<std::ptrdiff_t>(index));
    }
    for (auto& [name, values] : vectorFields_) {
        (void)name;
        if (index < values.size())
            values.erase(values.begin() + static_cast<std::ptrdiff_t>(index));
    }
    if (index < residues_.size())
        residues_.erase(residues_.begin() + static_cast<std::ptrdiff_t>(index));
    // Bond overrides: drop pairs touching the removed atom, shift the rest.
    const auto removed = static_cast<int>(index);
    for (auto* overrides : {&addedBonds_, &removedBonds_}) {
        std::erase_if(*overrides, [removed](const std::pair<int, int>& pair) {
            return pair.first == removed || pair.second == removed;
        });
        for (auto& [i, j] : *overrides) {
            if (i > removed)
                --i;
            if (j > removed)
                --j;
        }
    }
    // Same for the manual bond orders (keys are immutable — rebuild).
    std::map<std::pair<int, int>, int> orders;
    for (const auto& [pair, order] : bondOrders_) {
        if (pair.first == removed || pair.second == removed)
            continue;
        orders[{pair.first - (pair.first > removed ? 1 : 0),
                pair.second - (pair.second > removed ? 1 : 0)}] = order;
    }
    bondOrders_ = std::move(orders);
}

void Structure::clear()
{
    atoms_.clear();
    cell_ = UnitCell{};
    scalarFields_.clear();
    vectorFields_.clear();
    residues_.clear();
}

void Structure::setScalarField(const std::string& name, std::vector<double> values)
{
    if (values.size() != atoms_.size())
        return;
    scalarFields_[name] = std::move(values);
}

void Structure::setVectorField(const std::string& name, std::vector<Vec3> values)
{
    if (values.size() != atoms_.size())
        return;
    vectorFields_[name] = std::move(values);
}

const ResidueInfo& Structure::residue(std::size_t index) const
{
    // A shared empty annotation rather than an exception: callers ask this per
    // atom while walking a structure that may or may not carry residues, and
    // "no residue" is a normal answer, not an error.
    static const ResidueInfo kNone;
    return index < residues_.size() ? residues_[index] : kNone;
}

void Structure::setResidues(std::vector<ResidueInfo> values)
{
    if (values.size() != atoms_.size())
        return;
    residues_ = std::move(values);
}

std::string Structure::chemicalFormula() const
{
    std::map<std::string, int> counts;
    for (const Atom& atom : atoms_)
        ++counts[atom.symbol()];

    // Hill order: C first, then H, then everything else alphabetically.
    std::string formula;
    const auto append = [&formula, &counts](const std::string& symbol) {
        const auto it = counts.find(symbol);
        if (it == counts.end())
            return;
        formula += symbol;
        if (it->second > 1)
            formula += std::to_string(it->second);
        counts.erase(it);
    };
    if (counts.count("C")) {
        append("C");
        append("H");
    }
    for (const auto& [symbol, count] : std::map<std::string, int>(counts)) {
        (void)count;
        append(symbol);
    }
    return formula;
}

Vec3 Structure::centroid() const
{
    Vec3 sum;
    if (atoms_.empty())
        return sum;
    for (const Atom& atom : atoms_)
        sum += atom.position;
    return sum / static_cast<double>(atoms_.size());
}

double Structure::boundingRadius(const Vec3& center) const
{
    double maxSq = 0.0;
    for (const Atom& atom : atoms_) {
        const Vec3 d = atom.position - center;
        maxSq = std::max(maxSq, d.dot(d));
    }
    return std::sqrt(maxSq);
}

std::vector<Bond> Structure::detectBonds(double tolerance, bool autoDetect) const
{
    const auto pbc = cell_.pbc();
    const bool usePbc = cell_.isDefined() && (pbc[0] || pbc[1] || pbc[2]);

    // Minimum-image displacement from atom i to atom j and the image offset
    // applied to j (zero inside the cell). Used only for MANUAL bonds now: a
    // pair the user drew by hand names one specific contact, and the nearest
    // image is the one they meant.
    const auto minimumImage = [&](int i, int j, Vec3& d, Vec3& offset) {
        d = atoms_[static_cast<std::size_t>(j)].position
            - atoms_[static_cast<std::size_t>(i)].position;
        offset = Vec3{};
        if (!usePbc)
            return;
        const Vec3 frac = cell_.cartesianToFractional(d);
        const Vec3 shift{pbc[0] ? std::round(frac.x) : 0.0,
                         pbc[1] ? std::round(frac.y) : 0.0,
                         pbc[2] ? std::round(frac.z) : 0.0};
        if (shift.dot(shift) > 0.0) {
            offset = cell_.fractionalToCartesian(shift) * -1.0;
            d += offset;
        }
    };
    const auto hasOverride = [](const std::vector<std::pair<int, int>>& list,
                                int i, int j) {
        const auto pair = std::minmax(i, j);
        return std::find(list.begin(), list.end(),
                         std::pair<int, int>{pair.first, pair.second})
            != list.end();
    };

    std::vector<Bond> bonds;
    const auto n = static_cast<int>(atoms_.size());
    if (autoDetect && n > 0) {
        double largestRadius = 0.0;
        for (const Atom& atom : atoms_)
            largestRadius = std::max(largestRadius, static_cast<double>(atom.covalentRadius()));
        const double rMax = tolerance * 2.0 * largestRadius;

        // EVERY lattice image within reach, not just the nearest one.
        //
        // This is the whole fix. Minimum-image convention answers "where is the
        // closest copy of atom j", which is the right question for a distance
        // and the wrong one for bonding: in a crystal an atom bonds to SEVERAL
        // images of the same neighbour. A MoS2 monolayer is the clean example —
        // its Mo sits in a trigonal prism of six sulfurs, three above and three
        // below, and all six are images of the two S atoms the cell lists. Under
        // minimum image only one image of each survived, so four of the six
        // bonds simply did not exist.
        struct Translation {
            Vec3 shift;
            /// Lattice indices, kept so a self-image pair can be canonicalized
            /// by sign — see the j == i case below.
            int index[3];
        };
        std::vector<Translation> translations{Translation{Vec3{}, {0, 0, 0}}};
        if (usePbc) {
            translations.clear();
            const auto range = imageRange(cell_, rMax);
            const auto& v = cell_.vectors();
            for (int ia = -range[0]; ia <= range[0]; ++ia)
                for (int ib = -range[1]; ib <= range[1]; ++ib)
                    for (int ic = -range[2]; ic <= range[2]; ++ic) {
                        translations.push_back(
                            {v[0] * ia + v[1] * ib + v[2] * ic, {ia, ib, ic}});
                    }
        }

        SpatialGrid grid;
        grid.build(atoms_, rMax);

        for (int i = 0; i < n; ++i) {
            const auto& a = atoms_[static_cast<std::size_t>(i)];
            for (const Translation& translation : translations) {
                const Vec3& t = translation.shift;
                // Testing atom i against image (j, t) is the same as testing the
                // point (p_i - t) against the primary atom j, so one grid built
                // over the real atoms serves every image.
                const Vec3 query = a.position - t;
                grid.forEachNear(query, [&](int j) {
                    // Each unordered pair is emitted once: (i,j,+t) and
                    // (j,i,-t) are the same bond. j == i is kept for every
                    // non-zero translation, which is how an atom bonds to its
                    // OWN images — the only bonds a one-atom-per-cell lattice
                    // has, and previously unreachable because the scan started
                    // at j = i + 1.
                    if (j < i)
                        return;
                    if (j == i) {
                        if (t.dot(t) < 1e-12)
                            return; // the atom itself
                        // An atom's bond to its image at +t and its bond to the
                        // image at -t are two contacts along ONE line, and the
                        // renderer already draws a wrapped bond as a stub at
                        // each end. Emitting both would double every stub, and
                        // would make consumers that credit a bond to each of its
                        // endpoints count this atom's neighbours twice. Keep the
                        // lexicographically positive half.
                        const int* k = translation.index;
                        const bool positive = k[0] > 0
                            || (k[0] == 0 && (k[1] > 0 || (k[1] == 0 && k[2] > 0)));
                        if (!positive)
                            return;
                    }
                    if (hasOverride(removedBonds_, i, j)
                        || hasOverride(addedBonds_, i, j))
                        return; // suppressed, or handled below as a manual bond

                    const auto& b = atoms_[static_cast<std::size_t>(j)];
                    const Vec3 d = b.position + t - a.position;
                    const double cutoff =
                        tolerance * (a.covalentRadius() + b.covalentRadius());
                    const double distSq = d.dot(d);
                    if (distSq < cutoff * cutoff && distSq > 0.16) // 0.4 A floor
                        bonds.push_back({i, j, t, bondOrder(i, j)});
                });
            }
        }
    }

    // Manual bonds render regardless of distance (minimum-image geometry).
    for (const auto& [i, j] : addedBonds_) {
        if (i < 0 || j < 0 || i >= n || j >= n || i == j)
            continue;
        Vec3 d, offset;
        minimumImage(i, j, d, offset);
        bonds.push_back({i, j, offset, bondOrder(i, j)});
    }
    return bonds;
}

int Structure::bondOrder(int i, int j) const
{
    const auto pair = std::minmax(i, j);
    const auto it = bondOrders_.find({pair.first, pair.second});
    return it != bondOrders_.end() ? it->second : 1;
}

void Structure::setBondOrder(int i, int j, int order)
{
    if (i == j)
        return;
    const auto pair = std::minmax(i, j);
    if (order <= 1)
        bondOrders_.erase({pair.first, pair.second}); // single is the default
    else
        bondOrders_[{pair.first, pair.second}] = std::min(order, 4);
}

void Structure::addBondOverride(int i, int j)
{
    if (i == j || i < 0 || j < 0 || i >= static_cast<int>(atoms_.size())
        || j >= static_cast<int>(atoms_.size()))
        return;
    const auto pair = std::minmax(i, j);
    const std::pair<int, int> key{pair.first, pair.second};
    std::erase(removedBonds_, key);
    if (std::find(addedBonds_.begin(), addedBonds_.end(), key) == addedBonds_.end())
        addedBonds_.push_back(key);
}

void Structure::removeBondOverride(int i, int j)
{
    if (i == j)
        return;
    const auto pair = std::minmax(i, j);
    const std::pair<int, int> key{pair.first, pair.second};
    std::erase(addedBonds_, key);
    if (std::find(removedBonds_.begin(), removedBonds_.end(), key)
        == removedBonds_.end())
        removedBonds_.push_back(key);
}

void Structure::clearBondOverride(int i, int j)
{
    const auto pair = std::minmax(i, j);
    const std::pair<int, int> key{pair.first, pair.second};
    std::erase(addedBonds_, key);
    std::erase(removedBonds_, key);
}

void Structure::clearBondOverrides()
{
    addedBonds_.clear();
    removedBonds_.clear();
}

} // namespace calango::core
