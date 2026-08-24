#include "core/MoleculeGraph.hpp"

#include "core/Element.hpp"
#include "core/PhysicalConstants.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace calango::core {
namespace {

double deg2rad(double degrees) { return degrees * kPi / 180.0; }

/// Distance from (px, py) to the segment (ax, ay)-(bx, by).
double distanceToSegment(double px, double py, double ax, double ay, double bx,
                         double by)
{
    const double dx = bx - ax;
    const double dy = by - ay;
    const double lengthSq = dx * dx + dy * dy;
    if (lengthSq < 1e-12)
        return std::hypot(px - ax, py - ay);
    double t = ((px - ax) * dx + (py - ay) * dy) / lengthSq;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

/// Group of a main-group element, 1-18, or 0 when it is not one this module
/// reasons about.
int mainGroup(int z)
{
    switch (z) {
    case 1:  return 1;
    case 5: case 13: case 31: case 49: case 81: return 13;
    case 6: case 14: case 32: case 50: case 82: return 14;
    case 7: case 15: case 33: case 51: case 83: return 15;
    case 8: case 16: case 34: case 52: return 16;
    case 9: case 17: case 35: case 53: case 85: return 17;
    default: return 0;
    }
}

/// The neutral-atom valence list, before any charge correction.
const std::vector<int>& neutralValences(int z)
{
    static const std::map<int, std::vector<int>> kTable = {
        {1, {1}},                 // H
        {5, {3}},                 // B
        {6, {4}},                 // C
        {7, {3}},                 // N   (the 5 of a nitro group is drawn as
                                  //      N+ =O / -O-, which is why 5 is not here)
        {8, {2}},                 // O
        {9, {1}},                 // F
        {14, {4}},                // Si
        {15, {3, 5}},             // P
        {16, {2, 4, 6}},          // S
        {17, {1}},                // Cl
        {32, {4}},                // Ge
        {33, {3, 5}},             // As
        {34, {2, 4, 6}},          // Se
        {35, {1}},                // Br
        {50, {4}},                // Sn
        {51, {3, 5}},             // Sb
        {52, {2, 4, 6}},          // Te
        {53, {1, 3, 5, 7}},       // I
    };
    static const std::vector<int> kNone;
    const auto it = kTable.find(z);
    return it == kTable.end() ? kNone : it->second;
}

} // namespace

// ---------------------------------------------------------------------------
// Valence
// ---------------------------------------------------------------------------

std::vector<int> standardValences(int z, int charge)
{
    const std::vector<int>& base = neutralValences(z);
    if (base.empty())
        return {};
    if (charge == 0)
        return base;

    const int group = mainGroup(z);
    std::vector<int> shifted;
    shifted.reserve(base.size());
    for (int valence : base) {
        int value = valence;
        if (group >= 15) {
            // N+ tetravalent, O- monovalent: the lone pair either accepts a
            // bond or becomes one.
            value = valence + charge;
        } else if (group == 13) {
            // Borohydride is tetravalent, borenium divalent.
            value = valence - charge;
        } else {
            // Group 14 (and hydrogen): both ions are one bond short of the
            // neutral atom — a carbocation has an empty orbital, a carbanion a
            // filled one, and neither is bonding.
            value = valence - std::abs(charge);
        }
        if (value > 0)
            shifted.push_back(value);
    }
    std::sort(shifted.begin(), shifted.end());
    shifted.erase(std::unique(shifted.begin(), shifted.end()), shifted.end());
    return shifted;
}

bool hasOrganicValence(int z) { return !neutralValences(z).empty(); }

// ---------------------------------------------------------------------------
// MoleculeGraph
// ---------------------------------------------------------------------------

void MoleculeGraph::clear()
{
    atoms_.clear();
    bonds_.clear();
    captions_.clear();
}

int MoleculeGraph::addAtom(int atomicNumber, double x, double y)
{
    MolAtom atom;
    atom.atomicNumber = atomicNumber;
    atom.x = x;
    atom.y = y;
    return addAtom(atom);
}

int MoleculeGraph::addAtom(const MolAtom& atom)
{
    atoms_.push_back(atom);
    return static_cast<int>(atoms_.size()) - 1;
}

int MoleculeGraph::addBond(int a, int b, int order, BondStereo stereo)
{
    if (a == b || a < 0 || b < 0 || a >= atomCount() || b >= atomCount())
        return -1;
    if (const int existing = bondBetween(a, b); existing >= 0) {
        bonds_[static_cast<std::size_t>(existing)].order = std::clamp(order, 1, 3);
        bonds_[static_cast<std::size_t>(existing)].stereo = stereo;
        return existing;
    }
    MolBond bond;
    bond.a = a;
    bond.b = b;
    bond.order = std::clamp(order, 1, 3);
    bond.stereo = stereo;
    bonds_.push_back(bond);
    return static_cast<int>(bonds_.size()) - 1;
}

int MoleculeGraph::cycleBondOrder(int bond)
{
    if (bond < 0 || bond >= bondCount())
        return 0;
    MolBond& b = bonds_[static_cast<std::size_t>(bond)];
    if (b.stereo != BondStereo::None) {
        // A stereo bond returns to a plain single before it starts cycling:
        // "hashed double bond" is not a glyph anyone means to draw.
        b.stereo = BondStereo::None;
        b.order = 1;
        return b.order;
    }
    b.order = b.order >= 3 ? 1 : b.order + 1;
    return b.order;
}

void MoleculeGraph::removeAtom(int index) { removeAtoms({index}); }

void MoleculeGraph::removeAtoms(const std::vector<int>& indices)
{
    std::set<int> doomed;
    for (int index : indices) {
        if (index >= 0 && index < atomCount())
            doomed.insert(index);
    }
    if (doomed.empty())
        return;

    // Old index -> new index, -1 for the removed ones.
    std::vector<int> remap(atoms_.size(), -1);
    std::vector<MolAtom> kept;
    kept.reserve(atoms_.size() - doomed.size());
    for (int i = 0; i < atomCount(); ++i) {
        if (doomed.count(i))
            continue;
        remap[static_cast<std::size_t>(i)] = static_cast<int>(kept.size());
        kept.push_back(atoms_[static_cast<std::size_t>(i)]);
    }

    std::vector<MolBond> keptBonds;
    keptBonds.reserve(bonds_.size());
    for (const MolBond& bond : bonds_) {
        const int a = remap[static_cast<std::size_t>(bond.a)];
        const int b = remap[static_cast<std::size_t>(bond.b)];
        if (a < 0 || b < 0)
            continue; // an end went away, and so does the bond
        MolBond moved = bond;
        moved.a = a;
        moved.b = b;
        keptBonds.push_back(moved);
    }

    atoms_.swap(kept);
    bonds_.swap(keptBonds);
}

void MoleculeGraph::removeBond(int index)
{
    if (index < 0 || index >= bondCount())
        return;
    bonds_.erase(bonds_.begin() + index);
}

void MoleculeGraph::removeCaption(int index)
{
    if (index < 0 || index >= static_cast<int>(captions_.size()))
        return;
    captions_.erase(captions_.begin() + index);
}

int MoleculeGraph::bondBetween(int a, int b) const
{
    for (int i = 0; i < bondCount(); ++i) {
        const MolBond& bond = bonds_[static_cast<std::size_t>(i)];
        if ((bond.a == a && bond.b == b) || (bond.a == b && bond.b == a))
            return i;
    }
    return -1;
}

std::vector<int> MoleculeGraph::bondsAt(int atom) const
{
    std::vector<int> result;
    for (int i = 0; i < bondCount(); ++i) {
        if (bonds_[static_cast<std::size_t>(i)].touches(atom))
            result.push_back(i);
    }
    return result;
}

std::vector<int> MoleculeGraph::neighbors(int atom) const
{
    std::vector<int> result;
    for (const MolBond& bond : bonds_) {
        if (bond.touches(atom))
            result.push_back(bond.other(atom));
    }
    return result;
}

int MoleculeGraph::bondOrderSum(int atom) const
{
    int sum = 0;
    for (const MolBond& bond : bonds_) {
        if (bond.touches(atom))
            sum += bond.order;
    }
    return sum;
}

int MoleculeGraph::implicitHydrogens(int atom) const
{
    if (atom < 0 || atom >= atomCount())
        return 0;
    const MolAtom& a = atoms_[static_cast<std::size_t>(atom)];
    if (a.explicitHydrogens >= 0)
        return a.explicitHydrogens;

    const std::vector<int> valences = standardValences(a.atomicNumber, a.charge);
    if (valences.empty())
        return 0;
    const int used = bondOrderSum(atom) + a.radicalElectrons;
    for (int valence : valences) {
        if (valence >= used)
            return valence - used;
    }
    return 0; // over-valent: flagged by valenceViolated(), never "fixed" here
}

int MoleculeGraph::hydrogenCount(int atom) const
{
    int drawn = 0;
    for (int neighbor : neighbors(atom)) {
        if (atoms_[static_cast<std::size_t>(neighbor)].atomicNumber == 1)
            ++drawn;
    }
    return drawn + implicitHydrogens(atom);
}

bool MoleculeGraph::valenceViolated(int atom) const
{
    if (atom < 0 || atom >= atomCount())
        return false;
    const MolAtom& a = atoms_[static_cast<std::size_t>(atom)];
    const std::vector<int> valences = standardValences(a.atomicNumber, a.charge);
    if (valences.empty())
        return false; // no tabulated valence is not the same as a wrong one
    const int used = bondOrderSum(atom) + a.radicalElectrons
        + std::max(0, a.explicitHydrogens);
    return used > valences.back();
}

std::vector<std::vector<int>> MoleculeGraph::fragments() const
{
    std::vector<std::vector<int>> result;
    std::vector<bool> seen(atoms_.size(), false);
    // Adjacency once, rather than a neighbors() scan per atom — fragments() is
    // called on every repaint to colour multi-molecule canvases.
    std::vector<std::vector<int>> adjacency(atoms_.size());
    for (const MolBond& bond : bonds_) {
        adjacency[static_cast<std::size_t>(bond.a)].push_back(bond.b);
        adjacency[static_cast<std::size_t>(bond.b)].push_back(bond.a);
    }
    for (int start = 0; start < atomCount(); ++start) {
        if (seen[static_cast<std::size_t>(start)])
            continue;
        std::vector<int> component;
        std::deque<int> queue{start};
        seen[static_cast<std::size_t>(start)] = true;
        while (!queue.empty()) {
            const int atom = queue.front();
            queue.pop_front();
            component.push_back(atom);
            for (int neighbor : adjacency[static_cast<std::size_t>(atom)]) {
                if (!seen[static_cast<std::size_t>(neighbor)]) {
                    seen[static_cast<std::size_t>(neighbor)] = true;
                    queue.push_back(neighbor);
                }
            }
        }
        std::sort(component.begin(), component.end());
        result.push_back(std::move(component));
    }
    return result;
}

MoleculeGraph MoleculeGraph::subgraph(const std::vector<int>& atomIndices) const
{
    std::vector<int> sorted = atomIndices;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    std::map<int, int> remap;
    MoleculeGraph result;
    for (int index : sorted) {
        if (index < 0 || index >= atomCount())
            continue;
        remap[index] = result.addAtom(atoms_[static_cast<std::size_t>(index)]);
    }
    for (const MolBond& bond : bonds_) {
        const auto a = remap.find(bond.a);
        const auto b = remap.find(bond.b);
        if (a == remap.end() || b == remap.end())
            continue;
        result.addBond(a->second, b->second, bond.order, bond.stereo);
    }
    return result;
}

int MoleculeGraph::append(const MoleculeGraph& other, double dx, double dy)
{
    const int offset = atomCount();
    for (const MolAtom& atom : other.atoms()) {
        MolAtom moved = atom;
        moved.x += dx;
        moved.y += dy;
        atoms_.push_back(moved);
    }
    for (const MolBond& bond : other.bonds()) {
        MolBond moved = bond;
        moved.a += offset;
        moved.b += offset;
        bonds_.push_back(moved);
    }
    for (const MolCaption& caption : other.captions()) {
        MolCaption moved = caption;
        moved.x += dx;
        moved.y += dy;
        captions_.push_back(moved);
    }
    return offset;
}

std::string MoleculeGraph::formula() const
{
    std::vector<int> all(atoms_.size());
    for (std::size_t i = 0; i < atoms_.size(); ++i)
        all[i] = static_cast<int>(i);
    return formula(all);
}

std::string MoleculeGraph::formula(const std::vector<int>& atomIndices) const
{
    std::map<std::string, int> counts;
    for (int index : atomIndices) {
        if (index < 0 || index >= atomCount())
            continue;
        const MolAtom& atom = atoms_[static_cast<std::size_t>(index)];
        counts[Elements::data(atom.atomicNumber).symbol] += 1;
        counts["H"] += implicitHydrogens(index);
    }
    // Hill order: C first, then H, then everything else alphabetically. The
    // same convention Structure::chemicalFormula() uses, so a formula shown in
    // the sketcher and the tab title of its export read identically.
    std::string text;
    const auto emit = [&text, &counts](const std::string& symbol) {
        const auto it = counts.find(symbol);
        if (it == counts.end() || it->second <= 0)
            return;
        text += symbol;
        if (it->second > 1)
            text += std::to_string(it->second);
    };
    const bool organic = counts.count("C") && counts["C"] > 0;
    if (organic) {
        emit("C");
        emit("H");
    }
    for (const auto& [symbol, count] : counts) {
        if (count <= 0)
            continue;
        if (organic && (symbol == "C" || symbol == "H"))
            continue;
        emit(symbol);
    }
    return text;
}

bool MoleculeGraph::bounds(double& minX, double& minY, double& maxX,
                           double& maxY) const
{
    if (atoms_.empty() && captions_.empty())
        return false;
    minX = minY = 1e300;
    maxX = maxY = -1e300;
    for (const MolAtom& atom : atoms_) {
        minX = std::min(minX, atom.x);
        maxX = std::max(maxX, atom.x);
        minY = std::min(minY, atom.y);
        maxY = std::max(maxY, atom.y);
    }
    for (const MolCaption& caption : captions_) {
        minX = std::min(minX, caption.x);
        maxX = std::max(maxX, caption.x);
        minY = std::min(minY, caption.y);
        maxY = std::max(maxY, caption.y);
    }
    return true;
}

std::vector<std::vector<int>> MoleculeGraph::rings(int maxSize) const
{
    std::vector<std::vector<int>> found;
    std::set<std::set<int>> seen;

    std::vector<std::vector<int>> adjacency(atoms_.size());
    for (const MolBond& bond : bonds_) {
        adjacency[static_cast<std::size_t>(bond.a)].push_back(bond.b);
        adjacency[static_cast<std::size_t>(bond.b)].push_back(bond.a);
    }

    // The smallest ring through each bond: delete the bond, then BFS its two
    // ends back together. Not a true SSSR (it can miss an envelope ring in a
    // dense cage), but every system this module draws or embeds is covered and
    // the failure mode — one ring not restrained — is benign.
    for (const MolBond& bond : bonds_) {
        const int source = bond.a;
        const int target = bond.b;
        std::vector<int> previous(atoms_.size(), -1);
        std::vector<int> depth(atoms_.size(), -1);
        depth[static_cast<std::size_t>(source)] = 0;
        std::deque<int> queue{source};
        bool reached = false;
        while (!queue.empty() && !reached) {
            const int atom = queue.front();
            queue.pop_front();
            if (depth[static_cast<std::size_t>(atom)] >= maxSize)
                continue;
            for (int neighbor : adjacency[static_cast<std::size_t>(atom)]) {
                if (atom == source && neighbor == target)
                    continue; // the bond we removed
                if (atom == target && neighbor == source)
                    continue;
                if (depth[static_cast<std::size_t>(neighbor)] >= 0)
                    continue;
                depth[static_cast<std::size_t>(neighbor)] =
                    depth[static_cast<std::size_t>(atom)] + 1;
                previous[static_cast<std::size_t>(neighbor)] = atom;
                if (neighbor == target) {
                    reached = true;
                    break;
                }
                queue.push_back(neighbor);
            }
        }
        if (!reached)
            continue;

        std::vector<int> cycle;
        for (int atom = target; atom >= 0;
             atom = previous[static_cast<std::size_t>(atom)]) {
            cycle.push_back(atom);
            if (atom == source)
                break;
        }
        if (static_cast<int>(cycle.size()) < 3
            || static_cast<int>(cycle.size()) > maxSize)
            continue;
        const std::set<int> key(cycle.begin(), cycle.end());
        if (key.size() != cycle.size() || !seen.insert(key).second)
            continue;
        found.push_back(std::move(cycle));
    }

    std::sort(found.begin(), found.end(),
              [](const std::vector<int>& a, const std::vector<int>& b) {
                  return a.size() < b.size();
              });
    return found;
}

std::vector<bool> MoleculeGraph::perceiveAromaticBonds() const
{
    std::vector<bool> aromatic(bonds_.size(), false);
    const std::vector<std::vector<int>> ringList = rings(6);
    // Ring membership over ALL rings, needed below to tell a fused-system π
    // bond from a genuinely exocyclic one.
    std::vector<bool> inAnyRing(atoms_.size(), false);
    for (const std::vector<int>& ring : ringList) {
        for (int atom : ring)
            inAnyRing[static_cast<std::size_t>(atom)] = true;
    }
    for (const std::vector<int>& ring : ringList) {
        const int size = static_cast<int>(ring.size());
        if (size != 5 && size != 6)
            continue;

        const std::set<int> members(ring.begin(), ring.end());
        int piElectrons = 0;
        bool qualifies = true;
        for (int atom : ring) {
            const MolAtom& a = atoms_[static_cast<std::size_t>(atom)];
            int piBonds = 0;
            bool exocyclicMultiple = false;
            for (const MolBond& bond : bonds_) {
                if (!bond.touches(atom))
                    continue;
                if (bond.order < 2)
                    continue;
                const int partner = bond.other(atom);
                // A π bond that LEAVES this ring still belongs to the π system
                // when its far end is itself a ring atom — which is exactly
                // what a fused system looks like from inside one of its rings.
                // Naphthalene has Kekulé structures in which a ring-junction
                // carbon's double bond points into the OTHER ring, and reading
                // that as an exocyclic substituent declares half of
                // naphthalene non-aromatic.
                if (members.count(partner) || inAnyRing[static_cast<std::size_t>(partner)])
                    ++piBonds;
                else
                    exocyclicMultiple = true; // a carbonyl, an exocyclic alkene
            }
            if (exocyclicMultiple || piBonds > 1) {
                qualifies = false; // a cross-conjugated ketone is not aromatic
                break;
            }
            if (piBonds == 1) {
                piElectrons += 1;
                continue;
            }
            // No ring π bond: only a heteroatom donating its lone pair keeps
            // the system closed. An sp3 carbon (cyclopentadiene's CH2) does
            // not, and disqualifies the ring.
            const int z = a.atomicNumber;
            const bool donor = (z == 7 && a.charge <= 0) || z == 8 || z == 16
                || (z == 6 && a.charge < 0);
            if (!donor) {
                qualifies = false;
                break;
            }
            piElectrons += 2;
        }
        if (!qualifies || piElectrons < 6 || (piElectrons - 2) % 4 != 0)
            continue;

        for (int i = 0; i < bondCount(); ++i) {
            const MolBond& bond = bonds_[static_cast<std::size_t>(i)];
            if (members.count(bond.a) && members.count(bond.b))
                aromatic[static_cast<std::size_t>(i)] = true;
        }
    }
    return aromatic;
}

std::vector<std::vector<int>> MoleculeGraph::perceiveAromaticRings() const
{
    // Derived from the bond flags rather than by repeating the π count, so
    // there is exactly ONE aromaticity rule in this file and a ring can never
    // be filled as aromatic while its bonds are written as Kekulé (or the
    // other way round).
    const std::vector<bool> aromatic = perceiveAromaticBonds();
    std::vector<std::vector<int>> result;
    for (const std::vector<int>& ring : rings(6)) {
        if (ring.size() < 3)
            continue;
        bool all = true;
        for (std::size_t i = 0; i < ring.size() && all; ++i) {
            const int bond =
                bondBetween(ring[i], ring[(i + 1) % ring.size()]);
            all = bond >= 0 && aromatic[static_cast<std::size_t>(bond)];
        }
        if (all)
            result.push_back(ring);
    }
    return result;
}

int MoleculeGraph::atomAt(double x, double y, double radius) const
{
    int best = -1;
    double bestDistance = radius;
    for (int i = 0; i < atomCount(); ++i) {
        const MolAtom& atom = atoms_[static_cast<std::size_t>(i)];
        const double distance = std::hypot(atom.x - x, atom.y - y);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

int MoleculeGraph::bondAt(double x, double y, double radius) const
{
    int best = -1;
    double bestDistance = radius;
    for (int i = 0; i < bondCount(); ++i) {
        const MolBond& bond = bonds_[static_cast<std::size_t>(i)];
        const MolAtom& a = atoms_[static_cast<std::size_t>(bond.a)];
        const MolAtom& b = atoms_[static_cast<std::size_t>(bond.b)];
        const double distance = distanceToSegment(x, y, a.x, a.y, b.x, b.y);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

int MoleculeGraph::captionAt(double x, double y, double radius) const
{
    int best = -1;
    double bestDistance = radius;
    for (int i = 0; i < static_cast<int>(captions_.size()); ++i) {
        const MolCaption& caption = captions_[static_cast<std::size_t>(i)];
        const double distance = std::hypot(caption.x - x, caption.y - y);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

void MoleculeGraph::moveAtom(int index, double x, double y)
{
    if (index < 0 || index >= atomCount())
        return;
    atoms_[static_cast<std::size_t>(index)].x = x;
    atoms_[static_cast<std::size_t>(index)].y = y;
}

void MoleculeGraph::translate(const std::vector<int>& atomIndices,
                              const std::vector<int>& captionIndices, double dx,
                              double dy)
{
    for (int index : atomIndices) {
        if (index < 0 || index >= atomCount())
            continue;
        atoms_[static_cast<std::size_t>(index)].x += dx;
        atoms_[static_cast<std::size_t>(index)].y += dy;
    }
    for (int index : captionIndices) {
        if (index < 0 || index >= static_cast<int>(captions_.size()))
            continue;
        captions_[static_cast<std::size_t>(index)].x += dx;
        captions_[static_cast<std::size_t>(index)].y += dy;
    }
}

// ---------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------

const std::vector<RingTemplate>& ringTemplates()
{
    static const std::vector<RingTemplate> kAll = {
        RingTemplate::Cyclopropane,   RingTemplate::Cyclobutane,
        RingTemplate::Cyclopentane,   RingTemplate::Cyclohexane,
        RingTemplate::Cycloheptane,   RingTemplate::Cyclooctane,
        RingTemplate::Benzene,        RingTemplate::Cyclopentadiene,
        RingTemplate::Naphthalene,
    };
    return kAll;
}

const char* ringTemplateName(RingTemplate ring)
{
    switch (ring) {
    case RingTemplate::Cyclopropane:    return "Cyclopropane";
    case RingTemplate::Cyclobutane:     return "Cyclobutane";
    case RingTemplate::Cyclopentane:    return "Cyclopentane";
    case RingTemplate::Cyclohexane:     return "Cyclohexane";
    case RingTemplate::Cycloheptane:    return "Cycloheptane";
    case RingTemplate::Cyclooctane:     return "Cyclooctane";
    case RingTemplate::Benzene:         return "Benzene";
    case RingTemplate::Cyclopentadiene: return "Cyclopentadiene";
    case RingTemplate::Naphthalene:     return "Naphthalene";
    }
    return "Ring";
}

int ringTemplateSize(RingTemplate ring)
{
    switch (ring) {
    case RingTemplate::Cyclopropane:    return 3;
    case RingTemplate::Cyclobutane:     return 4;
    case RingTemplate::Cyclopentane:    return 5;
    case RingTemplate::Cyclopentadiene: return 5;
    case RingTemplate::Cyclohexane:     return 6;
    case RingTemplate::Benzene:         return 6;
    case RingTemplate::Cycloheptane:    return 7;
    case RingTemplate::Cyclooctane:     return 8;
    case RingTemplate::Naphthalene:     return 10;
    }
    return 6;
}

namespace {

/// Circumradius of a regular n-gon whose side is kBondLength.
double ringRadius(int size)
{
    return MoleculeGraph::kBondLength / (2.0 * std::sin(kPi / size));
}

/// Double bonds a template carries, as (ring position, ring position) pairs
/// walked around the cycle. Empty for a saturated ring.
std::vector<std::pair<int, int>> templateDoubleBonds(RingTemplate ring)
{
    switch (ring) {
    case RingTemplate::Benzene:
        return {{0, 1}, {2, 3}, {4, 5}};
    case RingTemplate::Cyclopentadiene:
        // Positions 0..4 with the sp3 CH2 at 4: double bonds 0=1 and 2=3.
        return {{0, 1}, {2, 3}};
    default:
        return {};
    }
}

} // namespace

MoleculeGraph makeRing(RingTemplate ring, double cx, double cy)
{
    MoleculeGraph graph;
    if (ring == RingTemplate::Naphthalene) {
        // Two six-rings sharing an edge, drawn the way it is always drawn:
        // the shared bond vertical in the middle, both rings regular.
        //
        // Built as a plain cyclohexane and then fused, so exactly one piece of
        // code decides where a fused ring's atoms go.
        MoleculeGraph left = makeRing(RingTemplate::Benzene, cx, cy);
        graph.append(left);
        // The shared bond is the one on the +x side of the first ring.
        int shared = -1;
        double bestX = -1e300;
        for (int i = 0; i < graph.bondCount(); ++i) {
            const MolBond& bond = graph.bonds()[static_cast<std::size_t>(i)];
            const double midX = 0.5
                * (graph.atoms()[static_cast<std::size_t>(bond.a)].x
                   + graph.atoms()[static_cast<std::size_t>(bond.b)].x);
            if (midX > bestX) {
                bestX = midX;
                shared = i;
            }
        }
        fuseRing(graph, shared, RingTemplate::Benzene);
        return graph;
    }

    const int size = ringTemplateSize(ring);
    const double radius = ringRadius(size);
    // Start at the top and walk clockwise, which puts a vertex (not an edge) at
    // 12 o'clock for odd rings and a flat top for even ones — the orientation
    // every drawing program uses.
    for (int i = 0; i < size; ++i) {
        const double angle = kPi / 2.0 + 2.0 * kPi * i / size
            + (size % 2 == 0 ? kPi / size : 0.0);
        graph.addAtom(6, cx + radius * std::cos(angle),
                      cy + radius * std::sin(angle));
    }
    for (int i = 0; i < size; ++i)
        graph.addBond(i, (i + 1) % size, 1);
    for (const auto& [from, to] : templateDoubleBonds(ring)) {
        const int bond = graph.bondBetween(from, to);
        if (bond >= 0)
            graph.bonds()[static_cast<std::size_t>(bond)].order = 2;
    }
    return graph;
}

int stampRing(MoleculeGraph& graph, RingTemplate ring, double cx, double cy)
{
    return graph.append(makeRing(ring, cx, cy));
}

bool fuseRing(MoleculeGraph& graph, int bondIndex, RingTemplate ring)
{
    if (bondIndex < 0 || bondIndex >= graph.bondCount())
        return false;
    if (ring == RingTemplate::Naphthalene)
        return false; // already a fused system; stamped free-standing instead

    const int size = ringTemplateSize(ring);
    const MolBond seed = graph.bonds()[static_cast<std::size_t>(bondIndex)];
    const MolAtom a = graph.atoms()[static_cast<std::size_t>(seed.a)];
    const MolAtom b = graph.atoms()[static_cast<std::size_t>(seed.b)];

    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length = std::hypot(dx, dy);
    if (length < 1e-9)
        return false;

    // The new ring's centre sits on the perpendicular bisector of the seed
    // bond, at the apothem of a regular n-gon whose side is that bond.
    const double midX = 0.5 * (a.x + b.x);
    const double midY = 0.5 * (a.y + b.y);
    const double apothem = length / (2.0 * std::tan(kPi / size));
    const double nx = -dy / length;
    const double ny = dx / length;

    // Both sides are candidates; the emptier one wins, so a ring fuses outward
    // from an existing system rather than on top of it.
    const auto crowding = [&graph](double px, double py) {
        double score = 0.0;
        for (const MolAtom& atom : graph.atoms()) {
            const double distance = std::hypot(atom.x - px, atom.y - py);
            score += 1.0 / (0.25 + distance * distance);
        }
        return score;
    };
    double sign = 1.0;
    if (crowding(midX + nx * apothem, midY + ny * apothem)
        > crowding(midX - nx * apothem, midY - ny * apothem)) {
        sign = -1.0;
    }
    const double cx = midX + sign * nx * apothem;
    const double cy = midY + sign * ny * apothem;

    // Walk from `a` to `b` the long way round the new ring, placing the
    // size - 2 atoms that do not already exist.
    const double radius = ringRadius(size) * (length / MoleculeGraph::kBondLength);
    double startAngle = std::atan2(a.y - cy, a.x - cx);
    const double endAngle = std::atan2(b.y - cy, b.x - cx);
    double step = 2.0 * kPi / size;
    // Choose the direction that walks AWAY from b, i.e. the long way round.
    double forward = endAngle - startAngle;
    while (forward <= -kPi)
        forward += 2.0 * kPi;
    while (forward > kPi)
        forward -= 2.0 * kPi;
    if (forward > 0.0)
        step = -step;

    std::vector<int> ringAtoms{seed.a};
    for (int i = 1; i < size - 1; ++i) {
        const double angle = startAngle + step * i;
        ringAtoms.push_back(
            graph.addAtom(6, cx + radius * std::cos(angle),
                          cy + radius * std::sin(angle)));
    }
    ringAtoms.push_back(seed.b);

    for (std::size_t i = 0; i + 1 < ringAtoms.size(); ++i)
        graph.addBond(ringAtoms[i], ringAtoms[i + 1], 1);

    // Unsaturation: place the template's double bonds on the NEW bonds only,
    // and only where BOTH ends still have valence left.
    //
    // Alternating blindly from a fixed starting offset is what produced a
    // naphthalene with two PENTAVALENT ring-junction carbons: each junction
    // atom already carried a π bond from the ring being fused onto, and the
    // new ring handed it a second one. The formula still came out C10H8 —
    // implicitHydrogens() returns 0 for an over-valent atom rather than a
    // negative count — so nothing downstream complained; only the aromatic
    // perception, which refuses an atom with two ring π bonds, noticed.
    //
    // Asking each candidate bond whether its two atoms can still take a π bond
    // makes the shared bond's own order fall out of the arithmetic instead of
    // being a special case, and handles a fusion onto a saturated ring, an
    // unsaturated one, and a heteroatom identically.
    if (!templateDoubleBonds(ring).empty()) {
        const int wanted = static_cast<int>(templateDoubleBonds(ring).size());
        int placed = 0;
        for (std::size_t i = 0; i + 1 < ringAtoms.size() && placed < wanted; ++i) {
            const int u = ringAtoms[i];
            const int v = ringAtoms[i + 1];
            // implicitHydrogens() IS the spare valence here: it is the
            // shortfall against the smallest standard valence that still fits
            // the bonds already drawn.
            if (graph.implicitHydrogens(u) < 1 || graph.implicitHydrogens(v) < 1)
                continue;
            const int bond = graph.bondBetween(u, v);
            if (bond < 0)
                continue;
            graph.bonds()[static_cast<std::size_t>(bond)].order = 2;
            ++placed;
            // An atom takes at most one π bond, so the bond sharing `v` cannot
            // be next. Skipping it explicitly (rather than leaving it to the
            // spare check) matters for a ring whose atoms have a spare valence
            // of two — a cyclopentadiene CH2 would otherwise become an allene.
            ++i;
        }
    }
    return true;
}

std::vector<int> growChain(MoleculeGraph& graph, int startAtom, double angleDeg,
                           int length)
{
    std::vector<int> created;
    if (length <= 0 || startAtom < 0 || startAtom >= graph.atomCount())
        return created;

    double x = graph.atoms()[static_cast<std::size_t>(startAtom)].x;
    double y = graph.atoms()[static_cast<std::size_t>(startAtom)].y;
    int previous = startAtom;
    for (int i = 0; i < length; ++i) {
        // ±30° about the requested heading: the drawn zig-zag, whose interior
        // angle is the 120° an sp3 chain is always sketched with.
        const double angle = deg2rad(angleDeg + (i % 2 == 0 ? 30.0 : -30.0));
        x += MoleculeGraph::kBondLength * std::cos(angle);
        y += MoleculeGraph::kBondLength * std::sin(angle);
        const int atom = graph.addAtom(6, x, y);
        graph.addBond(previous, atom, 1);
        created.push_back(atom);
        previous = atom;
    }
    return created;
}

// ---------------------------------------------------------------------------
// Tidy
// ---------------------------------------------------------------------------

void tidy(MoleculeGraph& graph, const std::vector<int>& atomIndices,
          int iterations)
{
    const int count = graph.atomCount();
    if (count < 2)
        return;

    std::vector<bool> movable(static_cast<std::size_t>(count),
                              atomIndices.empty());
    for (int index : atomIndices) {
        if (index >= 0 && index < count)
            movable[static_cast<std::size_t>(index)] = true;
    }

    // Which fragment each atom is in, so the weak spreading term never pushes
    // two separate molecules apart (or together) — their relative placement is
    // the user's, not the layout's.
    std::vector<int> fragmentOf(static_cast<std::size_t>(count), -1);
    const std::vector<std::vector<int>> fragmentList = graph.fragments();
    for (std::size_t f = 0; f < fragmentList.size(); ++f) {
        for (int atom : fragmentList[f])
            fragmentOf[static_cast<std::size_t>(atom)] = static_cast<int>(f);
    }

    const std::vector<std::vector<int>> ringList = graph.rings();
    std::vector<int> smallestRing(static_cast<std::size_t>(count), 0);
    for (const std::vector<int>& ring : ringList) {
        for (int atom : ring) {
            const int size = static_cast<int>(ring.size());
            int& current = smallestRing[static_cast<std::size_t>(atom)];
            if (current == 0 || size < current)
                current = size;
        }
    }

    std::vector<std::vector<int>> adjacency(static_cast<std::size_t>(count));
    for (const MolBond& bond : graph.bonds()) {
        adjacency[static_cast<std::size_t>(bond.a)].push_back(bond.b);
        adjacency[static_cast<std::size_t>(bond.b)].push_back(bond.a);
    }

    const double L = MoleculeGraph::kBondLength;
    std::vector<double> fx(static_cast<std::size_t>(count));
    std::vector<double> fy(static_cast<std::size_t>(count));

    for (int step = 0; step < iterations; ++step) {
        std::fill(fx.begin(), fx.end(), 0.0);
        std::fill(fy.begin(), fy.end(), 0.0);

        // 1. Bonds pull to the standard length.
        for (const MolBond& bond : graph.bonds()) {
            const MolAtom& a = graph.atoms()[static_cast<std::size_t>(bond.a)];
            const MolAtom& b = graph.atoms()[static_cast<std::size_t>(bond.b)];
            double dx = b.x - a.x;
            double dy = b.y - a.y;
            double distance = std::hypot(dx, dy);
            if (distance < 1e-9) {
                dx = 1e-3;
                dy = 0.0;
                distance = 1e-3;
            }
            const double push = 0.35 * (distance - L) / distance;
            fx[static_cast<std::size_t>(bond.a)] += push * dx;
            fy[static_cast<std::size_t>(bond.a)] += push * dy;
            fx[static_cast<std::size_t>(bond.b)] -= push * dx;
            fy[static_cast<std::size_t>(bond.b)] -= push * dy;
        }

        // 2. Bond PAIRS at a shared atom open to the angle the coordination
        //    implies. Applied as a 1-3 distance restraint, which is the same
        //    statement with none of an angular gradient's singularities.
        for (int atom = 0; atom < count; ++atom) {
            const std::vector<int>& neighborList =
                adjacency[static_cast<std::size_t>(atom)];
            if (neighborList.size() < 2)
                continue;
            const int ringSize = smallestRing[static_cast<std::size_t>(atom)];
            double target = 120.0;
            if (ringSize >= 3)
                target = 180.0 - 360.0 / ringSize;
            else if (neighborList.size() == 2 && graph.bondOrderSum(atom) >= 4)
                target = 180.0; // an sp centre: allene, alkyne, nitrile
            else if (neighborList.size() >= 4)
                target = 90.0;
            const double ideal = 2.0 * L * std::sin(deg2rad(target) / 2.0);

            for (std::size_t i = 0; i < neighborList.size(); ++i) {
                for (std::size_t j = i + 1; j < neighborList.size(); ++j) {
                    const int p = neighborList[i];
                    const int q = neighborList[j];
                    const MolAtom& u = graph.atoms()[static_cast<std::size_t>(p)];
                    const MolAtom& v = graph.atoms()[static_cast<std::size_t>(q)];
                    double dx = v.x - u.x;
                    double dy = v.y - u.y;
                    double distance = std::hypot(dx, dy);
                    if (distance < 1e-9) {
                        dx = 1e-3;
                        dy = 1e-3;
                        distance = std::hypot(dx, dy);
                    }
                    // Three or more substituents cannot all be `ideal` apart in
                    // a plane; the restraint is one-sided there (push apart,
                    // never pull together) so the drawing spreads instead of
                    // collapsing onto an unreachable target.
                    const bool oneSided = neighborList.size() > 2 && ringSize == 0;
                    if (oneSided && distance > ideal)
                        continue;
                    const double push = 0.12 * (distance - ideal) / distance;
                    fx[static_cast<std::size_t>(p)] += push * dx;
                    fy[static_cast<std::size_t>(p)] += push * dy;
                    fx[static_cast<std::size_t>(q)] -= push * dx;
                    fy[static_cast<std::size_t>(q)] -= push * dy;
                }
            }
        }

        // 3. Non-bonded atoms of the SAME fragment repel, so a folded drawing
        //    opens out. Separate fragments are left where the user put them.
        for (int i = 0; i < count; ++i) {
            for (int j = i + 1; j < count; ++j) {
                if (fragmentOf[static_cast<std::size_t>(i)]
                    != fragmentOf[static_cast<std::size_t>(j)])
                    continue;
                if (graph.bondBetween(i, j) >= 0)
                    continue;
                const MolAtom& a = graph.atoms()[static_cast<std::size_t>(i)];
                const MolAtom& b = graph.atoms()[static_cast<std::size_t>(j)];
                double dx = b.x - a.x;
                double dy = b.y - a.y;
                double distance = std::hypot(dx, dy);
                if (distance > 1.6 * L)
                    continue;
                if (distance < 1e-6) {
                    dx = 1e-3 * (i + 1);
                    dy = 1e-3 * (j + 1);
                    distance = std::hypot(dx, dy);
                }
                const double push = -0.05 * (1.6 * L - distance) / distance;
                fx[static_cast<std::size_t>(i)] += push * dx;
                fy[static_cast<std::size_t>(i)] += push * dy;
                fx[static_cast<std::size_t>(j)] -= push * dx;
                fy[static_cast<std::size_t>(j)] -= push * dy;
            }
        }

        double largest = 0.0;
        for (int atom = 0; atom < count; ++atom) {
            if (!movable[static_cast<std::size_t>(atom)])
                continue;
            const double dx = std::clamp(fx[static_cast<std::size_t>(atom)],
                                         -0.25 * L, 0.25 * L);
            const double dy = std::clamp(fy[static_cast<std::size_t>(atom)],
                                         -0.25 * L, 0.25 * L);
            graph.atoms()[static_cast<std::size_t>(atom)].x += dx;
            graph.atoms()[static_cast<std::size_t>(atom)].y += dy;
            largest = std::max(largest, std::hypot(dx, dy));
        }
        if (largest < 1e-6 * L)
            break;
    }
}

} // namespace calango::core
