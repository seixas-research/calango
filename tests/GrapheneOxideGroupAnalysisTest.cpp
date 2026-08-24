// GO Functional Group Analysis test.
//
// The geometric checks below deliberately do NOT reference
// GrapheneOxideBuilder's private geometry constants (kCO_epoxide, kOH, ...) --
// they are not part of the public API, and hardcoding a copy of them would
// only prove the analysis module agrees with a private implementation detail
// it might drift from unnoticed. Instead, each expected angle is derived
// INDEPENDENTLY from the built structure's own raw atom positions via the
// law of cosines, the same "recompute from coordinates, independently of the
// builder" philosophy tests/GrapheneOxideTest.cpp already uses throughout.

#include "core/GrapheneOxideGroupAnalysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using calango::core::GrapheneOxideBuilder;
using calango::core::GrapheneOxideGroupAnalysis;
using calango::core::Structure;
using calango::core::Vec3;
using calango::core::analyzeGrapheneOxideGroups;
using Group = GrapheneOxideBuilder::Group;
using Config = GrapheneOxideBuilder::Config;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition)
        ++failures;
}

double periodicDistance(const Structure& s, const Vec3& a, const Vec3& b)
{
    const auto& v = s.cell().vectors();
    const double lx = v[0].x;
    const double ly = v[1].y;
    const double shear = v[1].x;
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    const double dz = a.z - b.z;
    if (s.cell().pbc()[0] || s.cell().pbc()[1]) {
        const double nj = std::round(dy / ly);
        dy -= nj * ly;
        dx -= nj * shear;
        dx -= std::round(dx / lx) * lx;
    }
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// The angle at vertex `o` between `a` and `b`, from the three pairwise
/// distances alone (law of cosines) -- independent of
/// core::angleBetween()'s own vector-dot-product implementation, so a bug
/// shared by both would not hide.
double lawOfCosinesAngle(const Structure& s, int o, int a, int b)
{
    const auto& atoms = s.atoms();
    const double oa = periodicDistance(
        s, atoms[static_cast<std::size_t>(o)].position,
        atoms[static_cast<std::size_t>(a)].position);
    const double ob = periodicDistance(
        s, atoms[static_cast<std::size_t>(o)].position,
        atoms[static_cast<std::size_t>(b)].position);
    const double ab = periodicDistance(
        s, atoms[static_cast<std::size_t>(a)].position,
        atoms[static_cast<std::size_t>(b)].position);
    const double cosine =
        std::clamp((oa * oa + ob * ob - ab * ab) / (2.0 * oa * ob), -1.0, 1.0);
    return std::acos(cosine) * 180.0 / M_PI;
}

/// The single atom of `element` closest to any other atom already placed --
/// used to find "the epoxide's oxygen" etc. in a structure built with
/// exactly one instance of a group, without assuming atom ordering.
int onlyAtomOf(const Structure& s, int element)
{
    int found = -1;
    int count = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s.atoms()[i].atomicNumber == element) {
            found = static_cast<int>(i);
            ++count;
        }
    }
    return count == 1 ? found : -1;
}

std::vector<int> bondedFramework(const Structure& s, int atom, double cutoff)
{
    std::vector<int> hosts;
    const auto& atoms = s.atoms();
    const Vec3& atomPos = atoms[static_cast<std::size_t>(atom)].position;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (static_cast<int>(i) == atom)
            continue;
        if (atoms[i].atomicNumber != 6)
            continue;
        if (periodicDistance(s, atomPos, atoms[i].position) < cutoff)
            hosts.push_back(static_cast<int>(i));
    }
    return hosts;
}

const GrapheneOxideGroupAnalysis::Distribution* findDistribution(
    const std::vector<GrapheneOxideGroupAnalysis::Distribution>& list,
    const std::string& label)
{
    for (const auto& d : list)
        if (d.label == label)
            return &d;
    return nullptr;
}

} // namespace

int main()
{
    std::printf("Single epoxide -- known C-O-C angle, two sp3 carbons:\n");
    {
        // A small periodic sheet, dosed for exactly one epoxide: 2x2
        // rectangular supercell = 16 carbons; coverage 0.10 -> round(0.10 *
        // 16 / 2) = 1.
        Config config;
        config.supercell[0] = config.supercell[1] = 2;
        config.setCoverage(Group::Epoxide, 0.10);
        config.seed = 1;
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);
        check(report.placedFor(Group::Epoxide) == 1,
              "exactly one epoxide was placed");

        const int oxygen = onlyAtomOf(s, 8);
        check(oxygen >= 0, "exactly one oxygen atom exists");
        const std::vector<int> hosts = bondedFramework(s, oxygen, 1.6);
        check(hosts.size() == 2, "the oxygen bridges exactly two carbons");

        const auto analysis = analyzeGrapheneOxideGroups(s);
        check(analysis.groups[static_cast<std::size_t>(Group::Epoxide)]
                      .instances
                  == 1,
              "the analysis counts exactly one epoxide instance");
        check(analysis.groups[static_cast<std::size_t>(Group::Epoxide)]
                      .surfaceCarbons
                  == 2,
              "and exactly two sp3 (host) carbons for it");

        if (oxygen >= 0 && hosts.size() == 2) {
            const double expected =
                lawOfCosinesAngle(s, oxygen, hosts[0], hosts[1]);
            const auto* dist = findDistribution(analysis.cocAngles, "C-O-C (epoxide)");
            check(dist != nullptr && dist->samples.size() == 1,
                  "the analysis reports exactly one C-O-C sample");
            if (dist && dist->samples.size() == 1)
                check(std::abs(dist->samples[0] - expected) < 0.05,
                      "and it matches the law-of-cosines angle from raw coordinates");
            // Physically sane: an epoxide's C-O-C is well below the sp3
            // tetrahedral angle (109.5 deg) and well above zero -- a sanity
            // bound independent of the exact geometry constants.
            check(expected > 45.0 && expected < 75.0,
                  "and the angle itself is in the physically expected range "
                  "for a strained three-membered epoxide ring");
        }
    }

    std::printf("Single hydroxyl -- known C-O-H angle:\n");
    {
        Config config;
        config.supercell[0] = config.supercell[1] = 3;
        config.setCoverage(Group::Hydroxyl, 0.03); // round(0.03*36) = 1
        config.seed = 2;
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);
        check(report.placedFor(Group::Hydroxyl) == 1,
              "exactly one hydroxyl was placed");

        const int oxygen = onlyAtomOf(s, 8);
        const int hydrogen = onlyAtomOf(s, 1);
        check(oxygen >= 0 && hydrogen >= 0,
              "exactly one oxygen and one hydrogen exist");
        const std::vector<int> hosts = bondedFramework(s, oxygen, 1.7);
        check(hosts.size() == 1, "the oxygen sits on exactly one carbon");

        const auto analysis = analyzeGrapheneOxideGroups(s);
        check(analysis.groups[static_cast<std::size_t>(Group::Hydroxyl)]
                      .instances
                  == 1,
              "the analysis counts exactly one hydroxyl instance");

        if (oxygen >= 0 && hydrogen >= 0 && hosts.size() == 1) {
            const double expected =
                lawOfCosinesAngle(s, oxygen, hosts[0], hydrogen);
            const auto* dist =
                findDistribution(analysis.cohAngles, "C-O-H (hydroxyl)");
            check(dist != nullptr && dist->samples.size() == 1,
                  "the analysis reports exactly one C-O-H sample");
            if (dist && dist->samples.size() == 1)
                check(std::abs(dist->samples[0] - expected) < 0.1,
                      "and it matches the law-of-cosines angle from raw coordinates");
            // Close to the tetrahedral angle, as an sp3 -OH standing off an
            // sp3 carbon should be -- another geometry-constant-independent
            // sanity bound.
            check(expected > 95.0 && expected < 120.0,
                  "and the angle itself is near-tetrahedral, as expected for "
                  "an sp3 hydroxyl");
        }
    }

    std::printf("Carbonyl has no O-H -- reported as skipped, not silently absent:\n");
    {
        Config config = Config{};
        config.base = GrapheneOxideBuilder::Base::Nanoflake;
        config.flakeIndex = 3;
        config.setCoverage(Group::Carbonyl, 0.3);
        config.seed = 3;
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);
        check(report.placedFor(Group::Carbonyl) > 0,
              "at least one carbonyl was placed");

        const auto analysis = analyzeGrapheneOxideGroups(s);
        bool mentionsCarbonyl = false;
        for (const auto& note : analysis.skippedForNoHydrogen)
            if (note.find("carbonyl") != std::string::npos)
                mentionsCarbonyl = true;
        check(mentionsCarbonyl,
              "the analysis explicitly reports skipping carbonyl's C-O-H");
        bool anyCarbonylCoh = false;
        for (const auto& dist : analysis.cohAngles)
            if (dist.label.find("carbonyl") != std::string::npos)
                anyCarbonylCoh = true;
        check(!anyCarbonylCoh,
              "and carbonyl contributes no C-O-H samples at all");
    }

    std::printf("Group census matches the builder's own report:\n");
    {
        Config config = Config{};
        config.base = GrapheneOxideBuilder::Base::Nanoflake;
        config.flakeIndex = 4;
        config.setCoverage(Group::Epoxide, 0.15);
        config.setCoverage(Group::Hydroxyl, 0.15);
        config.setCoverage(Group::Carboxyl, 0.3);
        config.setCoverage(Group::Carbonyl, 0.25);
        config.seed = 4;
        GrapheneOxideBuilder::Report report;
        const Structure s = GrapheneOxideBuilder::build(config, &report);
        const auto analysis = analyzeGrapheneOxideGroups(s);

        bool allMatch = true;
        for (std::size_t g = 0; g < GrapheneOxideBuilder::kGroupCount; ++g) {
            const auto group = static_cast<Group>(g);
            allMatch = allMatch
                && analysis.groups[g].instances == report.placedFor(group);
        }
        check(allMatch,
              "every group instance count matches Report::placedFor()");

        check(analysis.frameworkCarbons == report.carbonCount,
              "framework carbon count matches the report's carbon count");
        check(analysis.pristineCarbons
                  == report.carbonCount - report.functionalizedCarbons,
              "pristine carbon count is framework minus functionalized");
        check(std::abs(analysis.pristineFraction()
                       - static_cast<double>(analysis.pristineCarbons)
                             / analysis.frameworkCarbons)
                  < 1e-12,
              "pristineFraction() matches its own definition");
        check(std::abs(analysis.surfaceConcentration(Group::Epoxide)
                       - static_cast<double>(report.placedFor(Group::Epoxide))
                             / analysis.frameworkCarbons)
                  < 1e-12,
              "surfaceConcentration() matches its own definition");

        // Bond-length and angle distributions actually populated: this
        // structure has both pristine and functionalized C-C bonds, both
        // functionalized-center and pristine-center C-C-C angles.
        check(findDistribution(analysis.ccBondLengths, "C-C (pristine)") != nullptr,
              "pristine C-C bond distribution is populated");
        check(findDistribution(analysis.ccBondLengths,
                               "C-C (functionalized-adjacent)")
                  != nullptr,
              "functionalized-adjacent C-C bond distribution is populated");
        check(findDistribution(analysis.cccAngles, "C-C-C (pristine center)")
                  != nullptr,
              "pristine-center C-C-C angle distribution is populated");
        check(findDistribution(analysis.cccAngles,
                               "C-C-C (functionalized center)")
                  != nullptr,
              "functionalized-center C-C-C angle distribution is populated");
        check(findDistribution(analysis.cohAngles, "C-O-H (carboxyl)") != nullptr,
              "carboxyl's own C-O-H distribution is populated");
    }

    std::printf("Antiposition pairs are counted:\n");
    {
        Config paired;
        paired.supercell[0] = paired.supercell[1] = 6;
        paired.dosing = GrapheneOxideBuilder::Dosing::ExplicitCoverage;
        paired.setCoverage(Group::Hydroxyl, 0.3);
        paired.hydroxylAntiposition = true;
        paired.seed = 5;
        const Structure pairedStructure = GrapheneOxideBuilder::build(paired);
        const auto pairedAnalysis = analyzeGrapheneOxideGroups(pairedStructure);
        check(pairedAnalysis.antipositionPairs > 0,
              "an antiposition build reports at least one pair");
        check(2 * pairedAnalysis.antipositionPairs
                  == pairedAnalysis.groups[static_cast<std::size_t>(Group::Hydroxyl)]
                         .instances,
              "every hydroxyl is accounted for by exactly one pair (2x pairs "
              "== hydroxyl instances)");

        // NOT asserted as exactly zero: findAntipositionPairs() has only
        // geometry to go on (see its own doc comment / GrapheneOxideTest.cpp's
        // "classifyFromBonding() fallback" block for the same limitation), so
        // at high enough density two independently-placed hydroxyls CAN
        // coincidentally land on bonded carbons with opposite faces. What
        // must hold is the comparative claim antiposition exists for: the
        // deliberately-paired build has a hydroxyl population that is
        // ENTIRELY paired (checked above), which an independent placement at
        // the same density has no reason to reproduce exactly.
        Config unpaired = paired;
        unpaired.hydroxylAntiposition = false;
        const Structure unpairedStructure = GrapheneOxideBuilder::build(unpaired);
        const auto unpairedAnalysis = analyzeGrapheneOxideGroups(unpairedStructure);
        const int unpairedHydroxyls =
            unpairedAnalysis.groups[static_cast<std::size_t>(Group::Hydroxyl)]
                .instances;
        check(2 * unpairedAnalysis.antipositionPairs <= unpairedHydroxyls,
              "an independently-placed build's pairing never exceeds what "
              "its own hydroxyl count could support");
        check(unpairedAnalysis.antipositionPairs
                  < pairedAnalysis.antipositionPairs,
              "and finds strictly fewer pairs than the deliberately-paired "
              "build at the same density");
    }

    std::printf("Trajectory: per-frame analysis is stateless and self-consistent:\n");
    {
        // Two frames from the SAME lattice/coverage, different seeds -- each
        // ends up with its groups on a different set of carbons, standing in
        // for consecutive MCMD trajectory frames without hand-editing any
        // geometry (same technique tests/GrapheneOxideTest.cpp's own
        // "per-frame classification tracks a relocated group" block uses).
        Config config;
        config.supercell[0] = config.supercell[1] = 6;
        config.setCoverage(Group::Epoxide, 0.15);
        config.setCoverage(Group::Hydroxyl, 0.15);

        std::vector<Structure> frames;
        for (std::uint32_t seed : {10u, 11u, 12u}) {
            config.seed = seed;
            frames.push_back(GrapheneOxideBuilder::build(config));
        }

        std::vector<GrapheneOxideGroupAnalysis> analyses;
        for (const Structure& frame : frames)
            analyses.push_back(analyzeGrapheneOxideGroups(frame));

        // Each frame's own analysis must match a FRESH, independent
        // recomputation on that same frame -- calling analyzeGrapheneOxide
        // Groups() for frame B must not have disturbed frame A's already-
        // computed result, and analyzing frames out of order must give the
        // same answer analyzing them in order did.
        bool consistent = true;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const auto recomputed = analyzeGrapheneOxideGroups(frames[i]);
            for (std::size_t g = 0; g < GrapheneOxideBuilder::kGroupCount; ++g)
                consistent = consistent
                    && recomputed.groups[g].instances == analyses[i].groups[g].instances
                    && recomputed.groups[g].surfaceCarbons
                        == analyses[i].groups[g].surfaceCarbons;
            consistent = consistent
                && recomputed.frameworkCarbons == analyses[i].frameworkCarbons
                && recomputed.pristineCarbons == analyses[i].pristineCarbons;
        }
        check(consistent,
              "each frame's analysis is reproducible and independent of the "
              "others");

        // The three frames were built with the same coverage but different
        // seeds, so at this density it is overwhelmingly likely (though not
        // logically guaranteed) that at least one geometric distribution
        // differs in sample count between frames -- a cheap signal that the
        // analysis is actually reading each frame's OWN atoms rather than
        // returning a cached result from the first call.
        bool anyDiffers = false;
        for (std::size_t i = 1; i < analyses.size(); ++i) {
            const auto* a = findDistribution(analyses[0].ccBondLengths,
                                             "C-C (functionalized-adjacent)");
            const auto* b = findDistribution(analyses[i].ccBondLengths,
                                             "C-C (functionalized-adjacent)");
            if ((a == nullptr) != (b == nullptr))
                anyDiffers = true;
            else if (a && b && a->samples.size() != b->samples.size())
                anyDiffers = true;
        }
        check(anyDiffers || analyses.size() < 2,
              "different seeds produce different per-frame geometry (not a "
              "cached, frame-0-only result)");
    }

    std::printf(failures == 0
                    ? "\nAll GO Functional Group Analysis checks passed.\n"
                    : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
