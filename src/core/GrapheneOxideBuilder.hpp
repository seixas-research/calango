#pragma once

#include "core/Structure.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace calango::core {

/// Graphene oxide generation: a carbon substrate — an infinite periodic sheet
/// or a finite flake — decorated with oxygen-bearing functional groups.
///
/// Graphene oxide has no single structure. It is a non-stoichiometric,
/// disordered material whose composition varies with synthesis route, and the
/// accepted picture (Lerf-Klinowski) is a basal plane carrying epoxides and
/// hydroxyls with carboxyls, carbonyls and phenolic hydroxyls at the edges and
/// defect sites. What a generator can honestly produce is therefore a
/// *representative sample* from that family at a requested composition, not
/// "the" structure — so this class reports exactly which sites it decorated and
/// lets the caller reseed.
///
/// Two hard constraints are enforced, both chemical rather than statistical:
///
///  1. **One group per carbon.** A carbon atom cannot host two functional
///     groups at once. It has a single out-of-plane valence once it
///     rehybridizes to sp3, and an edge carbon has a single substitutable
///     hydrogen; stacking two groups on either would produce a pentavalent
///     carbon — not merely improbable but impossible. Every placement goes
///     through one reservation table, and a reserved carbon leaves the pool
///     permanently.
///
///  2. **Basal chemistry stays basal, edge chemistry stays at the edge.** A
///     carbon is classified from its own coordination: three carbon neighbours
///     makes it *basal* (sp2, rehybridizes to sp3 under an epoxide or a
///     hydroxyl standing off the plane); fewer than three makes it an *edge*
///     carbon (it carries a substitutable hydrogen, and its chemistry is the
///     in-plane sp2 chemistry of carbonyls, carboxyls and phenols). A periodic
///     sheet has no edge carbons at all, so edge groups have no site pool
///     there — which is reported rather than quietly redirected onto the basal
///     plane.
class GrapheneOxideBuilder {
public:
    /// Which carbon skeleton to decorate.
    enum class Base : std::uint8_t {
        PeriodicSheet, ///< tiled graphene: infinite, therefore edgeless
        /// Finite hexagonal, all-armchair, D6h graphene nanoflake of index m:
        /// C(6m²)H(6m). The family is a continuum indexed by m, not one
        /// molecule — m = 3 happens to be circumcoronene, but nothing about the
        /// generator is specific to it.
        Nanoflake,
    };

    /// Which graphene cell to tile. Applies to Base::PeriodicSheet only.
    enum class Lattice : std::uint8_t {
        Primitive,     ///< 2-atom rhombohedral cell, a = b = 2.46 Å, 60°
        Rectangular,   ///< 4-atom orthogonal cell, a = 2.46 Å, b = 4.26 Å
    };

    /// The oxygen-bearing groups this builder can attach.
    ///
    /// Each occupies a different number of carbons and delivers a different
    /// number of oxygens, which is what makes both "coverage" and "C/O ratio"
    /// ambiguous unless defined carefully — see `Config::coverage` and
    /// `Config::targetCarbonToOxygen`.
    ///
    /// Edge chemistry is carboxyl and carbonyl ONLY.
    ///
    /// A phenolic edge -OH used to be offered as a third edge group and has
    /// been removed. The two that remain are the ones oxidative exfoliation
    /// actually produces at a flake rim in quantity, and they are the pair a
    /// C/O or O/C target is meaningfully split between; a third edge group with
    /// its own weight made the edge composition under-determined without adding
    /// chemistry anyone was asking the generator for.
    ///
    /// Note this is a statement about what is BUILT. A structure loaded from a
    /// file may well carry phenolic hydroxyls, and findFunctionalGroups() still
    /// finds them — reported as Hydroxyl, which is what they are; the host
    /// carbon's coordination (and the "edge" scalar field) says where it sits.
    enum class Group : std::uint8_t {
        Epoxide,       ///< BASAL: bridging -O- across a C-C bond; TWO carbons, both sp3
        Hydroxyl,      ///< BASAL: -OH standing off the plane on one sp3 carbon
        Carboxyl,      ///< EDGE: -COOH replacing an edge H; brings a carbon of its own
        Carbonyl,      ///< EDGE: =O replacing an edge H, in the plane (quinone-like)
    };
    static constexpr std::size_t kGroupCount = 4;

    /// Where a carbon sits chemically — and therefore which groups it can host.
    enum class Region : std::uint8_t {
        Basal,  ///< three carbon neighbours; sp2 → sp3 out-of-plane chemistry
        Edge,   ///< fewer than three; carries a substitutable H, sp2 chemistry
    };

    /// How the amount of oxygen is decided.
    enum class Dosing : std::uint8_t {
        /// Per-group coverages, each a fraction of the carbons available to
        /// that group's region.
        ExplicitCoverage,
        /// Groups are drawn and placed until the whole structure reaches a
        /// requested C/O ratio — the quantity an experiment actually measures.
        TargetRatio,
        /// The basal plane and the rim are dosed INDEPENDENTLY: an oxygen
        /// budget per basal carbon, and separately a fraction of the rim that
        /// carries a group at all.
        ///
        /// Why this exists next to TargetRatio rather than replacing it. A
        /// single O/C with a basal/edge split ties the two together: asking for
        /// a more oxidized basal plane silently moves oxygen off the rim, and
        /// asking for a bare rim silently makes the basal plane more oxidized,
        /// because the split is a partition of one budget. That is the wrong
        /// model for graphene oxide, where the two are set by different
        /// chemistry — basal epoxide/hydroxyl coverage tracks the oxidant
        /// exposure, while rim carboxyl/carbonyl content tracks the exfoliation
        /// and the workup, and a flake can be heavily oxidized on one and clean
        /// on the other. TargetRatio remains the right mode when what you have
        /// is an XPS composition for the whole structure.
        DecoupledRegions,
    };

    struct Config {
        Base base = Base::PeriodicSheet;

        // -- Base::PeriodicSheet ------------------------------------------
        Lattice lattice = Lattice::Rectangular;
        int supercell[2] = {4, 4};

        // -- Base::Nanoflake ----------------------------------------------
        /// Index m of the nanoflake: C(6m²)H(6m), built from 3m(m−1)+1 fused
        /// rings. Clamped to [1, kMaxFlakeIndex].
        ///
        /// m is the size knob, and the sizes it walks through happen to
        /// include several molecules with names of their own — benzene at
        /// m = 1, coronene at 2, circumcoronene at 3 — which flakeName()
        /// reports. Those names label points on the series; they do not define
        /// it.
        int flakeIndex = 3;
        /// Cap every edge carbon that did NOT receive a functional group with
        /// a hydrogen. Off leaves radical dangling bonds, which is a
        /// deliberate choice for edge-state studies and a mistake otherwise.
        bool hydrogenTerminateEdges = true;

        Dosing dosing = Dosing::ExplicitCoverage;

        /// Dosing::ExplicitCoverage — fraction of the carbons IN THAT GROUP'S
        /// REGION which the group consumes, in [0, 1].
        ///
        /// Defined against the carbon count rather than the group count so the
        /// numbers add up to something meaningful: an epoxide consumes two
        /// carbons and a hydroxyl one, so 0.10 epoxide + 0.10 hydroxyl means
        /// 20 % of the basal carbons are functionalized, not "20 % groups".
        /// Edge coverages are fractions of the EDGE carbons, so 0.5 carboxyl
        /// means half the rim. Requests that together exceed the available
        /// carbons are honored in the order listed and then truncated, with
        /// the shortfall reported.
        double coverage[kGroupCount] = {0.0, 0.0, 0.0, 0.0};

        /// Dosing::TargetRatio — relative propensity of each group within its
        /// region. Only the ratios matter; the absolute amount is set by
        /// `targetCarbonToOxygen`. A weight of 0 excludes the group.
        ///
        /// Epoxide : hydroxyl defaults to 1:2 — twice as many hydroxyls as
        /// epoxides among the basal groups, the ordinary case for oxidized
        /// graphene (see `basalHydroxylShare`, which states the same default
        /// for Dosing::DecoupledRegions). The edge weights are untouched.
        double weight[kGroupCount] = {1.0, 2.0, 1.0, 1.0};

        /// Dosing::TargetRatio — target C/O of the FINISHED structure: every
        /// carbon (framework plus the carboxyl carbons the groups bring with
        /// them) over every oxygen. That is the composition XPS reports, so it
        /// is the one worth targeting; heavily oxidized graphene oxide sits
        /// near 2, mildly oxidized near 4-10. Lower means more oxygen.
        double targetCarbonToOxygen = 4.0;

        /// Dosing::TargetRatio — share of the oxygen budget delivered by BASAL
        /// groups, in [0, 1]; the rest goes to the edges.
        ///
        /// Exactly 0 or 1 is categorical ("no basal chemistry" / "no edge
        /// chemistry") and is honored even if that means missing the target.
        /// Anything in between is a soft split: whichever region is furthest
        /// behind its share gets the next group, and one region carries on
        /// alone once the other runs out of sites.
        double basalOxygenShare = 0.7;

        // -- Dosing::DecoupledRegions -------------------------------------
        //
        // Two independent dials, each defined against ITS OWN carbon pool, so
        // moving one cannot move the other.

        /// Oxygen atoms per BASAL carbon delivered by basal groups, in
        /// [0, 0.5]. Not the structure's O/C: the rim's oxygen and the carbons
        /// carboxyls bring are excluded, precisely so that this number does not
        /// change when the rim setting does. 0.5 is the C2O ceiling — every
        /// oxygen needs two carbons to sit on.
        double basalOxygenToCarbon = 0.25;

        /// Fraction of the EDGE carbons carrying a functional group rather than
        /// a hydrogen, in [0, 1].
        ///
        /// Exactly 0 means the rim is strictly hydrogen-terminated: not "very
        /// little edge chemistry" but none, which is the state a
        /// hydrogen-passivated flake is actually in and the correct starting
        /// point for studying basal chemistry alone. As it rises, edge
        /// hydrogens are replaced by carboxyls and carbonyls in the proportion
        /// `carboxylShare` sets.
        double edgeOxidation = 0.0;

        /// Share of the EDGE oxygen delivered as carboxyl rather than carbonyl,
        /// in [0, 1]. 0 is all quinone-like =O; 1 is all -COOH.
        ///
        /// Stated in OXYGEN because that is what the composition is quoted in,
        /// and converted to a per-group propensity internally: a carboxyl
        /// carries two oxygens and a carbonyl one, so an even split of the
        /// oxygen is not an even split of the groups.
        double carboxylShare = 0.5;

        /// Hydrogen per oxygen on the basal plane — equivalently the hydroxyl
        /// share of the basal groups, since an epoxide brings one oxygen and no
        /// hydrogen while a hydroxyl brings one of each. 0 is all epoxide,
        /// 1 all hydroxyl. Defaults to 2/3: epoxide : hydroxyl = 1:2.
        double basalHydroxylShare = 2.0 / 3.0;

        /// Deterministic seed. The same seed and configuration reproduce the
        /// same structure exactly — a generated structure nobody can regenerate
        /// is not a result.
        std::uint32_t seed = 0;

        /// Attach basal groups to both faces of the sheet. Real graphene oxide
        /// is decorated on both sides; restricting to one produces an
        /// artificial dipole across the sheet. Edge groups lie in the plane and
        /// are unaffected.
        bool bothFaces = true;

        /// Force every hydroxyl onto a bonded PAIR of basal carbons — the same
        /// pool an epoxide draws from — one -OH standing off each carbon, on
        /// OPPOSITE faces: a trans-diol, not two independently sited -OH
        /// groups. This is the "antiposition" arrangement.
        ///
        /// Each successful placement therefore delivers TWO hydroxyls at once,
        /// never one: a request for an odd count under Dosing::ExplicitCoverage
        /// is rounded up by at most one, and under Dosing::TargetRatio the
        /// "closest reachable composition" search (which predicts one group's
        /// worth of oxygen per step) can overshoot by one hydroxyl's oxygen for
        /// the same reason. Both are reported like any other shortfall/overshoot
        /// this builder already tolerates elsewhere, not silently absorbed.
        ///
        /// The opposite-face requirement is what "anti" means, so a pair
        /// ignores `bothFaces` and always splits its own two carbons across
        /// both faces; `bothFaces` continues to govern epoxides, and any
        /// hydroxyl placed while this is off.
        bool hydroxylAntiposition = false;

        double coverageFor(Group g) const
        {
            return coverage[static_cast<std::size_t>(g)];
        }
        void setCoverage(Group g, double value)
        {
            coverage[static_cast<std::size_t>(g)] = value;
        }
        double weightFor(Group g) const
        {
            return weight[static_cast<std::size_t>(g)];
        }
        void setWeight(Group g, double value)
        {
            weight[static_cast<std::size_t>(g)] = value;
        }
    };

    /// What was actually built, as distinct from what was asked for.
    struct Report {
        /// Framework carbons before functionalization — the sheet or the bare
        /// flake. These are always the FIRST `carbonCount` atoms of the
        /// returned structure, in order.
        int carbonCount = 0;
        int basalCarbonCount = 0; ///< of those, three-coordinate carbons
        int edgeCarbonCount = 0;  ///< of those, under-coordinated carbons
        int placed[kGroupCount] = {0, 0, 0, 0};
        /// Dosing::ExplicitCoverage only — under Dosing::TargetRatio nothing
        /// was requested per group, and these stay zero.
        int requested[kGroupCount] = {0, 0, 0, 0};
        int functionalizedCarbons = 0;
        /// Every carbon in the finished structure: framework + carboxyl.
        int totalCarbonAtoms = 0;
        int oxygenAtoms = 0;
        int hydrogenAtoms = 0;
        /// Edge carbons capped with H rather than functionalized.
        int hydrogenTerminatedEdges = 0;
        /// Dosing::TargetRatio only: what was asked for, and whether the
        /// substrate had enough reactive sites to get there.
        double targetRatio = 0.0;
        bool targetReached = true;

        /// Dosing::DecoupledRegions only — each dial's request and what it
        /// actually achieved, reported SEPARATELY because the whole point of
        /// the mode is that the two are independent. A rim that ran out of
        /// room must not show up as a basal shortfall, and vice versa.
        int edgeGroupsRequested = 0;
        int edgeGroupsPlaced = 0;
        int basalOxygenRequested = 0;
        int basalOxygenPlaced = 0;
        /// Human-readable account of any shortfall, empty when every request
        /// was met. Never silently discarded — an unreported shortfall means
        /// the user believes they have a composition they do not have.
        std::string note;

        /// C/O of the finished structure: all carbons over all oxygens. 0 when
        /// no oxygen was placed (pristine carbon has no meaningful C/O).
        double carbonToOxygenRatio() const;
        int placedFor(Group g) const
        {
            return placed[static_cast<std::size_t>(g)];
        }
    };

    /// Build the decorated structure. Returns it; `report` receives the
    /// accounting.
    ///
    /// The result carries four per-atom scalar fields — the "Graphene Oxide
    /// Build" contract every downstream GO module (GO/MCMD, GO/MC-Opt, GO
    /// Functional
    /// Group Analysis, GO Pair Correlation) reads instead of re-deriving:
    ///
    ///  * "edge": 1 on every framework carbon classified as an edge carbon,
    ///    0 elsewhere.
    ///  * "go_group": the Group value (as an int, same encoding as
    ///    functionalGroupLabels()) for every atom belonging to a functional
    ///    group — its host carbon(s) included — and -1 for pristine
    ///    framework carbons, edge-terminating hydrogens and anything else.
    ///    This is exactly functionalGroupLabels()'s definition, persisted at
    ///    build time instead of recomputed from bonding on every call.
    ///  * "go_group_id": a non-negative integer, unique per placed group
    ///    INSTANCE and shared by every atom of that instance's cluster (the
    ///    group's own atoms plus its host carbon(s)) — so "every atom of
    ///    group #7" is a filter over this field rather than a re-run of
    ///    findFunctionalGroups(). -1 wherever "go_group" is -1.
    ///  * "go_pair_id": a non-negative integer shared by the TWO hydroxyl
    ///    instances placed together under Config::hydroxylAntiposition — the
    ///    antiposition pairing registry. -1 everywhere else, including
    ///    non-paired hydroxyls and every other group type.
    ///
    /// All four are ordinary Structure scalar fields, so they round-trip
    /// through .calproj and extxyz with no serializer changes, the same way
    /// "edge" already does.
    static Structure build(const Config& config, Report* report = nullptr);

    /// The clean substrate before functionalization: the bare sheet, or the
    /// nanoflake with its full complement of edge hydrogens (i.e. the parent
    /// hydrocarbon C(6m²)H(6m), not a bare carbon radical).
    static Structure pristine(const Config& config);

    // -- Finding the groups again, in a finished structure ------------------
    //
    // The builder knows what it placed, but that knowledge dies with the call.
    // A structure loaded from a file — someone else's graphene oxide, or one of
    // ours saved and reopened — carries no such record, and it is exactly as
    // much in need of being taken apart into its chemistry. So the groups are
    // re-derived from CONNECTIVITY, which both cases have.

    /// One functional group found in a structure: the group's own atoms
    /// together with the framework carbon(s) it is bonded to.
    ///
    /// The host carbon is part of the cluster on purpose. An epoxide oxygen
    /// floating on its own is not the thing anyone wants to select — the group
    /// IS the oxygen and the two carbons it rehybridized, and colouring the
    /// oxygen alone leaves the sp3 carbons drawn as though they were still part
    /// of the aromatic sheet.
    struct GroupCluster {
        Group kind;
        /// Atom indices into the structure the cluster was found in: the
        /// group's own atoms first, then its host carbon(s).
        std::vector<int> atoms;
    };

    /// Find the oxygen-bearing functional groups in `structure` from its
    /// bonding alone.
    ///
    /// Detection order matters and runs from the largest group down: a carboxyl
    /// contains a C=O and a C-OH, so looking for carbonyls or hydroxyls first
    /// would tear every carboxyl into two half-groups. Each atom joins at most
    /// one cluster; a group whose oxygen has already been claimed by a larger
    /// one is not reported twice.
    /// Bond tolerances for the classification below, as the multiple of the
    /// summed covalent radii within which two atoms count as bonded
    /// (Structure::detectBonds' own convention). kColdBondTolerance is the
    /// application-wide default and right for a built or relaxed geometry.
    /// kThermalBondTolerance is for a molecular-dynamics SNAPSHOT: measured
    /// per bond on a graphene oxide sheet under MACE-MP-0, an intact O-H at
    /// 300 K comes within 0.5 % of the 1.2x cutoff and crosses it at 400 K,
    /// and an intact epoxide C-O stretched past 1.15x at an instant read as
    /// "carbonyl" — two of six epoxides on one 335 K frame — which is what
    /// recoloured the per-frame Cast of a GO/MCMD or GO/MC-Opt run. A bond
    /// that has really
    /// gone is at 2 A or more within femtoseconds and clears either number.
    static constexpr double kColdBondTolerance = 1.15;
    static constexpr double kThermalBondTolerance = 1.3;

    static std::vector<GroupCluster> findFunctionalGroups(
        const Structure& structure, double bondTolerance = kColdBondTolerance);

    /// Per-atom group label: the Group value (as an int) for every atom that
    /// belongs to a functional group, its host carbon included, and -1 for the
    /// pristine framework, the terminating hydrogens and anything unrecognized.
    /// Index-aligned with `structure.atoms()`.
    static std::vector<int> functionalGroupLabels(
        const Structure& structure, double bondTolerance = kColdBondTolerance);

    /// Does `structure` carry a persisted Graphene Oxide Build classification
    /// ("go_group", index-aligned with its atoms)? The pre-flight check every
    /// downstream GO module runs before accepting a structure as input.
    static bool hasClassification(const Structure& structure);

    /// Compute "go_group" / "go_group_id" / "go_pair_id" from `structure`'s
    /// bonding alone, via findFunctionalGroups(), and write them onto it.
    ///
    /// The ONE fallback path for a structure that arrives without a
    /// classification already on it — whether that is a project saved before
    /// this contract existed, or graphene oxide imported from anywhere else
    /// entirely. Both go through the same code, so there is exactly one
    /// definition of "what counts as a group" no matter which door a
    /// structure came in through.
    ///
    /// Antiposition pairs are re-derived from geometry via
    /// findAntipositionPairs(). A structure that was never built with
    /// antiposition simply gets none — "go_pair_id" stays -1 throughout.
    ///
    /// Always overwrites any classification fields already present; call it
    /// only after hasClassification() has said there is none to trust.
    static void classifyFromBonding(Structure& structure);

    /// The antiposition pairs findable in `structure` from geometry alone,
    /// as index pairs into `clusters` (both indices refer to Hydroxyl
    /// clusters): two Hydroxyl clusters whose host carbons are bonded to
    /// each other and whose oxygens sit on opposite faces of that bond.
    ///
    /// Shared by classifyFromBonding() (which writes the pairing into
    /// "go_pair_id") and any analysis that needs the same pairing without
    /// touching the structure's scalar fields — one implementation of "what
    /// counts as an antiposition pair", not a copy per caller. `clusters`
    /// must be `findFunctionalGroups(structure)`'s own result — passed in
    /// rather than recomputed here, since most callers already have it.
    static std::vector<std::pair<int, int>> findAntipositionPairs(
        const Structure& structure, const std::vector<GroupCluster>& clusters);

    // -- Chemistry the UI must not duplicate -------------------------------

    static const char* name(Group group);
    /// Which region a group belongs to. This is a fact about the group's
    /// chemistry, not a policy knob.
    static Region region(Group group);
    /// Framework carbons one group consumes: 2 for an epoxide, 1 otherwise.
    static int carbonCost(Group group);
    /// Oxygen atoms one group delivers: 2 for a carboxyl, 1 otherwise.
    static int oxygensPerGroup(Group group);

    // -- The nanoflake family, C(6m²)H(6m) ---------------------------------

    static constexpr int kMaxFlakeIndex = 12;
    /// 6m² — carbons in generation m.
    static int flakeCarbonCount(int generation);
    /// 6m — edge (mono-hydrogenated) carbons in generation m.
    static int flakeEdgeCarbonCount(int generation);
    /// Trivial name of the molecule this index happens to be, e.g.
    /// "circumcoronene" for m = 3. Informative only: the generator is not
    /// specific to any of these, and the names run out long before m does.
    static const char* flakeName(int generation);
    /// Pristine formula, e.g. "C54H18".
    static std::string flakeFormula(int generation);
};

} // namespace calango::core
