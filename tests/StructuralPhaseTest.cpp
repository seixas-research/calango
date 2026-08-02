// Adaptive common-neighbour analysis: the ideal lattices must come out with
// every atom labelled, and a liquid-like cell must come out labelled nothing.
//
// The signatures are the whole algorithm, and three of them are easy to get
// subtly wrong:
//
//   * the third CNA index is the number of bonds in the largest CONNECTED
//     bond cluster, not the longest simple path. The two agree for fcc (4,2,1)
//     and hcp (4,2,2) and disagree for bcc (4,4,4) and icosahedral (5,5,5),
//     so an implementation checked only against fcc/hcp passes while silently
//     never identifying a bcc metal.
//   * the adaptive cutoffs are derived from the ideal shell geometry. A cutoff
//     that is right for fcc lands inside bcc's first-plus-second shell, so a
//     single global value cannot classify both — which is what these cases
//     pin by running fcc, bcc and diamond through the same call.
//   * diamond has no CNA signature of its own; it is identified through its
//     SECOND shell (fcc -> cubic, hcp -> hexagonal). That indirection is easy
//     to break without noticing, because a four-fold atom otherwise just
//     reports "Other" and an all-Other answer looks like a quiet no-op.
//
// Every cell here is periodic and primitive-ish on purpose: the classifier has
// to reach its 12 or 14 neighbours through periodic images of the atom itself.

#include "core/StructuralPhase.hpp"
#include "core/Structure.hpp"
#include "core/UnitCell.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using calango::core::identifyStructuralPhases;
using calango::core::Structure;
using calango::core::StructuralPhase;
using calango::core::StructuralPhaseOptions;
using calango::core::toString;
using calango::core::UnitCell;
using calango::core::Vec3;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

/// A conventional cubic cell with `basis` fractional sites, repeated `n` times
/// along each axis. Copper (Z = 29) unless told otherwise — the covalent radius
/// only has to make the search radius generous, and the adaptive cutoffs take
/// over from there.
Structure cubicCrystal(double a, const std::vector<Vec3>& basis, int n = 2,
                       int z = 29)
{
    Structure structure;
    structure.setCell(UnitCell({a * n, 0.0, 0.0}, {0.0, a * n, 0.0},
                               {0.0, 0.0, a * n}, {true, true, true}));
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            for (int k = 0; k < n; ++k)
                for (const Vec3& site : basis) {
                    structure.addAtom(
                        {z, {(site.x + i) * a, (site.y + j) * a,
                             (site.z + k) * a}});
                }
    return structure;
}

/// Fraction of atoms carrying `phase`.
double fractionOf(const Structure& structure, StructuralPhase phase,
                  const StructuralPhaseOptions& options = {})
{
    const auto result = identifyStructuralPhases(structure, options);
    if (result.phases.empty())
        return 0.0;
    return static_cast<double>(result.count(phase))
        / static_cast<double>(result.phases.size());
}

void report(const Structure& structure, const char* name)
{
    const auto result = identifyStructuralPhases(structure);
    std::printf("  ..  %s (%zu atoms):", name, result.phases.size());
    for (int i = 0; i < calango::core::kStructuralPhaseCount; ++i) {
        const int count = result.counts[static_cast<std::size_t>(i)];
        if (count > 0)
            std::printf(" %s=%d", toString(static_cast<StructuralPhase>(i)),
                        count);
    }
    std::printf("\n");
}

} // namespace

int main()
{
    std::printf("Ideal close-packed lattices:\n");
    {
        // fcc Cu, a = 3.615 A.
        const Structure fcc = cubicCrystal(
            3.615, {{0.0, 0.0, 0.0}, {0.5, 0.5, 0.0}, {0.5, 0.0, 0.5},
                    {0.0, 0.5, 0.5}});
        report(fcc, "fcc Cu");
        check(fractionOf(fcc, StructuralPhase::Fcc) == 1.0,
              "every atom of ideal fcc is labelled FCC");
    }
    {
        // Ideal hcp Mg: a = 3.21 A, c/a = sqrt(8/3), two atoms per cell.
        const double a = 3.21;
        const double c = a * std::sqrt(8.0 / 3.0);
        Structure hcp;
        const int n = 3;
        hcp.setCell(UnitCell({a * n, 0.0, 0.0},
                             {-0.5 * a * n, std::sqrt(3.0) / 2.0 * a * n, 0.0},
                             {0.0, 0.0, c * n}, {true, true, true}));
        const Vec3 a1{a, 0.0, 0.0};
        const Vec3 a2{-0.5 * a, std::sqrt(3.0) / 2.0 * a, 0.0};
        const Vec3 a3{0.0, 0.0, c};
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                for (int k = 0; k < n; ++k) {
                    const Vec3 origin = a1 * i + a2 * j + a3 * k;
                    hcp.addAtom({12, origin});
                    hcp.addAtom({12, origin + a1 * (1.0 / 3.0)
                                     + a2 * (2.0 / 3.0) + a3 * 0.5});
                }
        report(hcp, "hcp Mg");
        check(fractionOf(hcp, StructuralPhase::Hcp) == 1.0,
              "every atom of ideal hcp is labelled HCP");
    }

    std::printf("Body-centred cubic (the signature the longest-path reading "
                "gets wrong):\n");
    {
        // bcc Fe, a = 2.866 A.
        const Structure bcc =
            cubicCrystal(2.866, {{0.0, 0.0, 0.0}, {0.5, 0.5, 0.5}}, 3, 26);
        report(bcc, "bcc Fe");
        check(fractionOf(bcc, StructuralPhase::Bcc) == 1.0,
              "every atom of ideal bcc is labelled BCC");
    }

    std::printf("Diamond (identified through its second shell):\n");
    {
        // Cubic diamond Si, a = 5.431 A.
        const Structure diamond = cubicCrystal(
            5.431,
            {{0.0, 0.0, 0.0},    {0.5, 0.5, 0.0},    {0.5, 0.0, 0.5},
             {0.0, 0.5, 0.5},    {0.25, 0.25, 0.25}, {0.75, 0.75, 0.25},
             {0.75, 0.25, 0.75}, {0.25, 0.75, 0.75}},
            2, 14);
        report(diamond, "cubic diamond Si");
        check(fractionOf(diamond, StructuralPhase::CubicDiamond) == 1.0,
              "every atom of ideal cubic diamond is labelled Cubic diamond");
        check(fractionOf(diamond, StructuralPhase::CubicDiamond,
                         {1.9, /*detectDiamond=*/false})
                  == 0.0,
              "detectDiamond=false really skips the second-shell pass");
    }

    std::printf("Structures that must NOT be labelled:\n");
    {
        // A simple-cubic lattice: 6 neighbours, no CNA signature at all. It is
        // the clearest way to see the classifier decline rather than snap the
        // nearest label onto anything it is handed.
        const Structure simpleCubic = cubicCrystal(2.9, {{0.0, 0.0, 0.0}}, 4);
        report(simpleCubic, "simple cubic");
        check(fractionOf(simpleCubic, StructuralPhase::Other) == 1.0,
              "simple cubic is entirely Other");
    }
    {
        // Displaced fcc, twice. Being adaptive is what makes CNA usable on a
        // finite-temperature snapshot, so BOTH ends have to hold: thermal-scale
        // noise must not erase the labels, and genuine disorder must not keep
        // them. An implementation that only satisfies one of the two is either
        // useless on real MD or an amorphous-phase detector that says "fcc".
        //
        // Deterministic offsets from a fixed LCG, so the answer cannot depend
        // on the platform's rand().
        const auto jitteredFcc = [](double amplitude) {
            Structure structure = cubicCrystal(
                3.615, {{0.0, 0.0, 0.0}, {0.5, 0.5, 0.0}, {0.5, 0.0, 0.5},
                        {0.0, 0.5, 0.5}}, 3);
            unsigned state = 12345u;
            const auto next = [&state] {
                state = state * 1664525u + 1013904223u;
                return static_cast<double>(state >> 8) / 16777216.0 - 0.5;
            };
            for (auto& atom : structure.atoms()) {
                atom.position.x += next() * amplitude;
                atom.position.y += next() * amplitude;
                atom.position.z += next() * amplitude;
            }
            return structure;
        };
        // ~3 % of the 2.56 A nearest-neighbour distance: a warm solid.
        const Structure warm = jitteredFcc(0.30);
        report(warm, "thermally jittered fcc");
        check(fractionOf(warm, StructuralPhase::Fcc) > 0.9,
              "thermal-scale noise leaves an fcc crystal identified as FCC");
        // ~30 %: no crystal survives that, and neither should the label.
        const Structure melted = jitteredFcc(2.2);
        report(melted, "heavily disordered fcc");
        check(fractionOf(melted, StructuralPhase::Fcc) < 0.2,
              "a heavily disordered cell loses its FCC labels");
    }
    {
        // An isolated dimer: not enough neighbours to classify anything, and a
        // shape that used to be able to read past the end of a fixed shell.
        Structure dimer;
        dimer.addAtom({29, {0.0, 0.0, 0.0}});
        dimer.addAtom({29, {2.5, 0.0, 0.0}});
        const auto result = identifyStructuralPhases(dimer);
        check(result.phases.size() == 2
                  && result.phases[0] == StructuralPhase::Other
                  && result.phases[1] == StructuralPhase::Other,
              "a two-atom cluster is Other, not a crash");
    }
    {
        const Structure empty;
        const auto result = identifyStructuralPhases(empty);
        check(result.phases.empty(), "an empty structure yields no labels");
    }

    std::printf(failures == 0 ? "\nAll structural-phase checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
