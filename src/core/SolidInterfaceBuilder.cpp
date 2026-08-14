#include "core/SolidInterfaceBuilder.hpp"
#include "core/PhysicalConstants.hpp"
#include "core/RandomRotation.hpp"

#include "core/Element.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <random>
#include <sstream>
#include <stdexcept>

namespace calango::core {

namespace {

/// u -> g/cm³ for a mass per unit volume expressed in u/Å³.
constexpr double kUPerA3ToGCm3 = 1.6605390666;

using Matrix3 = std::array<Vec3, 3>; ///< rows

Vec3 apply(const Matrix3& m, const Vec3& v)
{
    return {m[0].dot(v), m[1].dot(v), m[2].dot(v)};
}

/// Right-handed rotation by `radians` about the unit vector `axis`
/// (Rodrigues), as row vectors.
Matrix3 rotationAbout(const Vec3& axis, double radians)
{
    const Vec3 k = axis.normalized();
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    const double t = 1.0 - c;
    return {Vec3{c + k.x * k.x * t, k.x * k.y * t - k.z * s,
                 k.x * k.z * t + k.y * s},
            Vec3{k.y * k.x * t + k.z * s, c + k.y * k.y * t,
                 k.y * k.z * t - k.x * s},
            Vec3{k.z * k.x * t - k.y * s, k.z * k.y * t + k.x * s,
                 c + k.z * k.z * t}};
}

/// Perpendicular spacing of the lattice planes normal to vector `index`.
double perpendicularSpacing(const std::array<Vec3, 3>& cell, int index)
{
    const Vec3& a = cell[static_cast<std::size_t>((index + 1) % 3)];
    const Vec3& b = cell[static_cast<std::size_t>((index + 2) % 3)];
    const Vec3 normal = a.cross(b);
    const double area = normal.norm();
    if (area < 1e-12)
        return 0.0;
    return std::abs(cell[static_cast<std::size_t>(index)].dot(normal)) / area;
}

double totalMassU(const Structure& s)
{
    double mass = 0.0;
    for (const Atom& atom : s.atoms())
        mass += Elements::atomicMass(atom.atomicNumber);
    return mass;
}

/// A uniform grid over the periodic box, used to answer "is any accepted atom
/// within `radius` of this point" in O(1) instead of O(N). A polycrystal is
/// the one construction here that reaches six figures of atoms, and the
/// duplicate check is quadratic without it.
class NeighbourGrid {
public:
    NeighbourGrid(const std::array<Vec3, 3>& cell, double radius)
        : cell_(cell)
        , radius_(radius)
    {
        for (int i = 0; i < 3; ++i) {
            const double spacing = perpendicularSpacing(cell, i);
            divisions_[static_cast<std::size_t>(i)] = std::max(
                1, static_cast<int>(std::floor(spacing / std::max(radius, 1e-6))));
            // A grid finer than 3 cells per axis costs more in bookkeeping
            // than it saves, and one coarser than the search radius is a
            // linear scan wearing a grid's clothes.
            divisions_[static_cast<std::size_t>(i)] =
                std::min(divisions_[static_cast<std::size_t>(i)], 256);
        }
        buckets_.resize(static_cast<std::size_t>(divisions_[0])
                        * static_cast<std::size_t>(divisions_[1])
                        * static_cast<std::size_t>(divisions_[2]));
    }

    void insert(const Vec3& fractional, const Vec3& cartesian)
    {
        buckets_[bucketOf(fractional)].push_back(cartesian);
    }

    /// True when some inserted point lies within `radius_` of `cartesian`,
    /// under the minimum-image convention.
    bool occupied(const Vec3& fractional, const Vec3& cartesian) const
    {
        const std::array<int, 3> home = coordinatesOf(fractional);
        for (int di = -1; di <= 1; ++di)
            for (int dj = -1; dj <= 1; ++dj)
                for (int dk = -1; dk <= 1; ++dk) {
                    const std::size_t index = flatten(
                        wrap(home[0] + di, divisions_[0]),
                        wrap(home[1] + dj, divisions_[1]),
                        wrap(home[2] + dk, divisions_[2]));
                    for (const Vec3& other : buckets_[index])
                        if (minimumImage(cartesian - other) < radius_)
                            return true;
                }
        return false;
    }

    double minimumImage(const Vec3& delta) const
    {
        double best = std::numeric_limits<double>::max();
        for (int i = -1; i <= 1; ++i)
            for (int j = -1; j <= 1; ++j)
                for (int k = -1; k <= 1; ++k)
                    best = std::min(
                        best,
                        (delta - (cell_[0] * i + cell_[1] * j + cell_[2] * k))
                            .norm());
        return best;
    }

private:
    static int wrap(int value, int count)
    {
        return ((value % count) + count) % count;
    }

    std::array<int, 3> coordinatesOf(const Vec3& fractional) const
    {
        const double f[3] = {fractional.x, fractional.y, fractional.z};
        std::array<int, 3> out{};
        for (int i = 0; i < 3; ++i) {
            const double wrapped = f[i] - std::floor(f[i]);
            out[static_cast<std::size_t>(i)] = std::clamp(
                static_cast<int>(wrapped * divisions_[static_cast<std::size_t>(i)]),
                0, divisions_[static_cast<std::size_t>(i)] - 1);
        }
        return out;
    }

    std::size_t flatten(int i, int j, int k) const
    {
        return (static_cast<std::size_t>(i) * static_cast<std::size_t>(divisions_[1])
                + static_cast<std::size_t>(j))
            * static_cast<std::size_t>(divisions_[2])
            + static_cast<std::size_t>(k);
    }

    std::size_t bucketOf(const Vec3& fractional) const
    {
        const auto c = coordinatesOf(fractional);
        return flatten(c[0], c[1], c[2]);
    }

    std::array<Vec3, 3> cell_;
    double radius_;
    std::array<int, 3> divisions_{1, 1, 1};
    std::vector<std::vector<Vec3>> buckets_;
};

/// How many repeats of `cell` are needed in each direction to be sure of
/// covering a box of the given diagonal, plus a margin.
std::array<int, 3> coverageRepeats(const std::array<Vec3, 3>& cell,
                                   double diagonal)
{
    std::array<int, 3> repeats{1, 1, 1};
    for (int i = 0; i < 3; ++i) {
        const double spacing = perpendicularSpacing(cell, i);
        repeats[static_cast<std::size_t>(i)] =
            spacing > 1e-9
                ? static_cast<int>(std::ceil(diagonal / spacing)) + 1
                : 1;
    }
    return repeats;
}

struct Box {
    std::array<Vec3, 3> vectors{};
    Matrix3 inverse{}; ///< Cartesian -> fractional, as rows

    Vec3 toFractional(const Vec3& cartesian) const
    {
        return apply(inverse, cartesian);
    }
    Vec3 toCartesian(const Vec3& fractional) const
    {
        return vectors[0] * fractional.x + vectors[1] * fractional.y
            + vectors[2] * fractional.z;
    }
    double volume() const
    {
        return std::abs(vectors[0].dot(vectors[1].cross(vectors[2])));
    }
};

Box makeBox(const std::array<Vec3, 3>& vectors)
{
    Box box;
    box.vectors = vectors;
    const double determinant =
        vectors[0].dot(vectors[1].cross(vectors[2]));
    if (std::abs(determinant) < 1e-12)
        throw std::invalid_argument("the cell is degenerate");
    const Vec3 r0 = vectors[1].cross(vectors[2]) / determinant;
    const Vec3 r1 = vectors[2].cross(vectors[0]) / determinant;
    const Vec3 r2 = vectors[0].cross(vectors[1]) / determinant;
    // Rows of the inverse of the matrix whose COLUMNS are the cell vectors:
    // f_i = r_i . x.
    box.inverse = {r0, r1, r2};
    return box;
}

/// Every atom of `lattice`, rotated by `rotation`, replicated to cover `box`,
/// expressed in box-fractional coordinates.
struct TiledAtom {
    Vec3 fractional;
    Vec3 cartesian;
    int atomicNumber = 0;
};

void tile(const Structure& lattice, const Matrix3& rotation, const Box& box,
          std::vector<TiledAtom>& out, std::size_t budget)
{
    std::array<Vec3, 3> rotated{};
    for (int i = 0; i < 3; ++i)
        rotated[static_cast<std::size_t>(i)] =
            apply(rotation, lattice.cell().vectors()[static_cast<std::size_t>(i)]);

    const double diagonal =
        (box.vectors[0] + box.vectors[1] + box.vectors[2]).norm();
    const std::array<int, 3> repeats = coverageRepeats(rotated, diagonal);

    for (int i = -repeats[0]; i <= repeats[0]; ++i)
        for (int j = -repeats[1]; j <= repeats[1]; ++j)
            for (int k = -repeats[2]; k <= repeats[2]; ++k) {
                const Vec3 shift =
                    rotated[0] * i + rotated[1] * j + rotated[2] * k;
                for (const Atom& atom : lattice.atoms()) {
                    const Vec3 position = apply(rotation, atom.position) + shift;
                    const Vec3 fractional = box.toFractional(position);
                    if (fractional.x < -1e-9 || fractional.x >= 1.0
                        || fractional.y < -1e-9 || fractional.y >= 1.0
                        || fractional.z < -1e-9 || fractional.z >= 1.0)
                        continue;
                    out.push_back({fractional, position, atom.atomicNumber});
                    if (out.size() > budget)
                        throw std::invalid_argument(
                            "the requested cell exceeds the atom budget — "
                            "reduce the box repeats or raise the budget");
                }
            }
}

/// Smallest distance from any box period to the nearest vector of the rotated
/// lattice. Zero for an exact coincidence-site relationship.
double commensurabilityResidual(const Structure& lattice,
                                const Matrix3& rotation, const Box& box)
{
    std::array<Vec3, 3> rotated{};
    for (int i = 0; i < 3; ++i)
        rotated[static_cast<std::size_t>(i)] =
            apply(rotation, lattice.cell().vectors()[static_cast<std::size_t>(i)]);
    const Box latticeBox = makeBox(rotated);

    double worst = 0.0;
    for (const Vec3& period : box.vectors) {
        const Vec3 fractional = latticeBox.toFractional(period);
        const Vec3 nearest{std::round(fractional.x), std::round(fractional.y),
                           std::round(fractional.z)};
        worst = std::max(
            worst, (period - latticeBox.toCartesian(nearest)).norm());
    }
    return worst;
}

double minimumSeparation(const Structure& s, std::size_t cap)
{
    const auto& atoms = s.atoms();
    if (atoms.size() < 2)
        return 0.0;
    const auto& cell = s.cell().vectors();
    // Reuse the grid: on a polycrystal this is the difference between a
    // diagnostic and a hang.
    NeighbourGrid grid(cell, 3.0);
    double best = std::numeric_limits<double>::max();
    if (atoms.size() > cap)
        return 0.0;
    for (std::size_t i = 0; i + 1 < atoms.size(); ++i)
        for (std::size_t j = i + 1; j < atoms.size(); ++j)
            best = std::min(
                best, grid.minimumImage(atoms[j].position - atoms[i].position));
    return best == std::numeric_limits<double>::max() ? 0.0 : best;
}

} // namespace

std::string SolidInterfaceBuilder::toString(Kind kind)
{
    switch (kind) {
    case Kind::TwinBoundary:          return "twin boundary";
    case Kind::Bicrystal:             return "bicrystal";
    case Kind::Polycrystal:           return "polycrystal";
    case Kind::MultiPhasePolycrystal: return "multi-phase polycrystal";
    case Kind::StackingFault:         break;
    }
    return "stacking fault";
}

std::string SolidInterfaceBuilder::toString(Axis axis)
{
    switch (axis) {
    case Axis::A: return "a";
    case Axis::B: return "b";
    case Axis::C: break;
    }
    return "c";
}

SolidInterfaceBuilder::Result SolidInterfaceBuilder::generate(
    const std::vector<Structure>& lattices, const Params& params)
{
    if (lattices.empty() || lattices.front().empty())
        throw std::invalid_argument("no parent lattice to build from");
    for (const Structure& lattice : lattices)
        if (!lattice.cell().isDefined())
            throw std::invalid_argument(
                "every parent lattice needs a periodic cell — the construction "
                "fills space by repeating it");

    const Structure& parent = lattices.front();
    Result result;
    const int axisIndex = static_cast<int>(params.axis);
    const int plane1 = (axisIndex + 1) % 3;
    const int plane2 = (axisIndex + 2) % 3;

    const auto planar = params.kind == Kind::StackingFault
        || params.kind == Kind::TwinBoundary;
    if (planar
        && (params.boundaryPosition <= 0.0 || params.boundaryPosition >= 1.0))
        throw std::invalid_argument(
            "the boundary position must lie strictly inside the cell");

    // -----------------------------------------------------------------------
    // Stacking fault: one rigid in-plane shift.
    // -----------------------------------------------------------------------
    if (params.kind == Kind::StackingFault) {
        result.structure = parent;
        const auto& cell = parent.cell().vectors();
        const Vec3 fault =
            cell[static_cast<std::size_t>(plane1)] * params.faultVector[0]
            + cell[static_cast<std::size_t>(plane2)] * params.faultVector[1];

        // Growing the cell along the axis by `gap` opens the interface without
        // also opening the one at the periodic boundary, which is why the
        // shift and the gap are applied to the same half.
        const double height = perpendicularSpacing(cell, axisIndex);
        const Vec3 normal =
            cell[static_cast<std::size_t>(plane1)]
                .cross(cell[static_cast<std::size_t>(plane2)])
                .normalized()
            * (cell[static_cast<std::size_t>(axisIndex)].dot(
                   cell[static_cast<std::size_t>(plane1)].cross(
                       cell[static_cast<std::size_t>(plane2)]))
                       > 0.0
                   ? 1.0
                   : -1.0);

        int shifted = 0;
        for (Atom& atom : result.structure.atoms()) {
            const Vec3 fractional =
                parent.cell().cartesianToFractional(atom.position);
            const double along = axisIndex == 0
                ? fractional.x
                : (axisIndex == 1 ? fractional.y : fractional.z);
            // Tolerant comparison, not a bare <.
            //
            // An atom sitting exactly ON the boundary plane has a fractional
            // coordinate that survives the round trip through Cartesian only
            // to the last bit or two, so a bare `<` assigns it to whichever
            // half the rounding happened to favour. That is invisible for a
            // Bravais lattice — one atom either shifts or does not, and either
            // is a valid fault — and it corrupts a MULTI-ATOM BASIS: the two
            // atoms of a diamond {111} pair straddle the plane, one is shifted
            // and the other is not, and the pair ends up sqrt(b^2 + dz^2)
            // apart instead of dz. The crystal is broken and nothing reports
            // it.
            //
            // Resolved by a fixed rule rather than by luck: within kOnPlane of
            // the plane counts as ON it, and an atom on the plane belongs to
            // the half that moves. Same convention as the twin path's
            // kOnPlane, so the two planar defects agree about what "on the
            // boundary" means.
            constexpr double kOnPlane = 1e-9;
            if (along - std::floor(along) < params.boundaryPosition - kOnPlane)
                continue;
            atom.position += fault + normal * params.gap;
            ++shifted;
        }
        if (params.gap != 0.0 && height > 1e-9) {
            auto vectors = result.structure.cell().vectors();
            vectors[static_cast<std::size_t>(axisIndex)] +=
                normal * params.gap;
            UnitCell grown = result.structure.cell();
            grown.setVectors(vectors);
            result.structure.setCell(grown);
        }
        result.interfaceCount = 2;
        result.warnings.emplace_back(
            "a periodic cell cannot hold an odd number of parallel faults: "
            "this cell has TWO — the one at the chosen plane and the one where "
            "the cell meets its own image. Halve the excess energy before "
            "quoting a stacking-fault energy");
        if (shifted == 0)
            result.warnings.emplace_back(
                "the boundary plane is above every atom, so nothing was "
                "shifted");
        result.description = "stacking fault on the plane normal to "
            + toString(params.axis);
    }

    // -----------------------------------------------------------------------
    // Twin boundary: the half above the plane is replaced by the mirror image
    // of the half below it.
    // -----------------------------------------------------------------------
    else if (params.kind == Kind::TwinBoundary) {
        const auto& cell = parent.cell().vectors();
        // A mirror through the boundary plane maps the lattice onto itself
        // only if the stacking vector is perpendicular to that plane.
        // Otherwise the reflected half is periodic with a DIFFERENT cell, and
        // the result is two crystals sharing a box that describes neither.
        const Vec3 normal = cell[static_cast<std::size_t>(plane1)]
                                .cross(cell[static_cast<std::size_t>(plane2)])
                                .normalized();
        const Vec3& stack = cell[static_cast<std::size_t>(axisIndex)];
        const double obliquity =
            (stack - normal * stack.dot(normal)).norm() / stack.norm();
        if (obliquity > 1e-3)
            throw std::invalid_argument(
                "the twin plane is not perpendicular to its lattice vector "
                "(the cell is tilted along that direction). Reflecting it "
                "would produce a half-crystal that is periodic with a "
                "different cell; orthogonalize the cell first");

        Structure twinned;
        twinned.setCell(parent.cell());
        const double plane = params.boundaryPosition;
        // Coincidence tolerance in fractional units. Atoms come from a lattice,
        // so an atom is either exactly on a mirror plane or clearly off it;
        // this only absorbs the rounding of the round trip through Cartesian
        // coordinates.
        constexpr double kOnPlane = 1e-6;
        int kept = 0;
        for (const Atom& atom : parent.atoms()) {
            const Vec3 fractional =
                parent.cell().cartesianToFractional(atom.position);
            double f[3] = {fractional.x, fractional.y, fractional.z};
            for (double& value : f)
                value -= std::floor(value);
            // t is the height above the twin plane, wrapped into [0, 1). A
            // periodic cell with a mirror at t = 0 necessarily has a second one
            // at t = 1/2, so the crystal splits into:
            //   t in (1/2, 1)  the half that SURVIVES, plus its mirror image
            //                  at 1 - t, which fills (0, 1/2)
            //   t in (0, 1/2)  the half that is replaced
            //   t = 0, t = 1/2 atoms lying ON a mirror plane. They belong to
            //                  both halves and are kept exactly ONCE —
            //                  duplicating them is the classic way a coherent
            //                  twin ends up with a doubled boundary layer at
            //                  zero separation.
            const double t =
                f[axisIndex] - plane - std::floor(f[axisIndex] - plane);
            const bool onPlane = t < kOnPlane || std::abs(t - 0.5) < kOnPlane;
            if (!onPlane && t < 0.5)
                continue;
            ++kept;
            const auto emit = [&](double height) {
                double mirrored[3] = {f[0], f[1], f[2]};
                mirrored[axisIndex] =
                    plane + height - std::floor(plane + height);
                Atom copy = atom;
                copy.position = parent.cell().fractionalToCartesian(
                    Vec3{mirrored[0], mirrored[1], mirrored[2]});
                twinned.addAtom(copy);
            };
            emit(t);
            if (!onPlane)
                emit(1.0 - t);
        }
        if (kept == 0)
            throw std::invalid_argument(
                "no atoms fall in the half-cell below the twin plane");

        result.structure = std::move(twinned);
        result.interfaceCount = 2;
        result.warnings.emplace_back(
            "a mirror in a periodic cell is two twin boundaries, not one: the "
            "chosen plane and its partner half a cell away");
        result.description = "coherent twin boundary normal to "
            + toString(params.axis);
    }

    // -----------------------------------------------------------------------
    // Bicrystal and polycrystals: fill regions with rotated lattices.
    // -----------------------------------------------------------------------
    else {
        std::array<Vec3, 3> boxVectors{};
        for (int i = 0; i < 3; ++i)
            boxVectors[static_cast<std::size_t>(i)] =
                parent.cell().vectors()[static_cast<std::size_t>(i)]
                * std::max(1, params.repeat[static_cast<std::size_t>(i)]);
        const Box box = makeBox(boxVectors);

        struct Region {
            Matrix3 rotation;
            Vec3 seed;
            int phase = 0;
        };
        std::vector<Region> regions;
        std::mt19937 rng(params.seed);

        const bool bicrystal = params.kind == Kind::Bicrystal;
        if (bicrystal) {
            const Vec3 normal =
                boxVectors[static_cast<std::size_t>(plane1)]
                    .cross(boxVectors[static_cast<std::size_t>(plane2)])
                    .normalized();
            regions.push_back({rotationAbout(normal, params.rotationA * kPi / 180.0),
                               Vec3{}, 0});
            regions.push_back({rotationAbout(normal, params.rotationB * kPi / 180.0),
                               Vec3{}, 0});
        } else {
            if (params.grainCount < 1)
                throw std::invalid_argument(
                    "a polycrystal needs at least one grain");
            std::uniform_real_distribution<double> uniform(0.0, 1.0);
            // Phase choice by cumulative weight, so a 3:1 mixture really is
            // 3:1 in expectation rather than in intent.
            std::vector<double> weights = params.phaseWeights;
            if (params.kind != Kind::MultiPhasePolycrystal)
                weights.assign(1, 1.0);
            if (weights.size() != lattices.size())
                weights.assign(lattices.size(), 1.0);
            double total = 0.0;
            for (const double weight : weights)
                total += std::max(0.0, weight);
            if (total <= 0.0)
                throw std::invalid_argument(
                    "every phase weight is zero — nothing would be placed");

            for (int grain = 0; grain < params.grainCount; ++grain) {
                const Vec3 seed = box.toCartesian(
                    Vec3{uniform(rng), uniform(rng), uniform(rng)});
                double draw = uniform(rng) * total;
                int phase = 0;
                for (std::size_t p = 0; p < weights.size(); ++p) {
                    draw -= std::max(0.0, weights[p]);
                    if (draw <= 0.0) {
                        phase = static_cast<int>(p);
                        break;
                    }
                }
                regions.push_back({randomRotationMatrix(rng), seed, phase});
            }
        }

        // The grain each point belongs to: nearest seed under the box's
        // minimum-image convention. Without the minimum image the grains stop
        // at the cell face instead of wrapping through it, and the result is a
        // polycrystal with a slab of one orientation glued to every boundary.
        const auto nearestRegion = [&](const Vec3& cartesian) {
            int best = 0;
            double bestDistance = std::numeric_limits<double>::max();
            for (std::size_t r = 0; r < regions.size(); ++r) {
                const Vec3 delta = cartesian - regions[r].seed;
                for (int i = -1; i <= 1; ++i)
                    for (int j = -1; j <= 1; ++j)
                        for (int k = -1; k <= 1; ++k) {
                            const double distance =
                                (delta
                                 - (boxVectors[0] * i + boxVectors[1] * j
                                    + boxVectors[2] * k))
                                    .norm();
                            if (distance < bestDistance) {
                                bestDistance = distance;
                                best = static_cast<int>(r);
                            }
                        }
            }
            return best;
        };

        Structure assembled;
        assembled.setCell(UnitCell(boxVectors[0], boxVectors[1], boxVectors[2],
                                   {true, true, true}));
        NeighbourGrid grid(boxVectors, std::max(params.mergeTolerance, 1e-6));
        std::vector<double> grainField;
        std::vector<double> phaseField;
        result.grains.resize(regions.size());
        for (std::size_t r = 0; r < regions.size(); ++r) {
            result.grains[r].index = static_cast<int>(r);
            result.grains[r].phase = regions[r].phase;
            result.grains[r].seed = regions[r].seed;
        }

        for (std::size_t r = 0; r < regions.size(); ++r) {
            const Structure& lattice =
                lattices[static_cast<std::size_t>(
                    std::min<std::size_t>(static_cast<std::size_t>(regions[r].phase),
                                          lattices.size() - 1))];
            std::vector<TiledAtom> tiled;
            tile(lattice, regions[r].rotation, box, tiled,
                 static_cast<std::size_t>(params.atomBudget));

            for (const TiledAtom& candidate : tiled) {
                if (bicrystal) {
                    const double along = static_cast<int>(params.axis) == 0
                        ? candidate.fractional.x
                        : (static_cast<int>(params.axis) == 1
                               ? candidate.fractional.y
                               : candidate.fractional.z);
                    const bool lower = along < params.boundaryPosition;
                    if ((r == 0) != lower)
                        continue;
                } else if (nearestRegion(candidate.cartesian)
                           != static_cast<int>(r)) {
                    continue;
                }
                // The seam: an atom of this grain sitting on top of one
                // already placed by a neighbour. Dropping it is what makes the
                // boundary a boundary rather than a pile-up.
                if (params.mergeTolerance > 0.0
                    && grid.occupied(candidate.fractional, candidate.cartesian)) {
                    ++result.mergedAtoms;
                    continue;
                }
                grid.insert(candidate.fractional, candidate.cartesian);
                Atom atom;
                atom.atomicNumber = candidate.atomicNumber;
                atom.position = candidate.cartesian;
                assembled.addAtom(atom);
                grainField.push_back(static_cast<double>(r));
                phaseField.push_back(static_cast<double>(regions[r].phase));
                ++result.grains[r].atomCount;
                if (assembled.size()
                    > static_cast<std::size_t>(params.atomBudget))
                    throw std::invalid_argument(
                        "the requested cell exceeds the atom budget — reduce "
                        "the box repeats or raise the budget");
            }
        }

        if (assembled.empty())
            throw std::invalid_argument(
                "the construction placed no atoms; check the box repeats");

        // Per-atom fields so the viewport can colour by grain (and by phase),
        // which is the only practical way to see whether a tessellation came
        // out the way it was asked for.
        assembled.setScalarField("grain", grainField);
        assembled.setScalarField("phase", phaseField);
        result.structure = std::move(assembled);
        result.interfaceCount = bicrystal ? 2 : static_cast<int>(regions.size());
        result.commensurabilityResidual =
            commensurabilityResidual(parent, regions.back().rotation, box);
        if (result.commensurabilityResidual > 0.3)
            result.warnings.emplace_back(
                "the rotated lattice misses the box periods by "
                + std::to_string(result.commensurabilityResidual)
                + " Å. Only coincidence-site (CSL) misorientations fit a "
                  "periodic box exactly; at any other angle the crystals meet "
                  "the boundary out of register and the seam there is an "
                  "artifact of the box, not of the physics");
        if (bicrystal)
            result.warnings.emplace_back(
                "a bicrystal in a periodic cell has TWO grain boundaries: the "
                "one at the chosen plane and the one at the cell face");

        std::ostringstream description;
        description << toString(params.kind);
        if (bicrystal)
            description << ", misorientation "
                        << (params.rotationB - params.rotationA) << " deg about "
                        << toString(params.axis);
        else
            description << ", " << regions.size() << " grains";
        if (params.kind == Kind::MultiPhasePolycrystal)
            description << " over " << lattices.size() << " phases";
        result.description = description.str();
    }

    // -- Shared bookkeeping -------------------------------------------------
    result.minSeparation = minimumSeparation(result.structure, 20000);
    const double volume = result.structure.cell().volume();
    if (volume > 1e-12)
        result.density =
            totalMassU(result.structure) / volume * kUPerA3ToGCm3;
    if (result.minSeparation > 0.0 && result.minSeparation < 0.7)
        result.warnings.emplace_back(
            "two atoms ended up closer than 0.7 Å. Raise the merge tolerance, "
            "or open a gap at the boundary");
    return result;
}

} // namespace calango::core
