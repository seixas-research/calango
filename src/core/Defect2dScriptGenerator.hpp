#pragma once

#include "core/CalculatorConfig.hpp"
#include "core/DefectScriptGenerator.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Parameters for a charged-defect study in a TWO-DIMENSIONAL material.
///
/// The quantity is the same as in the bulk case,
///
///   E_f[X^q](E_F) = E_tot[X^q] − E_tot[host] − Σ_i n_i μ_i
///                   + q (E_VBM + E_F) + E_corr(q),
///
/// but E_corr is not, and substituting the bulk one is not a small error — it
/// is the wrong functional form.
///
/// **Why the bulk correction does not apply.** Freysoldt-Neugebauer-Van de
/// Walle assumes the defect sits in a homogeneous medium of dielectric
/// constant ε, and the image-charge energy it removes goes as q²α/(2εL): it
/// converges, and extrapolating to L → ∞ is meaningful. A charged sheet in a
/// slab supercell has neither property. The medium is inhomogeneous — ε inside
/// the layer, 1 in the vacuum — so there is no scalar ε to divide by; and the
/// electrostatic energy of a charged 2D layer with a compensating jellium
/// background DIVERGES logarithmically with the vacuum thickness, so adding
/// vacuum does not converge the answer, it changes it without limit. Feeding
/// such a run to a bulk FNV correction returns a number of plausible magnitude
/// that is simply not the formation energy.
///
/// **What is done instead.** The scheme of Komsa and Pasquarello
/// (Phys. Rev. Lett. 110, 095505 (2013); Phys. Rev. X 4, 031044 (2014)): a
/// Gaussian model charge is placed in a slab-shaped dielectric profile
/// ε_∥(z), ε_⊥(z), and the Poisson equation ∇·(ε∇V) = −4πρ is solved twice —
/// once with the supercell's own periodicity, once in the isolated limit — with
/// the correction being the difference. Because ε depends only on z, the
/// equation separates: for each in-plane G_∥ it is a dense linear system in the
/// G_z components, which is what the generated script assembles and solves.
///
/// The profile is the physics input. `layerThickness` and the two dielectric
/// constants describe the SHEET, not the supercell, and they are what make the
/// correction independent of how much vacuum was used — which is the property
/// the whole scheme exists to restore.
struct Defect2dConfig {
    /// Provenance only — every charged SCF inherits its parameters from the
    /// neutral defect's `.gpw`, so the charge state is the only difference.
    CalculatorConfig calculator;

    /// ABSOLUTE path to the pristine 2D host supercell's `.gpw`.
    std::string pristinePath;
    /// ABSOLUTE path to the NEUTRAL defect supercell's `.gpw`.
    std::string neutralDefectPath;

    /// Charge states in units of |e|. q = 0 is always evaluated.
    std::vector<int> charges{-2, -1, 0, 1, 2};
    /// Species exchanged with the reservoirs; see DefectSpecies.
    std::vector<DefectSpecies> species;

    // -- The dielectric profile --------------------------------------------

    /// In-plane dielectric constant ε_∥ of the SHEET.
    ///
    /// Of the sheet, not of the supercell: a slab calculation reports an ε
    /// diluted by whatever vacuum was used, and putting that here would make
    /// the correction depend on the padding it is supposed to remove. Take it
    /// from a 2D Optics run's α₂D, or from the literature.
    double epsilonInPlane = 1.0;
    /// Out-of-plane dielectric constant ε_⊥ of the sheet. Usually much smaller
    /// than ε_∥ for a monolayer — the anisotropy is not a detail here, it is
    /// what distinguishes a sheet from a thin piece of bulk.
    double epsilonOutOfPlane = 1.0;
    /// Effective thickness of the dielectric slab, Å. For a monolayer this is
    /// the interlayer spacing of the parent bulk (≈ 6.5 Å for MoS₂, ≈ 3.35 Å
    /// for graphene), not the covalent thickness.
    double layerThickness = 6.0;
    /// Width over which the profile turns from the sheet value to 1, Å. A hard
    /// step has Fourier components at every G_z and makes the linear system
    /// needlessly stiff; a physical interface is not a step anyway.
    double interfaceWidth = 1.0;

    /// Cartesian axis normal to the sheet: 0 = x, 1 = y, 2 = z.
    int normalAxis = 2;
    /// Index of the defect site, whose z fixes the centre of the model charge.
    int defectIndex = 0;
    /// Gaussian width of the model charge, Å.
    double modelChargeRadius = 1.0;

    /// Number of z (out-of-plane) Fourier components in the Poisson solve.
    ///
    /// The cost is one dense (n × n) solve per in-plane G, so this is the knob
    /// that sets the run time. 64 is converged to a few meV for a normal slab;
    /// raise it if the profile is sharp relative to the cell.
    int zComponents = 64;
    /// In-plane reciprocal-space cutoff for the model, in units of 2π/a.
    int inPlaneCutoff = 12;

    /// Skip the correction and report raw formation energies.
    ///
    /// Kept for the same reason as in the bulk module: an uncorrected series is
    /// what a convergence study is made of, and the output records which was
    /// used.
    bool applyCorrection = true;

    /// Samples on the Fermi-level axis of the diagram.
    int fermiPoints = 401;
};

/// Turns a Defect2dConfig into a standalone ASE/GPAW script: one fixed-geometry
/// SCF per charge state, the 2D image-charge correction applied to each, and
/// `charged_defects_2d.json` written with the formation-energy lines, the lower
/// envelope and the transition levels.
std::string generateDefect2dScript(const Defect2dConfig& cfg);

} // namespace calango::core
