#pragma once

#include <vector>

#include "core/Structure.hpp"
#include "dft/DftTypes.hpp"

/// The SCC-DFTB gamma functional: the second-order (charge-fluctuation)
/// Coulomb interaction between atom-centred charge densities.
///
/// SOURCE / VERIFICATION. The closed form below is Elstner et al., Phys.
/// Rev. B 58, 7260 (1998)'s analytic overlap of two exponentially-decaying
/// (Slater-type 1s) charge densities, in the sign convention used by every
/// modern SCC-DFTB implementation (Elstner's own original thesis has a sign
/// typo in this term, noted independently and worked around identically by
/// DFTB+). Cross-checked against DFTB+'s own `shortgammafuncs.F90`
/// (dftbplus/dftbplus, module `dftbp_dftb_shortgammafuncs`, functions
/// `expGamma`/`gammaSubExprn_`) rather than transcribed from a paper's
/// rendered equations, AND independently re-derived here: expanding
/// `gammaShortRange` in a Taylor series as R -> 0 (tau_A == tau_B == tau)
/// gives gammaShortRange(R) = 1/R - 5*tau/16 + O(R^2), so
/// 1/R - gammaShortRange(R) -> 5*tau/16 = U exactly (tau = 3.2*U = 16*U/5) —
/// this is WHY gammaAtOrigin() below returns U directly rather than
/// evaluating a 1/R - (divergent) difference at R = 0, and DftbTest.cpp
/// checks the numerical limit matches this analytic one.
///
/// UNITS: Hartree/Bohr throughout, matching SlaterKosterFile.hpp and
/// calango::dft.
namespace calango::dftb {

/// tau = 3.2 * U ("16/5 * U", the standard SCC-DFTB decay-constant
/// convention — see the review literature DFTB+'s own comment cites).
constexpr double gammaDecayConstant(double hubbardUHartree)
{
    return 3.2 * hubbardUHartree;
}

/// The short-range correction gammaShortRange(R) such that
/// gamma_AB(R) = 1/R - gammaShortRange(R) for R > 0 (see gammaFunctional()
/// below, which applies this). Two atoms, each described by its own Hubbard
/// U (equivalently tau = 3.2*U); R in bohr, R > 0 required.
double gammaShortRange(double hubbardUAHartree, double hubbardUBHartree,
                        double rBohr);

/// The full SCC-DFTB gamma functional between atoms A and B (or the on-site
/// term when they are the SAME atom): gamma_AB(R) = 1/R - gammaShortRange(R)
/// for R > 0, and gamma_AA(0) = U_A (the analytic R -> 0 limit — see the
/// module doc's derivation). For a MOLECULE (no periodicity); periodic
/// systems use DftbEwaldSum below instead, since the bare 1/R part of this
/// formula is only conditionally convergent when lattice-summed.
double gammaFunctional(double hubbardUAHartree, double hubbardUBHartree,
                        double rBohr);

/// Periodic electrostatics for the SCC second-order term: the potential
/// shift on every atom, Sigma_C gamma_AC(R) * dQ_C, summed over the crystal
/// (all periodic images of every atom). Handles the non-periodic (molecule)
/// case too — it degenerates to the plain double sum over atoms with no
/// lattice images, i.e. gammaFunctional() applied pairwise.
///
/// SPLIT: the bare 1/R part of gamma is lattice-summed by a standard 3D
/// EWALD summation (needed because Sigma_images 1/|r+T| converges only
/// CONDITIONALLY in real space — see DftbTest.cpp's NaCl Madelung-constant
/// anchor for how this piece alone is verified); gammaShortRange(R)
/// decays exponentially and is instead summed directly in real space over
/// the same neighbor images the Hamiltonian builder already visits, out to
/// a distance where its own contribution is negligible.
class DftbEwaldSum {
public:
    /// Prepare for a fixed geometry. `hubbardUHartree` is one value per
    /// atom (index-aligned with `structure.atoms()`).
    dft::Outcome build(const core::Structure& structure,
                        const std::vector<double>& hubbardUHartree);

    /// Sigma_C gamma_AC * dQ_C for every atom A, given the current Mulliken
    /// charge fluctuations `deltaQ` (index-aligned with the atoms).
    std::vector<double> potentialShift(const std::vector<double>& deltaQ) const;

    /// The Ewald splitting parameter actually used (diagnostic only — a
    /// fixed, conservative value chosen in build(), not user-tunable).
    double alpha() const { return alpha_; }

private:
    struct RealSpaceTerm {
        int atomI = 0;
        int atomJ = 0;
        double distanceBohr = 0.0;
        /// Precomputed erfc(alpha*R)/R (Ewald real-space Coulomb) MINUS
        /// gammaShortRange(R) — the two short-ranged pieces are summed
        /// together since both decay exponentially/fast and both are real-
        /// space sums over the same neighbor list.
        double weight = 0.0;
    };
    struct ReciprocalTerm {
        core::Vec3 gVectorInverseBohr;
        double weight = 0.0; // (4*pi/V) * exp(-G^2/(4 alpha^2)) / G^2
    };

    std::vector<double> hubbardU_;
    std::vector<core::Vec3> positionsBohr_;
    std::vector<RealSpaceTerm> realSpace_;
    std::vector<ReciprocalTerm> reciprocal_;
    double alpha_ = 0.0;
    double selfTermPerAtom_ = 0.0; ///< -2*alpha/sqrt(pi), the Ewald self-energy
    bool periodic_ = false;
    core::UnitCell cellBohr_;
};

} // namespace calango::dftb
