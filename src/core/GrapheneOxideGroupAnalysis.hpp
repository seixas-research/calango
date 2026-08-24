#pragma once

#include "core/GrapheneOxideBuilder.hpp"
#include "core/Structure.hpp"

#include <array>
#include <string>
#include <vector>

namespace calango::core {

/// One functional-group census + geometric-distortion snapshot of a single
/// Graphene Oxide Build or trajectory frame — the GO Functional Group
/// Analysis module's core computation, run once per structure (a single
/// build, or once per frame of a GO/MCMD or GO/MC-Opt trajectory).
///
/// Classification always comes fresh from core::GrapheneOxideBuilder::
/// findFunctionalGroups() — the SAME classifier the builder, both Monte
/// Carlo modules (GO/MCMD, GO/MC-Opt) and the
/// viewport's functional-group Cast already use — never from a persisted
/// "go_group" field, even when one is present: the geometric distributions
/// below (bond lengths, angles) have to be measured from THIS frame's actual
/// atom positions regardless, and GrapheneOxideBuilder::build()'s own
/// contract guarantees "go_group" already agrees with
/// findFunctionalGroups() exactly — so there is no second definition of
/// "which carbon is which" to reconcile, and no persisted field this
/// analysis needs to trust or fall back from.
struct GrapheneOxideGroupAnalysis {
    struct GroupCount {
        int instances = 0;      ///< placed group instances (an antiposition pair = 2)
        int surfaceCarbons = 0; ///< framework carbons this group type occupies
    };
    /// Indexed by core::GrapheneOxideBuilder::Group.
    std::array<GroupCount, GrapheneOxideBuilder::kGroupCount> groups{};
    int pristineCarbons = 0;  ///< framework carbons in no group
    int frameworkCarbons = 0; ///< pristine + every group's surfaceCarbons
    /// Basal groups only (epoxide, hydroxyl): how many of their oxygens sit
    /// above vs. below the sheet — the sheet is assumed to lie in the xy
    /// plane, z the out-of-plane axis, which is true of every structure this
    /// application's Graphene Oxide Builder, GO/MCMD or GO/MC-Opt produces
    /// (neither
    /// ever rotates or translates the sheet as a whole). Edge groups (which
    /// lie IN the plane) are not counted here at all.
    int abovePlane = 0;
    int belowPlane = 0;
    /// Antiposition pairs found via core::GrapheneOxideBuilder::
    /// findAntipositionPairs() — 0 when none exist, same meaning as an
    /// unfunctionalized build, not "field absent".
    int antipositionPairs = 0;

    /// Surface concentration: instances of `g` over frameworkCarbons. 0 when
    /// frameworkCarbons == 0 (no framework at all — a pathological input).
    double surfaceConcentration(GrapheneOxideBuilder::Group g) const;
    /// Pristine sp2 carbon's own share of the framework.
    double pristineFraction() const;

    // -- Geometric distributions ---------------------------------------
    // One labelled population of scalar samples (a bond-length or angle
    // distribution) per environment — e.g. "C-C (pristine)" vs.
    // "C-C (functionalized-adjacent)". Empty when the structure has no
    // instance of that environment; the caller decides whether an empty
    // distribution is worth plotting.
    struct Distribution {
        std::string label;
        std::vector<double> samples; ///< A (bond lengths) or degrees (angles)
    };
    /// C-C bonds between two FRAMEWORK carbons, resolved by whether either
    /// endpoint is functionalized — the sp3-distortion signal.
    std::vector<Distribution> ccBondLengths;
    /// C-C-C angle at a framework carbon with >= 2 framework-carbon
    /// neighbours, resolved by whether the CENTER carbon is functionalized.
    std::vector<Distribution> cccAngles;
    /// C-O-C angle at an epoxide's bridging oxygen. One distribution
    /// ("Epoxide"), empty if the structure has none.
    std::vector<Distribution> cocAngles;
    /// C-O-H angle at a hydroxyl's or a carboxyl's acidic oxygen — both
    /// groups carry an explicit hydrogen (see
    /// core::GrapheneOxideBuilder::Group's own doc comment); one
    /// distribution per kind that has at least one instance.
    std::vector<Distribution> cohAngles;
    /// Group kinds present in the structure whose C-O-H analysis could not
    /// run because that kind has no explicit hydrogen on its oxygen —
    /// carbonyl, always, by chemistry (=O has no H). Reported rather than
    /// silently omitted, per the module's own requirement.
    std::vector<std::string> skippedForNoHydrogen;
};

/// Compute the census + geometric distributions for one structure. `armCutoff`
/// bounds the PBC minimum-image search every angle/bond-length measurement
/// uses (see core::angleBetween()/minimumImageVector()) — 2.5 A is generous
/// for any bond this application places.
GrapheneOxideGroupAnalysis analyzeGrapheneOxideGroups(
    const Structure& structure, double armCutoff = 2.5);

} // namespace calango::core
