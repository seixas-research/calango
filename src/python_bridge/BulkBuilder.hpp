#pragma once

#include "core/Structure.hpp"

#include <string>
#include <vector>

namespace calango::pybridge {

/// Bulk crystal generation through ASE, in the two flavors a crystal
/// structure is normally specified in:
///
///  - by *prototype* (`ase.build.bulk`): a name plus one of ASE's built-in
///    lattice types (fcc, bcc, hcp, diamond, rocksalt, zincblende, …). The
///    basis is implied by the prototype.
///  - by *space group + Wyckoff basis* (`ase.spacegroup.crystal`): explicit
///    representative site coordinates, from which the space-group symmetry
///    generates the full orbit. This is the general case — it covers
///    perovskites, spinels and any structure ASE has no prototype for.
///
/// Both throw std::runtime_error with ASE's own message on bad input
/// (unknown prototype, element/lattice mismatch, missing c/a, …).
/// GUI-thread only, like all of pybridge.
class BulkBuilder {
public:
    /// ase.build.bulk parameters. `c` and `u` are only read when the chosen
    /// lattice uses them (hcp/tetragonal/orthorhombic families); pass
    /// `hasC = false` to let ASE apply its ideal value.
    struct PrototypeSpec {
        std::string name;           ///< "Si", "NaCl", "MoS2" …
        std::string crystalStructure = "fcc";
        double a = 0.0;             ///< lattice constant (Å); 0 = ASE default
        double b = 0.0;             ///< second axis (Å), orthorhombic only
        bool hasB = false;
        double c = 0.0;             ///< third axis (Å)
        bool hasC = false;
        double covera = 0.0;        ///< c/a ratio, used when hasC is false
        bool hasCovera = false;
        double u = 0.0;             ///< internal parameter (wurtzite/mx2)
        bool hasU = false;
        bool cubic = false;         ///< force the cubic (conventional) cell
        bool orthorhombic = false;  ///< force an orthorhombic cell
    };

    /// One symmetry-inequivalent site of a space-group basis.
    struct WyckoffSite {
        std::string symbol;         ///< element symbol
        double u = 0.0;             ///< fractional coordinates of the
        double v = 0.0;             ///< representative site
        double w = 0.0;
        double occupancy = 1.0;     ///< informational (ASE builds ordered
                                    ///< cells; < 1 is reported, not applied)
    };

    /// ase.spacegroup.crystal parameters.
    struct SpaceGroupSpec {
        int spaceGroup = 225;
        std::vector<WyckoffSite> sites;
        double a = 1.0, b = 1.0, c = 1.0;          ///< cell lengths (Å)
        double alpha = 90.0, beta = 90.0, gamma = 90.0; ///< cell angles (°)
        bool primitive = false;     ///< reduce to the primitive cell
    };

    /// Ground-state crystallographic reference for a single element, taken from
    /// ASE's tabulated `ase.data.reference_states`. Used to auto-configure the
    /// Bulk builder when the user picks an element (e.g. Cu → fcc a≈3.61,
    /// Fe → bcc, C/Si → diamond).
    struct ReferenceState {
        bool found = false;             ///< the element has ASE reference data
        std::string crystalStructure;   ///< mapped onto prototypes(); empty when
                                        ///< ASE's structure isn't an offered
                                        ///< prototype (leave the combo as-is)
        double a = 0.0;                 ///< lattice constant a (Å); 0 = unknown
        double covera = 0.0;            ///< c/a ratio (hcp-like); valid iff
        bool hasCovera = false;         ///< hasCovera is true
    };

    static core::Structure buildPrototype(const PrototypeSpec& spec);
    static core::Structure buildFromSpaceGroup(const SpaceGroupSpec& spec);

    /// Look up `symbol`'s ground-state reference crystal (structure + lattice
    /// constants) from ASE. Returns `found == false` for unknown symbols or
    /// elements ASE has no reference state for. GUI-thread only.
    static ReferenceState referenceState(const std::string& symbol);

    /// The crystal-structure names `buildPrototype` accepts, in the order
    /// they should be offered in the UI. Kept here (not in the dialog) so
    /// the list and the builder can never drift apart.
    static const std::vector<std::string>& prototypes();

    /// True when `crystalStructure` needs an explicit c or c/a from the user
    /// (ASE raises for these if neither is supplied and the prototype has no
    /// tabulated ideal value).
    static bool usesCOverA(const std::string& crystalStructure);

    /// True when `crystalStructure` has an independent b axis.
    static bool usesB(const std::string& crystalStructure);
};

} // namespace calango::pybridge
