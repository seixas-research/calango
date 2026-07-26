#pragma once

#include "core/Structure.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace calango::core {

/// Graphene oxide generation: a graphene sheet decorated with oxygen-bearing
/// functional groups at target coverages.
///
/// Graphene oxide has no single structure. It is a non-stoichiometric,
/// disordered material whose composition varies with synthesis route, and the
/// accepted picture (Lerf-Klinowski) is a basal plane carrying epoxides and
/// hydroxyls with carboxyls and carbonyls at the edges and defect sites. What a
/// generator can honestly produce is therefore a *representative sample* from
/// that family at a requested composition, not "the" structure — so this class
/// reports exactly which sites it decorated and lets the caller reseed.
///
/// The one hard constraint enforced is chemical: a carbon atom cannot host two
/// functional groups at once. Each carbon has a single out-of-plane valence
/// once it rehybridizes to sp3, and stacking two groups on it would produce a
/// pentavalent carbon — a structure that is not merely improbable but
/// impossible. See the collision rules in the implementation.
class GrapheneOxideBuilder {
public:
    /// Which graphene cell to tile.
    enum class Lattice {
        Primitive,     ///< 2-atom rhombohedral cell, a = b = 2.46 Å, 60°
        Rectangular,   ///< 4-atom orthogonal cell, a = 2.46 Å, b = 4.26 Å
    };

    /// The oxygen-bearing groups this builder can attach.
    ///
    /// Each occupies a different number of carbons, which is what makes
    /// "coverage" ambiguous unless defined carefully — see `Config::coverage`.
    enum class Group : std::uint8_t {
        Epoxide,   ///< bridging -O- across a C-C bond: TWO carbons, both sp3
        Hydroxyl,  ///< -OH on one carbon, above or below the plane
        Carboxyl,  ///< -COOH on one carbon; adds a carbon of its own
        Carbonyl,  ///< =O on one carbon
    };

    struct Config {
        Lattice lattice = Lattice::Rectangular;
        int supercell[2] = {4, 4};

        /// Fraction of BASAL CARBONS consumed by each group, in [0, 1].
        ///
        /// Defined against the carbon count rather than the group count so the
        /// numbers add up to something meaningful: an epoxide consumes two
        /// carbons and a hydroxyl one, so 0.10 epoxide + 0.10 hydroxyl means
        /// 20 % of carbons are functionalized, not "20 % groups". Requests that
        /// together exceed the available carbons are honored in the order
        /// listed and then truncated, with the shortfall reported.
        double coverage[4] = {0.0, 0.0, 0.0, 0.0};

        /// Deterministic seed. The same seed and configuration reproduce the
        /// same structure exactly — a generated structure nobody can regenerate
        /// is not a result.
        std::uint32_t seed = 0;

        /// Attach groups to both faces of the sheet. Real graphene oxide is
        /// decorated on both sides; restricting to one produces an artificial
        /// dipole across the sheet.
        bool bothFaces = true;

        double coverageFor(Group g) const
        {
            return coverage[static_cast<std::size_t>(g)];
        }
        void setCoverage(Group g, double value)
        {
            coverage[static_cast<std::size_t>(g)] = value;
        }
    };

    /// What was actually built, as distinct from what was asked for.
    struct Report {
        int carbonCount = 0;          ///< basal carbons before functionalization
        int placed[4] = {0, 0, 0, 0}; ///< groups of each kind actually attached
        int requested[4] = {0, 0, 0, 0};
        int functionalizedCarbons = 0;
        /// Human-readable account of any shortfall, empty when every request
        /// was met. Never silently discarded — an unreported shortfall means
        /// the user believes they have a composition they do not have.
        std::string note;

        double carbonToOxygenRatio() const;
        int placedFor(Group g) const
        {
            return placed[static_cast<std::size_t>(g)];
        }
    };

    /// Build the decorated sheet. Returns the structure; `report` receives the
    /// accounting.
    static Structure build(const Config& config, Report* report = nullptr);

    /// The clean graphene sheet alone, before functionalization — the same
    /// lattice and supercell the full build starts from.
    static Structure pristine(const Config& config);

    static const char* name(Group group);
};

} // namespace calango::core
