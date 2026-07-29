#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// One chemical species whose count differs between the pristine host and the
/// defect supercell.
///
/// `count` is how many atoms of this species the DEFECT has that the pristine
/// cell does not: −1 for a vacancy, +1 for an interstitial, and a
/// substitution is two entries (−1 of the host species, +1 of the dopant).
/// The formation energy subtracts Σ count·μ, so the sign convention is the
/// one thing here that silently inverts a whole diagram if it is wrong.
struct DefectSpecies {
    std::string symbol;
    int count = 0;
    /// Chemical potential μ of the reservoir this species is exchanged with,
    /// in eV. It is NOT a property of the calculation: it encodes the growth
    /// condition (which phase the sample is in equilibrium with), so the same
    /// defect has different formation energies under, say, metal-rich and
    /// chalcogen-rich conditions. Nothing can derive it for the user.
    double chemicalPotentialEv = 0.0;
};

/// Parameters for a charged-defect formation-energy study.
///
/// The quantity produced is
///
///   E_f[X^q](E_F) = E_tot[X^q] − E_tot[host] − Σ_i n_i μ_i
///                   + q (E_VBM + E_F) + E_corr(q)
///
/// evaluated over a Fermi-level window spanning the host band gap, from which
/// the thermodynamic transition levels ε(q/q′) — the Fermi energies where the
/// lowest-energy charge state changes — follow as crossings of the lower
/// envelope.
///
/// E_corr is the Freysoldt-Neugebauer-Van de Walle correction. It exists
/// because a charged defect in a periodic supercell is not an isolated defect:
/// it is an infinite array of them plus a neutralizing background, and the
/// spurious interaction falls off only as 1/L. Without the correction the
/// formation energy of a q = ±2 defect in a workable supercell is wrong by
/// several tenths of an eV — comparable to the transition levels being
/// measured. FNV removes it by subtracting the periodic model-charge energy,
/// adding the isolated one back, and aligning the defect potential to the host
/// far from the defect.
struct DefectConfig {
    /// Provenance only — the engine the baseline ran under. Every parameter of
    /// the charged SCFs is inherited from the neutral defect's `.gpw`, so that
    /// the charge state is the ONLY difference between them.
    CalculatorConfig calculator;

    /// ABSOLUTE path to the pristine host supercell's `.gpw` (same cell and
    /// same settings as the defect run). Supplies E_tot[host], the VBM the
    /// Fermi level is referenced to, and the reference potential the FNV
    /// alignment is measured against.
    std::string pristinePath;
    /// ABSOLUTE path to the NEUTRAL defect supercell's `.gpw`. Supplies the
    /// relaxed geometry every charge state is computed at, and q = 0 itself.
    std::string neutralDefectPath;

    /// Charge states to evaluate, in units of |e|. q = 0 is always included
    /// (it is the baseline, and the diagram needs it), whatever this holds.
    std::vector<int> charges{-2, -1, 0, 1, 2};

    /// Species exchanged with the reservoirs — see DefectSpecies.
    std::vector<DefectSpecies> species;

    /// Macroscopic static dielectric constant ε of the HOST.
    ///
    /// The FNV correction scales as 1/ε, so this is the parameter the
    /// correction is most sensitive to. It is the ion-clamped-plus-ionic
    /// (static) constant for a defect that relaxes, and there is no way to
    /// guess it: run Optics, or take it from experiment.
    double dielectricConstant = 1.0;

    /// Index of the defect site in the PRISTINE cell — the centre the model
    /// charge is placed on and the averaging region is defined away from.
    int defectIndex = 0;

    /// Width of the Gaussian model charge, Å (FNV's r_c).
    double modelChargeRadius = 1.0;
    /// Plane-wave cutoff for the model potential, eV.
    double modelCutoffEv = 500.0;
    /// Radius of the bulk-atom averaging region, Å.
    double averagingRadius = 2.5;

    /// Skip the FNV correction entirely and report raw formation energies.
    ///
    /// Not a shortcut: it is what makes a supercell-convergence study
    /// possible, since the whole point of such a study is to see how big the
    /// uncorrected error is. The output records which was used.
    bool applyFnvCorrection = true;

    /// Samples on the Fermi-level axis of the diagram.
    int fermiPoints = 401;
};

/// Turns a DefectConfig into a standalone ASE/GPAW script that runs one
/// fixed-geometry SCF per charge state, applies the FNV correction to each,
/// and writes `charged_defects.json` (formation-energy lines, the lower
/// envelope, and the transition levels) plus the
/// `CALANGO_RESULT charged_defects=charged_defects.json` marker.
std::string generateDefectScript(const DefectConfig& cfg);

} // namespace calango::core
