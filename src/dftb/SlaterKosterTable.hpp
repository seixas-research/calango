#pragma once

#include "dftb/SlaterKosterFile.hpp"

#include <map>
#include <string>
#include <vector>

/// A directory of .skf files, indexed by ORDERED element pair.
///
/// Loads "<Sym1>-<Sym2>.skf" for every ordered pair (Z1, Z2) drawn from a
/// structure's element set — BOTH orderings for a heteronuclear pair, since
/// they are not interchangeable (see SlaterKosterTransform.hpp's derivation
/// comment for exactly why: X-Y.skf and Y-X.skf tabulate genuinely different
/// matrix elements, not the same one written twice). A homonuclear pair
/// loads and indexes one file under both "orderings" (they are the same
/// ordered pair).
///
/// Matches the naming convention every mainstream set (mio, 3ob, pbc,
/// matsci, and DFTB+ itself) ships: exactly "<symbol>-<symbol>.skf", element
/// symbols capitalised the normal chemical way (e.g. "C-H.skf", not
/// "c-h.skf" or "C_H.skf"). No alternate separator or casing is guessed at —
/// a mismatch is reported by `missingPairs()` rather than silently unmet.
namespace calango::dftb {

class SlaterKosterTable {
public:
    /// Load every ordered pair drawn from `atomicNumbers` (deduplicated) from
    /// `directory`. Partial failure is reported through `missingPairs()`
    /// afterward rather than through the Outcome here — `load()` itself only
    /// fails for a directory that cannot be opened at all; a directory that
    /// exists but is missing one pair's file is exactly the case the
    /// pre-flight check (mirroring VaspPotcarPreflight's pattern) exists to
    /// report per-element, not as one opaque failure.
    Outcome load(const std::string& directory,
                 const std::vector<int>& atomicNumbers);

    /// The parsed file for the ORDERED pair (first, second) — element(first)
    /// is "atom alpha" (the origin atom in the two-center integral's own
    /// convention), element(second) is "atom beta" (displaced by r0). Null
    /// if that pair was not requested or its file could not be read.
    const SlaterKosterFile* pair(int atomicNumberFirst,
                                  int atomicNumberSecond) const;

    /// On-site shell data for one element, always read from its own
    /// homonuclear file (pair(z, z)) — null if that element's homonuclear
    /// file is missing or unreadable.
    const std::array<OnsiteShell, 3>* onsite(int atomicNumber) const;

    /// "<Sym1>-<Sym2>.skf" filenames that were requested by `load()` but
    /// could not be read or parsed, each paired with why. Empty means every
    /// requested pair loaded — the pre-flight-passed case.
    struct MissingPair {
        std::string fileName;
        std::string reason;
    };
    const std::vector<MissingPair>& missingPairs() const { return missing_; }

    /// True when a p shell is part of `atomicNumber`'s basis in the loaded
    /// parameter set — the neutral-atom occupation fp > 0 on its own
    /// homonuclear on-site line (hydrogen in most sets: fp == 0, no p
    /// functions at all; carbon, nitrogen, oxygen, ...: fp > 0). False for
    /// an element whose homonuclear file did not load (callers must check
    /// `missingPairs()` first).
    bool hasPShell(int atomicNumber) const;

private:
    std::string directory_;
    std::map<std::pair<int, int>, SlaterKosterFile> files_;
    std::vector<MissingPair> missing_;
};

} // namespace calango::dftb
