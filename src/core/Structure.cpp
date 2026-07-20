#include "core/Structure.hpp"

#include <algorithm>
#include <cmath>
#include <map>

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

void Structure::removeAtom(std::size_t index)
{
    if (index < atoms_.size())
        atoms_.erase(atoms_.begin() + static_cast<std::ptrdiff_t>(index));
}

void Structure::clear()
{
    atoms_.clear();
    cell_ = UnitCell{};
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

std::vector<Bond> Structure::detectBonds(double tolerance) const
{
    const auto pbc = cell_.pbc();
    const bool usePbc = cell_.isDefined() && (pbc[0] || pbc[1] || pbc[2]);

    std::vector<Bond> bonds;
    const auto n = static_cast<int>(atoms_.size());
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            const auto& a = atoms_[static_cast<std::size_t>(i)];
            const auto& b = atoms_[static_cast<std::size_t>(j)];

            Vec3 d = b.position - a.position;
            Vec3 offset{};
            if (usePbc) {
                // Minimum-image convention along the periodic directions.
                const Vec3 frac = cell_.cartesianToFractional(d);
                const Vec3 shift{pbc[0] ? std::round(frac.x) : 0.0,
                                 pbc[1] ? std::round(frac.y) : 0.0,
                                 pbc[2] ? std::round(frac.z) : 0.0};
                if (shift.dot(shift) > 0.0) {
                    offset = cell_.fractionalToCartesian(shift) * -1.0;
                    d += offset;
                }
            }

            const double radiusSum = a.covalentRadius() + b.covalentRadius();
            const double cutoff = tolerance * radiusSum;
            const double distSq = d.dot(d);
            if (distSq < cutoff * cutoff && distSq > 0.16) { // 0.4 Å floor: overlapping atoms
                const int order = perceiveBondOrder(
                    a.atomicNumber, b.atomicNumber, std::sqrt(distSq) / radiusSum);
                bonds.push_back({i, j, offset, order});
            }
        }
    }
    return bonds;
}

} // namespace calango::core
