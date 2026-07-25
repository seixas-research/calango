// Polymer builder test.
//
// Checks the chemistry of the generated cell rather than the builder's own
// bookkeeping: bond lengths and valence angles measured from the coordinates,
// the composition of the repeat unit, that a self-avoiding walk really does not
// self-overlap, and that the three tacticities are actually distinguishable.
//
// GUI-free, Python-free.

#include "core/PolymerBuilder.hpp"

#include "core/Element.hpp"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

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

std::map<std::string, int> composition(const Structure& s)
{
    std::map<std::string, int> counts;
    for (const Atom& atom : s.atoms())
        ++counts[atom.symbol()];
    return counts;
}

/// Closest approach between any two atoms that are not plausibly bonded.
double closestNonBonded(const Structure& s)
{
    double closest = 1e30;
    const auto& atoms = s.atoms();
    for (std::size_t i = 0; i < atoms.size(); ++i)
        for (std::size_t j = i + 1; j < atoms.size(); ++j) {
            const double d = (atoms[i].position - atoms[j].position).norm();
            if (d > 1.9) // beyond any bond in these chemistries
                closest = std::min(closest, d);
        }
    return closest;
}

} // namespace

int main()
{
    // -- Composition of the repeat unit --------------------------------------
    // Polyethylene is -[CH2-CH2]-, so N monomers give 2N carbons and 4N
    // hydrogens, plus the two end caps. Getting this wrong means the "polymer"
    // has the wrong stoichiometry however good it looks.
    std::printf("Polyethylene composition:\n");
    {
        PolymerBuilder::Params params;
        params.monomer = PolymerBuilder::Monomer::Polyethylene;
        params.degreeOfPolymerization = 10;
        params.chainCount = 1;
        params.conformation = PolymerBuilder::Conformation::Extended;
        params.endCap = PolymerBuilder::EndCap::Hydrogen;
        const auto result = PolymerBuilder::generate(params);
        const auto counts = composition(result.structure);
        check(result.chains == 1, "the chain was placed");
        check(counts.at("C") == 20, "2N backbone carbons");
        check(counts.at("H") == 42, "4N hydrogens + 2 end-cap H");
        checkClose(PolymerBuilder::monomerMassU(
                       PolymerBuilder::Monomer::Polyethylene),
                   28.054, 0.01, "C2H4 repeat-unit mass");
    }

    // -- Backbone geometry ----------------------------------------------------
    // The all-trans chain is the crystalline conformation: consecutive backbone
    // carbons at 1.54 A and the valence angle at 109.47 degrees.
    std::printf("Extended (all-trans) backbone geometry:\n");
    {
        PolymerBuilder::Params params;
        params.monomer = PolymerBuilder::Monomer::Polyethylene;
        params.degreeOfPolymerization = 12;
        params.conformation = PolymerBuilder::Conformation::Extended;
        const auto result = PolymerBuilder::generate(params);

        // Backbone carbons are emitted first in each site group; walk them by
        // finding the C atoms that are 1.54 A apart in sequence.
        const auto& atoms = result.structure.atoms();
        std::vector<Vec3> backbone;
        for (const Atom& atom : atoms)
            if (atom.atomicNumber == 6)
                backbone.push_back(atom.position);

        double maxBondError = 0.0;
        for (std::size_t i = 1; i < backbone.size(); ++i)
            maxBondError = std::max(
                maxBondError, std::abs((backbone[i] - backbone[i - 1]).norm() - 1.54));
        check(maxBondError < 1e-6, "every C-C backbone bond is 1.54 A");

        double maxAngleError = 0.0;
        for (std::size_t i = 1; i + 1 < backbone.size(); ++i) {
            const Vec3 a = backbone[i - 1] - backbone[i];
            const Vec3 b = backbone[i + 1] - backbone[i];
            const double angle =
                std::acos(a.dot(b) / (a.norm() * b.norm())) * 180.0 / M_PI;
            maxAngleError = std::max(maxAngleError, std::abs(angle - 109.47));
        }
        check(maxAngleError < 1e-3, "every C-C-C valence angle is tetrahedral");

        // An all-trans chain is extended: end-to-end distance should be close
        // to the contour length, not a compact coil.
        const double endToEnd = (backbone.back() - backbone.front()).norm();
        const double contour = 1.54 * (backbone.size() - 1);
        check(endToEnd > 0.7 * contour,
              "the all-trans chain really is extended (not coiled)");
        std::printf("       end-to-end = %.2f A of %.2f A contour\n", endToEnd,
                    contour);
    }

    // -- Self-avoiding vs random walk -----------------------------------------
    // The SAW's defining property is that it does not run through itself. A
    // random walk has no such guarantee, and using one for an amorphous cell is
    // how overlapping atoms get into a "finished" structure.
    std::printf("Self-avoiding walk:\n");
    {
        PolymerBuilder::Params params;
        params.monomer = PolymerBuilder::Monomer::Polyethylene;
        params.degreeOfPolymerization = 40;
        params.conformation = PolymerBuilder::Conformation::SelfAvoidingWalk;
        params.minAtomDistance = 2.2;
        params.seed = 11;
        const auto result = PolymerBuilder::generate(params);
        check(result.chains == 1, "the self-avoiding chain was built");

        // Measure backbone-to-backbone approach, excluding near neighbours
        // along the chain (those are bonded by construction).
        const auto& atoms = result.structure.atoms();
        std::vector<Vec3> backbone;
        for (const Atom& atom : atoms)
            if (atom.atomicNumber == 6)
                backbone.push_back(atom.position);
        double closest = 1e30;
        for (std::size_t i = 0; i < backbone.size(); ++i)
            for (std::size_t j = i + 3; j < backbone.size(); ++j)
                closest = std::min(closest, (backbone[i] - backbone[j]).norm());
        std::printf("       closest non-adjacent backbone approach = %.2f A "
                    "(%ld steps rejected)\n",
                    closest, result.rejectedSteps);
        check(closest >= params.minAtomDistance - 1e-6,
              "the walk never comes within the minimum distance of itself");
    }

    // -- Tacticity ------------------------------------------------------------
    // Isotactic, syndiotactic and atactic must give genuinely different
    // structures — if the control did nothing, all three would be identical.
    std::printf("Tacticity:\n");
    {
        const auto build = [](PolymerBuilder::Tacticity tacticity) {
            PolymerBuilder::Params params;
            params.monomer = PolymerBuilder::Monomer::Polypropylene;
            params.degreeOfPolymerization = 12;
            params.tacticity = tacticity;
            params.conformation = PolymerBuilder::Conformation::Extended;
            params.seed = 7;
            return PolymerBuilder::generate(params);
        };
        const auto iso = build(PolymerBuilder::Tacticity::Isotactic);
        const auto syn = build(PolymerBuilder::Tacticity::Syndiotactic);
        const auto ata = build(PolymerBuilder::Tacticity::Atactic);

        const auto differs = [](const Structure& a, const Structure& b) {
            if (a.size() != b.size())
                return true;
            for (std::size_t i = 0; i < a.size(); ++i)
                if ((a.atoms()[i].position - b.atoms()[i].position).norm() > 1e-6)
                    return true;
            return false;
        };
        check(differs(iso.structure, syn.structure),
              "isotactic and syndiotactic differ");
        check(differs(iso.structure, ata.structure),
              "isotactic and atactic differ");
        // Same composition regardless: tacticity is stereochemistry, not
        // stoichiometry.
        check(composition(iso.structure) == composition(syn.structure),
              "all tacticities have identical composition");

        check(PolymerBuilder::hasTacticity(PolymerBuilder::Monomer::Polypropylene),
              "polypropylene has a stereocentre");
        check(!PolymerBuilder::hasTacticity(PolymerBuilder::Monomer::Polyethylene),
              "polyethylene does not (its substituents are identical)");
        check(!PolymerBuilder::hasTacticity(PolymerBuilder::Monomer::Ptfe),
              "PTFE does not either");
    }

    // -- Multi-chain packing --------------------------------------------------
    std::printf("Amorphous multi-chain packing:\n");
    {
        PolymerBuilder::Params params;
        params.monomer = PolymerBuilder::Monomer::Polyethylene;
        params.degreeOfPolymerization = 10;
        params.chainCount = 6;
        params.useDensityTarget = true;
        params.densityGCm3 = 0.85;
        params.minAtomDistance = 2.0;
        params.conformation = PolymerBuilder::Conformation::SelfAvoidingWalk;
        params.seed = 21;
        const auto result = PolymerBuilder::generate(params);
        std::printf("       %d/%d chains placed, density %.3f g/cm^3, "
                    "closest non-bonded %.2f A\n",
                    result.chains, params.chainCount, result.densityGCm3,
                    closestNonBonded(result.structure));
        check(result.chains >= 1, "at least one chain was packed");
        check(result.chains + result.failedChains == params.chainCount,
              "every requested chain is accounted for (placed or reported)");
        check(result.densityGCm3 > 0.0, "a density is reported");
    }

    // -- End caps -------------------------------------------------------------
    std::printf("End caps:\n");
    {
        const auto build = [](PolymerBuilder::EndCap cap) {
            PolymerBuilder::Params params;
            params.monomer = PolymerBuilder::Monomer::Polyethylene;
            params.degreeOfPolymerization = 6;
            params.conformation = PolymerBuilder::Conformation::Extended;
            params.endCap = cap;
            return PolymerBuilder::generate(params);
        };
        const auto hydrogen = build(PolymerBuilder::EndCap::Hydrogen);
        const auto methyl = build(PolymerBuilder::EndCap::Methyl);
        const auto hydroxyl = build(PolymerBuilder::EndCap::Hydroxyl);
        // -CH3 adds C+3H per end over -H; -OH adds O+H and drops the H.
        check(composition(methyl.structure).at("C") == composition(hydrogen.structure).at("C") + 2,
              "methyl caps add one carbon per chain end");
        check(composition(hydroxyl.structure).count("O") == 1
                  && composition(hydroxyl.structure).at("O") == 2,
              "hydroxyl caps add one oxygen per chain end");
    }

    // -- Contracts ------------------------------------------------------------
    std::printf("Contracts:\n");
    {
        bool threw = false;
        try {
            PolymerBuilder::Params params;
            params.degreeOfPolymerization = 0;
            PolymerBuilder::generate(params);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "a zero-length chain is rejected");
    }

    std::printf(failures == 0 ? "\nAll polymer builder checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
