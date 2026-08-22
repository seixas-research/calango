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
//      interior carbons; edge chemistry (carboxyl, carbonyl) belongs on
//      under-coordinated rim carbons. The classification is recomputed here
//      from coordination numbers, independently of the builder.
//
//   3. The nanoflake family is exact: index m gives C(6m²)H(6m). A
//      flake that comes out with a different formula is not a member of the
//      family whatever it is called.

#include "core/GrapheneOxideBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

using calango::core::Atom;
using calango::core::GrapheneOxideBuilder;
using calango::core::Structure;
using Base = GrapheneOxideBuilder::Base;
using Dosing = GrapheneOxideBuilder::Dosing;
using Group = GrapheneOxideBuilder::Group;
using Region = GrapheneOxideBuilder::Region;
using Builder = GrapheneOxideBuilder;
using Config = GrapheneOxideBuilder::Config;
using Report = GrapheneOxideBuilder::Report;

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

/// A geometry-only copy of `source`: same atoms, same cell, no scalar or
/// vector fields at all -- what a project saved before the Graphene Oxide
/// Build contract existed looks like, or graphene oxide imported from
/// anywhere else. setScalarField() silently no-ops on a size mismatch (see
/// Structure::setScalarField()), so overwriting a built structure's own
/// "go_group" with an empty vector does NOT clear it; a fresh atoms-only
/// copy is the only way to get a structure with none.
Structure stripClassification(const Structure& source)
{
    Structure copy;
    copy.setCell(source.cell());
    for (const Atom& atom : source.atoms())
        copy.addAtom(atom);
    return copy;
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
            GrapheneOxideBuilder::Report report;
            const Structure s = GrapheneOxideBuilder::build(config, &report);
            for (const auto& [carbon, n] :
                 attachmentsPerCarbon(s, report.carbonCount))
                worstOverload = std::max(worstOverload, n);

            // An edge carbon that reacted has had its hydrogen SUBSTITUTED, not
            // added to — the terminating count is what is left over.
            const int reacted = report.placedFor(Group::Carboxyl)
                + report.placedFor(Group::Carbonyl);
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
            + report.placedFor(Group::Carbonyl);
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
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);
        check(report.edgeCarbonCount == 0,
              "a periodic sheet reports no edge carbons");
        check(report.placedFor(Group::Carboxyl) == 0
                  && report.placedFor(Group::Carbonyl) == 0,
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
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);
        const auto counts = elementCounts(s);

        const int expectedO = report.placedFor(Group::Epoxide)
            + report.placedFor(Group::Hydroxyl)
            + 2 * report.placedFor(Group::Carboxyl)
            + report.placedFor(Group::Carbonyl);
        const int expectedH = report.hydrogenTerminatedEdges
            + report.placedFor(Group::Hydroxyl)
            + report.placedFor(Group::Carboxyl);
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
                  && sizes[static_cast<int>(Group::Carbonyl)] == 2,
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
        for (Group group : {Group::Epoxide, Group::Hydroxyl, Group::Carbonyl})
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
                  && basalOnly.placedFor(Group::Carbonyl) == 0,
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

    // The wizard's sliders are ratios; each has to mean exactly what its label
    // says at BOTH endpoints, because those are the settings people reach for
    // ("only epoxides", "no edge oxidation") and the ones a soft interpretation
    // would quietly betray.
    std::printf("Slider semantics — O/C, H/O and the edge controls:\n");
    {
        // O/C is what the UI shows; the builder works in C/O. The inversion has
        // to land the composition where the slider says.
        bool allClose = true;
        for (double oxygenToCarbon : {0.10, 0.20, 0.30, 0.40}) {
            GrapheneOxideBuilder::Config config = flakeConfig(6);
            config.dosing = Dosing::TargetRatio;
            config.targetCarbonToOxygen = 1.0 / oxygenToCarbon;
            config.basalOxygenShare = 0.7;
            GrapheneOxideBuilder::Report report;
            GrapheneOxideBuilder::build(config, &report);
            const double achieved =
                static_cast<double>(report.oxygenAtoms) / report.totalCarbonAtoms;
            allClose = allClose
                && std::abs(achieved - oxygenToCarbon) / oxygenToCarbon < 0.08;
        }
        check(allClose, "O/C from 0.10 to 0.40 lands within 8 % of the target");
    }
    {
        // H/O on the basal plane IS the hydroxyl share of the basal groups:
        // both groups deliver exactly one oxygen, so weighting by group and
        // weighting by oxygen are the same thing. That identity is what makes
        // the slider's label true rather than approximate.
        const auto basal = [](double hydrogenPerOxygen) {
            GrapheneOxideBuilder::Config config;
            config.supercell[0] = config.supercell[1] = 6;
            config.dosing = Dosing::TargetRatio;
            config.targetCarbonToOxygen = 1.0 / 0.25;
            config.basalOxygenShare = 1.0;
            config.setWeight(Group::Epoxide, 1.0 - hydrogenPerOxygen);
            config.setWeight(Group::Hydroxyl, hydrogenPerOxygen);
            config.setWeight(Group::Carboxyl, 0.0);
            config.setWeight(Group::Carbonyl, 0.0);
            GrapheneOxideBuilder::Report report;
            GrapheneOxideBuilder::build(config, &report);
            return report;
        };

        const GrapheneOxideBuilder::Report epoxideOnly = basal(0.0);
        check(epoxideOnly.placedFor(Group::Epoxide) > 0
                  && epoxideOnly.placedFor(Group::Hydroxyl) == 0,
              "H/O = 0 gives epoxides and nothing else");
        check(epoxideOnly.hydrogenAtoms == 0,
              "and therefore no hydrogen at all — H/O is literally zero");

        const GrapheneOxideBuilder::Report hydroxylOnly = basal(1.0);
        check(hydroxylOnly.placedFor(Group::Hydroxyl) > 0
                  && hydroxylOnly.placedFor(Group::Epoxide) == 0,
              "H/O = 1 gives hydroxyls and nothing else");
        check(hydroxylOnly.hydrogenAtoms == hydroxylOnly.oxygenAtoms,
              "and one hydrogen per oxygen — H/O is literally one");

        const GrapheneOxideBuilder::Report half = basal(0.5);
        const double achieved = static_cast<double>(half.hydrogenAtoms)
            / half.oxygenAtoms;
        check(std::abs(achieved - 0.5) < 0.12,
              "and H/O = 0.5 delivers about one hydrogen per two oxygens");
    }
    {
        // Edge oxidation: the density control and the composition control are
        // independent, which is the reason they are two sliders.
        const auto edge = [](double edgeShare, double carboxylOxygenShare) {
            GrapheneOxideBuilder::Config config = flakeConfig(5);
            config.dosing = Dosing::TargetRatio;
            config.targetCarbonToOxygen = 1.0 / 0.2;
            config.basalOxygenShare = 1.0 - edgeShare;
            config.setWeight(Group::Epoxide, 0.5);
            config.setWeight(Group::Hydroxyl, 0.5);
            // A carboxyl brings TWO oxygens, so an oxygen share of f is f/2 of
            // the groups — the conversion the wizard does.
            config.setWeight(Group::Carboxyl, carboxylOxygenShare / 2.0);
            config.setWeight(Group::Carbonyl, 1.0 - carboxylOxygenShare);
            GrapheneOxideBuilder::Report report;
            GrapheneOxideBuilder::build(config, &report);
            return report;
        };

        const GrapheneOxideBuilder::Report noEdge = edge(0.0, 0.5);
        check(noEdge.placedFor(Group::Carboxyl) == 0
                  && noEdge.placedFor(Group::Carbonyl) == 0,
              "edge share 0 leaves the rim untouched");

        const GrapheneOxideBuilder::Report carbonylOnly = edge(1.0, 0.0);
        check(carbonylOnly.placedFor(Group::Carbonyl) > 0
                  && carbonylOnly.placedFor(Group::Carboxyl) == 0,
              "COOH/O = 0 gives carbonyls and nothing else");
        const GrapheneOxideBuilder::Report carboxylOnly = edge(1.0, 1.0);
        check(carboxylOnly.placedFor(Group::Carboxyl) > 0
                  && carboxylOnly.placedFor(Group::Carbonyl) == 0,
              "COOH/O = 1 gives carboxyls and nothing else");

        // More edge share means more edge oxygen, holding everything else.
        const int lightEdge = edge(0.2, 0.5).placedFor(Group::Carboxyl)
            + edge(0.2, 0.5).placedFor(Group::Carbonyl);
        const int heavyEdge = edge(0.8, 0.5).placedFor(Group::Carboxyl)
            + edge(0.8, 0.5).placedFor(Group::Carbonyl);
        check(heavyEdge > lightEdge,
              "and the edge share slider really is a density control");
    }
    {
        // The generator must never place a phenolic edge -OH again. Checked
        // from the geometry, not from the report: an -OH on an sp2 rim carbon
        // is the thing that was removed, and no configuration may bring it
        // back.
        GrapheneOxideBuilder::Config config = flakeConfig(5);
        config.dosing = Dosing::TargetRatio;
        config.targetCarbonToOxygen = 2.5;
        config.basalOxygenShare = 0.0; // everything at the edge
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);
        const auto coordination = carbonCoordination(s, report.carbonCount);

        int hydroxylsOnEdgeCarbons = 0;
        for (std::size_t i = static_cast<std::size_t>(report.carbonCount);
             i < s.size(); ++i) {
            if (s.atoms()[i].atomicNumber != 8)
                continue;
            // An -OH: this oxygen carries a hydrogen.
            bool hasHydrogen = false;
            for (std::size_t j = static_cast<std::size_t>(report.carbonCount);
                 j < s.size(); ++j)
                if (s.atoms()[j].atomicNumber == 1
                    && distance(s.atoms()[i].position, s.atoms()[j].position)
                        < 1.1)
                    hasHydrogen = true;
            if (!hasHydrogen)
                continue;
            for (int c = 0; c < report.carbonCount; ++c)
                if (coordination[static_cast<std::size_t>(c)] < 3
                    && distance(s.atoms()[i].position,
                                s.atoms()[static_cast<std::size_t>(c)].position)
                        < 1.62)
                    ++hydroxylsOnEdgeCarbons;
        }
        check(report.oxygenAtoms > 0, "the edge-only run placed oxygen");
        check(hydroxylsOnEdgeCarbons == 0,
              "and not one -OH bonded directly to a rim carbon — phenolic edge "
              "hydroxyls are gone from the generator");
    }

    // ===== Decoupled basal / edge dosing ==================================
    //
    // The mode exists to make two dials INDEPENDENT. So the tests are about
    // independence, not about hitting a number: moving one dial must leave the
    // other region's chemistry alone. Under the shared-budget TargetRatio mode
    // every one of these would fail by construction, which is the point.
    std::printf("Decoupled basal / edge oxidation:\n");
    {
        const auto flakeWith = [](double basalOc, double edgeOx,
                                  double carboxylShare) {
            Config config;
            config.base = Base::Nanoflake;
            config.flakeIndex = 4; // 96 carbons, 24 of them rim
            config.dosing = Dosing::DecoupledRegions;
            config.basalOxygenToCarbon = basalOc;
            config.edgeOxidation = edgeOx;
            config.carboxylShare = carboxylShare;
            config.basalHydroxylShare = 0.5;
            config.seed = 7;
            Report report;
            Builder::build(config, &report);
            return report;
        };

        // 1. Edge oxidation at exactly zero is CATEGORICAL: a strictly
        //    hydrogen-terminated rim, whatever the basal plane is doing.
        const auto bare = flakeWith(0.30, 0.0, 0.5);
        check(bare.placedFor(Group::Carboxyl) == 0
                  && bare.placedFor(Group::Carbonyl) == 0,
              "edge oxidation 0 places no edge group at all");
        check(bare.hydrogenTerminatedEdges == bare.edgeCarbonCount,
              "and every rim carbon is hydrogen-terminated");
        check(bare.placedFor(Group::Epoxide) + bare.placedFor(Group::Hydroxyl)
                  > 0,
              "while the basal plane is oxidized independently");

        // 2. Sweeping the rim must not move the basal plane. This is the
        //    property a shared oxygen budget cannot have.
        const auto lowEdge = flakeWith(0.30, 0.1, 0.5);
        const auto highEdge = flakeWith(0.30, 0.9, 0.5);
        const int basalLow =
            lowEdge.placedFor(Group::Epoxide) + lowEdge.placedFor(Group::Hydroxyl);
        const int basalHigh = highEdge.placedFor(Group::Epoxide)
            + highEdge.placedFor(Group::Hydroxyl);
        check(lowEdge.basalOxygenPlaced == highEdge.basalOxygenPlaced,
              "the basal oxygen count is untouched by the edge dial");
        check(basalLow == basalHigh,
              "and so is the basal group count");
        const int edgeLow = lowEdge.placedFor(Group::Carboxyl)
            + lowEdge.placedFor(Group::Carbonyl);
        const int edgeHigh = highEdge.placedFor(Group::Carboxyl)
            + highEdge.placedFor(Group::Carbonyl);
        check(edgeHigh > edgeLow, "while the rim itself responds to it");

        // 3. And the converse: sweeping the basal plane must not move the rim.
        const auto lightBasal = flakeWith(0.05, 0.5, 0.5);
        const auto heavyBasal = flakeWith(0.45, 0.5, 0.5);
        check(lightBasal.edgeGroupsPlaced == heavyBasal.edgeGroupsPlaced,
              "the rim is untouched by the basal dial");

        // 4. Rising edge oxidation replaces edge HYDROGENS with groups: the
        //    two must move in opposite directions, one for one.
        check(highEdge.hydrogenTerminatedEdges < lowEdge.hydrogenTerminatedEdges,
              "more edge oxidation means fewer edge hydrogens");
        check(lowEdge.hydrogenTerminatedEdges + edgeLow
                  == lowEdge.edgeCarbonCount,
              "every rim carbon is either functionalized or hydrogen-capped");
        check(highEdge.hydrogenTerminatedEdges + edgeHigh
                  == highEdge.edgeCarbonCount,
              "with no rim carbon left as a bare radical");

        // 5. The carboxyl share steers the edge composition. Stated in OXYGEN,
        //    so an all-carboxyl rim delivers two oxygens per group.
        const auto allCarbonyl = flakeWith(0.0, 0.8, 0.0);
        const auto allCarboxyl = flakeWith(0.0, 0.8, 1.0);
        check(allCarbonyl.placedFor(Group::Carboxyl) == 0
                  && allCarbonyl.placedFor(Group::Carbonyl) > 0,
              "carboxyl share 0 gives quinone-like carbonyls only");
        check(allCarboxyl.placedFor(Group::Carbonyl) == 0
                  && allCarboxyl.placedFor(Group::Carboxyl) > 0,
              "carboxyl share 1 gives carboxyls only");
        check(allCarboxyl.oxygenAtoms > allCarbonyl.oxygenAtoms,
              "and a carboxyl rim carries more oxygen, bringing two each");

        // 6. A periodic sheet has no rim: the edge dial must be inert rather
        //    than an error, and the basal dial must still work.
        Config sheet;
        sheet.base = Base::PeriodicSheet;
        sheet.supercell[0] = sheet.supercell[1] = 5;
        sheet.dosing = Dosing::DecoupledRegions;
        sheet.basalOxygenToCarbon = 0.25;
        sheet.edgeOxidation = 1.0; // asking for a rim that does not exist
        Report sheetReport;
        Builder::build(sheet, &sheetReport);
        check(sheetReport.placedFor(Group::Carboxyl) == 0
                  && sheetReport.placedFor(Group::Carbonyl) == 0,
              "an edgeless sheet places no edge chemistry");
        check(sheetReport.basalOxygenPlaced > 0,
              "and its basal plane is oxidized as asked");
    }

    std::printf("Per-frame classification tracks a relocated group "
                "(MDMC-style swap):\n");
    {
        // Two structures from the SAME lattice and coverage, different
        // seeds, so each ends up with its epoxides on a DIFFERENT set of
        // carbon pairs — standing in for the "before" and "after" frame of
        // an accepted MDMC move without hand-constructing or hand-editing
        // any geometry. functionalGroupLabels() is exactly what
        // MainWindow::redefineFunctionalGroupCastForFrame() (the per-frame
        // Cast redefinition MDMC uses) calls on every streamed frame; this
        // exercises that call directly, independent of the GUI it feeds.
        Config config;
        config.supercell[0] = config.supercell[1] = 6;
        config.setCoverage(Group::Epoxide, 0.2);
        config.seed = 1;
        Report reportA;
        const Structure before = Builder::build(config, &reportA);
        config.seed = 7;
        Report reportB;
        const Structure after = Builder::build(config, &reportB);

        check(before.size() == after.size(),
              "both frames keep the same atom count -- MDMC relocates "
              "groups, it never adds or removes atoms");

        const auto labelsBefore = Builder::functionalGroupLabels(before);
        const auto labelsAfter = Builder::functionalGroupLabels(after);
        check(labelsBefore.size() == before.size()
                  && labelsAfter.size() == after.size(),
              "one label per atom, for both frames");

        const auto countEpoxide = [](const std::vector<int>& labels) {
            int n = 0;
            for (int label : labels)
                if (label == static_cast<int>(Group::Epoxide))
                    ++n;
            return n;
        };
        const int epoxideAtomsBefore = countEpoxide(labelsBefore);
        const int epoxideAtomsAfter = countEpoxide(labelsAfter);
        check(epoxideAtomsBefore > 0 && epoxideAtomsAfter > 0,
              "both frames actually placed at least one epoxide");
        // functionalGroupLabels() labels every atom OF the group, host
        // carbon(s) included -- an epoxide is 2 carbons + 1 bridging oxygen,
        // Builder::carbonCost()/oxygensPerGroup()'s own numbers for it.
        const int atomsPerEpoxide = Builder::carbonCost(Group::Epoxide)
            + Builder::oxygensPerGroup(Group::Epoxide);
        check(epoxideAtomsBefore == reportA.placedFor(Group::Epoxide) * atomsPerEpoxide
                  && epoxideAtomsAfter
                      == reportB.placedFor(Group::Epoxide) * atomsPerEpoxide,
              "the RECOMPUTED label count matches the builder's own "
              "placement count (2 carbons + 1 oxygen per epoxide), "
              "independently for each frame");

        // The payoff: the SET of epoxide-bonded atom indices differs
        // between the two frames -- classification tracks THIS frame's own
        // bonding, not a cached assignment from whichever frame ran first.
        // A per-frame Cast built from this moves WITH the group; one built
        // once from frame 0 would stay pinned to carbons that are bare by
        // the second frame.
        bool sameIndices = labelsBefore.size() == labelsAfter.size();
        for (std::size_t i = 0; sameIndices && i < labelsBefore.size(); ++i)
            sameIndices = sameIndices
                && (labelsBefore[i] == static_cast<int>(Group::Epoxide))
                    == (labelsAfter[i] == static_cast<int>(Group::Epoxide));
        check(!sameIndices,
              "the epoxide-bonded atom indices differ between the two "
              "frames");
    }

    std::printf("Default epoxide:hydroxyl ratio:\n");
    {
        // Deterministic checks on the raw Config defaults themselves, not on
        // a build's stochastic outcome -- the single source of truth for
        // "what does a caller get who touches nothing."
        Config config;
        check(config.weight[static_cast<std::size_t>(Group::Epoxide)] == 1.0
                  && config.weight[static_cast<std::size_t>(Group::Hydroxyl)]
                      == 2.0,
              "Dosing::TargetRatio default weight is epoxide:hydroxyl = 1:2");
        check(std::abs(config.basalHydroxylShare - 2.0 / 3.0) < 1e-12,
              "Dosing::DecoupledRegions default basalHydroxylShare is 2/3 "
              "(epoxide:hydroxyl = 1:2)");
        check(!config.hydroxylAntiposition,
              "hydroxylAntiposition defaults to off");
    }
    {
        // The build-based confirmation: leave every basal dial at its
        // default and check the OUTCOME is hydroxyl-majority, not just that
        // the numbers on paper look right. A large enough basal target that
        // a 1:2 weighting is overwhelmingly unlikely to invert by chance.
        Config config;
        config.supercell[0] = config.supercell[1] = 6;
        config.dosing = Dosing::DecoupledRegions;
        config.basalOxygenToCarbon = 0.3;
        Report report;
        Builder::build(config, &report);
        check(report.placedFor(Group::Hydroxyl) > report.placedFor(Group::Epoxide),
              "with every dial left at default, more hydroxyls than "
              "epoxides get placed (2:1 by weight)");
    }

    std::printf("Hydroxyls antiposition:\n");
    {
        // ExplicitCoverage: only hydroxyls enabled, paired.
        Config config;
        config.supercell[0] = config.supercell[1] = 8;
        config.dosing = Dosing::ExplicitCoverage;
        config.setCoverage(Group::Hydroxyl, 0.35);
        config.hydroxylAntiposition = true;
        config.seed = 3;
        Report report;
        const Structure s = Builder::build(config, &report);

        check(report.placedFor(Group::Hydroxyl) > 0,
              "antiposition placed at least one hydroxyl (ExplicitCoverage)");
        check(report.placedFor(Group::Hydroxyl) % 2 == 0,
              "antiposition never leaves a hydroxyl unpaired -- placed count "
              "is even (ExplicitCoverage)");

        double closest = 0.0;
        check(!anythingFused(s, closest),
              "no atom pair came out fused (ExplicitCoverage)");

        // One group per carbon still holds -- the pair-based placement path
        // goes through the SAME reservation table as everything else.
        const auto attachments = attachmentsPerCarbon(s, report.carbonCount);
        bool oneGroupPerCarbon = true;
        for (const auto& [carbon, count] : attachments)
            oneGroupPerCarbon = oneGroupPerCarbon && count <= 1;
        check(oneGroupPerCarbon,
              "no carbon hosts more than one functional group "
              "(ExplicitCoverage, antiposition)");

        // The payoff: recompute the pairing from bonding + geometry alone,
        // independent of the builder's own bookkeeping. Every hydroxyl's
        // host carbon must have a directly-bonded neighbour that is ALSO a
        // hydroxyl host, and the two oxygens must sit on opposite faces
        // (z of opposite sign) -- the antiposition motif itself.
        const auto clusters = Builder::findFunctionalGroups(s);
        std::map<int, calango::core::Vec3> hydroxylOxygenByCarbon;
        for (const auto& cluster : clusters) {
            if (cluster.kind != Group::Hydroxyl)
                continue;
            // GroupCluster::atoms is the group's own atoms first, then its
            // host carbon(s) -- one oxygen, one hydrogen, one host carbon.
            int oxygenAtom = -1;
            int hostCarbon = -1;
            for (int index : cluster.atoms) {
                const Atom& atom = s.atoms()[static_cast<std::size_t>(index)];
                if (atom.atomicNumber == 8)
                    oxygenAtom = index;
                else if (atom.atomicNumber == 6)
                    hostCarbon = index;
            }
            if (oxygenAtom >= 0 && hostCarbon >= 0)
                hydroxylOxygenByCarbon[hostCarbon] =
                    s.atoms()[static_cast<std::size_t>(oxygenAtom)].position;
        }
        check(static_cast<int>(hydroxylOxygenByCarbon.size())
                  == report.placedFor(Group::Hydroxyl),
              "findFunctionalGroups() recovers exactly the hydroxyls the "
              "builder reports placing");

        // At this density a carbon can easily have MORE than one bonded
        // hydroxyl neighbour -- its own antiposition partner plus, by pure
        // coincidence of the lattice, a hydroxyl from an entirely different,
        // unrelated pair. So the invariant to check is "at least one bonded
        // hydroxyl neighbour", and separately "at least one of those is on
        // the OPPOSITE face" -- not that the first neighbour found is
        // automatically the true partner, which a dense structure need not
        // satisfy even when every pair is correctly antiposition.
        int paired = 0;
        int hasOppositeFacePartner = 0;
        for (const auto& [carbon, oxygen] : hydroxylOxygenByCarbon) {
            const calango::core::Vec3 cPos =
                s.atoms()[static_cast<std::size_t>(carbon)].position;
            bool foundBondedHydroxyl = false;
            bool foundOppositeFace = false;
            for (const auto& [otherCarbon, otherOxygen] : hydroxylOxygenByCarbon) {
                if (otherCarbon == carbon)
                    continue;
                const calango::core::Vec3 oPos =
                    s.atoms()[static_cast<std::size_t>(otherCarbon)].position;
                if (periodicDistance(s, cPos, oPos) < 1.55) {
                    foundBondedHydroxyl = true;
                    // Relative to each carbon's OWN z, not the raw oxygen z:
                    // the sheet sits at z = kVacuum/2, not z = 0, so both
                    // faces' oxygens land on the same (positive) side of
                    // z = 0 and a raw sign comparison would be meaningless.
                    if ((oxygen.z - cPos.z) * (otherOxygen.z - oPos.z) < 0.0)
                        foundOppositeFace = true;
                }
            }
            if (foundBondedHydroxyl)
                ++paired;
            if (foundOppositeFace)
                ++hasOppositeFacePartner;
        }
        check(paired == static_cast<int>(hydroxylOxygenByCarbon.size()),
              "every hydroxyl's host carbon is directly bonded to another "
              "hydroxyl's host carbon");
        check(hasOppositeFacePartner == static_cast<int>(hydroxylOxygenByCarbon.size()),
              "every hydroxyl's host carbon has a bonded hydroxyl neighbour "
              "on the OPPOSITE face -- the antiposition motif");
    }
    {
        // DecoupledRegions: the mode the wizard actually drives. Same
        // invariants, different dosing path through place().
        Config config;
        config.supercell[0] = config.supercell[1] = 8;
        config.dosing = Dosing::DecoupledRegions;
        config.basalOxygenToCarbon = 0.3;
        config.basalHydroxylShare = 1.0; // isolate hydroxyls from epoxides
        config.hydroxylAntiposition = true;
        config.seed = 11;
        Report report;
        const Structure s = Builder::build(config, &report);

        check(report.placedFor(Group::Hydroxyl) > 0,
              "antiposition placed at least one hydroxyl (DecoupledRegions)");
        check(report.placedFor(Group::Hydroxyl) % 2 == 0,
              "antiposition never leaves a hydroxyl unpaired -- placed count "
              "is even (DecoupledRegions)");

        const auto clusters = Builder::findFunctionalGroups(s);
        std::map<int, calango::core::Vec3> hydroxylOxygenByCarbon;
        for (const auto& cluster : clusters) {
            if (cluster.kind != Group::Hydroxyl)
                continue;
            int oxygenAtom = -1;
            int hostCarbon = -1;
            for (int index : cluster.atoms) {
                const Atom& atom = s.atoms()[static_cast<std::size_t>(index)];
                if (atom.atomicNumber == 8)
                    oxygenAtom = index;
                else if (atom.atomicNumber == 6)
                    hostCarbon = index;
            }
            if (oxygenAtom >= 0 && hostCarbon >= 0)
                hydroxylOxygenByCarbon[hostCarbon] =
                    s.atoms()[static_cast<std::size_t>(oxygenAtom)].position;
        }

        // Same "at least one" reasoning as the ExplicitCoverage block above.
        int paired = 0;
        int hasOppositeFacePartner = 0;
        for (const auto& [carbon, oxygen] : hydroxylOxygenByCarbon) {
            const calango::core::Vec3 cPos =
                s.atoms()[static_cast<std::size_t>(carbon)].position;
            bool foundBondedHydroxyl = false;
            bool foundOppositeFace = false;
            for (const auto& [otherCarbon, otherOxygen] : hydroxylOxygenByCarbon) {
                if (otherCarbon == carbon)
                    continue;
                const calango::core::Vec3 oPos =
                    s.atoms()[static_cast<std::size_t>(otherCarbon)].position;
                if (periodicDistance(s, cPos, oPos) < 1.55) {
                    foundBondedHydroxyl = true;
                    // Relative to each carbon's OWN z, not the raw oxygen z:
                    // the sheet sits at z = kVacuum/2, not z = 0, so both
                    // faces' oxygens land on the same (positive) side of
                    // z = 0 and a raw sign comparison would be meaningless.
                    if ((oxygen.z - cPos.z) * (otherOxygen.z - oPos.z) < 0.0)
                        foundOppositeFace = true;
                }
            }
            if (foundBondedHydroxyl)
                ++paired;
            if (foundOppositeFace)
                ++hasOppositeFacePartner;
        }
        check(paired == static_cast<int>(hydroxylOxygenByCarbon.size()),
              "every hydroxyl's host carbon is directly bonded to another "
              "hydroxyl's host carbon (DecoupledRegions)");
        check(hasOppositeFacePartner == static_cast<int>(hydroxylOxygenByCarbon.size()),
              "every hydroxyl's host carbon has a bonded hydroxyl neighbour "
              "on the OPPOSITE face (DecoupledRegions)");
    }

    // ===== The Graphene Oxide Build contract ===============================
    //
    // "go_group" / "go_group_id" / "go_pair_id": the persisted classification
    // and antiposition registry every downstream GO module (GO-MDMC, GO
    // Functional Group Analysis, GO Pair Correlation) reads. Checked against
    // functionalGroupLabels()/findFunctionalGroups() -- the SAME classifier,
    // re-derived from bonding -- so the persisted fields cannot silently
    // drift from the one implementation of "what counts as a group".
    std::printf("The Graphene Oxide Build contract:\n");
    {
        Config config = flakeConfig(4);
        config.setCoverage(Group::Epoxide, 0.2);
        config.setCoverage(Group::Hydroxyl, 0.2);
        config.setCoverage(Group::Carboxyl, 0.3);
        config.setCoverage(Group::Carbonyl, 0.2);
        config.seed = 42;
        const Structure s = Builder::build(config);

        check(Builder::hasClassification(s),
              "a freshly built structure carries the classification");

        const auto& fields = s.scalarFields();
        check(fields.count("go_group") == 1 && fields.count("go_group_id") == 1
                  && fields.count("go_pair_id") == 1,
              "all three classification fields are present");
        const auto& group = fields.at("go_group");
        const auto& groupId = fields.at("go_group_id");
        const auto& pairId = fields.at("go_pair_id");
        check(group.size() == s.size() && groupId.size() == s.size()
                  && pairId.size() == s.size(),
              "every field is index-aligned with the atoms");

        // "go_group" must agree with functionalGroupLabels() atom for atom --
        // it is that function's own definition, persisted rather than
        // recomputed.
        const auto labels = Builder::functionalGroupLabels(s);
        bool groupMatchesLabels = true;
        for (std::size_t i = 0; i < s.size(); ++i)
            groupMatchesLabels = groupMatchesLabels
                && static_cast<int>(std::lround(group[i])) == labels[i];
        check(groupMatchesLabels,
              "\"go_group\" matches functionalGroupLabels() exactly");

        // "go_group_id": every non-negative id groups exactly the atoms of
        // one findFunctionalGroups() cluster -- no more, no fewer.
        const auto clusters = Builder::findFunctionalGroups(s);
        std::map<int, std::vector<int>> atomsById;
        for (std::size_t i = 0; i < s.size(); ++i) {
            const int id = static_cast<int>(std::lround(groupId[i]));
            if (id >= 0)
                atomsById[id].push_back(static_cast<int>(i));
        }
        check(atomsById.size() == clusters.size(),
              "one distinct \"go_group_id\" per placed group instance");
        bool everyIdIsACluster = true;
        for (auto& [id, atoms] : atomsById) {
            std::sort(atoms.begin(), atoms.end());
            bool matchesSomeCluster = false;
            for (const auto& cluster : clusters) {
                std::vector<int> clusterAtoms = cluster.atoms;
                std::sort(clusterAtoms.begin(), clusterAtoms.end());
                if (clusterAtoms == atoms) {
                    matchesSomeCluster = true;
                    break;
                }
            }
            everyIdIsACluster = everyIdIsACluster && matchesSomeCluster;
        }
        check(everyIdIsACluster,
              "and its atom set is exactly one findFunctionalGroups() "
              "cluster");

        // No antiposition was requested, so the pairing registry must be
        // empty throughout.
        bool noPairs = true;
        for (double p : pairId)
            noPairs = noPairs && p < 0.0;
        check(noPairs,
              "\"go_pair_id\" is -1 everywhere when antiposition is off");
    }
    {
        // The antiposition registry itself: every pair id is shared by
        // exactly two group instances, and every one of those is a
        // hydroxyl -- checked against the SAME geometric antiposition motif
        // (bonded host carbons, opposite-face oxygens) the earlier
        // "Hydroxyls antiposition" block verifies independently.
        Config config;
        config.supercell[0] = config.supercell[1] = 8;
        config.dosing = Dosing::ExplicitCoverage;
        config.setCoverage(Group::Hydroxyl, 0.35);
        config.hydroxylAntiposition = true;
        config.seed = 9;
        Report report;
        const Structure s = Builder::build(config, &report);

        const auto& fields = s.scalarFields();
        const auto& group = fields.at("go_group");
        const auto& groupId = fields.at("go_group_id");
        const auto& pairId = fields.at("go_pair_id");

        std::map<int, std::vector<int>> groupIdsByPair;
        for (std::size_t i = 0; i < s.size(); ++i) {
            const int p = static_cast<int>(std::lround(pairId[i]));
            if (p < 0)
                continue;
            const int gid = static_cast<int>(std::lround(groupId[i]));
            auto& ids = groupIdsByPair[p];
            if (std::find(ids.begin(), ids.end(), gid) == ids.end())
                ids.push_back(gid);
            check(static_cast<int>(std::lround(group[i]))
                      == static_cast<int>(Group::Hydroxyl),
                  "every paired atom belongs to a hydroxyl");
        }
        check(!groupIdsByPair.empty(), "at least one antiposition pair exists");
        bool everyPairHasTwoInstances = true;
        for (const auto& [p, ids] : groupIdsByPair)
            everyPairHasTwoInstances = everyPairHasTwoInstances
                && ids.size() == 2;
        check(everyPairHasTwoInstances,
              "every \"go_pair_id\" is shared by exactly two group "
              "instances");
        // Every antiposition hydroxyl belongs to exactly one pair (the
        // placement path never leaves one unpaired), so twice the pair
        // count must account for every hydroxyl the builder placed.
        check(2 * static_cast<int>(groupIdsByPair.size())
                  == report.placedFor(Group::Hydroxyl),
              "the pair count accounts for every hydroxyl placed");
    }

    std::printf("Legacy/imported structures -- classifyFromBonding() fallback:\n");
    {
        // Simulate a structure saved before this contract existed (or one
        // imported from anywhere else): a freshly built GO sample with its
        // classification fields stripped back off.
        Config config = flakeConfig(4);
        config.setCoverage(Group::Epoxide, 0.2);
        config.setCoverage(Group::Hydroxyl, 0.2);
        config.setCoverage(Group::Carboxyl, 0.3);
        config.seed = 5;
        Structure s = stripClassification(Builder::build(config));
        check(!Builder::hasClassification(s),
              "a structure with an empty \"go_group\" field is NOT "
              "classified");

        Builder::classifyFromBonding(s);
        check(Builder::hasClassification(s),
              "classifyFromBonding() leaves the structure classified");

        // The fallback must reach the SAME answer as the persisted field
        // would have -- it is the identical classifier, findFunctionalGroups(),
        // just called later.
        const auto labels = Builder::functionalGroupLabels(s);
        const auto& group = s.scalarFields().at("go_group");
        bool matches = group.size() == s.size();
        for (std::size_t i = 0; matches && i < s.size(); ++i)
            matches = static_cast<int>(std::lround(group[i])) == labels[i];
        check(matches,
              "the recomputed \"go_group\" matches functionalGroupLabels() "
              "exactly, same as the build-time field would");
    }
    {
        // The fallback's antiposition re-derivation: strip the fields off an
        // antiposition-built structure and check classifyFromBonding()
        // recovers the SAME pairing -- geometry alone is enough.
        Config config;
        config.supercell[0] = config.supercell[1] = 8;
        config.dosing = Dosing::ExplicitCoverage;
        config.setCoverage(Group::Hydroxyl, 0.35);
        config.hydroxylAntiposition = true;
        config.seed = 13;
        const Structure built = Builder::build(config);

        // The pairing the builder itself recorded, as a set of unordered
        // atom-index pairs (the two host carbons of each pair), independent
        // of the arbitrary pair-id numbering.
        const auto pairedHostCarbons = [&](const Structure& structure) {
            const auto& fields = structure.scalarFields();
            const auto& groupField = fields.at("go_group");
            const auto& groupIdField = fields.at("go_group_id");
            const auto& pairField = fields.at("go_pair_id");
            std::map<int, std::vector<int>> hostByPair;
            for (std::size_t i = 0; i < structure.size(); ++i) {
                const int p = static_cast<int>(std::lround(pairField[i]));
                if (p < 0
                    || static_cast<int>(std::lround(groupField[i]))
                        != static_cast<int>(Group::Hydroxyl)
                    || structure.atoms()[i].atomicNumber != 6)
                    continue;
                hostByPair[p].push_back(static_cast<int>(i));
            }
            std::set<std::pair<int, int>> pairs;
            for (auto& [p, hosts] : hostByPair) {
                if (hosts.size() != 2)
                    continue;
                std::sort(hosts.begin(), hosts.end());
                pairs.insert({hosts[0], hosts[1]});
            }
            (void)groupIdField;
            return pairs;
        };

        const auto builtPairs = pairedHostCarbons(built);
        check(!builtPairs.empty(), "the build itself produced some pairs");

        Structure s = stripClassification(built);
        Builder::classifyFromBonding(s);
        const auto recoveredPairs = pairedHostCarbons(s);

        check(recoveredPairs == builtPairs,
              "classifyFromBonding() recovers the SAME antiposition pairing "
              "from geometry alone, with no build-time record to consult");
    }
    {
        // A structure with no antiposition at all: classifyFromBonding() has
        // only geometry to go on, so at high enough density it CAN pair two
        // independently-placed hydroxyls that happen to land on bonded
        // carbons with opposite faces -- that is not a bug, it is the
        // documented limit of reconstructing a pairing that was never
        // recorded. What must hold unconditionally is SOUNDNESS: whatever
        // "go_pair_id" it assigns always satisfies the antiposition motif
        // it claims to have found.
        Config config;
        config.supercell[0] = config.supercell[1] = 6;
        config.setCoverage(Group::Hydroxyl, 0.3);
        config.seed = 21;
        Structure s = stripClassification(Builder::build(config));
        Builder::classifyFromBonding(s);

        const auto& groupField = s.scalarFields().at("go_group");
        const auto& pairField = s.scalarFields().at("go_pair_id");
        std::map<int, std::vector<int>> hostsByPair;
        for (std::size_t i = 0; i < s.size(); ++i) {
            const int p = static_cast<int>(std::lround(pairField[i]));
            if (p < 0 || s.atoms()[i].atomicNumber != 6)
                continue;
            hostsByPair[p].push_back(static_cast<int>(i));
        }
        bool everyPairIsGenuinelyAntiposition = true;
        std::vector<std::vector<int>> neighbours(s.size());
        for (const auto& bond : s.detectBonds()) {
            neighbours[static_cast<std::size_t>(bond.i)].push_back(bond.j);
            neighbours[static_cast<std::size_t>(bond.j)].push_back(bond.i);
        }
        for (const auto& [p, hosts] : hostsByPair) {
            if (hosts.size() != 2) {
                everyPairIsGenuinelyAntiposition = false;
                continue;
            }
            const int hostA = hosts[0];
            const int hostB = hosts[1];
            const auto& nb = neighbours[static_cast<std::size_t>(hostA)];
            const bool bonded =
                std::find(nb.begin(), nb.end(), hostB) != nb.end();
            const bool bothHydroxyl =
                static_cast<int>(std::lround(groupField[static_cast<std::size_t>(hostA)]))
                    == static_cast<int>(Group::Hydroxyl)
                && static_cast<int>(std::lround(groupField[static_cast<std::size_t>(hostB)]))
                    == static_cast<int>(Group::Hydroxyl);
            everyPairIsGenuinelyAntiposition =
                everyPairIsGenuinelyAntiposition && bonded && bothHydroxyl;
        }
        check(everyPairIsGenuinelyAntiposition,
              "every pair classifyFromBonding() reports (coincidental or "
              "not) has two bonded hydroxyl-host carbons -- it never "
              "invents a pairing the geometry does not support");
    }
    {
        // A plain structure with no oxygen chemistry at all -- e.g. bare
        // graphene -- is squarely "not classified", the case GO-MDMC's
        // pre-flight check must catch and refuse rather than crash on.
        Config config;
        config.supercell[0] = config.supercell[1] = 4;
        const Structure s = Builder::pristine(config);
        check(!Builder::hasClassification(s),
              "a bare, never-built substrate carries no classification");
    }

    std::printf("Thermal bond tolerance for MDMC frames:\n");
    {
        // An MDMC frame is a thermal snapshot. Measured on a sheet under
        // MACE-MP-0, an intact epoxide C-O momentarily stretched past the
        // application-wide 1.15x covalent-radius cutoff read as a carbonyl
        // -- two of six epoxides on one 335 K frame -- and recoloured the
        // per-frame Cast of a GO-MDMC run. The Cast therefore classifies
        // MDMC frames at kThermalBondTolerance. This pins the two numbers
        // against a closed-form geometry: one C-O at 1.78 A (a thermal
        // instant), the other at its 1.44 A rest length.
        Config config;
        config.supercell[0] = config.supercell[1] = 4;
        Structure s = Builder::pristine(config);
        const auto p0 = s.atoms()[0].position;
        std::size_t partner = 0;
        double best = 1e9;
        for (std::size_t j = 1; j < s.size(); ++j) {
            const auto& pj = s.atoms()[j].position;
            const double d = std::sqrt((pj.x - p0.x) * (pj.x - p0.x)
                                       + (pj.y - p0.y) * (pj.y - p0.y)
                                       + (pj.z - p0.z) * (pj.z - p0.z));
            if (d < best) {
                best = d;
                partner = j;
            }
        }
        const auto p1 = s.atoms()[partner].position;
        const double ux = (p1.x - p0.x) / best;
        const double uy = (p1.y - p0.y) / best;
        Atom oxygen;
        oxygen.atomicNumber = 8;
        // Rest geometry: O above the bond midpoint at the height that makes
        // both C-O 1.44 A on a 1.42 A bond.
        const double height = std::sqrt(1.44 * 1.44 - 0.25 * best * best);
        oxygen.position = calango::core::Vec3{0.5 * (p0.x + p1.x), 0.5 * (p0.y + p1.y),
                                             p0.z + height};
        s.addAtom(oxygen);
        const std::size_t o = s.size() - 1;
        const int epoxide = static_cast<int>(Group::Epoxide);

        auto cold = Builder::functionalGroupLabels(s);
        auto hot = Builder::functionalGroupLabels(
            s, Builder::kThermalBondTolerance);
        check(cold[o] == epoxide && hot[o] == epoxide,
              "a cold, intact epoxide is an epoxide at either tolerance");
        const bool hostsLabelled = cold[0] == epoxide && cold[partner] == epoxide;

        // One C-O stretched to 1.78 A, the other held at 1.44 A: solve for
        // the O in the plane of the bond and the normal.
        const double along = (1.78 * 1.78 - 1.44 * 1.44 + best * best)
                             / (2.0 * best);
        const double up = std::sqrt(1.78 * 1.78 - along * along);
        s.atoms()[o].position =
            calango::core::Vec3{p0.x + along * ux, p0.y + along * uy, p0.z + up};
        cold = Builder::functionalGroupLabels(s);
        hot = Builder::functionalGroupLabels(s, Builder::kThermalBondTolerance);
        check(cold[o] != epoxide,
              "at the cold 1.15x tolerance (C-O cutoff 1.63 A) the stretched "
              "bond is gone and the group is no longer an epoxide -- the "
              "recolouring a thermal frame used to get");
        check(hot[o] == epoxide
                  && (!hostsLabelled
                      || (hot[0] == epoxide && hot[partner] == epoxide)),
              "at kThermalBondTolerance (1.3x: 1.85 A) it is still the "
              "epoxide it is, hosts included");
        check(Builder::kThermalBondTolerance > Builder::kColdBondTolerance
                  && Builder::kColdBondTolerance == 1.15,
              "the cold default is the application-wide 1.15x, untouched for "
              "built and relaxed geometries");
    }

    std::printf(failures == 0 ? "\nAll graphene oxide checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
