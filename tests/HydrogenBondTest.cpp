// Hydrogen-bond perception test.
//
// The geometric criteria are what separate a real D-H...A contact from any two
// polar atoms that happen to be near each other, so the checks here are built
// around cases that a naive distance-only rule gets WRONG: a bent geometry, a
// C-H "donor", and a contact just past the distance cutoff.
//
// GUI-free, Python-free.

#include "core/HydrogenBonds.hpp"

#include "core/Element.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

void add(Structure& s, const char* symbol, double x, double y, double z)
{
    Atom atom;
    atom.atomicNumber = Elements::atomicNumber(symbol);
    atom.position = {x, y, z};
    s.addAtom(atom);
}

/// A linear water dimer: O-H...O along x, with the donor hydrogen pointing
/// straight at the acceptor. d(O...O) = `separation`.
Structure waterDimer(double separation)
{
    Structure s;
    add(s, "O", 0.0, 0.0, 0.0);        // donor
    add(s, "H", 0.98, 0.0, 0.0);       // donor hydrogen, aimed at the acceptor
    add(s, "H", -0.24, 0.93, 0.0);     // the other donor hydrogen
    add(s, "O", separation, 0.0, 0.0); // acceptor
    add(s, "H", separation + 0.24, 0.93, 0.0);
    add(s, "H", separation + 0.24, -0.93, 0.0);
    return s;
}

} // namespace

int main()
{
    std::printf("Linear water dimer:\n");
    {
        const auto bonds = detectHydrogenBonds(waterDimer(2.8));
        check(bonds.size() == 1, "exactly one hydrogen bond is found");
        if (!bonds.empty()) {
            const HydrogenBond& b = bonds.front();
            check(b.donor == 0 && b.hydrogen == 1 && b.acceptor == 3,
                  "donor / hydrogen / acceptor are identified correctly");
            check(std::abs(b.distanceDA - 2.8) < 1e-6, "D...A distance");
            check(b.angleDHA > 175.0, "the D-H...A angle is near-linear");
        }
    }

    std::printf("Distance criterion:\n");
    {
        // Just inside vs just outside the 3.5 A default. The cutoff has to
        // actually bite, or every polar pair in a crystal becomes a "bond".
        check(detectHydrogenBonds(waterDimer(3.4)).size() == 1,
              "a 3.4 A contact is accepted");
        check(detectHydrogenBonds(waterDimer(3.6)).empty(),
              "a 3.6 A contact is rejected");

        HydrogenBondOptions loose;
        loose.maxDonorAcceptor = 4.0;
        check(detectHydrogenBonds(waterDimer(3.6), loose).size() == 1,
              "and is accepted again when the cutoff is raised");
    }

    std::printf("Angle criterion:\n");
    {
        // Same O...O separation, but the hydrogen points AWAY from the
        // acceptor: distance alone would call this a hydrogen bond.
        Structure s;
        add(s, "O", 0.0, 0.0, 0.0);
        add(s, "H", -0.98, 0.0, 0.0); // pointing away from the acceptor
        add(s, "O", 2.8, 0.0, 0.0);
        check(detectHydrogenBonds(s).empty(),
              "a hydrogen pointing away from the acceptor is not a bond");

        // A bent case: the H is off-axis enough to fall under 120 degrees.
        Structure bent;
        add(bent, "O", 0.0, 0.0, 0.0);
        add(bent, "H", 0.0, 0.98, 0.0); // perpendicular -> ~45 deg at the H
        add(bent, "O", 2.8, 0.0, 0.0);
        check(detectHydrogenBonds(bent).empty(),
              "a strongly bent geometry is rejected");
    }

    std::printf("Donor chemistry:\n");
    {
        // C-H...O is not a (conventional) hydrogen bond: carbon is not
        // electronegative enough to polarize the H. A rule that only looked at
        // geometry would happily report one here.
        Structure s;
        add(s, "C", 0.0, 0.0, 0.0);
        add(s, "H", 1.09, 0.0, 0.0);
        add(s, "O", 2.8, 0.0, 0.0);
        check(detectHydrogenBonds(s).empty(), "C-H is not treated as a donor");

        // Nitrogen is: N-H...O must be found in the same geometry.
        Structure amide;
        add(amide, "N", 0.0, 0.0, 0.0);
        add(amide, "H", 1.01, 0.0, 0.0);
        add(amide, "O", 2.8, 0.0, 0.0);
        check(detectHydrogenBonds(amide).size() == 1, "N-H...O is a donor");
    }

    std::printf("Periodic images:\n");
    {
        // Donor at the cell edge, acceptor wrapped around: the contact only
        // exists through the periodic image, and its offset must be reported
        // so the dashed line is drawn to the image rather than back across the
        // whole cell.
        Structure s;
        s.setCell(UnitCell({10.0, 0, 0}, {0, 10.0, 0}, {0, 0, 10.0}));
        add(s, "O", 9.6, 5.0, 5.0);
        add(s, "H", 9.98, 5.0, 5.0); // pointing at the wrapped acceptor
        add(s, "O", 0.6, 5.0, 5.0);  // 1.0 A away through the boundary
        const auto bonds = detectHydrogenBonds(s);
        check(bonds.size() == 1, "a contact across the cell boundary is found");
        if (!bonds.empty())
            check(std::abs(bonds.front().acceptorOffset.x - 10.0) < 1e-6,
                  "and carries the periodic image offset for rendering");
    }

    std::printf(failures == 0 ? "\nAll hydrogen-bond checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
