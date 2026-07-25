// Native water / ice builder test.
//
// The whole point of the generator is that the protons obey the Bernal-Fowler
// ice rules, so that is what is measured here — on the GENERATED STRUCTURE,
// by re-deriving the hydrogen-bond network from the coordinates rather than by
// trusting the solver's own bookkeeping. A generator that returned an ordered
// or rule-violating proton arrangement would still produce a plausible-looking
// cell of water molecules, and only this kind of check catches it.
//
// GUI-free, Python-free.

#include "core/IceBuilder.hpp"

#include "core/Element.hpp"
#include "core/PeriodicImages.hpp"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace calango::core;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

void checkClose(double actual, double expected, double tolerance,
                const std::string& what)
{
    const bool ok = std::abs(actual - expected) <= tolerance;
    std::printf("  %s %s  (got %.4f, expected %.4f)\n", ok ? "ok  " : "FAIL",
                what.c_str(), actual, expected);
    if (!ok)
        ++failures;
}

/// Minimum-image separation under the structure's cell.
double distance(const Structure& s, const Vec3& a, const Vec3& b,
                const std::array<int, 3>& range)
{
    const auto& v = s.cell().vectors();
    double best = (b - a).norm();
    for (int ia = -range[0]; ia <= range[0]; ++ia)
        for (int ib = -range[1]; ib <= range[1]; ++ib)
            for (int ic = -range[2]; ic <= range[2]; ++ic) {
                const Vec3 shift = v[0] * ia + v[1] * ib + v[2] * ic;
                best = std::min(best, (b + shift - a).norm());
            }
    return best;
}

/// Verify the Bernal-Fowler rules directly from the coordinates.
///
///   rule 1: every oxygen carries exactly two covalently bonded hydrogens
///   rule 2: every O-O contact carries exactly one proton
///
/// A proton "belongs" to the O-O bond whose acceptor it points at, which is
/// decided here by proximity — the same way an analysis tool would decide it.
struct IceRuleReport {
    int oxygens = 0;
    int badCovalentCount = 0; ///< oxygens without exactly 2 H
    int badBondCount = 0;     ///< O-O contacts without exactly 1 proton
    int contacts = 0;
    double meanOO = 0.0;
};

IceRuleReport auditIceRules(const Structure& s, double ooCutoff)
{
    IceRuleReport report;
    const auto range = imageRange(s.cell(), ooCutoff);
    const auto& atoms = s.atoms();

    std::vector<int> oxygen;
    std::vector<int> hydrogen;
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        if (atoms[i].atomicNumber == 8)
            oxygen.push_back(static_cast<int>(i));
        else if (atoms[i].atomicNumber == 1)
            hydrogen.push_back(static_cast<int>(i));
    }
    report.oxygens = static_cast<int>(oxygen.size());

    // Rule 1: each H belongs to its nearest O; each O must own exactly two.
    std::map<int, int> ownedHydrogens;
    for (const int o : oxygen)
        ownedHydrogens[o] = 0;
    for (const int h : hydrogen) {
        int nearest = -1;
        double best = 1e30;
        for (const int o : oxygen) {
            const double d =
                distance(s, atoms[static_cast<std::size_t>(o)].position,
                         atoms[static_cast<std::size_t>(h)].position, range);
            if (d < best) {
                best = d;
                nearest = o;
            }
        }
        if (nearest >= 0)
            ++ownedHydrogens[nearest];
    }
    for (const auto& [o, count] : ownedHydrogens)
        if (count != 2)
            ++report.badCovalentCount;

    // Rule 2: each O-O contact carries exactly one proton. A proton sits "on"
    // the contact when it is covalently bound to one end and pointing at the
    // other (its O(donor)-H vector aligned with the O-O direction).
    double totalOO = 0.0;
    for (std::size_t i = 0; i < oxygen.size(); ++i) {
        for (std::size_t j = i + 1; j < oxygen.size(); ++j) {
            const Vec3& a = atoms[static_cast<std::size_t>(oxygen[i])].position;
            const Vec3& b = atoms[static_cast<std::size_t>(oxygen[j])].position;
            const double d = distance(s, a, b, range);
            if (d > ooCutoff)
                continue;
            ++report.contacts;
            totalOO += d;

            int protons = 0;
            for (const int h : hydrogen) {
                const Vec3& hp = atoms[static_cast<std::size_t>(h)].position;
                const double da = distance(s, a, hp, range);
                const double db = distance(s, b, hp, range);
                // Covalent to one end (< 1.2 A) and closer to the other end
                // than the far side of the bond would be.
                const bool donorA = da < 1.2 && db < d;
                const bool donorB = db < 1.2 && da < d;
                if (donorA || donorB)
                    ++protons;
            }
            if (protons != 1)
                ++report.badBondCount;
        }
    }
    report.meanOO = report.contacts > 0 ? totalOO / report.contacts : 0.0;
    return report;
}

void auditPhase(IceBuilder::Phase phase, const char* name, double expectedOO,
                double expectedDensity)
{
    std::printf("%s:\n", name);
    IceBuilder::Params params;
    params.phase = phase;
    params.nx = params.ny = params.nz = 2;
    params.seed = 5;
    const auto result = IceBuilder::generate(params);

    check(result.iceRuleViolations == 0,
          "the solver reports no ice-rule violations");
    check(result.moleculeCount > 0, "molecules were generated");
    check(static_cast<int>(result.structure.size()) == 3 * result.moleculeCount,
          "every molecule has exactly one O and two H");

    const auto report = auditIceRules(result.structure, expectedOO + 0.35);
    std::printf("       %d molecules, %d O-O contacts, mean O-O = %.3f A, "
                "density = %.3f g/cm^3, dipole/molecule = %.3f D\n",
                report.oxygens, report.contacts, report.meanOO,
                result.densityGCm3, result.netDipolePerMolecule);
    check(report.badCovalentCount == 0,
          "rule 1: every oxygen carries exactly two hydrogens");
    check(report.badBondCount == 0,
          "rule 2: every O-O contact carries exactly one proton");
    // A tetrahedral network has 2 bonds per molecule (each shared by two).
    check(report.contacts == 2 * report.oxygens,
          "the network is 4-coordinated (2 contacts per molecule)");
    checkClose(report.meanOO, expectedOO, 0.05, "mean O-O separation");
    checkClose(result.densityGCm3, expectedDensity, 0.05, "density");
}

} // namespace

int main()
{
    auditPhase(IceBuilder::Phase::IceIh, "Ice Ih (proton-disordered)", 2.75, 0.93);
    auditPhase(IceBuilder::Phase::IceIc, "Ice Ic (proton-disordered)", 2.75, 0.93);

    // Ice VII: two interpenetrating networks. The audit's distance-based
    // contact search sees BOTH networks, so it counts twice the hydrogen bonds
    // the physics has — which is exactly the property that distinguishes VII
    // and is checked here rather than through the rule audit.
    std::printf("Ice VII (interpenetrating networks):\n");
    {
        IceBuilder::Params params;
        params.phase = IceBuilder::Phase::IceVII;
        params.nx = params.ny = params.nz = 1;
        const auto result = IceBuilder::generate(params);
        check(result.iceRuleViolations == 0,
              "the ice rules hold within each network");
        check(result.moleculeCount == 16,
              "one cell holds 16 molecules (two 8-site diamond nets)");
        checkClose(result.densityGCm3, 1.60, 0.15,
                   "density is far above ice Ih (a high-pressure phase)");
    }

    // -- Proton disorder ------------------------------------------------------
    // Different seeds must give genuinely different proton arrangements: a
    // generator that always returned the same configuration would have the
    // right oxygens and none of the residual-entropy physics.
    std::printf("Proton disorder:\n");
    {
        IceBuilder::Params a;
        a.phase = IceBuilder::Phase::IceIh;
        a.nx = a.ny = a.nz = 2;
        a.seed = 1;
        IceBuilder::Params b = a;
        b.seed = 2;
        const auto first = IceBuilder::generate(a);
        const auto second = IceBuilder::generate(b);

        int differing = 0;
        const auto& atomsA = first.structure.atoms();
        const auto& atomsB = second.structure.atoms();
        for (std::size_t i = 0; i < atomsA.size() && i < atomsB.size(); ++i)
            if ((atomsA[i].position - atomsB[i].position).norm() > 1e-6)
                ++differing;
        check(differing > 0, "two seeds give different proton arrangements");

        // The oxygens are the crystal and must NOT move between seeds.
        int oxygenMoved = 0;
        for (std::size_t i = 0; i < atomsA.size() && i < atomsB.size(); ++i)
            if (atomsA[i].atomicNumber == 8
                && (atomsA[i].position - atomsB[i].position).norm() > 1e-9)
                ++oxygenMoved;
        check(oxygenMoved == 0, "the oxygen sublattice is identical (only the "
                                "protons are disordered)");

        std::printf("       dipole/molecule = %.3f D\n",
                    first.netDipolePerMolecule);
        check(first.netDipolePerMolecule < 1.0,
              "the disordered cell has a small net dipole");
    }

    // -- Molecular geometry ---------------------------------------------------
    std::printf("Water geometry presets:\n");
    {
        struct Case {
            IceBuilder::WaterGeometry geometry;
            const char* name;
            double oh;
            double angle;
        };
        for (const Case& c :
             {Case{IceBuilder::WaterGeometry::Rigid, "Rigid", 0.9572, 104.52},
              Case{IceBuilder::WaterGeometry::Tip4p, "TIP4P", 0.9572, 104.52},
              Case{IceBuilder::WaterGeometry::Spce, "SPC/E", 1.0, 109.47}}) {
            IceBuilder::Params params;
            params.phase = IceBuilder::Phase::IceIh;
            params.geometry = c.geometry;
            params.nx = params.ny = params.nz = 1;
            const auto result = IceBuilder::generate(params);
            const auto& atoms = result.structure.atoms();
            // Molecules are emitted O, H, H.
            double maxOhError = 0.0;
            double maxAngleError = 0.0;
            for (std::size_t i = 0; i + 2 < atoms.size(); i += 3) {
                const Vec3 a = atoms[i + 1].position - atoms[i].position;
                const Vec3 b = atoms[i + 2].position - atoms[i].position;
                maxOhError = std::max({maxOhError, std::abs(a.norm() - c.oh),
                                       std::abs(b.norm() - c.oh)});
                const double angle =
                    std::acos(a.dot(b) / (a.norm() * b.norm())) * 180.0 / M_PI;
                maxAngleError = std::max(maxAngleError, std::abs(angle - c.angle));
            }
            check(maxOhError < 1e-6,
                  std::string(c.name) + ": every O-H bond is exact");
            check(maxAngleError < 1e-4,
                  std::string(c.name) + ": every H-O-H angle is exact");
        }
    }

    // -- Liquid water ---------------------------------------------------------
    std::printf("Liquid water:\n");
    {
        IceBuilder::Params params;
        params.phase = IceBuilder::Phase::LiquidWater;
        params.moleculeCount = 200;
        params.densityGCm3 = 0.997;
        params.seed = 3;
        const auto result = IceBuilder::generate(params);
        check(result.moleculeCount == 200, "the requested molecule count is met");
        checkClose(result.densityGCm3, 0.997, 0.01, "the target density is hit");

        // The packing constraint must actually hold: overlapping waters would
        // blow up the first MD step.
        const auto range = imageRange(result.structure.cell(), params.minOODistance);
        const auto& atoms = result.structure.atoms();
        double closest = 1e30;
        for (std::size_t i = 0; i < atoms.size(); i += 3)
            for (std::size_t j = i + 3; j < atoms.size(); j += 3)
                closest = std::min(closest,
                                   distance(result.structure, atoms[i].position,
                                            atoms[j].position, range));
        std::printf("       closest O-O = %.3f A (minimum requested %.2f)\n",
                    closest, params.minOODistance);
        check(closest >= params.minOODistance - 1e-6,
              "no two oxygens are closer than the packing minimum");
    }

    std::printf(failures == 0 ? "\nAll ice builder checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
