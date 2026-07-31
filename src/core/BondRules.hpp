#pragma once

#include "core/Structure.hpp"

#include <utility>
#include <vector>

namespace calango::core {

/// Bond-editing RULES and how they propagate across a trajectory.
///
/// The Bond Editor lets a user say two different kinds of thing, and the
/// difference only becomes visible once there is more than one frame:
///
///   - "atoms 12 and 37 are bonded, with order 2" names ATOMS. An atom keeps
///     its index for the whole run, so the statement is frame-independent and
///     is simply copied onto every frame.
///
///   - "every Si-O pair between 1.4 and 1.9 Å is bonded" names a GEOMETRIC
///     CONDITION. Which pairs satisfy it is a property of one frame's
///     coordinates, so the rule is re-evaluated against each frame in turn.
///     Copying the first frame's match list forward would freeze a bond onto a
///     pair that has since dissociated — exactly the event a reactive
///     trajectory is being watched for, rendered invisible.
///
/// This header is the second kind. It is deliberately Qt-free: which pairs of
/// atoms satisfy a distance window is chemistry and geometry, not UI, and it
/// is the part that has to be right on frame 4000 of a run nobody will inspect
/// by eye.
struct ElementBondRule {
    int elementA = 0; ///< atomic number of the first partner
    int elementB = 0; ///< atomic number of the second partner
    /// Inclusive separation window (Å). Distances are DIRECT, not
    /// minimum-image: the rule is a user's statement about the atoms listed in
    /// the cell, and silently matching a partner's periodic image would create
    /// overrides for pairs that are nowhere near each other on screen.
    double minDistance = 0.0;
    double maxDistance = 2.0;

    bool matchesElements(int za, int zb) const
    {
        return (za == elementA && zb == elementB)
            || (za == elementB && zb == elementA);
    }
};

/// Index pairs (i < j) in `frame` that satisfy `rule`.
std::vector<std::pair<int, int>> matchingPairs(const Structure& frame,
                                               const ElementBondRule& rule);

/// Apply `rule` to every frame, RE-MATCHING against each frame's own
/// coordinates, and return the total number of pairs affected across all of
/// them. `bond` true forces the matching pairs bonded; false unbonds them
/// (clearing an explicit "added" mark and then suppressing the auto-perceived
/// bond, so the pair is truly unbonded either way).
///
/// Null entries are skipped. Frames are not required to be distinct objects;
/// the underlying overrides are idempotent, so a repeated frame costs a second
/// pass and changes nothing.
int applyElementRule(const std::vector<Structure*>& frames,
                     const ElementBondRule& rule, bool bond);

/// Force the bond between atoms `i` and `j` (0-based) on every frame, at
/// `order`. Frames too small to contain both indices are skipped rather than
/// bonded to whatever happens to sit there.
void applyIndexBond(const std::vector<Structure*>& frames, int i, int j,
                    int order);

/// Suppress the bond between `i` and `j` on every frame.
void applyIndexSuppression(const std::vector<Structure*>& frames, int i, int j);

/// Forget any override for `i`-`j` on every frame.
void clearPairOnAllFrames(const std::vector<Structure*>& frames, int i, int j);

/// Forget every override on every frame.
void clearAllOnAllFrames(const std::vector<Structure*>& frames);

} // namespace calango::core
