#include "core/Smiles.hpp"

#include "core/Element.hpp"
#include "core/PhysicalConstants.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace calango::core::smiles {
namespace {

/// Elements writable without brackets, and the only ones whose aromatic form
/// has a lowercase spelling.
bool isOrganicSubset(int z)
{
    switch (z) {
    case 5: case 6: case 7: case 8: case 9:
    case 15: case 16: case 17: case 35: case 53:
        return true;
    default:
        return false;
    }
}

/// Atomic number for a lowercase aromatic atom letter, 0 when it is not one.
int aromaticElement(char c)
{
    switch (c) {
    case 'b': return 5;
    case 'c': return 6;
    case 'n': return 7;
    case 'o': return 8;
    case 'p': return 15;
    case 's': return 16;
    default:  return 0;
    }
}

struct ParseState {
    MoleculeGraph* graph = nullptr;
    /// Per-atom aromatic flag as WRITTEN. Erased by kekulize(), which turns it
    /// into real bond orders — the graph itself never stores aromaticity.
    std::vector<bool> aromatic;
    /// Hydrogens a bracket atom stated explicitly, -1 when the atom was
    /// written without brackets (so the count is implicit).
    std::vector<int> bracketHydrogens;
    std::string error;
};

/// A pending ring-closure: the atom that opened it and the bond order written
/// on the opening (0 = unspecified).
struct RingOpen {
    int atom = -1;
    int order = 0;
    bool aromatic = false;
};

int orderForSymbol(char symbol)
{
    switch (symbol) {
    case '-': return 1;
    case '=': return 2;
    case '#': return 3;
    // '$' (quadruple, an organometallic bond) is deliberately NOT here. The
    // graph tops out at order 3, so accepting it would silently hand back a
    // triple bond — a different molecule, not a degraded one. Falling through
    // to 0 makes it an "unexpected character" with a position, which is the
    // honest answer.
    case ':': return 1; // aromatic bond, resolved by kekulize()
    case '/': case '\\': return 1; // configuration marks: order only
    default:  return 0;
    }
}

/// Read a bracket atom starting at `text[i]` (which is '['), advancing `i`
/// past the closing ']'. Returns false on a malformed bracket.
bool readBracketAtom(const std::string& text, std::size_t& i, int& z,
                     int& charge, int& hydrogens, bool& aromatic,
                     std::string& error)
{
    const std::size_t open = i;
    ++i; // '['
    // Isotope: digits, dropped.
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
        ++i;
    if (i >= text.size()) {
        error = "unclosed '[' at position " + std::to_string(open + 1);
        return false;
    }

    std::string symbol;
    if (text[i] == '*') {
        error = "wildcard atom '*' at position " + std::to_string(i + 1)
            + " is not a structure";
        return false;
    }
    if (!std::isalpha(static_cast<unsigned char>(text[i]))) {
        error = "expected an element symbol at position " + std::to_string(i + 1);
        return false;
    }
    symbol += text[i++];
    if (i < text.size() && std::islower(static_cast<unsigned char>(text[i]))
        && std::isupper(static_cast<unsigned char>(symbol[0]))) {
        // Two-letter symbol; but "Cl" vs "C" then "l" is unambiguous inside a
        // bracket, where the whole symbol is one token.
        const std::string two = symbol + text[i];
        if (Elements::atomicNumber(two) != 0) {
            symbol = two;
            ++i;
        }
    }
    aromatic = std::islower(static_cast<unsigned char>(symbol[0])) != 0;
    if (aromatic) {
        z = aromaticElement(symbol[0]);
        if (z == 0 && symbol.size() > 1) {
            std::string upper = symbol;
            upper[0] = static_cast<char>(
                std::toupper(static_cast<unsigned char>(upper[0])));
            z = Elements::atomicNumber(upper);
        }
    } else {
        z = Elements::atomicNumber(symbol);
    }
    if (z == 0) {
        error = "unknown element \"" + symbol + "\" at position "
            + std::to_string(open + 2);
        return false;
    }

    hydrogens = 0;
    charge = 0;
    while (i < text.size() && text[i] != ']') {
        const char c = text[i];
        if (c == '@') {
            ++i; // chirality, dropped
            if (i < text.size() && text[i] == '@')
                ++i;
            continue;
        }
        if (c == 'H') {
            ++i;
            int count = 1;
            if (i < text.size()
                && std::isdigit(static_cast<unsigned char>(text[i]))) {
                count = 0;
                while (i < text.size()
                       && std::isdigit(static_cast<unsigned char>(text[i]))) {
                    count = count * 10 + (text[i] - '0');
                    ++i;
                }
            }
            hydrogens = count;
            continue;
        }
        if (c == '+' || c == '-') {
            const int sign = c == '+' ? 1 : -1;
            ++i;
            if (i < text.size()
                && std::isdigit(static_cast<unsigned char>(text[i]))) {
                int magnitude = 0;
                while (i < text.size()
                       && std::isdigit(static_cast<unsigned char>(text[i]))) {
                    magnitude = magnitude * 10 + (text[i] - '0');
                    ++i;
                }
                charge += sign * magnitude;
            } else {
                // "++" / "--": one unit per repeated sign.
                charge += sign;
                while (i < text.size() && text[i] == c) {
                    charge += sign;
                    ++i;
                }
            }
            continue;
        }
        if (c == ':') {
            ++i; // atom map, dropped
            while (i < text.size()
                   && std::isdigit(static_cast<unsigned char>(text[i])))
                ++i;
            continue;
        }
        error = std::string("unexpected '") + c + "' inside brackets at position "
            + std::to_string(i + 1);
        return false;
    }
    if (i >= text.size()) {
        error = "unclosed '[' at position " + std::to_string(open + 1);
        return false;
    }
    ++i; // ']'
    return true;
}

/// Turn the aromatic flags into real alternating bond orders.
///
/// Every aromatic atom that needs a π bond is matched to exactly one aromatic
/// neighbour, by backtracking over the aromatic bonds. An atom that does NOT
/// need one — a pyrrole-type nitrogen, a furan oxygen, a thiophene sulfur, or
/// any aromatic atom already carrying an exocyclic double bond — donates its
/// lone pair instead and is left out of the matching.
///
/// Returns false when no perfect matching exists, which is what an
/// unkekulizable aromatic system (a typo, or a radical written as aromatic)
/// looks like.
bool kekulize(ParseState& state)
{
    MoleculeGraph& graph = *state.graph;
    const int count = graph.atomCount();

    std::vector<int> aromaticBonds;
    for (int i = 0; i < graph.bondCount(); ++i) {
        const MolBond& bond = graph.bonds()[static_cast<std::size_t>(i)];
        if (state.aromatic[static_cast<std::size_t>(bond.a)]
            && state.aromatic[static_cast<std::size_t>(bond.b)])
            aromaticBonds.push_back(i);
    }
    if (aromaticBonds.empty())
        return true;

    std::vector<bool> needsPi(static_cast<std::size_t>(count), false);
    for (int atom = 0; atom < count; ++atom) {
        if (!state.aromatic[static_cast<std::size_t>(atom)])
            continue;
        const MolAtom& a = graph.atoms()[static_cast<std::size_t>(atom)];
        // An exocyclic multiple bond already uses the π electron.
        bool exocyclicPi = false;
        for (const MolBond& bond : graph.bonds()) {
            if (bond.touches(atom) && bond.order >= 2)
                exocyclicPi = true;
        }
        if (exocyclicPi)
            continue;

        const int connections = static_cast<int>(graph.neighbors(atom).size());
        const int stated = state.bracketHydrogens[static_cast<std::size_t>(atom)];
        const int hydrogens = stated >= 0 ? stated : 0;
        const std::vector<int> valences = standardValences(a.atomicNumber, a.charge);
        if (valences.empty())
            continue;
        // Room for one more bond beyond the σ frame it already has? Aromatic
        // carbon always has; a three-connected neutral nitrogen has not, and
        // that is exactly the pyrrole case.
        needsPi[static_cast<std::size_t>(atom)] =
            valences.front() - connections - hydrogens >= 1;
    }

    // Backtracking perfect matching over the aromatic bonds.
    std::vector<bool> matched(static_cast<std::size_t>(count), false);
    std::vector<int> chosen;
    std::vector<std::vector<int>> incident(static_cast<std::size_t>(count));
    for (int bondIndex : aromaticBonds) {
        const MolBond& bond = graph.bonds()[static_cast<std::size_t>(bondIndex)];
        incident[static_cast<std::size_t>(bond.a)].push_back(bondIndex);
        incident[static_cast<std::size_t>(bond.b)].push_back(bondIndex);
    }

    // Recursive by hand so the "first unmatched atom" choice stays explicit —
    // always extending from the most constrained end keeps the search linear on
    // every fused system this parser will ever see.
    struct Matcher {
        const MoleculeGraph& graph;
        const std::vector<bool>& needsPi;
        const std::vector<std::vector<int>>& incident;
        std::vector<bool>& matched;
        std::vector<int>& chosen;

        bool solve(int from)
        {
            int atom = -1;
            int fewest = 0;
            for (int i = from; i < graph.atomCount(); ++i) {
                if (!needsPi[static_cast<std::size_t>(i)]
                    || matched[static_cast<std::size_t>(i)])
                    continue;
                int options = 0;
                for (int bondIndex : incident[static_cast<std::size_t>(i)]) {
                    const MolBond& bond =
                        graph.bonds()[static_cast<std::size_t>(bondIndex)];
                    const int other = bond.other(i);
                    if (needsPi[static_cast<std::size_t>(other)]
                        && !matched[static_cast<std::size_t>(other)])
                        ++options;
                }
                if (atom < 0 || options < fewest) {
                    atom = i;
                    fewest = options;
                }
                if (fewest == 0)
                    break;
            }
            if (atom < 0)
                return true; // everything that needed a π bond has one

            for (int bondIndex : incident[static_cast<std::size_t>(atom)]) {
                const MolBond& bond =
                    graph.bonds()[static_cast<std::size_t>(bondIndex)];
                const int other = bond.other(atom);
                if (!needsPi[static_cast<std::size_t>(other)]
                    || matched[static_cast<std::size_t>(other)])
                    continue;
                matched[static_cast<std::size_t>(atom)] = true;
                matched[static_cast<std::size_t>(other)] = true;
                chosen.push_back(bondIndex);
                if (solve(0))
                    return true;
                chosen.pop_back();
                matched[static_cast<std::size_t>(atom)] = false;
                matched[static_cast<std::size_t>(other)] = false;
            }
            return false;
        }
    };

    Matcher matcher{graph, needsPi, incident, matched, chosen};
    if (!matcher.solve(0)) {
        state.error = "this aromatic system cannot be written with alternating "
                      "single and double bonds";
        return false;
    }
    for (int bondIndex : chosen)
        graph.bonds()[static_cast<std::size_t>(bondIndex)].order = 2;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

bool parseTopology(const std::string& text, MoleculeGraph& graph,
                   std::string* error)
{
    MoleculeGraph built;
    ParseState state;
    state.graph = &built;

    std::vector<int> branchStack;
    std::map<int, RingOpen> ringOpens;
    int previous = -1;
    int pendingOrder = 0;
    bool pendingAromaticBond = false;

    const auto fail = [&](const std::string& message) {
        if (error)
            *error = message;
        return false;
    };

    const auto placeAtom = [&](int z, int charge, int hydrogens, bool aromatic) {
        MolAtom atom;
        atom.atomicNumber = z;
        atom.charge = charge;
        atom.explicitHydrogens = hydrogens; // -1 for a bare organic-subset atom
        const int index = built.addAtom(atom);
        state.aromatic.push_back(aromatic);
        state.bracketHydrogens.push_back(hydrogens);
        if (previous >= 0) {
            const int order = pendingOrder > 0 ? pendingOrder : 1;
            built.addBond(previous, index, order);
        }
        previous = index;
        pendingOrder = 0;
        pendingAromaticBond = false;
        return index;
    };

    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '(') {
            if (previous < 0)
                return fail("'(' at position " + std::to_string(i + 1)
                            + " opens a branch with no atom to branch from");
            branchStack.push_back(previous);
            ++i;
            continue;
        }
        if (c == ')') {
            if (branchStack.empty())
                return fail("')' at position " + std::to_string(i + 1)
                            + " closes a branch that was never opened");
            previous = branchStack.back();
            branchStack.pop_back();
            ++i;
            continue;
        }
        if (c == '.') {
            previous = -1;
            pendingOrder = 0;
            ++i;
            continue;
        }
        if (const int order = orderForSymbol(c); order != 0) {
            pendingOrder = order;
            pendingAromaticBond = c == ':';
            ++i;
            continue;
        }
        if (c == '%' || std::isdigit(static_cast<unsigned char>(c))) {
            int label = 0;
            if (c == '%') {
                ++i;
                if (i + 1 >= text.size()
                    || !std::isdigit(static_cast<unsigned char>(text[i]))
                    || !std::isdigit(static_cast<unsigned char>(text[i + 1])))
                    return fail("'%' at position " + std::to_string(i)
                                + " must be followed by two digits");
                label = (text[i] - '0') * 10 + (text[i + 1] - '0');
                i += 2;
            } else {
                label = c - '0';
                ++i;
            }
            if (previous < 0)
                return fail("ring-closure digit at position " + std::to_string(i)
                            + " with no atom to close onto");
            const auto open = ringOpens.find(label);
            if (open == ringOpens.end()) {
                ringOpens[label] = RingOpen{previous, pendingOrder,
                                            pendingAromaticBond};
            } else {
                if (open->second.atom == previous)
                    return fail("ring-closure " + std::to_string(label)
                                + " bonds an atom to itself");
                const int order = pendingOrder > 0     ? pendingOrder
                    : open->second.order > 0           ? open->second.order
                                                       : 1;
                built.addBond(open->second.atom, previous, order);
                ringOpens.erase(open);
            }
            pendingOrder = 0;
            pendingAromaticBond = false;
            continue;
        }
        if (c == '[') {
            int z = 0;
            int charge = 0;
            int hydrogens = 0;
            bool aromatic = false;
            std::string message;
            if (!readBracketAtom(text, i, z, charge, hydrogens, aromatic,
                                 message))
                return fail(message);
            placeAtom(z, charge, hydrogens, aromatic);
            continue;
        }
        if (c == '*')
            return fail("wildcard atom '*' at position " + std::to_string(i + 1)
                        + " is not a structure");
        if (std::isalpha(static_cast<unsigned char>(c))) {
            // Organic subset, no brackets. Two-letter symbols first: "Cl" and
            // "Br" are the only ones, and both would otherwise be read as a
            // carbon or a boron followed by a stray letter.
            int z = 0;
            bool aromatic = false;
            if (i + 1 < text.size()) {
                const std::string two = text.substr(i, 2);
                if (two == "Cl" || two == "Br") {
                    z = Elements::atomicNumber(two);
                    i += 2;
                }
            }
            if (z == 0) {
                if (std::isupper(static_cast<unsigned char>(c))) {
                    z = Elements::atomicNumber(std::string(1, c));
                    if (z == 0 || !isOrganicSubset(z))
                        return fail(std::string("\"") + c + "\" at position "
                                    + std::to_string(i + 1)
                                    + " is not in the organic subset — write it "
                                      "in brackets, e.g. [" + c + "]");
                } else {
                    z = aromaticElement(c);
                    if (z == 0)
                        return fail(std::string("\"") + c + "\" at position "
                                    + std::to_string(i + 1)
                                    + " is not an aromatic organic-subset atom");
                    aromatic = true;
                }
                ++i;
            }
            placeAtom(z, 0, -1, aromatic);
            continue;
        }
        return fail(std::string("unexpected character '") + c + "' at position "
                    + std::to_string(i + 1));
    }

    if (!branchStack.empty())
        return fail("a branch opened with '(' was never closed");
    if (!ringOpens.empty())
        return fail("ring-closure " + std::to_string(ringOpens.begin()->first)
                    + " was opened and never closed");

    if (!kekulize(state))
        return fail(state.error);

    // A bracket atom's hydrogen count is a statement and stays pinned; a bare
    // organic-subset atom's is not, and goes back to being derived from the
    // valence so that later editing on the canvas keeps it current.
    for (int atom = 0; atom < built.atomCount(); ++atom) {
        if (state.bracketHydrogens[static_cast<std::size_t>(atom)] < 0)
            built.atoms()[static_cast<std::size_t>(atom)].explicitHydrogens = -1;
    }

    graph = std::move(built);
    return true;
}

bool parse(const std::string& text, MoleculeGraph& graph, std::string* error)
{
    MoleculeGraph built;
    if (!parseTopology(text, built, error))
        return false;
    layout2d(built);
    graph = std::move(built);
    return true;
}

// ---------------------------------------------------------------------------
// 2D layout
// ---------------------------------------------------------------------------

namespace {

/// Place `ring`'s unplaced atoms as a regular polygon, anchored on whichever of
/// its atoms already have coordinates.
void placeRing(MoleculeGraph& graph, const std::vector<int>& ring,
               std::vector<bool>& placed)
{
    const int size = static_cast<int>(ring.size());
    const double radius = MoleculeGraph::kBondLength / (2.0 * std::sin(kPi / size));

    std::vector<int> anchors;
    for (int atom : ring) {
        if (placed[static_cast<std::size_t>(atom)])
            anchors.push_back(atom);
    }

    double cx = 0.0;
    double cy = 0.0;
    double startAngle = kPi / 2.0;
    int startIndex = 0;
    double direction = 1.0;

    if (anchors.empty()) {
        cx = 0.0;
        cy = 0.0;
    } else if (anchors.size() == 1) {
        // Hang the ring off the single placed atom, pointing away from
        // whatever that atom is already attached to.
        const int anchor = anchors.front();
        const MolAtom& a = graph.atoms()[static_cast<std::size_t>(anchor)];
        double ax = 0.0;
        double ay = 0.0;
        int neighborCount = 0;
        for (int neighbor : graph.neighbors(anchor)) {
            if (!placed[static_cast<std::size_t>(neighbor)])
                continue;
            const MolAtom& n = graph.atoms()[static_cast<std::size_t>(neighbor)];
            ax += n.x - a.x;
            ay += n.y - a.y;
            ++neighborCount;
        }
        double outX = -ax;
        double outY = -ay;
        if (neighborCount == 0 || std::hypot(outX, outY) < 1e-9) {
            outX = 0.0;
            outY = 1.0;
        }
        const double length = std::hypot(outX, outY);
        cx = a.x + radius * outX / length;
        cy = a.y + radius * outY / length;
        startAngle = std::atan2(a.y - cy, a.x - cx);
        startIndex = static_cast<int>(
            std::find(ring.begin(), ring.end(), anchor) - ring.begin());
    } else {
        // Two or more placed atoms: fuse across the first ADJACENT placed
        // pair, which is the shared edge of a fused system.
        int first = -1;
        int second = -1;
        for (int i = 0; i < size && first < 0; ++i) {
            const int a = ring[static_cast<std::size_t>(i)];
            const int b = ring[static_cast<std::size_t>((i + 1) % size)];
            if (placed[static_cast<std::size_t>(a)]
                && placed[static_cast<std::size_t>(b)]) {
                first = i;
                second = (i + 1) % size;
            }
        }
        if (first < 0) {
            // Placed but not adjacent — a spiro or bridged system this simple
            // layout cannot anchor exactly. Fall back to the single-anchor
            // case; tidy() cleans up what is left.
            std::vector<bool> single = placed;
            for (std::size_t i = 1; i < anchors.size(); ++i)
                single[static_cast<std::size_t>(anchors[i])] = false;
            placeRing(graph, ring, single);
            for (int atom : ring)
                placed[static_cast<std::size_t>(atom)] = true;
            return;
        }
        const MolAtom& a =
            graph.atoms()[static_cast<std::size_t>(ring[static_cast<std::size_t>(first)])];
        const MolAtom& b =
            graph.atoms()[static_cast<std::size_t>(ring[static_cast<std::size_t>(second)])];
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double length = std::max(1e-9, std::hypot(dx, dy));
        const double apothem = length / (2.0 * std::tan(kPi / size));
        const double midX = 0.5 * (a.x + b.x);
        const double midY = 0.5 * (a.y + b.y);
        const double nx = -dy / length;
        const double ny = dx / length;
        const auto crowding = [&graph, &placed](double px, double py) {
            double score = 0.0;
            for (int i = 0; i < graph.atomCount(); ++i) {
                if (!placed[static_cast<std::size_t>(i)])
                    continue;
                const MolAtom& atom = graph.atoms()[static_cast<std::size_t>(i)];
                score += 1.0 / (0.25 + std::hypot(atom.x - px, atom.y - py));
            }
            return score;
        };
        const double sign =
            crowding(midX + nx * apothem, midY + ny * apothem)
                > crowding(midX - nx * apothem, midY - ny * apothem)
            ? -1.0
            : 1.0;
        cx = midX + sign * nx * apothem;
        cy = midY + sign * ny * apothem;
        startAngle = std::atan2(a.y - cy, a.x - cx);
        startIndex = first;
        // Walk in whichever direction actually reaches `b` next, so the
        // polygon's vertex order matches the ring's traversal order.
        const double toB = std::atan2(b.y - cy, b.x - cx);
        double delta = toB - startAngle;
        while (delta <= -kPi)
            delta += 2.0 * kPi;
        while (delta > kPi)
            delta -= 2.0 * kPi;
        direction = delta > 0.0 ? 1.0 : -1.0;
    }

    const double step = direction * 2.0 * kPi / size;
    for (int k = 0; k < size; ++k) {
        const int atom = ring[static_cast<std::size_t>((startIndex + k) % size)];
        if (placed[static_cast<std::size_t>(atom)])
            continue;
        const double angle = startAngle + step * k;
        graph.moveAtom(atom, cx + radius * std::cos(angle),
                       cy + radius * std::sin(angle));
        placed[static_cast<std::size_t>(atom)] = true;
    }
    for (int atom : ring)
        placed[static_cast<std::size_t>(atom)] = true;
}

} // namespace

void layout2d(MoleculeGraph& graph)
{
    const int count = graph.atomCount();
    if (count == 0)
        return;

    const std::vector<std::vector<int>> ringList = graph.rings();
    // Which rings each atom belongs to, so expanding an atom can pull in the
    // whole ring at once.
    std::vector<std::vector<int>> ringsOf(static_cast<std::size_t>(count));
    for (std::size_t r = 0; r < ringList.size(); ++r) {
        for (int atom : ringList[r])
            ringsOf[static_cast<std::size_t>(atom)].push_back(static_cast<int>(r));
    }
    std::vector<bool> ringPlaced(ringList.size(), false);
    std::vector<bool> placed(static_cast<std::size_t>(count), false);

    double fragmentOffset = 0.0;
    for (const std::vector<int>& fragment : graph.fragments()) {
        // Seed on a ring atom when the fragment has a ring — starting inside
        // the ring system is what keeps a fused framework regular.
        int seed = fragment.front();
        for (int atom : fragment) {
            if (!ringsOf[static_cast<std::size_t>(atom)].empty()) {
                seed = atom;
                break;
            }
        }
        graph.moveAtom(seed, 0.0, 0.0);
        placed[static_cast<std::size_t>(seed)] = true;

        std::vector<int> frontier{seed};
        while (!frontier.empty()) {
            const int atom = frontier.back();
            frontier.pop_back();

            for (int ringIndex : ringsOf[static_cast<std::size_t>(atom)]) {
                if (ringPlaced[static_cast<std::size_t>(ringIndex)])
                    continue;
                ringPlaced[static_cast<std::size_t>(ringIndex)] = true;
                placeRing(graph, ringList[static_cast<std::size_t>(ringIndex)],
                          placed);
                for (int member : ringList[static_cast<std::size_t>(ringIndex)])
                    frontier.push_back(member);
            }

            for (int neighbor : graph.neighbors(atom)) {
                if (placed[static_cast<std::size_t>(neighbor)])
                    continue;
                // Try headings at 30° steps and take the emptiest — the cheap
                // stand-in for a real collision-avoiding layout, and enough
                // because tidy() runs afterwards.
                const MolAtom& base = graph.atoms()[static_cast<std::size_t>(atom)];
                double bestX = base.x + MoleculeGraph::kBondLength;
                double bestY = base.y;
                double bestScore = 1e300;
                for (int k = 0; k < 12; ++k) {
                    const double angle = k * kPi / 6.0;
                    const double px =
                        base.x + MoleculeGraph::kBondLength * std::cos(angle);
                    const double py =
                        base.y + MoleculeGraph::kBondLength * std::sin(angle);
                    double score = 0.0;
                    for (int i = 0; i < count; ++i) {
                        if (!placed[static_cast<std::size_t>(i)])
                            continue;
                        const MolAtom& other =
                            graph.atoms()[static_cast<std::size_t>(i)];
                        score += 1.0 / (0.1 + std::hypot(other.x - px, other.y - py));
                    }
                    if (score < bestScore) {
                        bestScore = score;
                        bestX = px;
                        bestY = py;
                    }
                }
                graph.moveAtom(neighbor, bestX, bestY);
                placed[static_cast<std::size_t>(neighbor)] = true;
                frontier.push_back(neighbor);
            }
        }

        // Relax this fragment alone, then slide it clear of the previous one.
        tidy(graph, fragment);
        double minX = 0.0;
        double minY = 0.0;
        double maxX = 0.0;
        double maxY = 0.0;
        bool first = true;
        for (int atom : fragment) {
            const MolAtom& a = graph.atoms()[static_cast<std::size_t>(atom)];
            if (first) {
                minX = maxX = a.x;
                minY = maxY = a.y;
                first = false;
            }
            minX = std::min(minX, a.x);
            maxX = std::max(maxX, a.x);
            minY = std::min(minY, a.y);
            maxY = std::max(maxY, a.y);
        }
        const double shift = fragmentOffset - minX;
        graph.translate(fragment, {}, shift, 0.0);
        fragmentOffset = maxX + shift + 2.0 * MoleculeGraph::kBondLength;
    }
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

namespace {

/// SMILES writer.
///
/// Two passes, and the split is the whole reason this is a class rather than a
/// recursive lambda: a ring-closure digit has to appear at BOTH ends of the
/// bond it closes, and the second end is not known until the walk gets there.
/// So pass 1 walks the spanning tree and records which bonds are back edges,
/// pass 2 walks it again and can emit each atom's digits as it writes the atom.
/// Assigning labels during a single walk is exactly the bug that produces
/// "C1CCCCC" — a ring opened and never closed.
class Writer {
public:
    explicit Writer(const MoleculeGraph& graph)
        : graph_(graph)
        , aromaticBond_(graph.perceiveAromaticBonds())
        , aromaticAtom_(static_cast<std::size_t>(graph.atomCount()), false)
    {
        for (int i = 0; i < graph_.bondCount(); ++i) {
            if (!aromaticBond_[static_cast<std::size_t>(i)])
                continue;
            const MolBond& bond = graph_.bonds()[static_cast<std::size_t>(i)];
            aromaticAtom_[static_cast<std::size_t>(bond.a)] = true;
            aromaticAtom_[static_cast<std::size_t>(bond.b)] = true;
        }
    }

    std::string run()
    {
        const std::size_t atoms = static_cast<std::size_t>(graph_.atomCount());
        const std::size_t bonds = static_cast<std::size_t>(graph_.bondCount());

        // Pass 1: which bonds close rings.
        seenAtom_.assign(atoms, false);
        treeBond_.assign(bonds, false);
        closure_.assign(bonds, false);
        for (int start : starts()) {
            if (seenAtom_[static_cast<std::size_t>(start)])
                continue;
            findClosures(start);
        }

        // Pass 2: emit.
        std::string out;
        seenAtom_.assign(atoms, false);
        writtenBond_.assign(bonds, false);
        for (int start : starts()) {
            if (seenAtom_[static_cast<std::size_t>(start)])
                continue;
            if (!out.empty())
                out += ".";
            emit(start, out);
        }
        return out;
    }

private:
    /// One starting atom per fragment: a terminal atom where the fragment has
    /// one, so "CCO" comes out instead of the equivalent but uglier "C(C)O".
    std::vector<int> starts() const
    {
        std::vector<int> result;
        for (const std::vector<int>& fragment : graph_.fragments()) {
            int start = fragment.front();
            for (int atom : fragment) {
                if (graph_.neighbors(atom).size() == 1) {
                    start = atom;
                    break;
                }
            }
            result.push_back(start);
        }
        return result;
    }

    void findClosures(int atom)
    {
        seenAtom_[static_cast<std::size_t>(atom)] = true;
        for (int bondIndex : graph_.bondsAt(atom)) {
            if (treeBond_[static_cast<std::size_t>(bondIndex)]
                || closure_[static_cast<std::size_t>(bondIndex)])
                continue;
            const MolBond& bond = graph_.bonds()[static_cast<std::size_t>(bondIndex)];
            const int other = bond.other(atom);
            if (seenAtom_[static_cast<std::size_t>(other)]) {
                closure_[static_cast<std::size_t>(bondIndex)] = true;
                continue;
            }
            treeBond_[static_cast<std::size_t>(bondIndex)] = true;
            findClosures(other);
        }
    }

    void emit(int atom, std::string& out)
    {
        seenAtom_[static_cast<std::size_t>(atom)] = true;
        out += atomToken(atom);

        // Ring-closure digits first — they bind to the atom that carries them.
        //
        // Labels are handed out HERE, in emission order, and returned to the
        // pool the moment the closure's second end is written. Assigning them
        // during the first pass instead produced valid but bewildering output
        // ("c2ccc1ccccc1c2"), and never reusing them runs a steroid or a
        // macrocycle past digit 9 for no reason.
        for (int bondIndex : graph_.bondsAt(atom)) {
            if (!closure_[static_cast<std::size_t>(bondIndex)])
                continue;
            const auto open = openLabel_.find(bondIndex);
            int label = 0;
            if (open == openLabel_.end()) {
                while (labelInUse_.count(nextLabel_))
                    ++nextLabel_;
                label = nextLabel_;
                labelInUse_.insert(label);
                openLabel_[bondIndex] = label;
                nextLabel_ = 1;
            } else {
                label = open->second;
                labelInUse_.erase(label);
                openLabel_.erase(open);
                nextLabel_ = 1;
            }
            out += bondToken(bondIndex);
            out += label < 10 ? std::to_string(label)
                              : "%" + std::to_string(label);
        }

        std::vector<int> branches;
        for (int bondIndex : graph_.bondsAt(atom)) {
            if (!treeBond_[static_cast<std::size_t>(bondIndex)]
                || writtenBond_[static_cast<std::size_t>(bondIndex)])
                continue;
            const MolBond& bond = graph_.bonds()[static_cast<std::size_t>(bondIndex)];
            if (seenAtom_[static_cast<std::size_t>(bond.other(atom))])
                continue;
            branches.push_back(bondIndex);
        }
        for (std::size_t i = 0; i < branches.size(); ++i) {
            const int bondIndex = branches[i];
            writtenBond_[static_cast<std::size_t>(bondIndex)] = true;
            const MolBond& bond = graph_.bonds()[static_cast<std::size_t>(bondIndex)];
            const int other = bond.other(atom);
            const bool last = i + 1 == branches.size();
            if (!last)
                out += "(";
            out += bondToken(bondIndex);
            emit(other, out);
            if (!last)
                out += ")";
        }
    }

    std::string bondToken(int bondIndex) const
    {
        if (aromaticBond_[static_cast<std::size_t>(bondIndex)])
            return {}; // implied between two lowercase atoms
        switch (graph_.bonds()[static_cast<std::size_t>(bondIndex)].order) {
        case 2:  return "=";
        case 3:  return "#";
        default: return {};
        }
    }

    std::string atomToken(int atom) const
    {
        const MolAtom& a = graph_.atoms()[static_cast<std::size_t>(atom)];
        std::string symbol = Elements::data(a.atomicNumber).symbol;
        const bool lower = aromaticAtom_[static_cast<std::size_t>(atom)];
        if (lower) {
            symbol[0] = static_cast<char>(
                std::tolower(static_cast<unsigned char>(symbol[0])));
        }
        const int hydrogens = graph_.implicitHydrogens(atom);

        // Brackets are written whenever the bare token would not read back as
        // the same atom. The aromatic-heteroatom clause is the subtle one:
        // bare `n` is a pyridine nitrogen with no hydrogen, so a pyrrole
        // nitrogen MUST be written `[nH]` or the ring comes back one hydrogen
        // short and unkekulizable.
        const bool pinnedDisagrees =
            a.explicitHydrogens >= 0 && a.explicitHydrogens != derivedHydrogens(atom);
        const bool needsBrackets = a.charge != 0 || a.radicalElectrons != 0
            || pinnedDisagrees || !isOrganicSubset(a.atomicNumber)
            || (lower && a.atomicNumber != 6 && hydrogens > 0);
        if (!needsBrackets)
            return symbol;

        std::string token = "[" + symbol;
        if (hydrogens == 1)
            token += "H";
        else if (hydrogens > 1)
            token += "H" + std::to_string(hydrogens);
        if (a.charge > 0)
            token += a.charge == 1 ? "+" : "+" + std::to_string(a.charge);
        else if (a.charge < 0)
            token += a.charge == -1 ? "-" : "-" + std::to_string(-a.charge);
        token += "]";
        return token;
    }

    /// The hydrogen count the valence WOULD imply, ignoring any pin — what a
    /// reader of the bare token would reconstruct.
    int derivedHydrogens(int atom) const
    {
        const MolAtom& a = graph_.atoms()[static_cast<std::size_t>(atom)];
        const int used = graph_.bondOrderSum(atom) + a.radicalElectrons;
        for (int valence : standardValences(a.atomicNumber, a.charge)) {
            if (valence >= used)
                return valence - used;
        }
        return 0;
    }

    const MoleculeGraph& graph_;
    std::vector<bool> aromaticBond_;
    std::vector<bool> aromaticAtom_;
    std::vector<bool> seenAtom_;
    std::vector<bool> treeBond_;
    std::vector<bool> writtenBond_;
    /// Which bonds are ring closures (back edges of the spanning tree).
    std::vector<bool> closure_;
    /// Closure bond -> the digit currently standing open on it.
    std::map<int, int> openLabel_;
    std::set<int> labelInUse_;
    int nextLabel_ = 1;
};

} // namespace

std::string write(const MoleculeGraph& graph)
{
    if (graph.atomCount() == 0)
        return {};
    return Writer(graph).run();
}

} // namespace calango::core::smiles
