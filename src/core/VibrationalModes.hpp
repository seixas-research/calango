#pragma once

#include "core/Structure.hpp"
#include "core/Vec3.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace calango::core {

/// Vibrational (phonon) normal modes: what a finished phonon run knows about
/// how the nuclei move, and what can be built from it.
///
/// A dispersion curve says a branch exists at 480 cm⁻¹; it does not say whether
/// that is a bond stretch, a libration or a soft mode about to drive a
/// transition. Only the eigenvector answers that, and the way to read an
/// eigenvector is to watch it — so everything here exists to turn one branch at
/// one q into displaced geometry.
///
/// Qt-free on purpose: the physics below (mass weighting, the q·R phase, the
/// closed-form velocities, F = −Mω²u) is the part that can be wrong in ways no
/// screenshot reveals, so it is pinned by a test that links no GUI. Decoding
/// phonon_modes.json stays on the GUI side, where QJsonDocument already lives —
/// the same split ElectronPhononIo made for the same reason.

/// How the eigenvector components in a mode set are normalized.
///
/// This is NOT cosmetic. The eigenvectors of the dynamical matrix
/// D_ij = Φ_ij/√(M_i M_j) are mass-weighted, w_i = √M_i · u_i, and animating
/// them as if they were displacements makes heavy atoms swing too far and light
/// atoms too little — a picture that looks entirely plausible and is wrong.
///
/// Calango's two phonon drivers disagree, which is why this exists:
///   * phonopy's `get_frequencies_with_eigenvectors` returns the dynamical
///     matrix's own eigenvectors → MassWeighted.
///   * ASE's `Phonons.band_structure(..., modes=True)` already multiplies by
///     1/√M before returning → Displacement.
enum class EigenvectorConvention : std::uint8_t {
    Displacement, ///< components are u_i directly (ASE)
    MassWeighted, ///< components are w_i = √M_i·u_i (phonopy, dynamical matrix)
};

/// One q-point's modes: frequencies (cm⁻¹) and, when the run exported them, the
/// per-atom complex eigenvectors of each branch.
struct VibrationalQPoint {
    std::string label;             ///< "Γ", "X", or the raw coordinates
    double q[3] = {0.0, 0.0, 0.0}; ///< fractional reciprocal coordinates
    std::vector<double> frequenciesCm; ///< negative = imaginary mode
    /// eigenvectorsReal[branch][atom] — Re e_{n,α}(q). Empty when the run did
    /// not export eigenvectors (phonon_band.json carries frequencies only).
    std::vector<std::vector<Vec3>> eigenvectorsReal;
    /// Im e_{n,α}(q). Zero away from a real-valued (Γ, or ASE) export, but
    /// always the same length as eigenvectorsReal so the two index alike.
    std::vector<std::vector<Vec3>> eigenvectorsImag;
    /// Irreducible-representation label per branch ("T2g", "Eu+A1g", …). Only Γ
    /// carries these — the factor group is not the little group of any other q
    /// — and only when the run could assign them; empty entries mean "no clean
    /// assignment".
    std::vector<std::string> irreps;

    std::size_t branchCount() const { return frequenciesCm.size(); }
    bool hasEigenvectors() const { return !eigenvectorsReal.empty(); }
};

/// Everything a phonon run exported about its modes.
struct VibrationalModeSet {
    std::vector<VibrationalQPoint> qpoints;
    EigenvectorConvention convention = EigenvectorConvention::MassWeighted;

    bool hasEigenvectors() const
    {
        return !qpoints.empty() && qpoints.front().hasEigenvectors();
    }
};

/// How a mode is turned into geometry.
struct ModeDisplacement {
    /// Peak displacement of the LARGEST-moving atom, in Å.
    ///
    /// Defined on the resulting motion rather than "per unit eigenvector
    /// component", because the eigenvector's own normalization is a convention
    /// of whichever code wrote the file — anchoring the slider to it would make
    /// the same physical mode animate at different sizes depending on the
    /// driver. Purely a visualization scale either way: a harmonic eigenvector
    /// has no intrinsic amplitude.
    double amplitudeAng = 0.3;
    /// Phase ωt in radians. u ∝ Re[e·exp(i(q·R − phase))], so phase 0 is a
    /// turning point (maximum displacement, zero velocity).
    double phase = 0.0;
    /// Also attach the instantaneous state of the motion: harmonic restoring
    /// forces as the "forces" vector field and atomic velocities as
    /// "velocities". Both are exact for the mode at this phase rather than
    /// differenced between frames, and both travel into an exported trajectory
    /// as their own columns.
    ///
    /// Off for a live animation, which needs neither and would pay for them
    /// thirty times a second.
    bool withDynamics = false;
};

/// `reference` displaced by branch `branch` at q-point `qIndex`.
///
/// Null when the indices are out of range, when the set carries no
/// eigenvectors, or when the branch's displacement pattern is identically zero
/// (which is what an all-zero eigenvector row from a truncated export looks
/// like). Returning null rather than the undisplaced structure is deliberate:
/// a caller that shows "nothing moved" has been told the mode is flat, which is
/// a physical claim this cannot make.
std::shared_ptr<Structure> displaceByMode(const Structure& reference,
                                          const VibrationalModeSet& modes,
                                          std::size_t qIndex, std::size_t branch,
                                          const ModeDisplacement& options);

/// One full vibrational period sampled uniformly in phase, `frames` long.
///
/// The closing frame is omitted: phase 2π is the same configuration as phase 0,
/// and a duplicated frame makes a looped playback stutter. Every frame carries
/// the dynamics fields regardless of `options.withDynamics` — a trajectory is
/// exactly where forces and velocities are inspected.
std::vector<std::shared_ptr<Structure>> modeTrajectory(
    const Structure& reference, const VibrationalModeSet& modes,
    std::size_t qIndex, std::size_t branch, const ModeDisplacement& options,
    int frames);

/// What a branch's eigenvector says about itself, independent of any picture.
///
/// These are the checks that catch a broken export or a misread convention
/// before a wrong animation is believed. All of them are evaluated in the
/// mass-weighted metric, where the eigenvectors of the dynamical matrix are
/// orthonormal, whatever convention the file used.
struct ModeDiagnostics {
    /// ‖w‖ in the mass-weighted metric. 1 for a properly normalized
    /// eigenvector; anything else means the file was rescaled or truncated.
    double norm = 0.0;
    /// Largest |⟨ŵ_b|ŵ_c⟩| against every other branch at this q. ~0 for a real
    /// eigenbasis; a large value means the export is not an eigenbasis at all
    /// (or that two branches were written twice).
    double maxOverlap = 0.0;
    /// Weight of this mode on the three rigid translations, 0…1.
    ///
    /// This is the acoustic sum rule read as a projection: Σ_i M_i u_i = 0 for
    /// any true vibration, and the three modes for which the sum does NOT
    /// vanish are precisely the acoustic branches, which at Γ are rigid
    /// translations at ω = 0.
    double translationWeight = 0.0;
    /// translationWeight > 0.99 — the mode moves the whole crystal and
    /// deforms nothing. Only meaningful at Γ.
    bool rigidTranslation = false;
    /// Number of branches at this q within `degeneracyToleranceCm` of this
    /// one's frequency, including itself. 1 for a non-degenerate mode; the
    /// count is what an irrep label like "E" or "T2g" implies.
    int degeneracy = 1;
};

/// Per-branch diagnostics for one q-point. Empty when the q-point carries no
/// eigenvectors. `reference` supplies the masses, so its atom count and order
/// must be the ones the phonon run used.
std::vector<ModeDiagnostics> analyzeModes(const Structure& reference,
                                          const VibrationalQPoint& qpoint,
                                          EigenvectorConvention convention,
                                          double degeneracyToleranceCm = 0.5);

/// Frequency conversions shared by everything that displays a mode. cm⁻¹ is the
/// storage unit because it is what both drivers are converted to on export.
double wavenumberToMev(double cm);
double wavenumberToThz(double cm);

} // namespace calango::core
