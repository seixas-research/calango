#pragma once

#include <string>

namespace calango::core {

/// Whether a completed ground state kept the FULL Brillouin-zone k-set.
///
/// Three states, not a bool, and the third is the point: "we could not tell"
/// is a different answer from "symmetry was on", and reporting it as the
/// latter would either cry wolf on a perfectly good baseline or — worse, if
/// the default went the other way — pass a bad one silently.
enum class SymmetryState {
    Off,     ///< the full zone is present; what a wannierization needs
    On,      ///< the k-set was folded into the irreducible wedge
    Unknown, ///< nothing in the run directory settles it
};

/// What the Wannier setup needs to know about the ground state it inherits.
///
/// WHY IT MATTERS. ASE's Wannier consumes Bloch states on the FULL Brillouin
/// zone: it forms overlaps between neighbouring k-points across the whole mesh,
/// and a calculation that stored only the irreducible wedge has no state to
/// offer at most of them. Whether the parent single point folded its k-points
/// is therefore not a detail — it decides whether the run can work at all —
/// and it is invisible in the wizard until someone shows it.
struct BaselineSummary {
    SymmetryState symmetry = SymmetryState::Unknown;
    int bands = 0;             ///< bands in the calculation; 0 = not recorded
    int kpts[3] = {0, 0, 0};   ///< Monkhorst-Pack mesh; 0 = not recorded
    int bzPoints = 0;          ///< k-points in the full zone; 0 = not recorded
    int ibzPoints = 0;         ///< k-points kept after folding
    int symmetryOperations = 0;///< operations GPAW found; 0 = not recorded
    std::string engine;        ///< human name from calculator.json ("GPAW")
    int engineKind = -1;       ///< CalculatorKind as int; -1 = unknown
    /// Which file each fact came from, for the tool tip — so a user who
    /// disagrees with the verdict can go and look at the same evidence.
    std::string evidence;

    /// True when the mesh is known and every one of its points survived, which
    /// is the operational form of "symmetry off": not "no operations were
    /// found" but "nothing was folded away".
    bool fullZoneConfirmed() const
    {
        return bzPoints > 0 && ibzPoints == bzPoints;
    }
};

/// Read everything `jobDir` records about the ground state it holds.
///
/// Sources, most authoritative first, because they are not equally good:
///   1. the engine's own text log — what the run ACTUALLY did, including the
///      BZ/IBZ counts that answer the question directly;
///   2. `calculator.json` — what Calango ASKED for, which settles the question
///      only when the key is present (an absent key is not a `false`);
///   3. the generated `run.py` — the last resort for a directory whose output
///      was moved or truncated.
///
/// Never throws: an unreadable or missing directory yields a summary that
/// reports Unknown, which is a true statement about it.
BaselineSummary readBaselineSummary(const std::string& jobDir);

} // namespace calango::core
