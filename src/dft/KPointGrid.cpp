#include "dft/KPointGrid.hpp"

#include <algorithm>
#include <cmath>

namespace calango::dft {
namespace {

/// Integer 3x3 determinant.
int determinant(const std::array<int, 9>& m)
{
    return m[0] * (m[4] * m[8] - m[5] * m[7])
        - m[1] * (m[3] * m[8] - m[5] * m[6])
        + m[2] * (m[3] * m[7] - m[4] * m[6]);
}

/// Inverse of an integer matrix with determinant ±1, which is again integer:
/// the adjugate divided by ±1. Returns false for anything else, which for a
/// symmetry operation would mean it does not preserve the cell volume.
bool integerInverse(const std::array<int, 9>& m, std::array<int, 9>& inverse)
{
    const int det = determinant(m);
    if (det != 1 && det != -1)
        return false;
    const std::array<int, 9> adjugate{
        m[4] * m[8] - m[5] * m[7], m[2] * m[7] - m[1] * m[8],
        m[1] * m[5] - m[2] * m[4], m[5] * m[6] - m[3] * m[8],
        m[0] * m[8] - m[2] * m[6], m[2] * m[3] - m[0] * m[5],
        m[3] * m[7] - m[4] * m[6], m[1] * m[6] - m[0] * m[7],
        m[0] * m[4] - m[1] * m[3]};
    for (std::size_t i = 0; i < 9; ++i)
        inverse[i] = adjugate[i] * det; // 1/det == det for det = ±1
    return true;
}

/// The transpose of the inverse — the matrix that acts on k.
bool reciprocalOperation(const std::array<int, 9>& w,
                         std::array<int, 9>& result)
{
    std::array<int, 9> inverse{};
    if (!integerInverse(w, inverse))
        return false;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            result[static_cast<std::size_t>(i * 3 + j)] =
                inverse[static_cast<std::size_t>(j * 3 + i)];
    return true;
}

/// Wrap a fractional coordinate into [0, 1) with a tolerance, so that a value
/// numerically just below 1 folds to 0 rather than staying at 0.999999.
double wrap(double x)
{
    double y = x - std::floor(x);
    if (y > 1.0 - 1.0e-9)
        y = 0.0;
    return y;
}

} // namespace

std::vector<SymmetryOperation> KPointGrid::latticePointGroup(
    const std::vector<std::array<double, 3>>& lattice, double tolerance)
{
    std::vector<SymmetryOperation> operations;
    if (lattice.size() != 3)
        return operations;

    // Metric tensor G_ij = a_i · a_j. A rotation is a lattice symmetry exactly
    // when its integer matrix W satisfies WᵀGW = G — the statement that the
    // Cartesian map is orthogonal AND takes lattice points to lattice points.
    std::array<double, 9> metric{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (int c = 0; c < 3; ++c)
                sum += lattice[static_cast<std::size_t>(i)]
                              [static_cast<std::size_t>(c)]
                    * lattice[static_cast<std::size_t>(j)]
                             [static_cast<std::size_t>(c)];
            metric[static_cast<std::size_t>(i * 3 + j)] = sum;
        }
    double scale = 0.0;
    for (int i = 0; i < 3; ++i)
        scale = std::max(scale, std::abs(metric[static_cast<std::size_t>(i * 4)]));
    if (!(scale > 0.0))
        return operations;

    // A symmetry sends each lattice vector to a lattice vector of the SAME
    // LENGTH, so the candidates are the short integer triples whose vector
    // matches each lattice vector's length. Enumerating those first turns a
    // search over all integer matrices into a product of three small lists.
    // Range ±3 covers every crystal system for a primitive cell; the metric
    // test below is what actually decides, so a candidate too many costs only
    // time.
    //
    // They become the COLUMNS of W, not the rows. With p' = Wp acting on
    // fractional coordinates, lattice vector a_j has fractional coordinates
    // e_j and its image is W e_j — column j. Building them as rows yields the
    // TRANSPOSED group, which is the same size, is still a group, and is
    // therefore completely invisible for a crystal with one atom at the
    // origin, where every operation maps the single position onto itself
    // regardless. It shows up only once a basis atom sits off-origin.
    std::array<std::vector<std::array<int, 3>>, 3> candidates;
    for (int row = 0; row < 3; ++row) {
        const double target = metric[static_cast<std::size_t>(row * 4)];
        for (int a = -3; a <= 3; ++a)
            for (int b = -3; b <= 3; ++b)
                for (int c = -3; c <= 3; ++c) {
                    if (a == 0 && b == 0 && c == 0)
                        continue;
                    // |n·a|² through the metric, no Cartesian vectors needed.
                    const int n[3] = {a, b, c};
                    double length = 0.0;
                    for (int i = 0; i < 3; ++i)
                        for (int j = 0; j < 3; ++j)
                            length += n[i] * n[j]
                                * metric[static_cast<std::size_t>(i * 3 + j)];
                    if (std::abs(length - target) <= tolerance * scale)
                        candidates[static_cast<std::size_t>(row)].push_back(
                            {a, b, c});
                }
    }

    for (const auto& c0 : candidates[0])
        for (const auto& c1 : candidates[1])
            for (const auto& c2 : candidates[2]) {
                // Columns, as argued above.
                const std::array<int, 9> w{c0[0], c1[0], c2[0], c0[1], c1[1],
                                           c2[1], c0[2], c1[2], c2[2]};
                const int det = determinant(w);
                if (det != 1 && det != -1)
                    continue;
                // WᵀGW = G, checked in full rather than only on the diagonal:
                // the column lengths already match by construction, so it is
                // the ANGLES between them that this test really enforces.
                bool preserves = true;
                for (int i = 0; i < 3 && preserves; ++i)
                    for (int j = 0; j < 3 && preserves; ++j) {
                        double sum = 0.0;
                        for (int p = 0; p < 3; ++p)
                            for (int q = 0; q < 3; ++q)
                                sum += w[static_cast<std::size_t>(p * 3 + i)]
                                    * metric[static_cast<std::size_t>(p * 3 + q)]
                                    * w[static_cast<std::size_t>(q * 3 + j)];
                        if (std::abs(sum
                                     - metric[static_cast<std::size_t>(i * 3 + j)])
                            > tolerance * scale)
                            preserves = false;
                    }
                if (preserves)
                    operations.push_back({w, false});
            }
    return operations;
}

std::vector<SymmetryOperation> KPointGrid::crystalPointGroup(
    const std::vector<std::array<double, 3>>& lattice,
    const std::vector<Atom>& atoms, double tolerance)
{
    const std::vector<SymmetryOperation> lattices =
        latticePointGroup(lattice, tolerance);
    if (atoms.empty() || lattices.empty())
        return lattices;

    // Cartesian → fractional. The lattice vectors are rows of A and a position
    // is r = Aᵀp, so p = (Aᵀ)⁻¹ r.
    std::array<double, 9> a{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            a[static_cast<std::size_t>(j * 3 + i)] =
                lattice[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
    const double det = a[0] * (a[4] * a[8] - a[5] * a[7])
        - a[1] * (a[3] * a[8] - a[5] * a[6])
        + a[2] * (a[3] * a[7] - a[4] * a[6]);
    if (std::abs(det) < 1.0e-12)
        return {};
    const std::array<double, 9> inverse{
        (a[4] * a[8] - a[5] * a[7]) / det, (a[2] * a[7] - a[1] * a[8]) / det,
        (a[1] * a[5] - a[2] * a[4]) / det, (a[5] * a[6] - a[3] * a[8]) / det,
        (a[0] * a[8] - a[2] * a[6]) / det, (a[2] * a[3] - a[0] * a[5]) / det,
        (a[3] * a[7] - a[4] * a[6]) / det, (a[1] * a[6] - a[0] * a[7]) / det,
        (a[0] * a[4] - a[1] * a[3]) / det};

    std::vector<std::array<double, 3>> fractional(atoms.size());
    for (std::size_t n = 0; n < atoms.size(); ++n)
        for (int i = 0; i < 3; ++i) {
            double sum = 0.0;
            for (int j = 0; j < 3; ++j)
                sum += inverse[static_cast<std::size_t>(i * 3 + j)]
                    * atoms[n].position[static_cast<std::size_t>(j)];
            fractional[n][static_cast<std::size_t>(i)] = wrap(sum);
        }

    std::vector<SymmetryOperation> result;
    for (const SymmetryOperation& operation : lattices) {
        // p → Wp, then look for a translation that puts the image back on the
        // atoms. Candidate translations are those taking the FIRST atom onto
        // some atom of the same species: any valid translation must do that,
        // so the list is short and complete.
        std::vector<std::array<double, 3>> rotated(atoms.size());
        for (std::size_t n = 0; n < atoms.size(); ++n)
            for (int i = 0; i < 3; ++i) {
                double sum = 0.0;
                for (int j = 0; j < 3; ++j)
                    sum += operation.at(i, j)
                        * fractional[n][static_cast<std::size_t>(j)];
                rotated[n][static_cast<std::size_t>(i)] = sum;
            }

        bool accepted = false;
        for (std::size_t candidate = 0;
             candidate < atoms.size() && !accepted; ++candidate) {
            if (atoms[candidate].atomicNumber != atoms[0].atomicNumber)
                continue;
            std::array<double, 3> shift{};
            for (int i = 0; i < 3; ++i)
                shift[static_cast<std::size_t>(i)] =
                    fractional[candidate][static_cast<std::size_t>(i)]
                    - rotated[0][static_cast<std::size_t>(i)];

            bool maps = true;
            for (std::size_t n = 0; n < atoms.size() && maps; ++n) {
                std::array<double, 3> image{};
                for (int i = 0; i < 3; ++i)
                    image[static_cast<std::size_t>(i)] = wrap(
                        rotated[n][static_cast<std::size_t>(i)]
                        + shift[static_cast<std::size_t>(i)]);
                bool found = false;
                for (std::size_t m = 0; m < atoms.size() && !found; ++m) {
                    if (atoms[m].atomicNumber != atoms[n].atomicNumber)
                        continue;
                    double worst = 0.0;
                    for (int i = 0; i < 3; ++i) {
                        double d = std::abs(
                            image[static_cast<std::size_t>(i)]
                            - fractional[m][static_cast<std::size_t>(i)]);
                        d = std::min(d, 1.0 - d); // periodic distance
                        worst = std::max(worst, d);
                    }
                    if (worst < 1.0e-4)
                        found = true;
                }
                maps = found;
            }
            accepted = maps;
        }
        if (accepted)
            result.push_back(operation);
    }
    return result;
}

Outcome KPointGrid::build(std::array<int, 3> divisions,
                          const std::vector<std::array<double, 3>>& lattice,
                          const std::vector<Atom>& atoms, Symmetry symmetry)
{
    points_.clear();
    operations_.clear();
    fullMesh_ = 0;
    crystalOperations_ = 0;

    if (lattice.empty()) {
        // A finite system has one k-point and it is not a choice.
        points_.push_back({{{0.0, 0.0, 0.0}}, 1.0, 1});
        fullMesh_ = 1;
        return Outcome::success();
    }
    if (lattice.size() != 3)
        return Outcome::invalid("k-points: a cell needs three lattice vectors");
    for (int& n : divisions)
        n = std::max(1, n);
    const std::size_t total = static_cast<std::size_t>(divisions[0])
        * static_cast<std::size_t>(divisions[1])
        * static_cast<std::size_t>(divisions[2]);
    fullMesh_ = total;

    // --- The operations that will fold the mesh ---------------------------
    std::vector<SymmetryOperation> group;
    if (symmetry == Symmetry::PointGroup) {
        group = crystalPointGroup(lattice, atoms);
        crystalOperations_ = group.size();
    } else {
        group.push_back({{1, 0, 0, 0, 1, 0, 0, 0, 1}, false});
        crystalOperations_ = 1;
    }
    if (symmetry == Symmetry::TimeReversal
        || symmetry == Symmetry::PointGroup) {
        // k → −k for a real Hamiltonian. Added as −W of everything already
        // present; duplicates are harmless, since folding is idempotent.
        const std::size_t existing = group.size();
        for (std::size_t i = 0; i < existing; ++i) {
            SymmetryOperation inverted = group[i];
            for (int& value : inverted.matrix)
                value = -value;
            inverted.fromTimeReversal = true;
            group.push_back(inverted);
        }
    }

    // --- Keep only what maps the MESH onto itself -------------------------
    // A 4x4x2 mesh does not have the symmetry of its own crystal. Using an
    // operation that carries a mesh point off the mesh would silently
    // mis-weight the sum, so it is discarded here rather than approximated
    // later.
    std::vector<std::array<int, 9>> reciprocal;
    for (const SymmetryOperation& operation : group) {
        std::array<int, 9> k{};
        if (!reciprocalOperation(operation.matrix, k))
            continue;
        bool compatible = true;
        for (int i = 0; i < 3 && compatible; ++i)
            for (int j = 0; j < 3 && compatible; ++j) {
                // Component i of the image of a mesh vector e_j/N_j is
                // k(i,j)/N_j, and it must be a multiple of 1/N_i.
                const int numerator = k[static_cast<std::size_t>(i * 3 + j)]
                    * divisions[static_cast<std::size_t>(i)];
                if (numerator % divisions[static_cast<std::size_t>(j)] != 0)
                    compatible = false;
            }
        if (!compatible)
            continue;
        if (std::find(reciprocal.begin(), reciprocal.end(), k)
            == reciprocal.end()) {
            reciprocal.push_back(k);
            operations_.push_back(operation);
        }
    }
    if (reciprocal.empty())
        reciprocal.push_back({1, 0, 0, 0, 1, 0, 0, 0, 1});

    // --- Fold the mesh ----------------------------------------------------
    // Everything is done in INTEGER mesh coordinates, so orbit membership is
    // an exact comparison rather than a floating-point one.
    const auto index = [&divisions](int p, int q, int r) {
        return (static_cast<std::size_t>(p) * divisions[1]
                + static_cast<std::size_t>(q))
            * divisions[2] + static_cast<std::size_t>(r);
    };
    std::vector<int> representative(total, -1);
    for (int p = 0; p < divisions[0]; ++p)
        for (int q = 0; q < divisions[1]; ++q)
            for (int r = 0; r < divisions[2]; ++r) {
                const std::size_t here = index(p, q, r);
                if (representative[here] >= 0)
                    continue;
                const int owner = static_cast<int>(points_.size());
                std::size_t orbit = 0;
                const int mesh[3] = {p, q, r};
                for (const std::array<int, 9>& k : reciprocal) {
                    // The image in MESH UNITS. With k_j = m_j/N_j the image is
                    //     m'_i = sum_j (N_i K_ij / N_j) m_j ,
                    // and the compatibility filter above is exactly the
                    // statement that every N_i K_ij / N_j is an integer, so
                    // this is exact integer arithmetic with no rounding.
                    int image[3] = {0, 0, 0};
                    for (int i = 0; i < 3; ++i) {
                        const int ni = divisions[static_cast<std::size_t>(i)];
                        int numerator = 0;
                        for (int j = 0; j < 3; ++j)
                            numerator += k[static_cast<std::size_t>(i * 3 + j)]
                                * ni / divisions[static_cast<std::size_t>(j)]
                                * mesh[j];
                        image[i] = ((numerator % ni) + ni) % ni;
                    }
                    const std::size_t there = index(image[0], image[1], image[2]);
                    if (representative[there] < 0) {
                        representative[there] = owner;
                        ++orbit;
                    }
                }
                KPoint point;
                point.fractional = {
                    {static_cast<double>(p) / divisions[0],
                     static_cast<double>(q) / divisions[1],
                     static_cast<double>(r) / divisions[2]}};
                point.orbitSize = orbit;
                point.weight = static_cast<double>(orbit)
                    / static_cast<double>(total);
                points_.push_back(point);
            }

    // The weights must sum to one exactly, or every extensive quantity is
    // scaled by the error. Cheap to assert and impossible to notice otherwise.
    double sum = 0.0;
    for (const KPoint& point : points_)
        sum += point.weight;
    if (std::abs(sum - 1.0) > 1.0e-12)
        return {Status::NumericalFailure,
                "k-point weights sum to " + std::to_string(sum)
                    + " rather than 1; the symmetry folding lost or "
                      "double-counted points"};
    return Outcome::success();
}

} // namespace calango::dft
