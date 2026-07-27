#include "core/StructureTransforms.hpp"

#include "core/UnitCell.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace calango::core {

void centerInCell(Structure& structure)
{
    if (!structure.cell().isDefined() || structure.empty())
        return;
    const auto& vectors = structure.cell().vectors();
    const Vec3 cellCenter = (vectors[0] + vectors[1] + vectors[2]) * 0.5;
    const Vec3 shift = cellCenter - structure.centroid();
    for (Atom& atom : structure.atoms())
        atom.position += shift;
}

bool addVacuum(Structure& structure, const VacuumOptions& options)
{
    if (!structure.cell().isDefined())
        return false;
    if (options.thickness <= 0.0)
        return false;
    if (!options.axes[0] && !options.axes[1] && !options.axes[2])
        return false;

    auto vectors = structure.cell().vectors();
    auto pbc = structure.cell().pbc();
    for (int axis = 0; axis < 3; ++axis) {
        if (!options.axes[axis])
            continue;
        const auto index = static_cast<std::size_t>(axis);
        const double length = vectors[index].norm();
        if (length < 1e-9)
            continue; // degenerate lattice vector: nothing to extend along

        const Vec3 unit = vectors[index] / length;
        vectors[index] = unit * (length + options.thickness);
        if (options.clearPbc)
            pbc[index] = false;

        if (options.bothSides) {
            // Re-centre along this direction: project the structure's extent
            // onto the axis and shift so equal vacuum sits on either side.
            double lo = std::numeric_limits<double>::max();
            double hi = std::numeric_limits<double>::lowest();
            for (const Atom& atom : structure.atoms()) {
                const double projection = atom.position.dot(unit);
                lo = std::min(lo, projection);
                hi = std::max(hi, projection);
            }
            if (lo <= hi) {
                const double target =
                    0.5 * (length + options.thickness - (hi - lo));
                const Vec3 shift = unit * (target - lo);
                for (Atom& atom : structure.atoms())
                    atom.position += shift;
            }
        }
    }

    structure.setCell(UnitCell(vectors[0], vectors[1], vectors[2], pbc));
    return true;
}

int wrapIntoCell(Structure& structure, const std::vector<std::size_t>& indices)
{
    if (!structure.cell().isDefined())
        return 0;

    std::vector<std::size_t> rows = indices;
    if (rows.empty()) {
        rows.resize(structure.size());
        std::iota(rows.begin(), rows.end(), std::size_t{0});
    }

    const UnitCell& cell = structure.cell();
    const std::array<bool, 3> pbc = cell.pbc();
    const bool anyPeriodic = pbc[0] || pbc[1] || pbc[2];

    int moved = 0;
    for (const std::size_t row : rows) {
        if (row >= structure.size())
            continue;
        Atom& atom = structure.atoms()[row];
        Vec3 fractional = cell.cartesianToFractional(atom.position);
        double* components[3] = {&fractional.x, &fractional.y, &fractional.z};
        bool changed = false;
        for (int axis = 0; axis < 3; ++axis) {
            if (anyPeriodic && !pbc[static_cast<std::size_t>(axis)])
                continue;
            const double wrapped =
                *components[axis] - std::floor(*components[axis]);
            if (std::abs(wrapped - *components[axis]) > 1e-12) {
                *components[axis] = wrapped;
                changed = true;
            }
        }
        if (!changed)
            continue;
        atom.position = cell.fractionalToCartesian(fractional);
        ++moved;
    }
    return moved;
}

} // namespace calango::core
