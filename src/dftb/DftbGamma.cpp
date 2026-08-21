#include "dftb/DftbGamma.hpp"

#include "core/PeriodicImages.hpp"
#include "dft/Constants.hpp"

#include <cmath>

namespace calango::dftb {

namespace {

/// DFTB+'s gammaSubExprn_(rab, tau1, tau2) — see the module doc for the
/// verification this was checked against (dftbplus/dftbplus
/// shortgammafuncs.F90).
double gammaSubExpr(double rab, double tau1, double tau2)
{
    const double tau1sq = tau1 * tau1;
    const double tau2sq = tau2 * tau2;
    const double diff = tau1sq - tau2sq;
    return std::exp(-tau1 * rab)
        * (0.5 * tau2sq * tau2sq * tau1 / (diff * diff)
           - (tau2sq * tau2sq * tau2sq - 3.0 * tau2sq * tau2sq * tau1sq)
               / (rab * diff * diff * diff));
}

} // namespace

double gammaShortRange(double hubbardUAHartree, double hubbardUBHartree,
                        double rBohr)
{
    const double tauA = gammaDecayConstant(hubbardUAHartree);
    const double tauB = gammaDecayConstant(hubbardUBHartree);
    if (std::fabs(hubbardUAHartree - hubbardUBHartree) < 1.0e-8) {
        const double tauMean = 0.5 * (tauA + tauB);
        return std::exp(-tauMean * rBohr)
            * (1.0 / rBohr + 0.6875 * tauMean
               + 0.1875 * rBohr * tauMean * tauMean
               + (1.0 / 48.0) * rBohr * rBohr * tauMean * tauMean * tauMean);
    }
    return gammaSubExpr(rBohr, tauA, tauB) + gammaSubExpr(rBohr, tauB, tauA);
}

double gammaFunctional(double hubbardUAHartree, double hubbardUBHartree,
                        double rBohr)
{
    if (rBohr < 1.0e-9) {
        // The analytic R -> 0 limit (see the module doc's derivation);
        // only meaningful for the SAME atom (A == B), which is the only
        // physical R == 0 case.
        return hubbardUAHartree;
    }
    return 1.0 / rBohr - gammaShortRange(hubbardUAHartree, hubbardUBHartree, rBohr);
}

namespace {

/// Reciprocal lattice vectors (2*pi convention: a_i . b_j = 2*pi*delta_ij),
/// from real-space vectors already in bohr.
std::array<core::Vec3, 3> reciprocalVectors(const std::array<core::Vec3, 3>& a)
{
    const double volume = a[0].dot(a[1].cross(a[2]));
    std::array<core::Vec3, 3> b{};
    if (std::fabs(volume) < 1.0e-12)
        return b;
    b[0] = a[1].cross(a[2]) * (2.0 * dft::kPi / volume);
    b[1] = a[2].cross(a[0]) * (2.0 * dft::kPi / volume);
    b[2] = a[0].cross(a[1]) * (2.0 * dft::kPi / volume);
    return b;
}

} // namespace

dft::Outcome DftbEwaldSum::build(const core::Structure& structure,
                                  const std::vector<double>& hubbardUHartree)
{
    const auto& atoms = structure.atoms();
    if (atoms.size() != hubbardUHartree.size())
        return dft::Outcome::invalid(
            "one Hubbard U value is required per atom");
    hubbardU_ = hubbardUHartree;
    positionsBohr_.clear();
    positionsBohr_.reserve(atoms.size());
    for (const auto& atom : atoms)
        positionsBohr_.push_back(atom.position * dft::kBohrPerAngstrom);

    const std::array<bool, 3> pbc = structure.cell().pbc();
    periodic_ = pbc[0] || pbc[1] || pbc[2];
    realSpace_.clear();
    reciprocal_.clear();

    if (!periodic_) {
        // Molecule: gammaFunctional() applied pairwise, no lattice sum, no
        // splitting parameter needed at all — encode this as alpha_ == 0
        // and let potentialShift() branch on periodic_.
        alpha_ = 0.0;
        selfTermPerAtom_ = 0.0;
        return dft::Outcome::success();
    }

    std::array<core::Vec3, 3> latticeBohr{};
    for (int a = 0; a < 3; ++a)
        latticeBohr[static_cast<std::size_t>(a)] =
            structure.cell().vectors()[static_cast<std::size_t>(a)]
            * dft::kBohrPerAngstrom;
    cellBohr_ = core::UnitCell(latticeBohr[0], latticeBohr[1], latticeBohr[2],
                                pbc);

    // A fixed, conservative splitting parameter and matching real/reciprocal
    // cutoffs (erfc(alpha*Rmax) and exp(-Gmax^2/4alpha^2) both below 1e-10).
    // Not tuned for performance — DFTB target systems are modest, and a
    // fixed, always-safe choice is simpler to trust than a heuristic that
    // could silently under-converge for an unusual cell. A very long vacuum
    // axis (the 2D-slab case) needs correspondingly many reciprocal terms
    // along that axis; loop bounds below are capped to keep this bounded.
    alpha_ = 0.35; // bohr^-1
    const double realCutoffBohr = 4.3 / alpha_;    // erfc(4.3) ~ 2e-9
    const double reciprocalCutoffInvBohr = 4.3 * 2.0 * alpha_; // matching precision
    selfTermPerAtom_ = -2.0 * alpha_ / std::sqrt(dft::kPi);

    // Real-space part: erfc(alpha*R)/R (Ewald screened Coulomb) minus
    // gammaShortRange(R), for every pair (including same-atom periodic
    // images) within realCutoffBohr, EXCLUDING the atomI==atomJ,
    // image==(0,0,0) term (handled by gammaAA(0) = U_A directly, in
    // potentialShift()).
    const std::array<int, 3> range = core::imageRange(cellBohr_, realCutoffBohr);
    const int n1max = pbc[0] ? range[0] : 0;
    const int n2max = pbc[1] ? range[1] : 0;
    const int n3max = pbc[2] ? range[2] : 0;
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        for (std::size_t j = 0; j < atoms.size(); ++j) {
            for (int n1 = -n1max; n1 <= n1max; ++n1) {
                for (int n2 = -n2max; n2 <= n2max; ++n2) {
                    for (int n3 = -n3max; n3 <= n3max; ++n3) {
                        if (i == j && n1 == 0 && n2 == 0 && n3 == 0)
                            continue;
                        const core::Vec3 translation =
                            latticeBohr[0] * static_cast<double>(n1)
                            + latticeBohr[1] * static_cast<double>(n2)
                            + latticeBohr[2] * static_cast<double>(n3);
                        const core::Vec3 r0 = positionsBohr_[j] + translation
                            - positionsBohr_[i];
                        const double d = r0.norm();
                        if (d < 1.0e-9 || d > realCutoffBohr)
                            continue;
                        const double screened = std::erfc(alpha_ * d) / d;
                        const double shortRange = gammaShortRange(
                            hubbardU_[i], hubbardU_[j], d);
                        realSpace_.push_back(
                            {static_cast<int>(i), static_cast<int>(j), d,
                             screened - shortRange});
                    }
                }
            }
        }
    }

    // Reciprocal-space part: standard Ewald sum, G != 0 only (assumes
    // overall charge neutrality, which SCC-DFTB's occupation choice
    // guarantees — see the header doc).
    const auto b = reciprocalVectors(latticeBohr);
    const double volume = latticeBohr[0].dot(latticeBohr[1].cross(latticeBohr[2]));
    const int m1max = pbc[0] && b[0].norm() > 1.0e-9
        ? std::min(40, static_cast<int>(reciprocalCutoffInvBohr / b[0].norm()) + 1)
        : 0;
    const int m2max = pbc[1] && b[1].norm() > 1.0e-9
        ? std::min(40, static_cast<int>(reciprocalCutoffInvBohr / b[1].norm()) + 1)
        : 0;
    const int m3max = pbc[2] && b[2].norm() > 1.0e-9
        ? std::min(40, static_cast<int>(reciprocalCutoffInvBohr / b[2].norm()) + 1)
        : 0;
    for (int m1 = -m1max; m1 <= m1max; ++m1) {
        for (int m2 = -m2max; m2 <= m2max; ++m2) {
            for (int m3 = -m3max; m3 <= m3max; ++m3) {
                if (m1 == 0 && m2 == 0 && m3 == 0)
                    continue;
                const core::Vec3 g = b[0] * static_cast<double>(m1)
                    + b[1] * static_cast<double>(m2)
                    + b[2] * static_cast<double>(m3);
                const double g2 = g.dot(g);
                if (g2 > reciprocalCutoffInvBohr * reciprocalCutoffInvBohr)
                    continue;
                const double weight =
                    (4.0 * dft::kPi / volume) * std::exp(-g2 / (4.0 * alpha_ * alpha_)) / g2;
                reciprocal_.push_back({g, weight});
            }
        }
    }

    return dft::Outcome::success();
}

std::vector<double> DftbEwaldSum::potentialShift(
    const std::vector<double>& deltaQ) const
{
    const auto n = positionsBohr_.size();
    std::vector<double> shift(n, 0.0);

    if (!periodic_) {
        for (std::size_t i = 0; i < n; ++i) {
            double s = hubbardU_[i] * deltaQ[i]; // on-site, gamma_AA(0) = U_A
            for (std::size_t j = 0; j < n; ++j) {
                if (j == i)
                    continue;
                const double d = (positionsBohr_[j] - positionsBohr_[i]).norm();
                s += gammaFunctional(hubbardU_[i], hubbardU_[j], d) * deltaQ[j];
            }
            shift[i] = s;
        }
        return shift;
    }

    // On-site term first (excluded from both sums below).
    for (std::size_t i = 0; i < n; ++i)
        shift[i] = hubbardU_[i] * deltaQ[i];

    // Real-space short-ranged part.
    for (const auto& term : realSpace_)
        shift[static_cast<std::size_t>(term.atomI)] +=
            term.weight * deltaQ[static_cast<std::size_t>(term.atomJ)];

    // Reciprocal-space part + self-term.
    for (std::size_t i = 0; i < n; ++i) {
        double recipSum = 0.0;
        for (std::size_t j = 0; j < n; ++j) {
            if (deltaQ[j] == 0.0)
                continue;
            const core::Vec3 rij = positionsBohr_[i] - positionsBohr_[j];
            double perAtom = 0.0;
            for (const auto& g : reciprocal_)
                perAtom += g.weight * std::cos(g.gVectorInverseBohr.dot(rij));
            recipSum += deltaQ[j] * perAtom;
        }
        shift[i] += recipSum + selfTermPerAtom_ * deltaQ[i];
    }

    return shift;
}

} // namespace calango::dftb
