#include "core/Structure.hpp"

#include <algorithm>
#include <map>

namespace calango::core {

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
    std::vector<Bond> bonds;
    const auto n = static_cast<int>(atoms_.size());
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            const auto& a = atoms_[static_cast<std::size_t>(i)];
            const auto& b = atoms_[static_cast<std::size_t>(j)];
            const double cutoff = tolerance * (a.covalentRadius() + b.covalentRadius());
            const Vec3 d = a.position - b.position;
            const double distSq = d.dot(d);
            if (distSq < cutoff * cutoff && distSq > 0.16) // 0.4 Å floor: overlapping atoms
                bonds.push_back({i, j});
        }
    }
    return bonds;
}

} // namespace calango::core
