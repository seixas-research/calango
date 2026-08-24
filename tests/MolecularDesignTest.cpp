// Molecular Design: the sketch model, the SMILES layer, and the 2D -> 3D
// pipeline that "Send to 3D Viewport" runs.
//
// THE BENZENE TEST IS THE POINT OF THIS FILE. A sketcher can look right and
// still export nonsense, so the assertions here are on the CHEMISTRY of the
// exported structure, against closed-form and literature values rather than
// against a previous run:
//
//   * C6H6 — six carbons drawn, six hydrogens the valence implies and the
//     export creates;
//   * planar to within a tolerance far tighter than any drawing error;
//   * C–C in the aromatic range, and all six EQUAL (the alternating
//     1.34/1.52 Å hexagon a Kekulé structure relaxes to without aromatic
//     perception is the exact failure this pins);
//   * C–C–C = 120°, which is what a regular hexagon means.
//
// The same assertions run twice: once on a ring stamped through the ring
// template's own code path (what the palette button does), and once on
// `c1ccccc1` through the SMILES parser. Two routes into one pipeline, so a
// regression in either is attributable.
//
// GUI-free and Python-free — every line of this is core/.

#include "core/MoleculeEmbed3d.hpp"
#include "core/MoleculeGraph.hpp"
#include "core/PhysicalConstants.hpp"
#include "core/Smiles.hpp"
#include "core/Structure.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <tuple>
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

void checkNear(double value, double expected, double tolerance,
               const std::string& what)
{
    const bool ok = std::fabs(value - expected) <= tolerance;
    std::printf("  %s %s (got %.4f, expected %.4f +- %.4f)\n",
                ok ? "ok  " : "FAIL", what.c_str(), value, expected, tolerance);
    if (!ok)
        ++failures;
}

void checkRange(double value, double low, double high, const std::string& what)
{
    const bool ok = value >= low && value <= high;
    std::printf("  %s %s (got %.4f, wanted %.4f..%.4f)\n",
                ok ? "ok  " : "FAIL", what.c_str(), value, low, high);
    if (!ok)
        ++failures;
}

int countElement(const Structure& structure, int z)
{
    int count = 0;
    for (const Atom& atom : structure.atoms()) {
        if (atom.atomicNumber == z)
            ++count;
    }
    return count;
}

/// Every distance below `cutoff` between atoms of the two given elements.
std::vector<double> pairDistances(const Structure& structure, int za, int zb,
                                  double cutoff)
{
    std::vector<double> distances;
    for (std::size_t i = 0; i < structure.size(); ++i) {
        for (std::size_t j = i + 1; j < structure.size(); ++j) {
            const int a = structure.atoms()[i].atomicNumber;
            const int b = structure.atoms()[j].atomicNumber;
            if (!((a == za && b == zb) || (a == zb && b == za)))
                continue;
            const double d =
                (structure.atoms()[i].position - structure.atoms()[j].position)
                    .norm();
            if (d < cutoff)
                distances.push_back(d);
        }
    }
    return distances;
}

/// RMS distance of every atom from the best-fit plane through the structure.
///
/// The plane is found from the inertia tensor rather than assumed to be z = 0:
/// asserting on |z| alone would pass a structure the embedder happened to
/// leave in the drawing plane while failing an equally flat one that had been
/// rotated, and "flat" is a property of the molecule, not of its orientation.
double planarityRms(const Structure& structure)
{
    if (structure.size() < 3)
        return 0.0;
    Vec3 centroid = structure.centroid();
    double xx = 0.0;
    double xy = 0.0;
    double xz = 0.0;
    double yy = 0.0;
    double yz = 0.0;
    double zz = 0.0;
    for (const Atom& atom : structure.atoms()) {
        const Vec3 d = atom.position - centroid;
        xx += d.x * d.x;
        xy += d.x * d.y;
        xz += d.x * d.z;
        yy += d.y * d.y;
        yz += d.y * d.z;
        zz += d.z * d.z;
    }
    // The plane normal is the eigenvector of the SMALLEST eigenvalue of the
    // covariance matrix. Found by inverse power iteration on the deflated
    // matrix, which for a 3x3 is just repeated multiplication by the adjugate.
    const double a[3][3] = {{xx, xy, xz}, {xy, yy, yz}, {xz, yz, zz}};
    // Adjugate of `a`: A^-1 * det(A). Multiplying by it amplifies the SMALLEST
    // eigendirection, which is exactly the normal we want.
    const double adj[3][3] = {
        {a[1][1] * a[2][2] - a[1][2] * a[2][1],
         a[0][2] * a[2][1] - a[0][1] * a[2][2],
         a[0][1] * a[1][2] - a[0][2] * a[1][1]},
        {a[1][2] * a[2][0] - a[1][0] * a[2][2],
         a[0][0] * a[2][2] - a[0][2] * a[2][0],
         a[0][2] * a[1][0] - a[0][0] * a[1][2]},
        {a[1][0] * a[2][1] - a[1][1] * a[2][0],
         a[0][1] * a[2][0] - a[0][0] * a[2][1],
         a[0][0] * a[1][1] - a[0][1] * a[1][0]}};
    Vec3 normal{0.31, 0.47, 0.82}; // an arbitrary seed, deliberately not an axis
    for (int iteration = 0; iteration < 200; ++iteration) {
        const Vec3 next{
            adj[0][0] * normal.x + adj[0][1] * normal.y + adj[0][2] * normal.z,
            adj[1][0] * normal.x + adj[1][1] * normal.y + adj[1][2] * normal.z,
            adj[2][0] * normal.x + adj[2][1] * normal.y + adj[2][2] * normal.z};
        if (next.norm() < 1e-14)
            break; // exactly planar: the adjugate is singular, and we are done
        normal = next.normalized();
    }
    double sum = 0.0;
    for (const Atom& atom : structure.atoms()) {
        const double height = (atom.position - centroid).dot(normal);
        sum += height * height;
    }
    return std::sqrt(sum / static_cast<double>(structure.size()));
}

/// Every C–C–C angle in a structure, in degrees, using a 1.8 Å bond cutoff.
std::vector<double> carbonAngles(const Structure& structure)
{
    std::vector<double> angles;
    for (std::size_t c = 0; c < structure.size(); ++c) {
        if (structure.atoms()[c].atomicNumber != 6)
            continue;
        std::vector<std::size_t> neighbors;
        for (std::size_t k = 0; k < structure.size(); ++k) {
            if (k == c || structure.atoms()[k].atomicNumber != 6)
                continue;
            if ((structure.atoms()[k].position - structure.atoms()[c].position)
                    .norm()
                < 1.8)
                neighbors.push_back(k);
        }
        for (std::size_t i = 0; i < neighbors.size(); ++i) {
            for (std::size_t j = i + 1; j < neighbors.size(); ++j) {
                const Vec3 u = (structure.atoms()[neighbors[i]].position
                                - structure.atoms()[c].position)
                                   .normalized();
                const Vec3 v = (structure.atoms()[neighbors[j]].position
                                - structure.atoms()[c].position)
                                   .normalized();
                angles.push_back(std::acos(std::clamp(u.dot(v), -1.0, 1.0))
                                 * 180.0 / kPi);
            }
        }
    }
    return angles;
}

/// The full benzene assertion set, run against whichever route produced
/// `structure`.
///
/// TOLERANCES ARE WHAT THE INTERNAL OPTIMIZER HONESTLY DELIVERS, not what a
/// DFT geometry would. Its aromatic C–C target is 0.915 x (2 x r_cov(C)) =
/// 1.391 Å against the experimental 1.397 Å, and its C–H target is
/// r_cov(C) + r_cov(H) = 1.070 Å against the experimental 1.084 Å; the
/// windows below are set around those targets wide enough to cover the
/// residual strain of a converged relaxation and no wider. A structure that
/// needs a real geometry goes through the Geometry Optimization wizard after
/// this, which is what the new tab exists for.
void assertBenzene(const Structure& structure, const std::string& route)
{
    std::printf("%s:\n", route.c_str());

    check(structure.chemicalFormula() == "C6H6",
          "the exported structure is C6H6");
    check(countElement(structure, 6) == 6, "six carbons");
    check(countElement(structure, 1) == 6,
          "six hydrogens, none of them drawn — the valence implied all six");

    checkNear(planarityRms(structure), 0.0, 1e-3,
              "planar: RMS deviation from the best-fit plane (A)");

    const std::vector<double> cc = pairDistances(structure, 6, 6, 1.8);
    check(cc.size() == 6, "six C-C bonds — the ring closed");
    if (!cc.empty()) {
        const auto [shortest, longest] =
            std::minmax_element(cc.begin(), cc.end());
        checkRange(*shortest, 1.36, 1.44, "shortest C-C is aromatic (A)");
        checkRange(*longest, 1.36, 1.44, "longest C-C is aromatic (A)");
        // The one that catches a missing aromatic perception: a Kekulé
        // benzene relaxed against alternating single/double targets closes
        // perfectly well as a hexagon with 1.52 and 1.34 A sides, and every
        // "is it in range" test above would still pass on the average.
        checkNear(*longest - *shortest, 0.0, 5e-3,
                  "all six C-C are the SAME length (A) — no bond alternation");
    }

    const std::vector<double> ch = pairDistances(structure, 6, 1, 1.4);
    check(ch.size() == 6, "six C-H bonds");
    if (!ch.empty()) {
        const auto [shortest, longest] =
            std::minmax_element(ch.begin(), ch.end());
        checkRange(*shortest, 1.02, 1.12, "shortest C-H (A)");
        checkRange(*longest, 1.02, 1.12, "longest C-H (A)");
    }

    const std::vector<double> angles = carbonAngles(structure);
    check(angles.size() == 6, "six C-C-C angles");
    for (double angle : angles) {
        if (std::fabs(angle - 120.0) > 1.0) {
            checkNear(angle, 120.0, 1.0, "C-C-C angle (deg)");
            return;
        }
    }
    if (angles.size() == 6)
        check(true, "every C-C-C angle is 120 deg to within 1 deg");
}

} // namespace

int main()
{
    // -----------------------------------------------------------------------
    // Valence arithmetic — what every implicit hydrogen count rests on
    // -----------------------------------------------------------------------
    std::printf("Valence:\n");
    {
        check(standardValences(6) == std::vector<int>{4}, "carbon is tetravalent");
        check(standardValences(7) == std::vector<int>{3}, "nitrogen trivalent");
        check(standardValences(16) == std::vector<int>({2, 4, 6}),
              "sulfur has three valences, ascending");
        check(standardValences(7, +1) == std::vector<int>{4},
              "ammonium nitrogen is tetravalent");
        check(standardValences(8, -1) == std::vector<int>{1},
              "hydroxide oxygen is monovalent");
        check(standardValences(6, +1) == std::vector<int>{3},
              "a carbocation is trivalent");
        check(standardValences(6, -1) == std::vector<int>{3},
              "and so is a carbanion — a charge of EITHER sign costs group 14 "
              "a bond");
        check(standardValences(5, -1) == std::vector<int>{4},
              "borohydride boron is tetravalent");
        check(standardValences(26).empty(),
              "iron has no sketch valence, so nothing is guessed for it");
    }

    // -----------------------------------------------------------------------
    // The graph itself
    // -----------------------------------------------------------------------
    std::printf("Graph model:\n");
    {
        MoleculeGraph graph;
        const int a = graph.addAtom(6, 0.0, 0.0);
        const int b = graph.addAtom(6, 1.0, 0.0);
        graph.addBond(a, b, 1);
        check(graph.implicitHydrogens(a) == 3, "ethane carbon carries three H");
        check(graph.formula() == "C2H6", "and the pair is C2H6");

        check(graph.cycleBondOrder(0) == 2, "drawing on a bond makes it double");
        check(graph.implicitHydrogens(a) == 2, "ethene carbon now carries two");
        check(graph.cycleBondOrder(0) == 3, "then triple");
        check(graph.formula() == "C2H2", "acetylene");
        check(graph.cycleBondOrder(0) == 1, "and wraps back to single");

        // A pentavalent carbon is REPORTED, not refused — the whole point of
        // the valence check being visual.
        MoleculeGraph strained;
        const int centre = strained.addAtom(6, 0.0, 0.0);
        for (int i = 0; i < 5; ++i) {
            const int arm = strained.addAtom(6, std::cos(i * 1.2),
                                             std::sin(i * 1.2));
            strained.addBond(centre, arm, 1);
        }
        check(strained.valenceViolated(centre),
              "a pentavalent carbon is flagged");
        check(strained.implicitHydrogens(centre) == 0,
              "and given no hydrogens rather than a negative count");
        check(!strained.valenceViolated(1),
              "while its perfectly ordinary neighbours are not");

        // Removing an atom compacts the indices and takes its bonds with it.
        MoleculeGraph chain;
        for (int i = 0; i < 4; ++i)
            chain.addAtom(6, static_cast<double>(i), 0.0);
        for (int i = 0; i < 3; ++i)
            chain.addBond(i, i + 1, 1);
        chain.removeAtom(1);
        check(chain.atomCount() == 3, "removing an atom leaves three");
        check(chain.bondCount() == 1,
              "and takes BOTH bonds that touched it, leaving one");

        // Disconnected fragments are legal and counted separately.
        MoleculeGraph twoMolecules;
        twoMolecules.addAtom(8, 0.0, 0.0);
        twoMolecules.addAtom(8, 5.0, 0.0);
        check(twoMolecules.fragments().size() == 2,
              "two unbonded atoms are two fragments");
        check(twoMolecules.formula() == "H4O2",
              "and the formula covers both — two waters");
    }

    // -----------------------------------------------------------------------
    // Ring templates
    // -----------------------------------------------------------------------
    std::printf("Ring templates:\n");
    {
        for (RingTemplate ring : ringTemplates()) {
            MoleculeGraph graph = makeRing(ring, 0.0, 0.0);
            const int size = ringTemplateSize(ring);
            const std::string name = ringTemplateName(ring);
            check(graph.atomCount() == size,
                  name + " has " + std::to_string(size) + " atoms");
            // Naphthalene's two fused six-rings share a bond, so it has 11
            // bonds for 10 atoms; every other template is a simple cycle.
            const int expectedBonds =
                ring == RingTemplate::Naphthalene ? size + 1 : size;
            check(graph.bondCount() == expectedBonds,
                  name + " has " + std::to_string(expectedBonds) + " bonds");
        }

        check(makeRing(RingTemplate::Benzene, 0, 0).formula() == "C6H6",
              "the benzene template is C6H6");
        check(makeRing(RingTemplate::Cyclohexane, 0, 0).formula() == "C6H12",
              "cyclohexane is C6H12");
        check(makeRing(RingTemplate::Cyclopentadiene, 0, 0).formula() == "C5H6",
              "cyclopentadiene is C5H6");
        check(makeRing(RingTemplate::Naphthalene, 0, 0).formula() == "C10H8",
              "naphthalene is C10H8 — the two ring-junction carbons carry no "
              "hydrogen");

        // Fusing onto a bond adds size-2 atoms, not size.
        MoleculeGraph fused = makeRing(RingTemplate::Benzene, 0.0, 0.0);
        const int before = fused.atomCount();
        check(fuseRing(fused, 0, RingTemplate::Benzene),
              "a second benzene fuses onto the first ring's bond");
        check(fused.atomCount() == before + 4,
              "adding four atoms, not six — the shared bond is reused");
        check(fused.formula() == "C10H8",
              "and the result is naphthalene, C10H8");

        // A chain grows the number of carbons asked for.
        MoleculeGraph chain;
        const int seed = chain.addAtom(6, 0.0, 0.0);
        const std::vector<int> grown = growChain(chain, seed, 30.0, 5);
        check(grown.size() == 5, "a five-carbon chain creates five atoms");
        check(chain.formula() == "C6H14", "seed plus chain is hexane, C6H14");
    }

    // -----------------------------------------------------------------------
    // Aromatic perception — derived, never stored
    // -----------------------------------------------------------------------
    std::printf("Aromatic perception:\n");
    {
        const MoleculeGraph benzene = makeRing(RingTemplate::Benzene, 0, 0);
        const std::vector<bool> aromatic = benzene.perceiveAromaticBonds();
        check(std::count(aromatic.begin(), aromatic.end(), true) == 6,
              "all six benzene bonds are perceived aromatic");

        const MoleculeGraph cyclohexane =
            makeRing(RingTemplate::Cyclohexane, 0, 0);
        const std::vector<bool> saturated = cyclohexane.perceiveAromaticBonds();
        check(std::count(saturated.begin(), saturated.end(), true) == 0,
              "cyclohexane has none");

        const MoleculeGraph diene =
            makeRing(RingTemplate::Cyclopentadiene, 0, 0);
        const std::vector<bool> cpd = diene.perceiveAromaticBonds();
        check(std::count(cpd.begin(), cpd.end(), true) == 0,
              "cyclopentadiene has none either — its sp3 CH2 breaks the ring");

        const MoleculeGraph naphthalene =
            makeRing(RingTemplate::Naphthalene, 0, 0);
        const std::vector<bool> fused = naphthalene.perceiveAromaticBonds();
        check(std::count(fused.begin(), fused.end(), true) == 11,
              "every naphthalene bond is aromatic, the shared one included");

        // The RING form of the same perception, which the canvas fills. Not a
        // second rule: perceiveAromaticRings() is derived from the bond flags
        // above, so the two can never disagree about a ring.
        check(benzene.perceiveAromaticRings().size() == 1,
              "benzene reports one aromatic ring");
        if (!benzene.perceiveAromaticRings().empty()) {
            check(benzene.perceiveAromaticRings().front().size() == 6,
                  "of six atoms — a polygon the highlight can fill directly");
        }
        check(cyclohexane.perceiveAromaticRings().empty(),
              "cyclohexane reports no aromatic ring");
        check(diene.perceiveAromaticRings().empty(),
              "cyclopentadiene reports none");
        check(naphthalene.perceiveAromaticRings().size() == 2,
              "naphthalene reports BOTH of its rings, not the 10-cycle");

        // Pyridine and pyrrole: the heteroatom cases the rule is written for,
        // reached through SMILES because that is how a user gets one onto the
        // canvas without drawing it.
        for (const auto& [smiles, rings, label] :
             {std::tuple<const char*, std::size_t, const char*>{
                  "c1ccncc1", 1, "pyridine is aromatic"},
              std::tuple<const char*, std::size_t, const char*>{
                  "c1cc[nH]c1", 1, "pyrrole is aromatic"},
              std::tuple<const char*, std::size_t, const char*>{
                  "C1=CCCCC1", 0, "cyclohexene is not"},
              std::tuple<const char*, std::size_t, const char*>{
                  "O=C1C=CC=CC1", 0,
                  "a cross-conjugated cyclohexadienone is not"}}) {
            MoleculeGraph parsed;
            std::string error;
            const bool ok = smiles::parseTopology(smiles, parsed, &error);
            check(ok, std::string("parsed ") + smiles + " (" + error + ")");
            if (ok)
                check(parsed.perceiveAromaticRings().size() == rings, label);
        }

        // The highlight annotation is chemistry-free: it rides on the atom,
        // survives everything that moves atoms, and nothing reads it.
        MoleculeGraph marked = makeRing(RingTemplate::Benzene, 0, 0);
        marked.atoms()[0].highlight = 2;
        marked.atoms()[1].highlight = 2;
        check(marked.formula() == "C6H6",
              "a highlighted atom does not change the formula");
        const std::vector<bool> stillAromatic = marked.perceiveAromaticBonds();
        check(std::count(stillAromatic.begin(), stillAromatic.end(), true) == 6,
              "nor the perceived aromaticity");
        const MoleculeGraph copied = marked.subgraph({0, 1, 2});
        check(copied.atoms()[0].highlight == 2
                  && copied.atoms()[1].highlight == 2,
              "and it travels with the atoms through subgraph() — which is "
              "what makes copy/paste carry a highlighted region");
        marked.removeAtoms({0});
        check(marked.atomCount() == 5 && marked.atoms()[0].highlight == 2,
              "removeAtoms() takes the highlight away with its atom and "
              "leaves the rest attached to the right ones after compaction");
    }

    // -----------------------------------------------------------------------
    // SMILES
    // -----------------------------------------------------------------------
    std::printf("SMILES parsing:\n");
    {
        struct Case {
            const char* smiles;
            const char* formula;
        };
        const Case cases[] = {
            {"C", "CH4"},
            {"CCO", "C2H6O"},
            {"CC(=O)O", "C2H4O2"},
            {"O", "H2O"},
            {"CC#N", "C2H3N"},
            {"c1ccccc1", "C6H6"},
            {"C1=CC=CC=C1", "C6H6"},
            {"C1CCCCC1", "C6H12"},
            {"c1ccncc1", "C5H5N"},
            {"c1cc[nH]c1", "C4H5N"},
            {"c1ccsc1", "C4H4S"},
            {"c1ccoc1", "C4H4O"},
            {"c1ccc2ccccc2c1", "C10H8"},
            {"CC(C)(C)Br", "C4H9Br"},
            {"[NH4+].[Cl-]", "ClH4N"},
            {"OC(=O)c1ccccc1", "C7H6O2"},
            {"CC(N)C(=O)O", "C3H7NO2"},
        };
        for (const Case& one : cases) {
            MoleculeGraph graph;
            std::string error;
            const bool ok = smiles::parseTopology(one.smiles, graph, &error);
            if (!ok) {
                check(false, std::string(one.smiles) + " parses (" + error + ")");
                continue;
            }
            check(graph.formula() == one.formula,
                  std::string(one.smiles) + " -> " + one.formula + " (got "
                      + graph.formula() + ")");
        }

        // Kekulization: the graph never stores an aromatic bond order, so an
        // aromatic input must come back as alternating single/double.
        MoleculeGraph benzene;
        smiles::parseTopology("c1ccccc1", benzene, nullptr);
        int doubles = 0;
        for (const MolBond& bond : benzene.bonds()) {
            check(bond.order == 1 || bond.order == 2,
                  "every parsed aromatic bond is a Kekule single or double");
            if (bond.order == 2)
                ++doubles;
        }
        check(doubles == 3, "benzene kekulizes to exactly three double bonds");
    }

    std::printf("SMILES errors are reported, not swallowed:\n");
    {
        const char* broken[] = {"C1CC",       // unclosed ring
                                "CC(C",       // unclosed branch
                                "CC)C",       // unopened branch
                                "CXC",        // not an element
                                "[Zz]",       // not an element, in brackets
                                "c1cccc1",    // five aromatic carbons: no
                                              // Kekule structure exists
                                "*",          // a wildcard is not a structure
                                "C$C"};       // quadruple: the graph tops out
                                              // at 3, so accepting it would
                                              // silently mean a triple bond
        for (const char* text : broken) {
            MoleculeGraph graph;
            graph.addAtom(6, 0.0, 0.0); // something to lose, if it were lost
            std::string error;
            const bool ok = smiles::parseTopology(text, graph, &error);
            check(!ok, std::string("\"") + text + "\" is refused");
            check(!error.empty(), "  with a stated reason: " + error);
            check(graph.atomCount() == 1,
                  "  and the caller's graph is untouched");
        }
    }

    std::printf("SMILES round-trips:\n");
    {
        // Write-then-read is the honest round trip: the exact STRING need not
        // be reproduced (many SMILES denote one molecule), but the molecule
        // must be.
        const char* inputs[] = {"CCO",       "CC(=O)O",  "c1ccccc1",
                                "c1ccncc1",  "c1cc[nH]c1", "C1CCCCC1",
                                "c1ccc2ccccc2c1", "CC(C)(C)Br",
                                "OC(=O)c1ccccc1"};
        for (const char* text : inputs) {
            MoleculeGraph first;
            if (!smiles::parseTopology(text, first, nullptr)) {
                check(false, std::string(text) + " parses");
                continue;
            }
            const std::string written = smiles::write(first);
            MoleculeGraph second;
            std::string error;
            const bool ok = smiles::parseTopology(written, second, &error);
            check(ok, std::string(text) + " -> \"" + written
                          + "\" parses again (" + error + ")");
            if (!ok)
                continue;
            check(second.formula() == first.formula(),
                  "  and denotes the same molecule: " + first.formula()
                      + " vs " + second.formula());
            check(second.bondCount() == first.bondCount(),
                  "  with the same bond count");
        }

        // Aromatic rings are written lowercase — a chemist expects
        // "c1ccccc1", not the equally valid "C1=CC=CC=C1".
        check(smiles::write(makeRing(RingTemplate::Benzene, 0, 0)) == "c1ccccc1",
              "the benzene template writes as c1ccccc1");
        check(smiles::write(makeRing(RingTemplate::Cyclohexane, 0, 0))
                  == "C1CCCCC1",
              "cyclohexane writes uppercase");
    }

    std::printf("SMILES layout:\n");
    {
        MoleculeGraph benzene;
        std::string error;
        check(smiles::parse("c1ccccc1", benzene, &error),
              "a parsed structure comes back laid out");
        // Every ring bond the same length, which is what "cleanly laid out"
        // means for a ring.
        double shortest = 1e300;
        double longest = 0.0;
        for (const MolBond& bond : benzene.bonds()) {
            const MolAtom& a = benzene.atoms()[static_cast<std::size_t>(bond.a)];
            const MolAtom& b = benzene.atoms()[static_cast<std::size_t>(bond.b)];
            const double d = std::hypot(b.x - a.x, b.y - a.y);
            shortest = std::min(shortest, d);
            longest = std::max(longest, d);
        }
        checkNear(shortest, MoleculeGraph::kBondLength, 0.05,
                  "the laid-out ring's shortest bond is the standard length");
        checkNear(longest - shortest, 0.0, 0.05,
                  "and every bond is that same length");

        // Two fragments are placed side by side, not on top of each other.
        MoleculeGraph salt;
        check(smiles::parse("[NH4+].[Cl-]", salt, &error),
              "a two-fragment SMILES lays out");
        check(salt.atomCount() == 2, "  as two atoms");
        if (salt.atomCount() == 2) {
            const double separation =
                std::hypot(salt.atoms()[1].x - salt.atoms()[0].x,
                           salt.atoms()[1].y - salt.atoms()[0].y);
            check(separation > MoleculeGraph::kBondLength,
                  "  placed clear of each other, not stacked");
        }
    }

    // -----------------------------------------------------------------------
    // Tidy
    // -----------------------------------------------------------------------
    std::printf("Tidy:\n");
    {
        // A deliberately mangled hexagon: right topology, wrong geometry.
        MoleculeGraph ugly = makeRing(RingTemplate::Cyclohexane, 0.0, 0.0);
        for (int i = 0; i < ugly.atomCount(); ++i) {
            ugly.atoms()[static_cast<std::size_t>(i)].x *= 0.35;
            ugly.atoms()[static_cast<std::size_t>(i)].y *= 1.9;
        }
        const auto spread = [](const MoleculeGraph& graph) {
            double shortest = 1e300;
            double longest = 0.0;
            for (const MolBond& bond : graph.bonds()) {
                const MolAtom& a = graph.atoms()[static_cast<std::size_t>(bond.a)];
                const MolAtom& b = graph.atoms()[static_cast<std::size_t>(bond.b)];
                const double d = std::hypot(b.x - a.x, b.y - a.y);
                shortest = std::min(shortest, d);
                longest = std::max(longest, d);
            }
            return longest - shortest;
        };
        const double before = spread(ugly);
        tidy(ugly);
        const double after = spread(ugly);
        check(before > 0.3, "the mangled ring really is irregular to start");
        check(after < 0.05,
              "and tidy leaves every bond the same length (spread "
                  + std::to_string(after) + " A)");

        // Tidying a SELECTION leaves everything else exactly where it was.
        MoleculeGraph twoRings = makeRing(RingTemplate::Cyclohexane, 0.0, 0.0);
        stampRing(twoRings, RingTemplate::Cyclohexane, 6.0, 0.0);
        const double untouchedX = twoRings.atoms()[7].x;
        const double untouchedY = twoRings.atoms()[7].y;
        std::vector<int> firstRing{0, 1, 2, 3, 4, 5};
        for (int i : firstRing)
            twoRings.atoms()[static_cast<std::size_t>(i)].x *= 0.4;
        tidy(twoRings, firstRing);
        checkNear(twoRings.atoms()[7].x, untouchedX, 1e-9,
                  "an atom outside the selection did not move (x)");
        checkNear(twoRings.atoms()[7].y, untouchedY, 1e-9,
                  "an atom outside the selection did not move (y)");
    }

    // -----------------------------------------------------------------------
    // THE BENZENE TEST — both routes into the 2D -> 3D pipeline
    // -----------------------------------------------------------------------
    {
        // Route 1: the ring template's own code path, which is exactly what
        // clicking Benzene in the palette runs.
        MoleculeGraph drawn;
        stampRing(drawn, RingTemplate::Benzene, 0.0, 0.0);
        check(drawn.formula() == "C6H6",
              "the drawn benzene is C6H6 before any export");
        Structure structure;
        const EmbedResult result = embed(drawn, structure);
        check(result.ok, "the export pipeline succeeded");
        check(result.hydrogensAdded == 6, "adding six hydrogens");
        check(result.residual < 1e-4,
              "and converging (residual " + std::to_string(result.residual)
                  + ")");
        assertBenzene(structure, "Benzene, drawn with the ring template");
    }
    {
        // Route 2: SMILES.
        MoleculeGraph parsed;
        std::string error;
        check(smiles::parse("c1ccccc1", parsed, &error),
              "\"c1ccccc1\" imports");
        Structure structure;
        const EmbedResult result = embed(parsed, structure);
        check(result.ok, "and exports");
        assertBenzene(structure, "Benzene, imported from \"c1ccccc1\"");
    }

    // -----------------------------------------------------------------------
    // The rest of the export pipeline
    // -----------------------------------------------------------------------
    std::printf("Export pipeline:\n");
    {
        // Hydrogens off leaves the skeleton alone.
        MoleculeGraph benzene = makeRing(RingTemplate::Benzene, 0, 0);
        EmbedOptions bare;
        bare.addHydrogens = false;
        Structure skeleton;
        embed(benzene, skeleton, bare);
        check(skeleton.size() == 6, "with hydrogens off, six atoms are sent");
        check(countElement(skeleton, 1) == 0, "and none of them is hydrogen");

        // A SELECTION wins over the whole canvas — the contract the button's
        // tooltip states.
        MoleculeGraph twoRings = makeRing(RingTemplate::Benzene, 0.0, 0.0);
        stampRing(twoRings, RingTemplate::Cyclohexane, 8.0, 0.0);
        check(twoRings.fragments().size() == 2, "two rings are two fragments");
        Structure everything;
        embed(twoRings, everything);
        check(everything.chemicalFormula() == "C12H18",
              "no selection sends BOTH fragments (C12H18)");
        Structure onlyFirst;
        embed(twoRings, {0, 1, 2, 3, 4, 5}, onlyFirst);
        check(onlyFirst.chemicalFormula() == "C6H6",
              "a selection sends only what is selected (C6H6)");

        // Ethanol: a molecule that must NOT come out flat.
        MoleculeGraph ethanol;
        smiles::parse("CCO", ethanol, nullptr);
        Structure relaxed;
        embed(ethanol, relaxed);
        check(relaxed.chemicalFormula() == "C2H6O", "ethanol exports as C2H6O");
        check(planarityRms(relaxed) > 0.2,
              "and is genuinely three-dimensional, not the flat drawing "
              "(RMS out of plane "
                  + std::to_string(planarityRms(relaxed)) + " A)");
        const std::vector<double> cc = pairDistances(relaxed, 6, 6, 1.8);
        check(cc.size() == 1, "one C-C bond");
        if (!cc.empty())
            checkRange(cc.front(), 1.48, 1.58, "a single C-C bond (A)");
        const std::vector<double> co = pairDistances(relaxed, 6, 8, 1.8);
        check(co.size() == 1, "one C-O bond");
        if (!co.empty())
            checkRange(co.front(), 1.38, 1.48, "a single C-O bond (A)");

        // Acetic acid: the case that caught an INVERTED SIGN in the sp2
        // planarity restraint. The carboxyl carbon is the only sp2 centre
        // whose plane is not already fixed by a ring, so it is the only place
        // a planarity term that pushes OUT of the plane instead of back into
        // it shows up — and when it did, every heavy-atom bond in the molecule
        // came out stretched (C–C 1.79 A) and the relaxation plateaued at a
        // residual of 2e-2 instead of converging. Benzene passed throughout:
        // a drawing starts exactly flat, so the restraint's argument was zero.
        MoleculeGraph acid;
        smiles::parse("CC(=O)O", acid, nullptr);
        Structure carboxyl;
        const EmbedResult acidResult = embed(acid, carboxyl);
        check(carboxyl.chemicalFormula() == "C2H4O2",
              "acetic acid exports as C2H4O2");
        check(acidResult.residual < 1e-5,
              "and the relaxation CONVERGES (residual "
                  + std::to_string(acidResult.residual) + ")");
        {
            const std::vector<double> acidCc = pairDistances(carboxyl, 6, 6, 1.9);
            check(acidCc.size() == 1, "one C-C bond");
            if (!acidCc.empty())
                checkRange(acidCc.front(), 1.48, 1.58,
                           "its C-C is a single bond, not stretched (A)");
            const std::vector<double> co = pairDistances(carboxyl, 6, 8, 1.9);
            check(co.size() == 2, "two C-O bonds");
            if (co.size() == 2) {
                const auto [shorter, longer] =
                    std::minmax_element(co.begin(), co.end());
                checkRange(*shorter, 1.20, 1.30, "the C=O double bond (A)");
                checkRange(*longer, 1.38, 1.48, "the C-O single bond (A)");
            }
            // The carboxyl group itself must be FLAT: the three heavy atoms
            // around the sp2 carbon plus that carbon.
            Structure group;
            for (const Atom& atom : carboxyl.atoms()) {
                if (atom.atomicNumber != 1)
                    group.addAtom(atom);
            }
            checkNear(planarityRms(group), 0.0, 0.02,
                      "and the four heavy atoms are coplanar (A)");
        }

        // Acetylene: the linear case the angle model has to get right.
        MoleculeGraph acetylene;
        smiles::parse("C#C", acetylene, nullptr);
        Structure linear;
        embed(acetylene, linear);
        check(linear.chemicalFormula() == "C2H2", "acetylene exports as C2H2");
        {
            // H-C-C must be 180 deg. Indices are found by element rather than
            // assumed: embed() appends hydrogens after the drawn atoms, but a
            // test that encodes that ordering would start failing for a reason
            // that has nothing to do with the geometry.
            std::vector<std::size_t> carbons;
            std::vector<std::size_t> hydrogens;
            for (std::size_t i = 0; i < linear.size(); ++i) {
                if (linear.atoms()[i].atomicNumber == 6)
                    carbons.push_back(i);
                else if (linear.atoms()[i].atomicNumber == 1)
                    hydrogens.push_back(i);
            }
            check(carbons.size() == 2 && hydrogens.size() == 2,
                  "two carbons and two hydrogens");
            if (carbons.size() == 2 && hydrogens.size() == 2) {
                // Pair each hydrogen with its OWN carbon (the nearer one),
                // then measure that H-C-C.
                for (std::size_t h : hydrogens) {
                    const double d0 = (linear.atoms()[h].position
                                       - linear.atoms()[carbons[0]].position)
                                          .norm();
                    const double d1 = (linear.atoms()[h].position
                                       - linear.atoms()[carbons[1]].position)
                                          .norm();
                    const std::size_t own = d0 < d1 ? carbons[0] : carbons[1];
                    const std::size_t far = d0 < d1 ? carbons[1] : carbons[0];
                    const Vec3 u = (linear.atoms()[h].position
                                    - linear.atoms()[own].position)
                                       .normalized();
                    const Vec3 v = (linear.atoms()[far].position
                                    - linear.atoms()[own].position)
                                       .normalized();
                    const double angle =
                        std::acos(std::clamp(u.dot(v), -1.0, 1.0)) * 180.0 / kPi;
                    checkNear(angle, 180.0, 2.0, "H-C-C is linear (deg)");
                }
            }
        }

        // An empty canvas is refused with a message, not a crash.
        MoleculeGraph nothing;
        Structure unused;
        const EmbedResult refused = embed(nothing, unused);
        check(!refused.ok, "an empty sketch is refused");
        check(!refused.error.empty(), "with an explanation");
    }

    std::printf(failures == 0 ? "\nAll Molecular Design checks passed.\n"
                              : "\nchecks FAILED.\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
