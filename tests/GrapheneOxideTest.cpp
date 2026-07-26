// Graphene oxide builder test.
//
// The constraint that matters here is chemical, not statistical: a carbon has
// ONE out-of-plane valence once it rehybridizes to sp3, so it can host exactly
// one functional group. Two groups on the same carbon is not an improbable
// structure, it is an impossible one — a pentavalent carbon. Everything below
// is checked by re-deriving it from the returned coordinates rather than by
// trusting the builder's own bookkeeping.

#include "core/GrapheneOxideBuilder.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>

using calango::core::Atom;
using calango::core::GrapheneOxideBuilder;
using calango::core::Structure;
using Group = GrapheneOxideBuilder::Group;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition)
        ++failures;
}

double distance(const calango::core::Vec3& a, const calango::core::Vec3& b)
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// Distance under the MINIMUM IMAGE convention in the sheet plane.
///
/// Needed for the bonding checks: an epoxide bridging a bond that crosses the
/// cell boundary sits at that bond's minimum-image midpoint, so in raw
/// coordinates it is far from one of its two carbons while being 1.44 A from
/// that carbon's periodic image. Judging it by raw distance would call a
/// correct structure broken.
double periodicDistance(const Structure& s, const calango::core::Vec3& a,
                        const calango::core::Vec3& b)
{
    const auto& v = s.cell().vectors();
    const double lx = v[0].x;
    const double ly = v[1].y;
    const double shear = v[1].x;
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    const double dz = a.z - b.z;
    const double nj = std::round(dy / ly);
    dy -= nj * ly;
    dx -= nj * shear;
    dx -= std::round(dx / lx) * lx;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// Every basal carbon's count of attached non-carbon neighbours (plus carboxyl
/// carbons, which sit directly above their host). Derived from coordinates, so
/// it cannot inherit a bug from the builder's own accounting.
std::map<int, int> attachmentsPerBasalCarbon(const Structure& s, int carbonCount)
{
    std::map<int, int> counts;
    for (int c = 0; c < carbonCount; ++c)
        counts[c] = 0;
    for (std::size_t i = static_cast<std::size_t>(carbonCount);
         i < s.size(); ++i) {
        const Atom& attached = s.atoms()[i];
        // Hydrogens hang off oxygens, not off the sheet; they would double
        // count. Only the atoms bonded directly to a basal carbon matter.
        if (attached.atomicNumber == 1)
            continue;
        for (int c = 0; c < carbonCount; ++c) {
            // 1.6 Å covers every C-O and C-C attachment used by the builder
            // (longest is the carboxyl C-C at 1.52 Å) without reaching the
            // 2.46 Å next-nearest carbon.
            if (periodicDistance(s, attached.position,
                                 s.atoms()[static_cast<std::size_t>(c)].position)
                < 1.62)
                ++counts[c];
        }
    }
    return counts;
}

} // namespace

int main()
{
    std::printf("Pristine lattices:\n");
    {
        GrapheneOxideBuilder::Config config;
        config.lattice = GrapheneOxideBuilder::Lattice::Primitive;
        config.supercell[0] = config.supercell[1] = 1;
        const Structure s = GrapheneOxideBuilder::pristine(config);
        check(s.size() == 2, "primitive cell has 2 carbons");
        const auto& v = s.cell().vectors();
        check(std::abs(std::sqrt(v[0].x * v[0].x + v[0].y * v[0].y) - 2.46) < 1e-6,
              "primitive |a1| = 2.46 A");
        // The C-C bond is a/sqrt(3) = 1.42 A — the number that makes it
        // graphene rather than a generic honeycomb.
        check(std::abs(distance(s.atoms()[0].position, s.atoms()[1].position)
                       - 1.42) < 0.01,
              "C-C bond length is 1.42 A");
    }
    {
        GrapheneOxideBuilder::Config config;
        config.lattice = GrapheneOxideBuilder::Lattice::Rectangular;
        config.supercell[0] = config.supercell[1] = 1;
        const Structure s = GrapheneOxideBuilder::pristine(config);
        check(s.size() == 4, "rectangular cell has 4 carbons");
        const auto& v = s.cell().vectors();
        check(std::abs(v[1].x) < 1e-9 && std::abs(v[0].y) < 1e-9,
              "rectangular cell axes are orthogonal");
    }
    {
        GrapheneOxideBuilder::Config config;
        config.supercell[0] = 4;
        config.supercell[1] = 3;
        check(GrapheneOxideBuilder::pristine(config).size() == 4u * 4 * 3,
              "supercell scales the carbon count");
    }

    std::printf("Single-group-per-carbon constraint:\n");
    {
        // Deliberately over-subscribed: 25 % of carbons to each of four groups
        // is 100 % of the lattice, so the assignment is under maximum pressure
        // and any collision bug will show.
        GrapheneOxideBuilder::Config config;
        config.supercell[0] = config.supercell[1] = 6;
        config.setCoverage(Group::Epoxide, 0.25);
        config.setCoverage(Group::Hydroxyl, 0.25);
        config.setCoverage(Group::Carboxyl, 0.25);
        config.setCoverage(Group::Carbonyl, 0.25);
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);

        const auto counts = attachmentsPerBasalCarbon(s, report.carbonCount);
        int overloaded = 0;
        int decorated = 0;
        for (const auto& [carbon, n] : counts) {
            if (n > 1)
                ++overloaded;
            if (n == 1)
                ++decorated;
        }
        check(overloaded == 0,
              "no carbon carries more than one functional group");
        check(decorated > 0, "the sheet is actually decorated");
        check(decorated == report.functionalizedCarbons,
              "the report's carbon count matches the geometry");
    }
    {
        // Repeat across seeds: one lucky draw proves nothing about a
        // randomized assignment.
        int worstOverload = 0;
        for (std::uint32_t seed = 0; seed < 12; ++seed) {
            GrapheneOxideBuilder::Config config;
            config.supercell[0] = config.supercell[1] = 5;
            config.seed = seed;
            config.setCoverage(Group::Epoxide, 0.30);
            config.setCoverage(Group::Hydroxyl, 0.30);
            config.setCoverage(Group::Carbonyl, 0.20);
            GrapheneOxideBuilder::Report report;
            const Structure s = GrapheneOxideBuilder::build(config, &report);
            for (const auto& [carbon, n] :
                 attachmentsPerBasalCarbon(s, report.carbonCount))
                worstOverload = std::max(worstOverload, n);
        }
        check(worstOverload <= 1, "the constraint holds across 12 seeds");
    }

    std::printf("Composition:\n");
    {
        GrapheneOxideBuilder::Config config;
        config.supercell[0] = config.supercell[1] = 6;
        config.setCoverage(Group::Epoxide, 0.20);
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);
        // An epoxide consumes TWO carbons, so 20 % coverage means 0.20*N/2
        // groups — the definition that makes the coverages additive.
        const int expected = static_cast<int>(std::llround(0.20 * report.carbonCount / 2.0));
        check(report.placedFor(Group::Epoxide) == expected,
              "epoxide count follows the two-carbons-per-group definition");
        check(report.functionalizedCarbons == 2 * report.placedFor(Group::Epoxide),
              "each epoxide rehybridizes exactly two carbons");
        check(s.size() > static_cast<std::size_t>(report.carbonCount),
              "oxygens were added to the sheet");
    }
    {
        // Determinism: same seed, same structure. A generated structure nobody
        // can regenerate is not a result.
        GrapheneOxideBuilder::Config config;
        config.supercell[0] = config.supercell[1] = 4;
        config.seed = 1234;
        config.setCoverage(Group::Hydroxyl, 0.25);
        const Structure a = GrapheneOxideBuilder::build(config);
        const Structure b = GrapheneOxideBuilder::build(config);
        bool identical = a.size() == b.size();
        for (std::size_t i = 0; identical && i < a.size(); ++i)
            identical = a.atoms()[i].atomicNumber == b.atoms()[i].atomicNumber
                && distance(a.atoms()[i].position, b.atoms()[i].position) < 1e-12;
        check(identical, "the same seed reproduces the structure exactly");

        config.seed = 5678;
        const Structure c = GrapheneOxideBuilder::build(config);
        bool differs = c.size() != a.size();
        for (std::size_t i = 0; !differs && i < a.size(); ++i)
            differs = distance(a.atoms()[i].position, c.atoms()[i].position) > 1e-9;
        check(differs, "a different seed gives a different sample");
    }
    {
        // Over-subscription must be REPORTED, not silently truncated.
        GrapheneOxideBuilder::Config config;
        config.supercell[0] = config.supercell[1] = 3;
        config.setCoverage(Group::Epoxide, 0.9);
        config.setCoverage(Group::Hydroxyl, 0.9);
        GrapheneOxideBuilder::Report report;
        GrapheneOxideBuilder::build(config, &report);
        check(!report.note.empty(),
              "an unsatisfiable request reports the shortfall");
    }

    std::printf(failures == 0 ? "\nAll graphene oxide checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
