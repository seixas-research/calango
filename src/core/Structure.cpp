#include "core/Structure.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace calango::core {

namespace {

/// Distance-based bond-order heuristic. Calibrated on typical bond lengths
/// relative to the sum of covalent radii (e.g. C–O 1.43 Å / 1.42 Å ≈ 1.01,
/// C=O 1.21 Å ≈ 0.85, C≡O 1.13 Å ≈ 0.80). Only elements that commonly form
/// multiple bonds participate — short metal-metal contacts must not be
/// mistaken for double bonds. Real bond-order data (e.g. from SMILES or a
/// force field) can override this later via Bond::order.
int perceiveBondOrder(int zA, int zB, double distanceRatio)
{
    const auto formsMultipleBonds = [](int z) {
        switch (z) {
        case 6: case 7: case 8: case 15: case 16: case 33: case 34: // C N O P S As Se
            return true;
        default:
            return false;
        }
    };
    if (!formsMultipleBonds(zA) || !formsMultipleBonds(zB))
        return 1;
    if (distanceRatio < 0.81)
        return 3;
    if (distanceRatio < 0.92)
        return 2;
    return 1;
}

} // namespace

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
}

void Structure::clear()
{
    atoms_.clear();
    cell_ = UnitCell{};
    scalarFields_.clear();
    vectorFields_.clear();
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

    // Minimum-image displacement from atom i to atom j and the image
    // offset applied to j (zero inside the cell).
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
    if (autoDetect) {
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                if (hasOverride(removedBonds_, i, j) || hasOverride(addedBonds_, i, j))
                    continue; // suppressed, or handled below as a manual bond
                const auto& a = atoms_[static_cast<std::size_t>(i)];
                const auto& b = atoms_[static_cast<std::size_t>(j)];

                Vec3 d, offset;
                minimumImage(i, j, d, offset);

                const double radiusSum = a.covalentRadius() + b.covalentRadius();
                const double cutoff = tolerance * radiusSum;
                const double distSq = d.dot(d);
                if (distSq < cutoff * cutoff && distSq > 0.16) { // 0.4 Å floor
                    const int order = perceiveBondOrder(
                        a.atomicNumber, b.atomicNumber, std::sqrt(distSq) / radiusSum);
                    bonds.push_back({i, j, offset, order});
                }
            }
        }
    }

    // Manual bonds render regardless of distance (minimum-image geometry).
    for (const auto& [i, j] : addedBonds_) {
        if (i < 0 || j < 0 || i >= n || j >= n || i == j)
            continue;
        Vec3 d, offset;
        minimumImage(i, j, d, offset);
        const double radiusSum = atoms_[static_cast<std::size_t>(i)].covalentRadius()
            + atoms_[static_cast<std::size_t>(j)].covalentRadius();
        const double dist = std::sqrt(d.dot(d));
        const int order = dist > 1e-6
            ? perceiveBondOrder(atoms_[static_cast<std::size_t>(i)].atomicNumber,
                                atoms_[static_cast<std::size_t>(j)].atomicNumber,
                                dist / radiusSum)
            : 1;
        bonds.push_back({i, j, offset, order});
    }
    return bonds;
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
