#pragma once

#include "core/ConvexHull.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace calango::core {

/// eV per atom -> J per mole of atoms, i.e. e·N_A.
///
/// Every number crossing from the ab-initio side of this module to the CALPHAD
/// side goes through this constant, because the two fields never agreed on a
/// unit: a DFT total energy is eV/atom and a `.tdb` is J/mol from end to end.
/// Getting it wrong is not a subtle error — it is a factor of 10^5, so it
/// shows up as a phase diagram with a melting point of 0.01 K — but it is also
/// the kind of thing that gets applied twice, which is why the conversion
/// lives in exactly one place and the struct fields carry their unit in the
/// name.
inline constexpr double kEvPerAtomToJPerMol = 96485.33212331001;

/// Molar gas constant, J/(mol·K). Exact since the 2019 SI redefinition fixed
/// k_B and N_A.
inline constexpr double kGasConstantJPerMolK = 8.31446261815324;

/// One Redlich-Kister coefficient, linear in temperature:
///
///   L_ν(T) = a + b·T          [J/mol]
///
/// The linear-in-T form is not a simplification for its own sake: `a` is an
/// excess ENTHALPY and `b` a (negated) excess ENTROPY, so the two halves are
/// exactly what a static DFT calculation and a vibrational free energy
/// respectively contribute. A fit with only E_0 data determines `a` and leaves
/// `b` at zero, which is the honest result rather than a hidden default.
struct RedlichKisterTerm {
    double a = 0.0; ///< J/mol
    double b = 0.0; ///< J/(mol·K)

    double at(double temperatureK) const { return a + b * temperatureK; }
};

/// Excess Gibbs energy of a binary substitutional solution, J/mol of atoms:
///
///   G_ex(x, T) = x_A x_B Σ_ν L_ν(T) (x_A − x_B)^ν
///
/// with `moleFractionB` = x_B and x_A = 1 − x_B.
///
/// TRAP — THE SIGN OF THE ODD TERMS. The polynomial variable is (x_A − x_B),
/// NOT (x_B − x_A), and which species is "A" is fixed by the order the
/// constituents are written in the TDB parameter `L(PHASE,A,B;ν)`. Swap the
/// two names and every odd-ν coefficient changes sign while the even ones do
/// not, so the mistake produces a diagram that is subtly asymmetric in the
/// wrong direction rather than an obviously broken one. Everything in this
/// module states x as the fraction of the SECOND-named constituent, and
/// tdbWrite() re-orders constituents into the alphabetical order TDB requires,
/// flipping the odd terms when it does.
double redlichKisterExcess(const std::vector<RedlichKisterTerm>& terms,
                           double moleFractionB, double temperatureK);

/// Ideal (configurational) mixing free energy of a binary substitutional
/// solution, J/mol of atoms:
///
///   G_ideal(x, T) = RT [ x_A ln x_A + x_B ln x_B ]
///
/// Always negative, and its derivative diverges at the endpoints — which is
/// why a solution phase is never entirely eliminated at finite temperature and
/// why terminal solid solubility exists at all.
double idealMixingGibbs(double moleFractionB, double temperatureK);

/// Molar Gibbs energy of a binary substitutional solution phase, J/mol atoms:
///
///   G = x_A G_A° + x_B G_B° + G_ideal + G_ex
///
/// `gibbsA`/`gibbsB` are the pure-endmember energies in the SAME phase and the
/// same reference as the caller intends the diagram to be drawn in. For a
/// first-principles assessment they are the DFT energies of the pure elements
/// in that structure; for a database they come out of the G parameters.
double binarySolutionGibbs(double gibbsAJPerMol, double gibbsBJPerMol,
                           const std::vector<RedlichKisterTerm>& terms,
                           double moleFractionB, double temperatureK);

/// One (x, T) observation of the excess Gibbs energy, the thing that is fitted.
struct RedlichKisterSample {
    double moleFractionB = 0.0;
    double temperatureK = 0.0;
    double excessJPerMol = 0.0;
    double weight = 1.0;
};

struct RedlichKisterFit {
    bool ok = false;
    /// Index is the Redlich-Kister order ν, so terms.size() == order + 1.
    std::vector<RedlichKisterTerm> terms;
    double rmsResidualJPerMol = 0.0;
    double maxResidualJPerMol = 0.0;
    /// Samples that actually entered the fit (endpoints are dropped — see
    /// fitRedlichKister).
    int usedSamples = 0;
    std::string note;
};

/// Weighted linear least-squares fit of `order + 1` Redlich-Kister terms.
///
/// The model is linear in its coefficients, so this is a normal-equation solve
/// and not an optimization: there is one answer and no starting guess. Columns
/// are normalized to unit norm before the solve and un-scaled afterwards,
/// because the T-dependent basis functions are ~10^3 times larger than the
/// T-independent ones and the un-normalized normal matrix loses six digits of
/// conditioning to nothing but a choice of unit.
///
/// Samples at x = 0 or x = 1 are DROPPED rather than fitted. Every basis
/// function carries the factor x_A·x_B, so an endpoint contributes an
/// identically-zero row: it cannot influence the coefficients, but it would
/// pad the residual statistics with exact zeros and make a bad fit report a
/// good RMS.
///
/// `temperatureDependent` turns on the b·T half of every term. Asking for it
/// from samples at a single temperature is rank-deficient — a and b·T are then
/// the same column — and is refused with a note that says so rather than
/// returning whatever the pivoting happened to produce.
RedlichKisterFit fitRedlichKister(const std::vector<RedlichKisterSample>& samples,
                                  int order, bool temperatureDependent);

// ---------------------------------------------------------------------------
// The DFT -> CALPHAD pipeline
// ---------------------------------------------------------------------------

/// One first-principles configuration entering an assessment.
struct CalphadConfiguration {
    std::string label;             ///< e.g. "Ag3Au1 (frame 12)"
    double moleFractionB = 0.0;    ///< x of the second element
    double energyEvPerAtom = 0.0;  ///< DFT total energy per atom
    /// F_vib(T) per atom, on the assessment's temperature grid, in eV.
    ///
    /// This is PhononThermoPoint::freeEnergyEv from
    /// core::computePhononThermodynamics divided by the number of atoms in the
    /// cell the phonons were computed for — that function reports PER CELL and
    /// the assessment works per atom, and forgetting the division scales the
    /// vibrational term by the supercell size.
    ///
    /// Empty when the configuration has no phonon data; the assessment is then
    /// static (E_0 only) and says so in its note.
    std::vector<double> vibFreeEnergyEvPerAtom;
};

struct CalphadAssessmentInput {
    std::string elementA;          ///< x = 0 endpoint
    std::string elementB;          ///< x = 1 endpoint
    std::string phaseName = "SOLUTION";

    /// Per-atom DFT energies of the pure endpoints IN THE SAME STRUCTURE as
    /// the configurations. The formation energies are referenced to these, so
    /// quoting a pure element in its own ground-state structure while the
    /// configurations are on a different lattice silently folds the lattice
    /// stability into the interaction parameters.
    double referenceEnergyAEvPerAtom = 0.0;
    double referenceEnergyBEvPerAtom = 0.0;
    /// F_vib(T)/atom for the pure endpoints, same grid and same units as
    /// CalphadConfiguration::vibFreeEnergyEvPerAtom.
    std::vector<double> referenceVibAEvPerAtom;
    std::vector<double> referenceVibBEvPerAtom;

    std::vector<CalphadConfiguration> configurations;
    /// Temperatures the excess energy is sampled at, K. A single entry is
    /// legal and yields a temperature-independent assessment.
    std::vector<double> temperaturesK{300.0, 600.0, 900.0, 1200.0};

    int order = 2;                   ///< highest Redlich-Kister order fitted
    bool temperatureDependent = true;
};

struct CalphadAssessment {
    bool ok = false;
    std::string note;
    /// True when every configuration AND both endpoints carried vibrational
    /// data, so the excess entropy in `fit` is a real one.
    bool vibrational = false;
    RedlichKisterFit fit;
    std::vector<RedlichKisterSample> samples;
    /// Static (T = 0, E_0 only) formation-energy hull of the configurations,
    /// annotated by core::computeConvexHull. What is on the hull is what is
    /// stable against decomposition before entropy is considered, and it is
    /// the standard sanity check on an assessment: a solution model that
    /// predicts a single phase across a composition range where the hull has
    /// an ordered ground state is describing the wrong system.
    ConvexHullResult staticHull;
};

/// Turn first-principles energies into Redlich-Kister excess samples.
///
/// For every configuration at every temperature:
///
///   ΔE_0    = E/N − [x_A E_A + x_B E_B]              (eV/atom)
///   ΔF_vib  = F/N − [x_A F_A(T) + x_B F_B(T)]        (eV/atom, if available)
///   G_ex    = (ΔE_0 + ΔF_vib) · e·N_A                (J/mol)
///
/// No ideal-entropy term is subtracted, and that is the line to read twice. A
/// DFT energy is the energy of one atomic arrangement and carries no
/// configurational entropy; the −T·S_config term belongs to the CALPHAD model
/// and G_ex is by definition what sits on top of it. Removing it here would
/// cancel it against the model's own and produce a solution phase with no
/// configurational entropy at all — no terminal solubility, and a miscibility
/// gap that never closes. The full argument is at the assignment site.
CalphadAssessment assessBinaryFromFirstPrinciples(
    const CalphadAssessmentInput& input);

} // namespace calango::core
