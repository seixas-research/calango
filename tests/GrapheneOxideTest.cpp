// Graphene oxide builder test.
//
// Three constraints here are chemical rather than statistical, and each is
// checked by re-deriving it from the returned coordinates rather than by
// trusting the builder's own bookkeeping:
//
//   1. A carbon has ONE out-of-plane valence once it rehybridizes to sp3, and
//      an edge carbon has ONE substitutable hydrogen, so either can host
//      exactly one functional group. Two groups on one carbon is not an
//      improbable structure, it is an impossible one — a pentavalent carbon.
//
//   2. Basal chemistry (epoxide, sp3 hydroxyl) belongs on three-coordinate
//      interior carbons; edge chemistry (carboxyl, carbonyl, phenolic
//      hydroxyl) belongs on under-coordinated rim carbons. The classification
//      is recomputed here from coordination numbers, independently of the
//      builder.
//
//   3. The nanoflake family is exact: index m gives C(6m²)H(6m). A
//      flake that comes out with a different formula is not a member of the
//      family whatever it is called.

#include "core/GrapheneOxideBuilder.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <vector>

using calango::core::Atom;
using calango::core::GrapheneOxideBuilder;
using calango::core::Structure;
using Base = GrapheneOxideBuilder::Base;
using Dosing = GrapheneOxideBuilder::Dosing;
using Group = GrapheneOxideBuilder::Group;
using Region = GrapheneOxideBuilder::Region;

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
/// correct structure broken. Harmless for a flake, whose box is far larger than
/// the molecule.
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

/// Every framework carbon's count of attached non-carbon neighbours (plus
/// carboxyl carbons, which sit one bond out from their host). Derived from
/// coordinates, so it cannot inherit a bug from the builder's own accounting.
std::map<int, int> attachmentsPerCarbon(const Structure& s, int carbonCount)
{
    std::map<int, int> counts;
    for (int c = 0; c < carbonCount; ++c)
        counts[c] = 0;
    for (std::size_t i = static_cast<std::size_t>(carbonCount);
         i < s.size(); ++i) {
        const Atom& attached = s.atoms()[i];
        // Hydrogens hang off oxygens, or cap an unreacted edge carbon; neither
        // is a functional group. Only the atoms bonded directly to a framework
        // carbon matter.
        if (attached.atomicNumber == 1)
            continue;
        for (int c = 0; c < carbonCount; ++c) {
            // 1.62 Å covers every C-O and C-C attachment used by the builder
            // (longest is the basal hydroxyl C-O at 1.48 Å) without reaching
            // the 2.46 Å next-nearest carbon.
            if (periodicDistance(s, attached.position,
                                 s.atoms()[static_cast<std::size_t>(c)].position)
                < 1.62)
                ++counts[c];
        }
    }
    return counts;
}

/// Coordination number of each framework carbon, counting only other FRAMEWORK
/// carbons. This is the definition the whole basal/edge split rests on, so the
/// test recomputes it instead of asking the builder.
std::vector<int> carbonCoordination(const Structure& s, int carbonCount)
{
    std::vector<int> coordination(static_cast<std::size_t>(carbonCount), 0);
    for (int i = 0; i < carbonCount; ++i) {
        for (int j = 0; j < carbonCount; ++j) {
            if (i == j)
                continue;
            if (periodicDistance(s, s.atoms()[static_cast<std::size_t>(i)].position,
                                 s.atoms()[static_cast<std::size_t>(j)].position)
                < 1.75)
                ++coordination[static_cast<std::size_t>(i)];
        }
    }
    return coordination;
}

/// Whether any two atoms are closer than the shortest real bond of their kind —
/// that is, whether anything came out FUSED.
///
/// The builder emits unrelaxed geometry, so strained contacts are expected and
/// fine: an epoxide and a hydroxyl on neighbouring carbons start at an O···O of
/// 1.9 Å and relax apart. Atoms closer than a covalent bond are a different
/// thing entirely, and no optimizer recovers from them. The shortest bonds the
/// builder ever writes are the 0.98 Å O–H and the 1.21 Å carboxyl C=O, so
/// anything under those is not a bond and not a contact — it is two atoms in
/// the same place. This is the failure mode that counting atoms and groups
/// cannot see.
bool anythingFused(const Structure& s, double& closest)
{
    closest = 1e9;
    bool fused = false;
    const auto& v = s.cell().vectors();
    const bool periodic = s.cell().pbc()[0];
    for (std::size_t i = 0; i < s.size(); ++i) {
        for (std::size_t j = i + 1; j < s.size(); ++j) {
            const auto& a = s.atoms()[i].position;
            const auto& b = s.atoms()[j].position;
            double dx = a.x - b.x;
            double dy = a.y - b.y;
            const double dz = a.z - b.z;
            if (periodic) {
                const double nj = std::round(dy / v[1].y);
                dy -= nj * v[1].y;
                dx -= nj * v[1].x;
                dx -= std::round(dx / v[0].x) * v[0].x;
            }
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            closest = std::min(closest, d);
            const bool anyHydrogen = s.atoms()[i].atomicNumber == 1
                || s.atoms()[j].atomicNumber == 1;
            if (d < (anyHydrogen ? 0.95 : 1.15))
                fused = true;
        }
    }
    return fused;
}

GrapheneOxideBuilder::Config flakeConfig(int generation)
{
    GrapheneOxideBuilder::Config config;
    config.base = Base::Nanoflake;
    config.flakeIndex = generation;
    return config;
}

std::map<int, int> elementCounts(const Structure& s)
{
    std::map<int, int> counts;
    for (const Atom& atom : s.atoms())
        ++counts[atom.atomicNumber];
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

    std::printf("Nanoflake family, C(6m^2)H(6m):\n");
    {
        // The formula IS the family. Benzene, coronene, circumcoronene and the
        // two beyond it are the reference points anyone will recognise, and a
        // flake with the wrong atom count is not a member of the series
        // whatever the dialog labels it.
        bool allExact = true;
        bool allBondsGood = true;
        bool coordinationSane = true;
        for (int m = 1; m <= 5; ++m) {
            const Structure s = GrapheneOxideBuilder::pristine(flakeConfig(m));
            const auto counts = elementCounts(s);
            allExact = allExact && counts.at(6) == 6 * m * m
                && counts.at(1) == 6 * m;

            GrapheneOxideBuilder::Report report;
            GrapheneOxideBuilder::build(flakeConfig(m), &report);
            const auto coordination = carbonCoordination(s, report.carbonCount);
            int edges = 0;
            for (std::size_t i = 0; i < coordination.size(); ++i) {
                if (coordination[i] < 2 || coordination[i] > 3)
                    coordinationSane = false;
                if (coordination[i] < 3)
                    ++edges;
            }
            // 6m edge carbons, 6m(m-1) basal — the split the chemistry uses.
            coordinationSane = coordinationSane && edges == 6 * m
                && report.edgeCarbonCount == 6 * m
                && report.basalCarbonCount == 6 * m * (m - 1);

            for (int i = 0; i < report.carbonCount; ++i) {
                for (int j = i + 1; j < report.carbonCount; ++j) {
                    const double d = distance(s.atoms()[i].position,
                                              s.atoms()[j].position);
                    if (d < 1.75 && std::abs(d - 1.42) > 0.01)
                        allBondsGood = false;
                }
            }
        }
        check(allExact, "m = 1..5 give exactly C6H6, C24H12, C54H18, C96H24, C150H30");
        check(allBondsGood, "every C-C bond in the flake is 1.42 A");
        check(coordinationSane,
              "6m edge carbons and 6m(m-1) basal carbons, none under-bonded");
    }
    {
        const Structure s = GrapheneOxideBuilder::pristine(flakeConfig(3));
        check(!s.cell().pbc()[0] && !s.cell().pbc()[1] && !s.cell().pbc()[2],
              "a flake is aperiodic in all three directions");
        // The box exists because plane-wave codes demand one; what it must not
        // have is a neighbour.
        const auto& v = s.cell().vectors();
        double lo = 1e9;
        double hi = -1e9;
        for (const Atom& atom : s.atoms()) {
            lo = std::min(lo, atom.position.x);
            hi = std::max(hi, atom.position.x);
        }
        check(lo > 9.9 && v[0].x - hi > 9.9,
              "the flake box carries 10 A of vacuum on every side");
    }
    {
        GrapheneOxideBuilder::Config config = flakeConfig(3);
        config.hydrogenTerminateEdges = false;
        const auto counts = elementCounts(GrapheneOxideBuilder::pristine(config));
        check(counts.count(1) == 0 && counts.at(6) == 54,
              "termination off leaves the bare C54 radical");
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

        const auto counts = attachmentsPerCarbon(s, report.carbonCount);
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
                 attachmentsPerCarbon(s, report.carbonCount))
                worstOverload = std::max(worstOverload, n);
        }
        check(worstOverload <= 1, "the constraint holds across 12 seeds");
    }
    {
        // The same pressure on a flake, where BOTH pools are being consumed at
        // once and an edge carbon can be claimed either by a group or by a
        // terminating hydrogen.
        int worstOverload = 0;
        bool terminationConsistent = true;
        for (std::uint32_t seed = 0; seed < 8; ++seed) {
            GrapheneOxideBuilder::Config config = flakeConfig(4);
            config.seed = seed;
            config.setCoverage(Group::Epoxide, 0.4);
            config.setCoverage(Group::Hydroxyl, 0.4);
            config.setCoverage(Group::Carboxyl, 0.5);
            config.setCoverage(Group::Carbonyl, 0.5);
            config.setCoverage(Group::EdgeHydroxyl, 0.5);
            GrapheneOxideBuilder::Report report;
            const Structure s = GrapheneOxideBuilder::build(config, &report);
            for (const auto& [carbon, n] :
                 attachmentsPerCarbon(s, report.carbonCount))
                worstOverload = std::max(worstOverload, n);

            // An edge carbon that reacted has had its hydrogen SUBSTITUTED, not
            // added to — the terminating count is what is left over.
            const int reacted = report.placedFor(Group::Carboxyl)
                + report.placedFor(Group::Carbonyl)
                + report.placedFor(Group::EdgeHydroxyl);
            terminationConsistent = terminationConsistent
                && report.hydrogenTerminatedEdges
                    == report.edgeCarbonCount - reacted;
        }
        check(worstOverload <= 1, "the constraint holds on a saturated flake");
        check(terminationConsistent,
              "a functionalized edge carbon loses its terminating hydrogen");
    }

    std::printf("No two groups occupy the same space:\n");
    {
        // Two groups on neighbouring sites can point their substituents at each
        // other. Before the steric check existed, that put oxygens 0.14 Å
        // apart — a structure that passes every count-based test and is
        // physically meaningless.
        bool fused = false;
        double closest = 1e9;
        for (std::uint32_t seed = 0; seed < 6; ++seed) {
            std::vector<GrapheneOxideBuilder::Config> cases;

            GrapheneOxideBuilder::Config sheet;
            sheet.supercell[0] = sheet.supercell[1] = 6;
            sheet.setCoverage(Group::Epoxide, 0.35);
            sheet.setCoverage(Group::Hydroxyl, 0.35);
            cases.push_back(sheet);

            GrapheneOxideBuilder::Config flake = flakeConfig(4);
            flake.setCoverage(Group::Epoxide, 0.35);
            flake.setCoverage(Group::Hydroxyl, 0.35);
            flake.setCoverage(Group::Carboxyl, 0.4);
            flake.setCoverage(Group::Carbonyl, 0.3);
            flake.setCoverage(Group::EdgeHydroxyl, 0.3);
            cases.push_back(flake);

            GrapheneOxideBuilder::Config heavy = flakeConfig(4);
            heavy.dosing = Dosing::TargetRatio;
            heavy.targetCarbonToOxygen = 2.0;
            cases.push_back(heavy);

            for (GrapheneOxideBuilder::Config config : cases) {
                config.seed = seed;
                double here = 0.0;
                fused = anythingFused(GrapheneOxideBuilder::build(config), here)
                    || fused;
                closest = std::min(closest, here);
            }
        }
        check(!fused,
              "nothing is fused, across sheets, flakes and 6 seeds");
        // The shortest thing in the structure should be the O-H bond itself.
        // Anything shorter is two atoms sharing a position.
        check(closest > 0.95 && closest < 1.0,
              "the closest approach anywhere is the 0.98 A hydroxyl O-H");
    }
    {
        // The steric refusal must cost capacity only where it has to. If it
        // were over-eager, ordinary coverages would silently come up short.
        GrapheneOxideBuilder::Config config;
        config.supercell[0] = config.supercell[1] = 8;
        config.setCoverage(Group::Epoxide, 0.4);
        config.setCoverage(Group::Hydroxyl, 0.4);
        GrapheneOxideBuilder::Report report;
        GrapheneOxideBuilder::build(config, &report);
        check(report.placedFor(Group::Epoxide) == report.requested[0]
                  && report.placedFor(Group::Hydroxyl) == report.requested[1],
              "80 % total basal coverage is still met in full");
    }

    std::printf("Basal and edge chemistry stay in their regions:\n");
    {
        GrapheneOxideBuilder::Config config = flakeConfig(4);
        config.setCoverage(Group::Epoxide, 0.35);
        config.setCoverage(Group::Hydroxyl, 0.35);
        config.setCoverage(Group::Carboxyl, 0.4);
        config.setCoverage(Group::Carbonyl, 0.3);
        config.setCoverage(Group::EdgeHydroxyl, 0.3);
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);

        const auto coordination = carbonCoordination(s, report.carbonCount);
        const auto counts = attachmentsPerCarbon(s, report.carbonCount);

        // Recount the two regions from geometry and compare against what the
        // builder claims it placed. An epoxide occupies two basal carbons and
        // every other group one.
        int decoratedBasal = 0;
        int decoratedEdge = 0;
        for (const auto& [carbon, n] : counts) {
            if (n == 0)
                continue;
            (coordination[static_cast<std::size_t>(carbon)] >= 3 ? decoratedBasal
                                                                 : decoratedEdge)++;
        }
        const int basalGroups = 2 * report.placedFor(Group::Epoxide)
            + report.placedFor(Group::Hydroxyl);
        const int edgeGroups = report.placedFor(Group::Carboxyl)
            + report.placedFor(Group::Carbonyl)
            + report.placedFor(Group::EdgeHydroxyl);
        check(decoratedBasal == basalGroups && decoratedEdge == edgeGroups,
              "every group landed on a carbon of its own region");
        check(edgeGroups > 0 && basalGroups > 0,
              "both regions were actually used");

        // The scalar field the viewport colours by must agree with the
        // classification the chemistry used.
        const auto& fields = s.scalarFields();
        bool fieldMatches = fields.count("edge") == 1;
        if (fieldMatches) {
            const auto& edgeField = fields.at("edge");
            fieldMatches = edgeField.size() == s.size();
            for (int c = 0; fieldMatches && c < report.carbonCount; ++c)
                fieldMatches =
                    (edgeField[static_cast<std::size_t>(c)] > 0.5)
                    == (coordination[static_cast<std::size_t>(c)] < 3);
        }
        check(fieldMatches, "the \"edge\" scalar field matches the geometry");
    }
    {
        // Basal-only request on a flake: the rim must stay hydrogen-terminated.
        GrapheneOxideBuilder::Config config = flakeConfig(3);
        config.setCoverage(Group::Epoxide, 0.3);
        config.setCoverage(Group::Hydroxyl, 0.3);
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);
        const auto coordination = carbonCoordination(s, report.carbonCount);
        bool edgeClean = true;
        for (const auto& [carbon, n] : attachmentsPerCarbon(s, report.carbonCount))
            if (coordination[static_cast<std::size_t>(carbon)] < 3 && n > 0)
                edgeClean = false;
        check(edgeClean, "no basal group strayed onto an edge carbon");
        check(report.hydrogenTerminatedEdges == report.edgeCarbonCount,
              "the whole rim stayed hydrogen-terminated");
    }
    {
        // Edge chemistry on a periodic sheet has no site pool at all. This is
        // the case that must NOT silently redirect onto the basal plane — the
        // modeling compromise the old builder made.
        GrapheneOxideBuilder::Config config;
        config.supercell[0] = config.supercell[1] = 5;
        config.setCoverage(Group::Carboxyl, 0.2);
        config.setCoverage(Group::Carbonyl, 0.2);
        config.setCoverage(Group::EdgeHydroxyl, 0.2);
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);
        check(report.edgeCarbonCount == 0,
              "a periodic sheet reports no edge carbons");
        check(report.placedFor(Group::Carboxyl) == 0
                  && report.placedFor(Group::Carbonyl) == 0
                  && report.placedFor(Group::EdgeHydroxyl) == 0,
              "no edge group is placed on an edgeless sheet");
        check(!report.note.empty(),
              "and the reason is reported rather than left as a silent no-op");
        check(s.size() == static_cast<std::size_t>(report.carbonCount),
              "the sheet came back pristine");
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
        const int expected =
            static_cast<int>(std::llround(0.20 * report.carbonCount / 2.0));
        check(report.placedFor(Group::Epoxide) == expected,
              "epoxide count follows the two-carbons-per-group definition");
        check(report.functionalizedCarbons == 2 * report.placedFor(Group::Epoxide),
              "each epoxide rehybridizes exactly two carbons");
        check(s.size() > static_cast<std::size_t>(report.carbonCount),
              "oxygens were added to the sheet");
    }
    {
        // Edge coverages are fractions of the EDGE carbons, not of the whole
        // flake — 50 % carboxyl means half the rim.
        GrapheneOxideBuilder::Config config = flakeConfig(4);
        config.setCoverage(Group::Carboxyl, 0.5);
        GrapheneOxideBuilder::Report report;
        GrapheneOxideBuilder::build(config, &report);
        check(report.placedFor(Group::Carboxyl) == report.edgeCarbonCount / 2,
              "edge coverage is measured against the edge carbons");
    }
    {
        // The atom counts have to add up, or the reported C/O is fiction.
        GrapheneOxideBuilder::Config config = flakeConfig(4);
        config.setCoverage(Group::Epoxide, 0.2);
        config.setCoverage(Group::Hydroxyl, 0.2);
        config.setCoverage(Group::Carboxyl, 0.3);
        config.setCoverage(Group::Carbonyl, 0.2);
        config.setCoverage(Group::EdgeHydroxyl, 0.2);
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);
        const auto counts = elementCounts(s);

        const int expectedO = report.placedFor(Group::Epoxide)
            + report.placedFor(Group::Hydroxyl)
            + 2 * report.placedFor(Group::Carboxyl)
            + report.placedFor(Group::Carbonyl)
            + report.placedFor(Group::EdgeHydroxyl);
        const int expectedH = report.hydrogenTerminatedEdges
            + report.placedFor(Group::Hydroxyl)
            + report.placedFor(Group::Carboxyl)
            + report.placedFor(Group::EdgeHydroxyl);
        check(counts.at(8) == expectedO && report.oxygenAtoms == expectedO,
              "the oxygen count matches the groups placed");
        check(counts.at(1) == expectedH && report.hydrogenAtoms == expectedH,
              "the hydrogen count matches the groups placed");
        // A carboxyl brings a carbon of its own, and that carbon counts in the
        // C/O an experiment would measure.
        check(counts.at(6) == report.carbonCount + report.placedFor(Group::Carboxyl)
                  && report.totalCarbonAtoms == counts.at(6),
              "carboxyl carbons are counted in the total carbon");
        check(std::abs(report.carbonToOxygenRatio()
                       - static_cast<double>(counts.at(6)) / counts.at(8))
                  < 1e-9,
              "C/O is total carbon over total oxygen");
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

    std::printf("Functional groups found again from connectivity:\n");
    {
        // The builder knows what it placed. findFunctionalGroups() has to reach
        // the same answer WITHOUT that record, from bonding alone, because that
        // is the only information a structure loaded from a file carries.
        GrapheneOxideBuilder::Config config = flakeConfig(4);
        config.setCoverage(Group::Epoxide, 0.15);
        config.setCoverage(Group::Hydroxyl, 0.15);
        config.setCoverage(Group::Carboxyl, 0.3);
        config.setCoverage(Group::Carbonyl, 0.25);
        config.setCoverage(Group::EdgeHydroxyl, 0.25);
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);

        const auto clusters = GrapheneOxideBuilder::findFunctionalGroups(s);
        std::map<int, int> counts;
        std::map<int, std::size_t> sizes;
        bool sizesConsistent = true;
        for (const auto& cluster : clusters) {
            const int kind = static_cast<int>(cluster.kind);
            ++counts[kind];
            if (sizes.count(kind) && sizes[kind] != cluster.atoms.size())
                sizesConsistent = false;
            sizes[kind] = cluster.atoms.size();
        }

        bool everyKindMatches = true;
        for (std::size_t g = 0; g < GrapheneOxideBuilder::kGroupCount; ++g) {
            const auto group = static_cast<Group>(g);
            everyKindMatches = everyKindMatches
                && counts[static_cast<int>(g)] == report.placedFor(group);
        }
        check(everyKindMatches,
              "every group the builder placed is found again, and no others");
        check(sizesConsistent, "each kind of group is always the same size");
        // Epoxide O + 2 C; hydroxyl O + H + C; carboxyl C + 2 O + H + ring C;
        // carbonyl O + C. The host carbon is in the cluster — that is the
        // point of it.
        check(sizes[static_cast<int>(Group::Epoxide)] == 3
                  && sizes[static_cast<int>(Group::Hydroxyl)] == 3
                  && sizes[static_cast<int>(Group::Carboxyl)] == 5
                  && sizes[static_cast<int>(Group::Carbonyl)] == 2
                  && sizes[static_cast<int>(Group::EdgeHydroxyl)] == 3,
              "each cluster carries its group AND its host carbon(s)");

        // A carboxyl contains a C=O and a C-OH. If it were not matched first,
        // every one of them would be reported as a carbonyl plus a hydroxyl.
        check(counts[static_cast<int>(Group::Carboxyl)] > 0
                  && counts[static_cast<int>(Group::Carbonyl)]
                      == report.placedFor(Group::Carbonyl),
              "carboxyls are not torn into a carbonyl and a hydroxyl");

        const auto labels = GrapheneOxideBuilder::functionalGroupLabels(s);
        check(labels.size() == s.size(), "the label array covers every atom");
        int labelledCarbons = 0;
        for (int c = 0; c < report.carbonCount; ++c)
            if (labels[static_cast<std::size_t>(c)] >= 0)
                ++labelledCarbons;
        check(labelledCarbons == report.functionalizedCarbons,
              "exactly the functionalized framework carbons are labelled");

        // No atom may end up in two clusters: they become render casts, and an
        // atom belongs to one cast.
        std::map<int, int> membership;
        for (const auto& cluster : clusters)
            for (const int atom : cluster.atoms)
                ++membership[atom];
        bool unique = true;
        for (const auto& [atom, times] : membership)
            unique = unique && times == 1;
        check(unique, "no atom belongs to two groups at once");
    }
    {
        // The same on a PERIODIC sheet, where an epoxide can bridge the cell
        // boundary and is only found if the bonding respects minimum image.
        GrapheneOxideBuilder::Config config;
        config.supercell[0] = config.supercell[1] = 5;
        config.setCoverage(Group::Epoxide, 0.2);
        config.setCoverage(Group::Hydroxyl, 0.2);
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);
        std::map<int, int> counts;
        for (const auto& cluster : GrapheneOxideBuilder::findFunctionalGroups(s))
            ++counts[static_cast<int>(cluster.kind)];
        check(counts[static_cast<int>(Group::Epoxide)]
                      == report.placedFor(Group::Epoxide)
                  && counts[static_cast<int>(Group::Hydroxyl)]
                      == report.placedFor(Group::Hydroxyl),
              "groups bridging the periodic boundary are found too");
    }

    std::printf("Target C/O ratio:\n");
    {
        // The ratio the builder converges on is the one an experiment would
        // measure — every carbon over every oxygen — and it has to land close
        // to the request across the whole useful range.
        bool allClose = true;
        int previousOxygen = 1 << 30;
        bool monotone = true;
        for (double target : {2.0, 3.0, 4.0, 6.0, 10.0}) {
            GrapheneOxideBuilder::Config config = flakeConfig(5);
            config.dosing = Dosing::TargetRatio;
            config.targetCarbonToOxygen = target;
            GrapheneOxideBuilder::Report report;
            GrapheneOxideBuilder::build(config, &report);
            const double achieved = report.carbonToOxygenRatio();
            if (std::abs(achieved - target) > 0.06 * target)
                allClose = false;
            if (!report.targetReached)
                allClose = false;
            // More oxidation means strictly more oxygen. If this inverts, the
            // knob is not doing what its label says.
            monotone = monotone && report.oxygenAtoms < previousOxygen;
            previousOxygen = report.oxygenAtoms;
        }
        check(allClose, "C/O lands within 6 % of the target from 2 to 10");
        check(monotone, "a lower target C/O always yields more oxygen");
    }
    {
        // A carboxyl moves BOTH sides of the ratio: it adds two oxygens and a
        // carbon. Driving the composition with carboxyls only is the case a
        // closed-form "oxygens = carbons / target" would get wrong.
        GrapheneOxideBuilder::Config config = flakeConfig(5);
        config.dosing = Dosing::TargetRatio;
        config.targetCarbonToOxygen = 12.0;
        config.basalOxygenShare = 0.0; // categorical: edge chemistry only
        for (Group group : {Group::Epoxide, Group::Hydroxyl, Group::Carbonyl,
                            Group::EdgeHydroxyl})
            config.setWeight(group, 0.0);
        GrapheneOxideBuilder::Report report;
        GrapheneOxideBuilder::build(config, &report);
        check(report.placedFor(Group::Carboxyl) > 0
                  && report.oxygenAtoms == 2 * report.placedFor(Group::Carboxyl),
              "a carboxyl-only run places two oxygens per group");
        // With only carboxyls available the composition is quantized in coarse
        // steps — n groups give (N + n) / 2n — so "close to 12" is not the
        // right claim. The right claim is that it stopped on the CLOSEST
        // reachable value, neither overshooting nor stopping one short.
        const int n = report.placedFor(Group::Carboxyl);
        const auto reachable = [&](int groups) {
            return groups <= 0
                ? std::numeric_limits<double>::infinity()
                : (report.carbonCount + groups) / (2.0 * groups);
        };
        const double missed = std::abs(report.carbonToOxygenRatio() - 12.0);
        check(missed <= std::abs(reachable(n - 1) - 12.0)
                  && missed <= std::abs(reachable(n + 1) - 12.0),
              "and stops at the closest reachable ratio despite adding carbon");
    }
    {
        // The basal : edge split is a real control, and its endpoints are
        // categorical rather than advisory.
        GrapheneOxideBuilder::Config config = flakeConfig(5);
        config.dosing = Dosing::TargetRatio;
        config.targetCarbonToOxygen = 6.0;
        config.basalOxygenShare = 0.7;
        GrapheneOxideBuilder::Report mixed;
        GrapheneOxideBuilder::build(config, &mixed);
        const int basalO =
            mixed.placedFor(Group::Epoxide) + mixed.placedFor(Group::Hydroxyl);
        const double share = static_cast<double>(basalO) / mixed.oxygenAtoms;
        check(std::abs(share - 0.7) < 0.1,
              "70 % of the oxygen is delivered by basal groups");

        config.basalOxygenShare = 1.0;
        GrapheneOxideBuilder::Report basalOnly;
        GrapheneOxideBuilder::build(config, &basalOnly);
        check(basalOnly.placedFor(Group::Carboxyl) == 0
                  && basalOnly.placedFor(Group::Carbonyl) == 0
                  && basalOnly.placedFor(Group::EdgeHydroxyl) == 0,
              "a 100 % basal split places no edge chemistry at all");

        config.basalOxygenShare = 0.0;
        GrapheneOxideBuilder::Report edgeOnly;
        GrapheneOxideBuilder::build(config, &edgeOnly);
        check(edgeOnly.placedFor(Group::Epoxide) == 0
                  && edgeOnly.placedFor(Group::Hydroxyl) == 0,
              "a 0 % basal split places no basal chemistry at all");
    }
    {
        // An unreachable target must be reported, not approximated in silence.
        // A small flake has only 6m edge carbons, and edge-only chemistry
        // cannot oxidize it much.
        GrapheneOxideBuilder::Config config = flakeConfig(3);
        config.dosing = Dosing::TargetRatio;
        config.targetCarbonToOxygen = 1.5;
        config.basalOxygenShare = 0.0;
        GrapheneOxideBuilder::Report report;
        GrapheneOxideBuilder::build(config, &report);
        check(!report.targetReached && !report.note.empty(),
              "an unreachable C/O target is reported, not approximated");
        check(report.carbonToOxygenRatio() > 1.5,
              "and the structure it did build is under-oxidized, as stated");
    }
    {
        // Same constraint as everywhere else: driving by ratio must not stack
        // two groups on one carbon.
        int worstOverload = 0;
        for (std::uint32_t seed = 0; seed < 8; ++seed) {
            GrapheneOxideBuilder::Config config = flakeConfig(4);
            config.dosing = Dosing::TargetRatio;
            config.targetCarbonToOxygen = 2.0;
            config.seed = seed;
            GrapheneOxideBuilder::Report report;
            const Structure s = GrapheneOxideBuilder::build(config, &report);
            for (const auto& [carbon, n] :
                 attachmentsPerCarbon(s, report.carbonCount))
                worstOverload = std::max(worstOverload, n);
        }
        check(worstOverload <= 1,
              "heavy ratio-driven oxidation still puts one group per carbon");
    }

    std::printf(failures == 0 ? "\nAll graphene oxide checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
