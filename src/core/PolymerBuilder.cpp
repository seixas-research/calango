#include "core/PolymerBuilder.hpp"

#include "core/Element.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

namespace calango::core {

namespace {

constexpr double kUPerA3ToGCm3 = 1.66053907;
/// Sp3 C–C backbone bond length and the tetrahedral valence angle.
constexpr double kCCBond = 1.54;
constexpr double kTetrahedralDeg = 109.47;
constexpr double kCHBond = 1.09;
constexpr double kCFBond = 1.35;
constexpr double kCClBond = 1.79;
constexpr double kCOBond = 1.43;
constexpr double kOHBond = 0.96;
constexpr double kCNBond = 1.47;
constexpr double kNHBond = 1.01;
constexpr double kCOdouble = 1.23;

/// A substituent hanging off one backbone site.
struct Substituent {
    int z = 1;          ///< atomic number of the attached atom
    double bond = kCHBond;
    bool stereo = false; ///< true for the group whose side tacticity decides
    /// Extra atoms making up a larger group, as (Z, bond length, spread) built
    /// out from the attached atom. Empty for a single atom.
    std::vector<std::pair<int, double>> tail;
};

/// One backbone site: which element the backbone atom is, and what hangs off it.
struct BackboneSite {
    int z = 6;
    std::vector<Substituent> groups;
};

/// The repeat unit as a sequence of backbone sites.
std::vector<BackboneSite> monomerSites(PolymerBuilder::Monomer monomer)
{
    using M = PolymerBuilder::Monomer;
    const Substituent h{1, kCHBond, false, {}};
    switch (monomer) {
    case M::Polyethylene:
        return {{6, {h, h}}, {6, {h, h}}};
    case M::Polypropylene:
        // The methyl carbon is the stereocentre's substituent; its three
        // hydrogens follow it as a tail.
        return {{6, {h, h}},
                {6, {h, Substituent{6, kCCBond, true, {{1, kCHBond},
                                                       {1, kCHBond},
                                                       {1, kCHBond}}}}}};
    case M::Polystyrene:
        // The phenyl ring is represented by its ipso carbon plus a para carbon
        // stub: a full C6H5 ring would need its own internal geometry, and the
        // stub keeps the steric bulk and the stereochemistry that tacticity is
        // about without fabricating ring coordinates.
        return {{6, {h, h}},
                {6, {h, Substituent{6, kCCBond, true, {{6, 1.39}, {1, kCHBond}}}}}};
    case M::Ptfe: {
        const Substituent f{9, kCFBond, false, {}};
        return {{6, {f, f}}, {6, {f, f}}};
    }
    case M::PolyvinylChloride:
        return {{6, {h, h}},
                {6, {h, Substituent{17, kCClBond, true, {}}}}};
    case M::Nylon66: {
        // -[NH-(CH2)6-NH-CO-(CH2)4-CO]- : amide N-H, six methylenes, a second
        // amide, then the diacid's four methylenes between two carbonyls.
        std::vector<BackboneSite> sites;
        sites.push_back({7, {Substituent{1, kNHBond, false, {}}}});
        for (int i = 0; i < 6; ++i)
            sites.push_back({6, {h, h}});
        sites.push_back({7, {Substituent{1, kNHBond, false, {}}}});
        sites.push_back({6, {Substituent{8, kCOdouble, false, {}}}});
        for (int i = 0; i < 4; ++i)
            sites.push_back({6, {h, h}});
        sites.push_back({6, {Substituent{8, kCOdouble, false, {}}}});
        return sites;
    }
    }
    return {{6, {h, h}}, {6, {h, h}}};
}

/// Bond length between consecutive backbone sites.
double backboneBond(int za, int zb)
{
    if ((za == 7 && zb == 6) || (za == 6 && zb == 7))
        return kCNBond;
    return kCCBond;
}

/// An orthonormal frame with `forward` as its first axis.
void frameFor(const Vec3& forward, Vec3& right, Vec3& up)
{
    const Vec3 f = forward.normalized();
    const Vec3 helper = std::abs(f.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    right = helper.cross(f).normalized();
    up = f.cross(right).normalized();
}

/// Place the next backbone atom by the Natural Extension Reference Frame
/// construction: given the previous three positions A, B, C, put D at bond
/// length `bond` from C, with valence angle `theta` at C and torsion `phi`
/// about the B-C bond.
///
/// The torsion MUST be referenced to the A-B-C plane. Measuring it against an
/// arbitrary perpendicular frame instead (the obvious shortcut) makes every
/// step's reference independent of the last, so an "all-trans" chain folds into
/// a coil rather than extending — the geometry stays locally valid and the
/// global conformation is silently wrong.
Vec3 nerfPlace(const Vec3& a, const Vec3& b, const Vec3& c, double bond,
               double theta, double phi)
{
    const Vec3 bc = (c - b).normalized();
    Vec3 normal = (b - a).cross(bc);
    if (normal.norm() < 1e-9) {
        // Collinear A-B-C: any perpendicular will do to seed the frame.
        const Vec3 helper = std::abs(bc.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        normal = helper.cross(bc);
    }
    normal = normal.normalized();
    const Vec3 cross = normal.cross(bc);
    const double d0 = -bond * std::cos(theta);
    const double d1 = bond * std::sin(theta) * std::cos(phi);
    const double d2 = bond * std::sin(theta) * std::sin(phi);
    return c + bc * d0 + cross * d1 + normal * d2;
}

/// Rotate `v` about the unit axis `axis` by `angle` radians (Rodrigues).
Vec3 rotateAbout(const Vec3& v, const Vec3& axis, double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return v * c + axis.cross(v) * s + axis * (axis.dot(v) * (1.0 - c));
}

} // namespace

std::string PolymerBuilder::toString(Monomer monomer)
{
    switch (monomer) {
    case Monomer::Polyethylene: return "Polyethylene";
    case Monomer::Polypropylene: return "Polypropylene";
    case Monomer::Polystyrene: return "Polystyrene";
    case Monomer::Ptfe: return "PTFE";
    case Monomer::PolyvinylChloride: return "Poly(vinyl chloride)";
    case Monomer::Nylon66: return "Nylon-6,6";
    }
    return "Polymer";
}

std::string PolymerBuilder::toString(Tacticity tacticity)
{
    switch (tacticity) {
    case Tacticity::Isotactic: return "isotactic";
    case Tacticity::Syndiotactic: return "syndiotactic";
    case Tacticity::Atactic: return "atactic";
    }
    return "atactic";
}

bool PolymerBuilder::hasTacticity(Monomer monomer)
{
    // Only a backbone carbon bearing two DIFFERENT substituents is a
    // stereocentre. Polyethylene and PTFE have identical pairs, so "isotactic
    // polyethylene" is not a thing and the control is meaningless for them.
    return monomer == Monomer::Polypropylene || monomer == Monomer::Polystyrene
        || monomer == Monomer::PolyvinylChloride;
}

double PolymerBuilder::monomerMassU(Monomer monomer)
{
    double total = 0.0;
    for (const BackboneSite& site : monomerSites(monomer)) {
        total += Elements::atomicMass(site.z);
        for (const Substituent& group : site.groups) {
            total += Elements::atomicMass(group.z);
            for (const auto& [z, bond] : group.tail) {
                (void)bond;
                total += Elements::atomicMass(z);
            }
        }
    }
    return total;
}

PolymerBuilder::Result PolymerBuilder::generate(const Params& params)
{
    if (params.degreeOfPolymerization < 1)
        throw std::invalid_argument("the degree of polymerization must be >= 1");
    if (params.chainCount < 1)
        throw std::invalid_argument("at least one chain is required");

    const std::vector<BackboneSite> repeat = monomerSites(params.monomer);
    const int sitesPerMonomer = static_cast<int>(repeat.size());
    const int backboneLength = sitesPerMonomer * params.degreeOfPolymerization;

    // -- Box --------------------------------------------------------------
    double lx = params.boxLx, ly = params.boxLy, lz = params.boxLz;
    if (params.useDensityTarget) {
        if (params.densityGCm3 <= 0.0)
            throw std::invalid_argument("density must be positive");
        const double chainMass =
            monomerMassU(params.monomer) * params.degreeOfPolymerization;
        const double volume = params.chainCount * chainMass * kUPerA3ToGCm3
            / params.densityGCm3;
        lx = ly = lz = std::cbrt(volume);
    }
    if (lx <= 0.0 || ly <= 0.0 || lz <= 0.0)
        throw std::invalid_argument("box dimensions must be positive");

    std::mt19937 rng(params.seed);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::normal_distribution<double> gaussian(0.0, 1.0);

    Structure structure;
    structure.setCell(UnitCell({lx, 0, 0}, {0, ly, 0}, {0, 0, lz}));

    Result result;
    const double minSq = params.minAtomDistance * params.minAtomDistance;
    // Positions already committed, for the inter-chain overlap test. Kept flat
    // (rather than per-chain) because the check is the same either way.
    std::vector<Vec3> placed;

    const double valence = kTetrahedralDeg * M_PI / 180.0;
    const double torsion = params.helixTorsionDeg * M_PI / 180.0;

    for (int chain = 0; chain < params.chainCount; ++chain) {
        // -- Grow the backbone ------------------------------------------------
        std::vector<Vec3> backbone;
        backbone.reserve(static_cast<std::size_t>(backboneLength));
        bool placedChain = false;

        for (int attempt = 0; attempt < 40 && !placedChain; ++attempt) {
            backbone.clear();
            Vec3 start{unit(rng) * lx, unit(rng) * ly, unit(rng) * lz};
            Vec3 direction{gaussian(rng), gaussian(rng), gaussian(rng)};
            if (direction.norm() < 1e-9)
                direction = {1, 0, 0};
            direction = direction.normalized();
            backbone.push_back(start);

            // Seed the first two atoms: NeRF needs three prior positions,
            // so the first bond is placed along the random start direction and
            // the second by a plain valence-angle bend.
            Vec3 right, up;
            frameFor(direction, right, up);
            bool failed = false;
            if (backboneLength > 1)
                backbone.push_back(start + direction * kCCBond);
            if (backboneLength > 2)
                backbone.push_back(
                    backbone.back()
                    + (direction * std::cos(M_PI - valence)
                       + right * std::sin(M_PI - valence))
                          .normalized()
                          * kCCBond);

            for (int step = static_cast<int>(backbone.size());
                 step < backboneLength && !failed; ++step) {
                const int za = repeat[static_cast<std::size_t>((step - 1)
                                                               % sitesPerMonomer)].z;
                const int zb =
                    repeat[static_cast<std::size_t>(step % sitesPerMonomer)].z;
                const double bond = backboneBond(za, zb);

                Vec3 next;
                bool accepted = false;
                for (int trial = 0; trial < 60 && !accepted; ++trial) {
                    // The valence angle is fixed — that is chemistry, not
                    // shape. Only the torsion distinguishes the conformations.
                    double phi = 0.0;
                    switch (params.conformation) {
                    case Conformation::Extended:
                        phi = M_PI; // all-trans
                        break;
                    case Conformation::Helical:
                        phi = torsion;
                        break;
                    case Conformation::RandomWalk:
                    case Conformation::SelfAvoidingWalk:
                        // A real chain samples the rotational isomeric states
                        // (trans and the two gauche minima), not a uniform
                        // torsion — uniform sampling produces eclipsed
                        // conformations no molecule adopts.
                        {
                            const double r = unit(rng);
                            phi = r < 0.5 ? M_PI
                                : (r < 0.75 ? M_PI / 3.0 : -M_PI / 3.0);
                            phi += (unit(rng) - 0.5) * 0.35; // thermal spread
                        }
                        break;
                    }
                    const std::size_t n = backbone.size();
                    next = nerfPlace(backbone[n - 3], backbone[n - 2],
                                     backbone[n - 1], bond, valence, phi);

                    accepted = true;
                    if (params.conformation == Conformation::SelfAvoidingWalk) {
                        // Skip the two nearest bonded neighbours: they are
                        // legitimately within the cutoff by construction.
                        for (std::size_t i = 0; i + 2 < backbone.size(); ++i) {
                            const Vec3 d = backbone[i] - next;
                            if (d.dot(d) < minSq) {
                                accepted = false;
                                ++result.rejectedSteps;
                                break;
                            }
                        }
                    }
                }
                if (!accepted)
                    failed = true; // the walk boxed itself in; restart it
                else
                    backbone.push_back(next);
            }
            if (failed || static_cast<int>(backbone.size()) < backboneLength)
                continue;

            // -- Reject the whole chain if it clashes with an earlier one ----
            bool clash = false;
            for (const Vec3& site : backbone) {
                for (const Vec3& existing : placed) {
                    const Vec3 d = site - existing;
                    if (d.dot(d) < minSq) {
                        clash = true;
                        break;
                    }
                }
                if (clash)
                    break;
            }
            if (!clash)
                placedChain = true;
        }

        if (!placedChain) {
            ++result.failedChains;
            continue;
        }

        // -- Emit the chain ---------------------------------------------------
        const std::size_t chainStart = structure.size();
        for (int index = 0; index < backboneLength; ++index) {
            const BackboneSite& site =
                repeat[static_cast<std::size_t>(index % sitesPerMonomer)];
            Atom backboneAtom;
            backboneAtom.atomicNumber = site.z;
            backboneAtom.position = backbone[static_cast<std::size_t>(index)];
            structure.addAtom(backboneAtom);
            placed.push_back(backboneAtom.position);

            // Local frame: along the chain, and perpendicular to it.
            const Vec3 ahead = index + 1 < backboneLength
                ? (backbone[static_cast<std::size_t>(index + 1)]
                   - backbone[static_cast<std::size_t>(index)])
                      .normalized()
                : (backbone[static_cast<std::size_t>(index)]
                   - backbone[static_cast<std::size_t>(index - 1)])
                      .normalized();
            const Vec3 behind = index > 0
                ? (backbone[static_cast<std::size_t>(index - 1)]
                   - backbone[static_cast<std::size_t>(index)])
                      .normalized()
                : ahead * -1.0;
            // The two substituents sit opposite the two chain bonds, which is
            // what makes the site tetrahedral.
            Vec3 outward = (ahead + behind) * -1.0;
            if (outward.norm() < 1e-6)
                outward = index > 0 ? behind.cross(Vec3{0, 0, 1}) : Vec3{0, 0, 1};
            outward = outward.normalized();
            Vec3 side = ahead.cross(behind);
            if (side.norm() < 1e-6)
                side = outward.cross(ahead);
            side = side.normalized();

            // Tacticity: which side of the backbone plane the stereo group
            // goes on. Atactic randomizes per monomer, syndiotactic alternates,
            // isotactic keeps it constant — this is the ONLY thing that
            // distinguishes the three, and it is per MONOMER, not per site.
            const int monomerIndex = index / sitesPerMonomer;
            double stereoSign = 1.0;
            switch (params.tacticity) {
            case Tacticity::Isotactic:
                stereoSign = 1.0;
                break;
            case Tacticity::Syndiotactic:
                stereoSign = (monomerIndex % 2 == 0) ? 1.0 : -1.0;
                break;
            case Tacticity::Atactic:
                stereoSign = unit(rng) < 0.5 ? 1.0 : -1.0;
                break;
            }

            const int groupCount = static_cast<int>(site.groups.size());
            for (int g = 0; g < groupCount; ++g) {
                const Substituent& group = site.groups[static_cast<std::size_t>(g)];
                // Two groups straddle the backbone plane; a lone group points
                // straight out along the bisector.
                double sign = groupCount > 1 ? (g == 0 ? 1.0 : -1.0) : 0.0;
                if (group.stereo)
                    sign = stereoSign;
                const double half = 0.5 * (180.0 - kTetrahedralDeg) * M_PI / 180.0;
                const Vec3 direction =
                    (outward * std::cos(half) + side * (sign * std::sin(half)))
                        .normalized();

                Atom attached;
                attached.atomicNumber = group.z;
                attached.position = backboneAtom.position + direction * group.bond;
                structure.addAtom(attached);
                placed.push_back(attached.position);

                // Tail atoms continue outward from the attached atom, fanned
                // apart so a methyl does not collapse to a single point.
                Vec3 tailRight, tailUp;
                frameFor(direction, tailRight, tailUp);
                const int tailCount = static_cast<int>(group.tail.size());
                for (int t = 0; t < tailCount; ++t) {
                    const auto& [z, bond] = group.tail[static_cast<std::size_t>(t)];
                    const double phi =
                        2.0 * M_PI * static_cast<double>(t) / std::max(tailCount, 1);
                    const Vec3 bent = direction * std::cos(M_PI - valence)
                        + tailRight * std::sin(M_PI - valence);
                    const Vec3 tailDirection =
                        rotateAbout(bent, direction, phi).normalized();
                    Atom tailAtom;
                    tailAtom.atomicNumber = z;
                    tailAtom.position = attached.position + tailDirection * bond;
                    structure.addAtom(tailAtom);
                    placed.push_back(tailAtom.position);
                }
            }
        }

        // -- End caps ---------------------------------------------------------
        // The two chain ends have a dangling valence; capping them is what
        // turns the fragment into a molecule rather than a radical.
        const auto cap = [&](const Vec3& origin, const Vec3& direction) {
            switch (params.endCap) {
            case EndCap::Hydrogen: {
                Atom atom;
                atom.atomicNumber = 1;
                atom.position = origin + direction * kCHBond;
                structure.addAtom(atom);
                placed.push_back(atom.position);
                break;
            }
            case EndCap::Methyl: {
                Atom carbon;
                carbon.atomicNumber = 6;
                carbon.position = origin + direction * kCCBond;
                structure.addAtom(carbon);
                placed.push_back(carbon.position);
                Vec3 right, up;
                frameFor(direction, right, up);
                for (int t = 0; t < 3; ++t) {
                    const double phi = 2.0 * M_PI * t / 3.0;
                    const Vec3 bent = direction * std::cos(M_PI - valence)
                        + right * std::sin(M_PI - valence);
                    Atom hydrogen;
                    hydrogen.atomicNumber = 1;
                    hydrogen.position = carbon.position
                        + rotateAbout(bent, direction, phi).normalized() * kCHBond;
                    structure.addAtom(hydrogen);
                    placed.push_back(hydrogen.position);
                }
                break;
            }
            case EndCap::Hydroxyl: {
                Atom oxygen;
                oxygen.atomicNumber = 8;
                oxygen.position = origin + direction * kCOBond;
                structure.addAtom(oxygen);
                placed.push_back(oxygen.position);
                Vec3 right, up;
                frameFor(direction, right, up);
                const Vec3 bent = direction * std::cos(M_PI - valence)
                    + right * std::sin(M_PI - valence);
                Atom hydrogen;
                hydrogen.atomicNumber = 1;
                hydrogen.position = oxygen.position + bent.normalized() * kOHBond;
                structure.addAtom(hydrogen);
                placed.push_back(hydrogen.position);
                break;
            }
            }
        };
        const Vec3 headDirection =
            (backbone[0] - backbone[1]).normalized();
        const Vec3 tailDirection =
            (backbone[static_cast<std::size_t>(backboneLength - 1)]
             - backbone[static_cast<std::size_t>(backboneLength - 2)])
                .normalized();
        cap(backbone.front(), headDirection);
        cap(backbone.back(), tailDirection);

        ++result.chains;
        if (result.atomsPerChain == 0)
            result.atomsPerChain =
                static_cast<int>(structure.size() - chainStart);
    }

    // -- Report --------------------------------------------------------------
    double mass = 0.0;
    for (const Atom& atom : structure.atoms())
        mass += Elements::atomicMass(atom.atomicNumber);
    result.densityGCm3 = mass * kUPerA3ToGCm3 / (lx * ly * lz);
    result.structure = std::move(structure);
    result.description = toString(params.monomer) + ": " + std::to_string(result.chains)
        + " chain(s) of " + std::to_string(params.degreeOfPolymerization)
        + " monomers";
    if (hasTacticity(params.monomer))
        result.description += ", " + toString(params.tacticity);
    if (result.failedChains > 0)
        result.description += " (" + std::to_string(result.failedChains)
            + " chain(s) could not be packed without overlap)";
    return result;
}

} // namespace calango::core
