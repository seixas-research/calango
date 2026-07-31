#include "core/BondRules.hpp"

#include <cstddef>

namespace calango::core {

std::vector<std::pair<int, int>> matchingPairs(const Structure& frame,
                                               const ElementBondRule& rule)
{
    std::vector<std::pair<int, int>> pairs;
    const auto& atoms = frame.atoms();
    for (std::size_t i = 0; i + 1 < atoms.size(); ++i) {
        for (std::size_t j = i + 1; j < atoms.size(); ++j) {
            if (!rule.matchesElements(atoms[i].atomicNumber,
                                      atoms[j].atomicNumber))
                continue;
            const double d = (atoms[j].position - atoms[i].position).norm();
            if (d >= rule.minDistance && d <= rule.maxDistance)
                pairs.emplace_back(static_cast<int>(i), static_cast<int>(j));
        }
    }
    return pairs;
}

int applyElementRule(const std::vector<Structure*>& frames,
                     const ElementBondRule& rule, bool bond)
{
    int total = 0;
    for (Structure* frame : frames) {
        if (!frame)
            continue;
        // The re-match is the whole point: this is where a bond that existed
        // at t = 0 correctly fails to appear at t = 10 ps.
        const auto pairs = matchingPairs(*frame, rule);
        for (const auto& [i, j] : pairs) {
            if (bond) {
                frame->addBondOverride(i, j);
            } else {
                // Both directions. Dropping the explicit "added" mark alone
                // would leave an auto-perceived bond in place for a pair the
                // user just asked to unbond.
                frame->clearBondOverride(i, j);
                frame->removeBondOverride(i, j);
            }
        }
        total += static_cast<int>(pairs.size());
    }
    return total;
}

namespace {

/// True when both indices name an atom that exists in `frame`.
bool addressable(const Structure& frame, int i, int j)
{
    const int count = static_cast<int>(frame.size());
    return i >= 0 && j >= 0 && i < count && j < count && i != j;
}

} // namespace

void applyIndexBond(const std::vector<Structure*>& frames, int i, int j,
                    int order)
{
    for (Structure* frame : frames) {
        if (!frame || !addressable(*frame, i, j))
            continue;
        frame->addBondOverride(i, j);
        frame->setBondOrder(i, j, order);
    }
}

void applyIndexSuppression(const std::vector<Structure*>& frames, int i, int j)
{
    for (Structure* frame : frames)
        if (frame && addressable(*frame, i, j))
            frame->removeBondOverride(i, j);
}

void clearPairOnAllFrames(const std::vector<Structure*>& frames, int i, int j)
{
    for (Structure* frame : frames)
        if (frame)
            frame->clearBondOverride(i, j);
}

void clearAllOnAllFrames(const std::vector<Structure*>& frames)
{
    for (Structure* frame : frames)
        if (frame)
            frame->clearBondOverrides();
}

} // namespace calango::core
