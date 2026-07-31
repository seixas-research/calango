#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Which localized shell the Hubbard correction acts on. The values are the
/// angular momentum quantum number, which is exactly what both engines want —
/// VASP's LDAUL and Quantum ESPRESSO's Hubbard manifold both name l.
enum class HubbardShell {
    P = 1, ///< p — rare, but it is what a p-block oxide anion needs
    D = 2, ///< d — transition metals; the overwhelmingly common case
    F = 3, ///< f — lanthanides and actinides
};

/// One perturbed site: which atom of the supercell gets the α shift, and what
/// it is made of.
struct HubbardSite {
    int atomIndex = 0;         ///< index into the PRIMITIVE structure
    std::string element = "Fe";
    HubbardShell shell = HubbardShell::D;
};

/// Linear-response Hubbard U, after Cococcioni and de Gironcoli,
/// Phys. Rev. B 71, 035105 (2005).
///
/// The idea in one paragraph, because the generated script is unreadable
/// without it. Add a localized potential α to the Hubbard manifold of ONE atom
/// and watch how the occupation of that manifold responds. Two responses
/// matter and they are not the same: the SELF-CONSISTENT one,
/// χ = dn/dα, in which the rest of the electrons are allowed to rearrange and
/// screen the perturbation, and the NON-INTERACTING one, χ₀ = dn⁰/dα, taken
/// after a single diagonalization at the unperturbed self-consistent
/// potential, before any screening has happened. The difference between the
/// two inverse responses is precisely the spurious curvature the Hubbard term
/// is there to remove:
///
///     U_eff = (χ₀⁻¹ − χ⁻¹)
///
/// Two things about this are easy to get wrong and are why this is a module
/// rather than a checkbox:
///
///   * χ₀ is NOT "the response with U = 0". It is the response of the
///     Kohn-Sham system at FIXED potential — one diagonalization, no SCF. Run
///     it self-consistently by mistake and you compute χ twice, and U comes
///     out zero.
///   * The perturbation must be localized. In a periodic cell it is applied to
///     every image of the atom at once, so what is measured is the response to
///     a lattice of perturbations rather than to one. The cure is a supercell
///     big enough that the images stop talking, and the only way to know it is
///     big enough is to repeat the calculation on a larger one — which is why
///     the supercell is a control here and not a hidden constant.
///
/// The matrices are square over the perturbed sites. With a single site they
/// collapse to scalars and U = 1/χ₀ − 1/χ; with several, the inverse is a real
/// matrix inverse, which is what makes an inequivalent-site calculation
/// different from N independent ones.
struct HubbardRunConfig {
    /// Sites that carry a Hubbard manifold. Every one of them is perturbed in
    /// turn (one run per site per α), and all of them are measured in every
    /// run — the off-diagonal χ_ij is what the matrix inversion needs.
    std::vector<HubbardSite> sites;

    /// Supercell repetitions applied to the input structure before anything
    /// else. See the note above: this is the convergence parameter of the
    /// method, not a performance knob.
    int supercell[3] = {2, 2, 2};

    /// Perturbation strengths in eV. Symmetric about zero and small: the
    /// response has to be linear for the fit to mean anything, and the α = 0
    /// point is supplied by the unperturbed run rather than being listed here.
    std::vector<double> alphas = {-0.15, -0.10, -0.05, 0.05, 0.10, 0.15};

    /// The SCF that every step of the pipeline runs. Bootstrapped from the
    /// Single-point configuration, so the perturbed runs are converged exactly
    /// as a standalone single point would be — the response is a difference of
    /// occupations, and two runs converged to different criteria produce a
    /// difference dominated by that instead of by the physics.
    CalculatorConfig calculator;

    /// Write the response matrices and the per-α occupations to
    /// `hubbard_response.json` beside the result. On by default: a U that
    /// cannot be traced back to the fit it came from cannot be checked, and
    /// the usual failure (a non-linear response, i.e. α too large) is visible
    /// in nothing else.
    bool writeResponseData = true;
};

/// Which engines this module can drive. VASP through LDAUTYPE = 3, Quantum
/// ESPRESSO through Hubbard_alpha; nothing else here exposes a per-site
/// potential shift on the Hubbard projectors, which is the one primitive the
/// method needs.
bool hubbardSupportsCalculator(CalculatorKind kind);

/// Human name of a shell ("d"), for labels and generated comments.
std::string toString(HubbardShell shell);

/// The self-contained Python driver for the whole pipeline: supercell,
/// unperturbed SCF, the perturbed runs, the χ / χ₀ fits and U_eff.
///
/// `structureFile` is the staged geometry the script reads (extxyz).
std::string generateHubbardScript(const HubbardRunConfig& config,
                                  const std::string& structureFile);

} // namespace calango::core
