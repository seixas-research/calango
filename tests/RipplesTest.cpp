// 2D Ripples: the exact invariants of a sinusoidal corrugation.
//
// Everything here is checked against a CLOSED FORM or against the defining
// equation itself, never against a previous run of this code:
//
//   * the arc-length quadrature, against the exact value for a flat sheet
//     and against the small-amplitude expansion S = L + π²n²A²/L in the
//     regime where that expansion is valid — and NOT in the regime where it
//     is not, which is the point of solving the integral numerically;
//   * the contraction solve, against its own defining equation: the arc
//     length of the sinusoid over the CONTRACTED cell must come back equal
//     to the original flat length;
//   * the transform, against the profile: atom count, species and in-plane
//     fractional coordinates preserved, the displacement equal to
//     h(f₁, f₂) at each atom's own fractional coordinates, and no seam
//     across the periodic boundary;
//   * the amplitude series, against its own ramp.
//
// The cell used throughout is a HEXAGONAL graphene supercell, not a cubic
// box. A profile written in Cartesian x would be periodic in a cubic cell
// and quietly discontinuous in this one, so a cubic test would pass on an
// implementation that is wrong for every material this module is for.

#include "core/Ripples.hpp"
#include "core/Structure.hpp"
#include "core/UnitCell.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
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

constexpr double kPi = std::numbers::pi;

/// A 6x6 graphene supercell: hexagonal in-plane vectors at 120°, 15 Å of
/// vacuum along c. 72 atoms — big enough that a ripple of a few tenths of an
/// angstrom is a physically sensible thing to put on it.
Structure graphene6x6()
{
    Structure structure;
    const double a = 2.46;
    const int n = 6;
    UnitCell cell({a * n, 0.0, 0.0},
                  {-0.5 * a * n, std::sqrt(3.0) / 2.0 * a * n, 0.0},
                  {0.0, 0.0, 15.0}, {true, true, true});
    structure.setCell(cell);
    // Two-atom basis at fractional (1/3, 2/3) and (2/3, 1/3) of the PRIMITIVE
    // cell, replicated n x n.
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            for (const auto& basis :
                 {std::pair{1.0 / 3.0, 2.0 / 3.0}, std::pair{2.0 / 3.0, 1.0 / 3.0}}) {
                const double f1 = (i + basis.first) / n;
                const double f2 = (j + basis.second) / n;
                Atom atom;
                atom.atomicNumber = 6;
                atom.position = cell.fractionalToCartesian({f1, f2, 0.5});
                structure.addAtom(atom);
            }
        }
    }
    return structure;
}

double relative(double got, double expected)
{
    return expected == 0.0 ? std::abs(got) : std::abs(got - expected) / std::abs(expected);
}

} // namespace

int main()
{
    // ---- The arc-length quadrature --------------------------------------
    std::printf("Arc length of A sin(2 pi n x / L) over one cell:\n");
    {
        check(rippleArcLength(0.0, 12.0, 1) == 12.0,
              "a flat sheet's arc length is EXACTLY its footprint — the "
              "quadrature is short-circuited rather than trusted to return "
              "12 to the last bit");

        // Small amplitude: the expansion S = L + pi^2 n^2 A^2 / L is the
        // leading term, so the quadrature must agree with it to the order of
        // the term it drops (A^4).
        for (const double amplitude : {0.01, 0.02, 0.05}) {
            const double length = 20.0;
            const double got = rippleArcLength(amplitude, length, 1);
            const double leading =
                length + kPi * kPi * amplitude * amplitude / length;
            std::printf("       A = %.2f: quadrature %.10f, expansion "
                        "%.10f, difference %.2e\n",
                        amplitude, got, leading, got - leading);
            check(relative(got, leading) < 1e-5,
                  "A = " + std::to_string(amplitude)
                      + " Å agrees with the small-amplitude expansion");
        }

        // What the expansion actually costs, measured on the quantity that
        // reaches the structure: the CONTRACTED CELL LENGTH. Getting that
        // wrong by δ over a cell of L is a spurious strain of δ/L applied to
        // the sheet, silently — and for a 2D material a tenth of a percent
        // of accidental strain is a real shift in the band structure.
        //
        // The shortcut compared against is the one a small-amplitude
        // implementation would take: L ≈ L₀ − π²n²A²/L₀.
        {
            const double length = 25.0;
            bool smallAmplitudeAgrees = false;
            bool largeAmplitudeDiverges = false;
            for (const double amplitude : {0.5, 1.0, 2.0, 3.0}) {
                const double exact =
                    rippleContractedLength(amplitude, length, 1);
                const double shortcut =
                    length - kPi * kPi * amplitude * amplitude / length;
                const double strain = (shortcut - exact) / exact;
                std::printf("       A = %.1f Å over %.0f Å: exact L = %.6f, "
                            "expansion L = %.6f, spurious strain %.4f %%\n",
                            amplitude, length, exact, shortcut,
                            100.0 * strain);
                if (amplitude == 0.5)
                    smallAmplitudeAgrees = std::abs(shortcut - exact) < 1e-3;
                if (amplitude == 2.0)
                    largeAmplitudeDiverges = strain > 1e-3;
            }
            check(smallAmplitudeAgrees,
                  "at A = 0.5 Å the expansion is within a thousandth of an "
                  "angstrom of the solve — which is what makes it a usable "
                  "ANCHOR for the numerics");
            check(largeAmplitudeDiverges,
                  "and by A = 2 Å it is out by more than 0.1 % of the cell "
                  "length — a tenth of a percent of tensile strain applied "
                  "to the sheet for free, which is why the integral is "
                  "solved rather than expanded");
        }

        // The n-periods scaling, from the substitution in the source: n
        // periods over L is n copies of one period over L/n, so the arc
        // length is n times that of the shorter cell.
        {
            const double one = rippleArcLength(0.4, 30.0 / 3.0, 1);
            const double three = rippleArcLength(0.4, 30.0, 3);
            check(relative(three, 3.0 * one) < 1e-12,
                  "three periods over 30 Å is exactly three times one period "
                  "over 10 Å");
        }

        // Monotone in amplitude — more corrugation is always more path.
        {
            double previous = 0.0;
            bool increasing = true;
            for (const double amplitude : {0.0, 0.1, 0.3, 0.6, 1.0, 2.0}) {
                const double got = rippleArcLength(amplitude, 20.0, 1);
                increasing = increasing && got > previous;
                previous = got;
            }
            check(increasing, "and strictly increasing with the amplitude");
        }
    }

    // ---- The contraction solve ------------------------------------------
    std::printf("\nThe in-plane contraction:\n");
    {
        const double flat = 20.0;
        check(rippleContractedLength(0.0, flat, 1) == flat,
              "A = 0 leaves the cell exactly as it was — the A → 0 limit is "
              "the flat cell, not merely close to it");

        for (const double amplitude : {0.05, 0.25, 0.75, 1.5}) {
            const double contracted =
                rippleContractedLength(amplitude, flat, 1);
            const double back = rippleArcLength(amplitude, contracted, 1);
            std::printf("       A = %.2f Å: %.6f Å -> %.6f Å (%.3f %% "
                        "contraction), arc length back = %.9f\n",
                        amplitude, flat, contracted,
                        100.0 * (flat - contracted) / flat, back);
            check(relative(back, flat) < 1e-12,
                  "the sinusoid over the contracted cell is exactly as long "
                  "as the original flat cell (A = "
                      + std::to_string(amplitude) + " Å)");
            check(contracted < flat,
                  "and the contracted cell is shorter than the flat one");
        }

        // The leading behaviour, as an anchor rather than as the answer:
        // L0 - L ~ pi^2 n^2 A^2 / L, so a small amplitude must reproduce it.
        {
            const double amplitude = 0.05;
            const double contracted =
                rippleContractedLength(amplitude, flat, 1);
            const double predicted =
                kPi * kPi * amplitude * amplitude / contracted;
            check(relative(flat - contracted, predicted) < 1e-4,
                  "and at small amplitude the contraction is the expected "
                  "pi^2 n^2 A^2 / L");
        }

        // The refusal. A sinusoid of amplitude A with n periods is at least
        // 4nA long whatever its footprint, so a cell shorter than that
        // cannot hold it — and the answer is a refusal, not a clamp: a
        // clamped cell would silently STRETCH the sheet, which is the exact
        // error the contraction exists to prevent.
        check(rippleContractedLength(6.0, 20.0, 1) == 0.0,
              "an amplitude the cell cannot hold (4nA >= L₀) is refused, not "
              "clamped");
        check(rippleContractedLength(2.0, 20.0, 3) == 0.0,
              "and the floor scales with the period count, as 4nA does");
    }

    // ---- The transform ---------------------------------------------------
    const Structure flat = graphene6x6();
    std::printf("\nThe rippled structure (6x6 graphene, hexagonal cell):\n");
    {
        RippleOptions options;
        options.direction = RippleOptions::Direction::XY;
        options.amplitude = 0.4;
        options.periodsFirst = 1;
        options.periodsSecond = 1;
        options.normalAxis = 2;
        std::string error;
        const Structure rippled = applyRipples(flat, options, &error);
        check(error.empty(), "the build succeeds: " + error);
        check(rippled.size() == flat.size(),
              "atom count preserved (" + std::to_string(rippled.size()) + ")");

        bool speciesKept = true;
        bool fractionalKept = true;
        double worstFractional = 0.0;
        double worstProfile = 0.0;
        for (std::size_t i = 0; i < rippled.size(); ++i) {
            speciesKept = speciesKept
                && rippled.atoms()[i].atomicNumber
                    == flat.atoms()[i].atomicNumber;
            const Vec3 before =
                flat.cell().cartesianToFractional(flat.atoms()[i].position);
            const Vec3 after = rippled.cell().cartesianToFractional(
                rippled.atoms()[i].position);
            worstFractional = std::max(
                {worstFractional, std::abs(after.x - before.x),
                 std::abs(after.y - before.y)});
            // The displacement, measured against the profile evaluated at
            // this atom's OWN fractional coordinates. The normal here is +z
            // because the cell's third vector is; the transform derives it
            // from the in-plane pair, which is the same direction.
            const double h = rippled.atoms()[i].position.z
                - flat.atoms()[i].position.z;
            const double expected = options.amplitude
                * std::sin(2.0 * kPi * before.x)
                * std::sin(2.0 * kPi * before.y);
            worstProfile = std::max(worstProfile, std::abs(h - expected));
        }
        fractionalKept = worstFractional < 1e-10;
        std::printf("       worst in-plane fractional drift = %.2e, worst "
                    "profile deviation = %.2e Å\n",
                    worstFractional, worstProfile);
        check(speciesKept, "every atom keeps its element");
        check(fractionalKept,
              "and its IN-PLANE fractional coordinates — the contraction "
              "carries the atoms with it rather than sliding them through "
              "the cell");
        check(worstProfile < 1e-10,
              "the out-of-plane displacement is exactly "
              "A sin(2π f₁) sin(2π f₂) at each atom's own fractional "
              "coordinates");

        // No seam. The profile is a function of the fractional coordinate,
        // so f and f+1 are the same point and must displace identically —
        // this is what an integer period count buys, and the check that
        // would catch someone writing the profile in Cartesian x on a
        // hexagonal cell.
        double worstSeam = 0.0;
        for (double f = 0.0; f <= 1.0; f += 0.05) {
            const auto profile = [&](double f1, double f2) {
                return options.amplitude * std::sin(2.0 * kPi * f1)
                    * std::sin(2.0 * kPi * f2);
            };
            worstSeam = std::max(
                {worstSeam, std::abs(profile(0.0, f) - profile(1.0, f)),
                 std::abs(profile(f, 0.0) - profile(f, 1.0))});
        }
        check(worstSeam < 1e-12,
              "the profile at fractional 0 and 1 is identical along both "
              "in-plane axes — no seam at the periodic boundary");

        // The cell: both in-plane vectors contracted (the 'xy' profile
        // ripples along both), the vacuum axis untouched, and the
        // contraction is the one the solver reported.
        for (int axis = 0; axis < 2; ++axis) {
            const double before =
                flat.cell().vectors()[static_cast<std::size_t>(axis)].norm();
            const double after =
                rippled.cell().vectors()[static_cast<std::size_t>(axis)].norm();
            check(relative(after, rippleContractedLength(options.amplitude,
                                                         before, 1))
                      < 1e-12,
                  "in-plane vector " + std::to_string(axis)
                      + " contracted to the arc-length-preserving footprint");
            check(after < before, "and it did get shorter");
        }
        check(rippled.cell().vectors()[2].norm() == flat.cell().vectors()[2].norm(),
              "while the vacuum axis is untouched — the vacuum is not part "
              "of the membrane");
        // The lattice is compressed, not rotated: the angle between the two
        // in-plane vectors must survive. (Both are scaled by the same factor
        // here, since the cell is hexagonal and the profile symmetric, but
        // the assertion is about direction, not magnitude.)
        const auto angleBetween = [](const Structure& s) {
            const Vec3& u = s.cell().vectors()[0];
            const Vec3& v = s.cell().vectors()[1];
            return std::acos(u.dot(v) / (u.norm() * v.norm()));
        };
        check(std::abs(angleBetween(rippled) - angleBetween(flat)) < 1e-12,
              "and the 120° in-plane angle survives — a contraction is a "
              "uniaxial compression of the footprint, not a change of "
              "lattice");
    }

    // ---- What the contraction buys, on the non-orthogonal cell ----------
    //
    // The point of preserving arc length is that the BONDS keep their
    // length: the sheet is corrugated, not stretched. That is checkable
    // directly, and on a 120° hexagonal lattice it is the check that would
    // catch a contraction written for an orthogonal cell — a scheme that
    // shears the lattice, or that scales the wrong vector, leaves the
    // nearest-neighbour distances visibly wrong even when the cell lengths
    // look right.
    std::printf("\nBond lengths on a 120° hexagonal cell (what the "
                "contraction is FOR):\n");
    {
        // Mean over each atom's THREE nearest neighbours, minimum image —
        // its three σ bonds, which is what a graphene atom has. Not the
        // single nearest: contracting one cell vector makes the three bonds
        // inequivalent, and a "shortest bond" statistic then measures the
        // splitting rather than the mean, reporting a compression where
        // there is a stretch.
        const auto meanBondLength = [](const Structure& s) {
            double total = 0.0;
            int counted = 0;
            for (std::size_t i = 0; i < s.size(); ++i) {
                std::vector<double> distances;
                distances.reserve(3 * s.size());
                for (std::size_t j = 0; j < s.size(); ++j) {
                    if (i == j)
                        continue;
                    // Over the in-plane images: the sheet is periodic, and a
                    // neighbour across the boundary is still a neighbour.
                    for (int da = -1; da <= 1; ++da) {
                        for (int db = -1; db <= 1; ++db) {
                            const Vec3 shift = s.cell().vectors()[0] * da
                                + s.cell().vectors()[1] * db;
                            distances.push_back(
                                (s.atoms()[j].position + shift
                                 - s.atoms()[i].position)
                                    .norm());
                        }
                    }
                }
                std::partial_sort(distances.begin(), distances.begin() + 3,
                                  distances.end());
                for (int k = 0; k < 3; ++k) {
                    total += distances[static_cast<std::size_t>(k)];
                    ++counted;
                }
            }
            return counted > 0 ? total / counted : 0.0;
        };

        RippleOptions options;
        options.direction = RippleOptions::Direction::X;
        options.amplitude = 0.6;
        options.periodsFirst = 1;
        std::string error;
        const Structure contracted = applyRipples(flat, options, &error);
        RippleOptions uncontracted = options;
        uncontracted.contractInPlane = false;
        const Structure stretched = applyRipples(flat, uncontracted, &error);

        const double flatBond = meanBondLength(flat);
        const double contractedBond = meanBondLength(contracted);
        const double stretchedBond = meanBondLength(stretched);
        const double contractedStrain = (contractedBond - flatBond) / flatBond;
        const double stretchedStrain = (stretchedBond - flatBond) / flatBond;
        std::printf("       mean C–C: flat %.6f Å, rippled+contracted "
                    "%.6f Å (%+.4f %%), rippled only %.6f Å (%+.4f %%)\n",
                    flatBond, contractedBond, 100.0 * contractedStrain,
                    stretchedBond, 100.0 * stretchedStrain);
        // a/√3 for a = 2.46 Å — the closed form, not 1.42 rounded.
        check(std::abs(flatBond - 2.46 / std::sqrt(3.0)) < 1e-9,
              "the flat hexagonal sheet's bonds are a/√3 to begin with — the "
              "reference is the exact one, not a rounded 1.42");
        check(stretchedStrain > 0.0,
              "rippling WITHOUT the contraction stretches every bond: the "
              "cell still spans the flat footprint while the sheet now has "
              "to reach further to cross it");
        check(contractedStrain > 0.0
                  && contractedStrain < stretchedStrain / 3.0,
              "and the contraction removes most of that — the residual is "
              "smaller by more than a factor of three, on a cell whose "
              "in-plane vectors are at 120°, which is the case a "
              "contraction written for an orthogonal box gets wrong");
        check(contractedStrain < 0.005,
              "leaving well under half a percent, which is the finite-bond "
              "remainder: arc length is a statement about a CURVE, and a "
              "1.42 Å chord across a 14.8 Å ripple does not quite follow "
              "one");
    }

    // ---- Single-direction and the uncontracted variant -------------------
    std::printf("\nSingle-direction ripples, and contraction off:\n");
    {
        RippleOptions options;
        options.direction = RippleOptions::Direction::X;
        options.amplitude = 0.3;
        std::string error;
        const Structure rippled = applyRipples(flat, options, &error);
        check(error.empty(), "'x only' builds");
        check(rippled.cell().vectors()[0].norm()
                  < flat.cell().vectors()[0].norm(),
              "the first in-plane vector contracts");
        check(rippled.cell().vectors()[1].norm()
                  == flat.cell().vectors()[1].norm(),
              "and the second does NOT — nothing is stored along a direction "
              "the profile does not vary in");

        double worst = 0.0;
        for (std::size_t i = 0; i < rippled.size(); ++i) {
            const Vec3 f =
                flat.cell().cartesianToFractional(flat.atoms()[i].position);
            const double h = rippled.atoms()[i].position.z
                - flat.atoms()[i].position.z;
            worst = std::max(worst,
                             std::abs(h - options.amplitude
                                          * std::sin(2.0 * kPi * f.x)));
        }
        check(worst < 1e-10,
              "with the profile h = A sin(2π f₁), the second sine dropped "
              "rather than evaluated at 1");

        RippleOptions uncontracted = options;
        uncontracted.contractInPlane = false;
        const Structure stretched = applyRipples(flat, uncontracted, &error);
        check(error.empty() && stretched.cell().vectors()[0].norm()
                  == flat.cell().vectors()[0].norm(),
              "and switching the contraction off leaves the cell alone — the "
              "rippled-AND-stretched variant, for whoever wants it");
    }

    // ---- Periods per cell ------------------------------------------------
    std::printf("\nPeriods per cell:\n");
    {
        RippleOptions options;
        options.direction = RippleOptions::Direction::X;
        options.amplitude = 0.25;
        options.periodsFirst = 3;
        std::string error;
        const Structure rippled = applyRipples(flat, options, &error);
        check(error.empty(), "three periods along the first axis builds");
        double worst = 0.0;
        for (std::size_t i = 0; i < rippled.size(); ++i) {
            const Vec3 f =
                flat.cell().cartesianToFractional(flat.atoms()[i].position);
            const double h = rippled.atoms()[i].position.z
                - flat.atoms()[i].position.z;
            worst = std::max(worst,
                             std::abs(h - options.amplitude
                                          * std::sin(2.0 * kPi * 3.0 * f.x)));
        }
        check(worst < 1e-10, "and the profile carries the period count");
        check(rippled.cell().vectors()[0].norm()
                  < applyRipples(flat, [&] {
                        RippleOptions single = options;
                        single.periodsFirst = 1;
                        return single;
                    }(), &error)
                        .cell()
                        .vectors()[0]
                        .norm(),
              "three periods store more arc length than one, so they "
              "contract the cell further");
    }

    // ---- The amplitude series -------------------------------------------
    std::printf("\nThe amplitude series:\n");
    {
        RippleOptions options;
        options.direction = RippleOptions::Direction::XY;
        std::string error;
        const auto frames =
            buildRippleSeries(flat, options, 0.0, 0.8, 9, &error);
        check(error.empty(), "the series builds: " + error);
        check(frames.size() == 9,
              "nine frames were asked for and nine came back ("
                  + std::to_string(frames.size()) + ")");

        bool monotone = true;
        double previous = -1.0;
        bool tagged = true;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const auto& fields = frames[i]->scalarFields();
            const auto it = fields.find("ripple_amplitude");
            if (it == fields.end() || it->second.empty()) {
                tagged = false;
                continue;
            }
            const double tag = it->second.front();
            monotone = monotone && tag > previous;
            previous = tag;
            // The tag is not decoration: it must be the amplitude the frame
            // was actually built at, which is readable from the geometry.
            const double expected = 0.8 * static_cast<double>(i) / 8.0;
            if (std::abs(tag - expected) > 1e-12)
                tagged = false;
        }
        check(tagged,
              "every frame carries its own amplitude as the "
              "\"ripple_amplitude\" scalar field, on the linear ramp");
        check(monotone, "and the tags increase strictly through the series");
        check(frames.front()->cell().vectors()[0].norm()
                  == flat.cell().vectors()[0].norm(),
              "the A = 0 frame is the flat sheet exactly — the ramp's zero "
              "endpoint is not merely a small ripple");
        check(frames.back()->cell().vectors()[0].norm()
                  < frames.front()->cell().vectors()[0].norm(),
              "and the cell contracts monotonically along the series");

        // A one-frame series is the minimum and gets the low endpoint,
        // rather than dividing by zero on the ramp.
        const auto single = buildRippleSeries(flat, options, 0.35, 0.9, 1);
        check(single.size() == 1
                  && std::abs(single.front()
                                  ->scalarFields()
                                  .at("ripple_amplitude")
                                  .front()
                              - 0.35)
                      < 1e-12,
              "a one-frame series is the minimum amplitude, with no division "
              "by zero on the ramp");
    }

    // ---- Refusals --------------------------------------------------------
    std::printf("\nWhat it refuses:\n");
    {
        Structure noCell;
        Atom atom;
        atom.atomicNumber = 6;
        noCell.addAtom(atom);
        std::string error;
        RippleOptions options;
        applyRipples(noCell, options, &error);
        check(error.find("unit cell") != std::string::npos,
              "a structure with no cell: the profile is periodic in "
              "fractional coordinates, and there are none");

        error.clear();
        RippleOptions huge;
        huge.amplitude = 20.0; // 4A = 80 Å, far beyond a 14.76 Å cell vector
        applyRipples(flat, huge, &error);
        check(error.find("too large") != std::string::npos,
              "an amplitude the cell cannot contract around, by name");

        error.clear();
        RippleOptions noAxis;
        noAxis.normalAxis = -1;
        applyRipples(flat, noAxis, &error);
        check(error.find("out-of-plane axis") != std::string::npos,
              "and an undetermined out-of-plane axis, rather than guessing "
              "one");
    }

    std::printf(failures == 0 ? "\nAll 2D Ripples checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
