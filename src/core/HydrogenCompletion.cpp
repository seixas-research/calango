#include "core/HydrogenCompletion.hpp"

#include "core/Element.hpp"

#include <cmath>
#include <map>
#include <vector>

namespace calango::core {

namespace {

/// Covalent radius of hydrogen (Cordero et al.), used for the default X–H
/// length when no explicit one is given.
constexpr double kHydrogenRadius = 0.31;

/// A bond order of 4 means AROMATIC in this codebase, not a quadruple bond;
/// against a valence it counts as the 1.5 that makes benzene's carbons come
/// out with exactly one hydrogen each.
double valenceContribution(int order)
{
    if (order == 4)
        return 1.5;
    return order < 1 ? 1.0 : static_cast<double>(order);
}

/// Estimate a bond's order from its LENGTH, for the bonds nobody has assigned
/// one to — which, since orders are never auto-perceived, is almost all of
/// them in a structure read from a file.
///
/// This matters more than it sounds. Counting every perceived bond as single
/// puts a hydrogen on every carbonyl carbon in a protein: that carbon already
/// has three neighbours (N, Cα, O) and only looks one short. Tabulated
/// covalent radii are SINGLE-bond radii, so a bond markedly shorter than their
/// sum is a multiple bond, and the ratio separates the cases cleanly —
/// C=O lands at 0.87, an amide C–N and an aromatic C–C at ~0.91, and every
/// genuine single bond at 0.99–1.02.
///
/// Returns the valence contribution, not an integer order: the aromatic and
/// amide cases are worth 1.5 each, which is exactly what makes a peptide
/// nitrogen come out with one hydrogen and its carbonyl carbon with none.
double estimatedContribution(int zi, int zj, double length)
{
    // A terminal element cannot carry a multiple bond, so leave anything
    // touching H or a halogen at single regardless of how short it looks.
    // Likewise anything on an element with no tabulated valence — a metal
    // coordination distance is not evidence of a double bond.
    if (standardValence(zi) < 2 || standardValence(zj) < 2)
        return 1.0;
    const double single =
        static_cast<double>(Elements::data(zi).covalentRadius)
        + static_cast<double>(Elements::data(zj).covalentRadius);
    if (single < 1e-6)
        return 1.0;
    const double ratio = length / single;
    if (ratio <= 0.830)
        return 3.0;  // C≡C 0.79, C≡N 0.79
    if (ratio <= 0.890)
        return 2.0;  // C=C 0.88, C=O 0.87
    if (ratio <= 0.945)
        return 1.5;  // aromatic C–C 0.91, amide C–N 0.90
    return 1.0;
}

} // namespace

int standardValence(int z)
{
    // Main-group elements whose neutral valence is unambiguous enough to build
    // on. Deliberately narrow: the table is the guard that keeps this feature
    // from decorating a metal surface or a transition-metal centre with
    // hydrogens that no chemist asked for.
    static const std::map<int, int> kValence = {
        {5, 3},   // B
        {6, 4},   // C
        {7, 3},   // N
        {8, 2},   // O
        {9, 1},   // F
        {14, 4},  // Si
        {15, 3},  // P
        {16, 2},  // S
        {17, 1},  // Cl
        {32, 4},  // Ge
        {33, 3},  // As
        {34, 2},  // Se
        {35, 1},  // Br
        {50, 4},  // Sn
        {51, 3},  // Sb
        {52, 2},  // Te
        {53, 1},  // I
    };
    const auto it = kValence.find(z);
    return it == kValence.end() ? 0 : it->second;
}

HydrogenCompletionResult completeWithHydrogens(
    Structure& structure, const HydrogenCompletionOptions& options)
{
    HydrogenCompletionResult result;
    if (structure.empty())
        return result;

    const std::size_t original = structure.size();

    // Bonds are perceived ONCE, before anything is added. Re-perceiving after
    // each atom would let a freshly placed hydrogen shift the coordination of
    // its own neighbours mid-run, and the answer would depend on atom order.
    const std::vector<Bond> bonds =
        structure.detectBonds(options.bondTolerance, options.autoBonds);

    // Per heavy atom: the summed bond order it already carries, and the unit
    // directions to its neighbours (at the bonded periodic image, so a bond
    // that wraps the cell points out of the cell rather than back across it).
    std::vector<double> orderSum(original, 0.0);
    std::vector<std::vector<Vec3>> neighbourDirs(original);
    for (const Bond& bond : bonds) {
        const auto i = static_cast<std::size_t>(bond.i);
        const auto j = static_cast<std::size_t>(bond.j);
        if (i >= original || j >= original)
            continue;
        const Vec3 pi = structure.atoms()[i].position;
        const Vec3 pj = structure.atoms()[j].position + bond.imageOffset;
        const Vec3 delta = pj - pi;
        // An order of 1 is the DEFAULT, never a stored value (setBondOrder(1)
        // resets the pair), so it means "nobody said" — exactly the case the
        // length estimate is for. An explicitly assigned 2, 3 or aromatic 4 is
        // a statement of chemistry and always wins over the geometry.
        const double contribution = bond.order > 1
            ? valenceContribution(bond.order)
            : estimatedContribution(structure.atoms()[i].atomicNumber,
                                    structure.atoms()[j].atomicNumber,
                                    delta.norm());
        orderSum[i] += contribution;
        orderSum[j] += contribution;
        if (delta.norm() < 1e-6)
            continue;
        neighbourDirs[i].push_back(delta.normalized());
        neighbourDirs[j].push_back((pi - pj).normalized());
    }

    std::vector<ResidueInfo> residues;
    const bool annotated = structure.hasResidues();
    if (annotated)
        residues = structure.residues();

    for (std::size_t index = 0; index < original; ++index) {
        const Atom& atom = structure.atoms()[index];
        if (atom.atomicNumber == 1)
            continue; // hydrogen completes others, never itself
        const int valence = standardValence(atom.atomicNumber);
        if (valence == 0) {
            ++result.skippedAtoms;
            continue;
        }
        // Round rather than truncate: an aromatic carbon sums to 3.0 exactly,
        // but a mixed aromatic/single environment can land on 2.999…
        const int missing = static_cast<int>(
            std::lround(static_cast<double>(valence) - orderSum[index]));
        if (missing <= 0)
            continue;

        // --- Where the new bonds point ------------------------------------
        //
        // Existing neighbours are fixed points on the unit sphere; the new
        // directions relax against them, and against each other, under a
        // repulsive force. That is VSEPR expressed as a minimization instead
        // of a lookup table, so it produces the tetrahedral, trigonal, bent
        // and linear arrangements from the coordination number alone — and
        // still does something sensible for the cases a table would miss.
        const std::vector<Vec3>& fixed = neighbourDirs[index];
        std::vector<Vec3> fresh;
        fresh.reserve(static_cast<std::size_t>(missing));

        // Seeding matters more than it looks: relaxation on a sphere is a
        // gradient descent, and a seed sitting exactly on — or exactly
        // opposite — an existing bond feels a force with no tangential
        // component, so it would never move off a start that happens to be
        // aligned. Axis-aligned input geometry makes that the common case,
        // not a rare one.
        //
        // So the seeds are placed AWAY from the existing bonds to begin with:
        // the anti-resultant of the fixed directions is where the missing
        // bonds belong, and for one, two or three existing neighbours it is
        // already the right answer (the opposite of a lone bond, the bisector
        // of a pair, the fourth vertex of a tetrahedron). Relaxation then only
        // has to refine it.
        Vec3 bias{};
        for (const Vec3& direction : fixed)
            bias = bias - direction;
        const double biasNorm = bias.norm();
        if (biasNorm > 1e-6) {
            bias = bias * (1.0 / biasNorm);
            // Orthonormal frame about the bias, to fan several new bonds
            // around it. The seed axis is whichever of x/y/z is least
            // parallel to the bias, so the cross product never degenerates.
            const Vec3 axis = std::fabs(bias.x) < 0.9 ? Vec3{1.0, 0.0, 0.0}
                                                      : Vec3{0.0, 1.0, 0.0};
            const Vec3 e1 = bias.cross(axis).normalized();
            const Vec3 e2 = bias.cross(e1);
            // A tetrahedral opening about the bias: right for the sp3 case
            // that dominates, and a harmless start for everything else.
            const double cone = missing == 1 ? 0.0 : 1.2310; // 70.53°
            for (int k = 0; k < missing; ++k) {
                const double phi =
                    2.0 * M_PI * static_cast<double>(k) / static_cast<double>(missing);
                const Vec3 lateral =
                    e1 * (std::cos(phi) * std::sin(cone))
                    + e2 * (std::sin(phi) * std::sin(cone));
                fresh.push_back((bias * std::cos(cone) + lateral).normalized());
            }
        } else {
            // No bonds at all, or a set whose directions cancel: no preferred
            // side exists, so spread the seeds evenly with the golden-angle
            // spiral and let the relaxation find the symmetric arrangement.
            constexpr double kGolden = 2.39996322972865332; // π(3 − √5)
            for (int k = 0; k < missing; ++k) {
                const double t = (static_cast<double>(k) + 0.5)
                    / static_cast<double>(missing);
                const double z = 1.0 - 2.0 * t;
                const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
                const double phi = kGolden * static_cast<double>(k);
                fresh.push_back(
                    Vec3{r * std::cos(phi), r * std::sin(phi), z}.normalized());
            }
        }

        constexpr int kRelaxSteps = 200;
        constexpr double kStep = 0.08;
        for (int step = 0; step < kRelaxSteps; ++step) {
            std::vector<Vec3> force(fresh.size(), Vec3{});
            const auto repel = [](const Vec3& from, const Vec3& to) {
                const Vec3 delta = to - from;
                const double d = delta.norm();
                if (d < 1e-6) {
                    // Exactly coincident: the repulsion direction is
                    // undefined, so push along a fixed perpendicular rather
                    // than dividing by zero. Any direction will do — it only
                    // has to break the tie so the next step has a gradient.
                    const Vec3 axis = std::fabs(to.x) < 0.9
                        ? Vec3{1.0, 0.0, 0.0}
                        : Vec3{0.0, 1.0, 0.0};
                    return to.cross(axis).normalized();
                }
                return delta * (1.0 / (d * d * d));
            };
            for (std::size_t a = 0; a < fresh.size(); ++a) {
                for (const Vec3& other : fixed)
                    force[a] = force[a] + repel(other, fresh[a]);
                for (std::size_t b = 0; b < fresh.size(); ++b)
                    if (a != b)
                        force[a] = force[a] + repel(fresh[b], fresh[a]);
            }
            for (std::size_t a = 0; a < fresh.size(); ++a) {
                Vec3 moved = fresh[a] + force[a] * kStep;
                const double norm = moved.norm();
                if (norm < 1e-9)
                    continue; // pathological; leave this one where it is
                fresh[a] = moved * (1.0 / norm); // back onto the unit sphere
            }
        }

        // --- Place the hydrogens ------------------------------------------
        const double length = options.bondLength > 0.0
            ? options.bondLength
            : static_cast<double>(atom.covalentRadius()) + kHydrogenRadius;
        const Vec3 center = atom.position;
        for (const Vec3& direction : fresh) {
            Atom hydrogen;
            hydrogen.atomicNumber = 1;
            hydrogen.position = center + direction * length;
            structure.addAtom(hydrogen);
            if (annotated) {
                // Inherit the parent's residue so a completed protein still
                // groups by chain and residue — an added hydrogen belongs to
                // the residue of the atom it hangs off.
                ResidueInfo info = residues[index];
                info.atomName = "H";
                residues.push_back(info);
            }
            ++result.added;
        }
        ++result.completedAtoms;
    }

    if (annotated && residues.size() == structure.size())
        structure.setResidues(std::move(residues));
    return result;
}

} // namespace calango::core
