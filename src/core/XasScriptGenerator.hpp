#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Which core level the hole is made in.
enum class XasCoreLevel {
    K,   ///< 1s — the K edge, and what almost every XAS measurement means
    L1,  ///< 2s
    L23, ///< 2p — the L2,3 edges
};

/// How the core hole is occupied, i.e. which approximation the spectrum is
/// computed in.
///
/// The choice is a real physical one and it changes the answer, so it is the
/// user's rather than a default buried in the script.
enum class XasCoreHole {
    /// Half a hole (0.5 electrons removed). The transition-potential
    /// approximation: one calculation gives the whole spectrum because the
    /// final-state relaxation is averaged over initial and final states. This
    /// is what the GPAW tutorial uses and what most published XAS is.
    Half,
    /// A full hole. The excited final state proper — better for the first
    /// resonance, worse for the rest of the spectrum, and it needs the
    /// delta-Kohn-Sham correction to sit on an absolute energy scale.
    Full,
    /// No hole. The unperturbed ground state, i.e. what the spectrum would look
    /// like if the core hole did not pull the excited states down. Offered
    /// because comparing against it is how you see how large that effect is.
    None,
};

/// X-ray absorption spectroscopy, following the GPAW XAS tutorial.
///
/// The physics that makes this awkward to automate: a core-level spectrum
/// needs a PAW dataset that has a HOLE in the core level, and no such dataset
/// ships with GPAW — one has to be generated for the absorbing element first.
/// So the script is three stages, not one: generate the setup, run a ground
/// state that uses it on the absorbing atom only, then evaluate the spectrum
/// from the resulting wavefunctions.
///
/// Two constraints are worth knowing before reading the generated script:
///
///   * `gpaw.xas` requires the LEGACY GPAW engine. Every other script Calango
///     emits sets GPAW_NEW=1; this one must not, and must pass
///     `legacy_gpaw=True`, or the run dies with "New-GPAW not supported".
///   * The setup is written into the job directory and found through
///     `setup_paths`, so the run is self-contained and reproducible — nothing
///     is installed into the GPAW distribution.
struct XasRunConfig {
    /// Chemical symbol of the absorbing element, e.g. "O".
    std::string element = "O";
    /// Index of the absorbing atom in the structure. Only this atom gets the
    /// core-hole setup: giving it to every atom of the species would model a
    /// solid in which every one of them is simultaneously excited.
    int absorbingAtom = 0;

    XasCoreLevel coreLevel = XasCoreLevel::K;
    XasCoreHole coreHole = XasCoreHole::Half;

    /// The ground state the spectrum is evaluated from. Only the GPAW fields
    /// are used; `task` is ignored.
    CalculatorConfig calculator;

    /// Unoccupied bands to converge, as a NEGATIVE count of extra bands above
    /// the occupied ones (GPAW's `nbands=-n` convention). The spectrum is a
    /// sum over these, so too few truncates it — visibly, as a spectrum that
    /// simply stops.
    int extraBands = 30;

    /// Gaussian broadening applied to every transition (eV).
    double fwhm = 0.5;
    /// Linear broadening ramp [fwhm0, e_start, e_stop]: the broadening grows
    /// with energy above the edge, which is the physical behaviour (core-hole
    /// lifetime plus final-state broadening) and what makes a computed
    /// spectrum comparable with a measured one. Disabled when `linearBroadening`
    /// is false.
    bool linearBroadening = true;
    double linearBroadeningStart = 1.5;
    double linearBroadeningEnergyStart = 536.0;
    double linearBroadeningEnergyStop = 540.0;

    /// Shift putting the spectrum on an absolute energy scale, from a
    /// delta-Kohn-Sham total-energy difference. 0 leaves the relative scale
    /// GPAW produces, whose zero is the first unoccupied state.
    double dksEnergy = 0.0;
    /// Run the two extra total-energy calculations that produce that shift
    /// rather than taking the number above. Roughly doubles the cost.
    bool computeDks = false;

    std::string resultsJson = "xas.json";
};

class XasScriptGenerator {
public:
    /// Full ASE/GPAW script: core-hole setup generation, ground state, and the
    /// spectrum, written to `xas.json` for the results viewer.
    static std::string generate(const XasRunConfig& config,
                                const std::string& structureFile);
};

} // namespace calango::core
