#pragma once

#include "dft/DftTypes.hpp"
#include "dftb/SlaterKosterTable.hpp"

#include <vector>

/// The engine's fixed orbital scope: s and p only.
///
/// A .skf file's d-shell columns (and, when present, on-site d data) are
/// parsed by SlaterKosterFile.hpp so a heavier-element file still round-trips
/// exactly, but no d orbital is ever assembled into a Hamiltonian here.
/// Every mainstream light/medium-element parameter set this engine targets
/// (mio, 3ob, pbc, matsci — organic/biological chemistry and light solids)
/// is s,p for its non-metal elements; d-shell metals go to FUTURE.md.
///
/// Whether a given element's basis is {s} alone or {s, px, py, pz} is NOT a
/// user setting (unlike DFTB+'s MaxAngularMomentum, which must be typed per
/// element) — it is read straight off the element's own homonuclear on-site
/// line: fp > 0 means the neutral free atom actually has p electrons in this
/// parametrization, so the set's author included p basis functions for it
/// (hydrogen: fp == 0 in every mainstream set, s-only; carbon/nitrogen/
/// oxygen/...: fp > 0, s and p). See SlaterKosterTable::hasPShell().
namespace calango::dftb {

/// One atom's slice of the global orbital basis. Orbitals are always in the
/// canonical order [s, px, py, pz] (a 1- or 4-element prefix of that list —
/// see `orbitalCount()`), matching SlaterKosterTransform.hpp's skBlock().
struct AtomOrbitals {
    int atomicNumber = 0;
    bool hasP = false;
    /// Offset of this atom's first orbital in the global H(k)/S(k) matrices.
    int firstOrbital = 0;

    int orbitalCount() const { return hasP ? 4 : 1; }
};

struct DftbBasis {
    std::vector<AtomOrbitals> atoms;
    int totalOrbitals = 0;

    /// Build the basis for a structure's element list, in atom order.
    /// Fails (InvalidInput) if any element's homonuclear on-site data is
    /// missing from `table` — a basis cannot be built without knowing every
    /// atom's own shell set and on-site energies.
    static calango::dft::Outcome build(const std::vector<int>& atomicNumbers,
                                        const SlaterKosterTable& table,
                                        DftbBasis& out);
};

} // namespace calango::dftb
