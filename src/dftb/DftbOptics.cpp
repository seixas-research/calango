#include "dftb/DftbOptics.hpp"

#include "dft/Constants.hpp"

#include <cmath>

namespace calango::dftb {

std::vector<std::complex<double>> dftbVelocityMatrix(
    const DftbHamiltonianBuilder& hamiltonian,
    const std::array<core::Vec3, 3>& latticeVectorsBohr,
    const std::array<double, 3>& kFrac, const std::vector<double>& shift,
    const std::vector<std::complex<double>>& eigenvectors,
    const std::vector<double>& eigenvaluesHartree, int direction,
    double stepInverseBohr)
{
    const auto n = eigenvaluesHartree.size();
    std::vector<std::complex<double>> result(n * n, {0.0, 0.0});
    if (n == 0)
        return result;

    // dk_frac,i = (a_i . e_direction) / (2*pi) * step — see the header's
    // derivation for why this, not a plain fractional-k step, is what
    // makes the result a true CARTESIAN (energy * length) velocity.
    std::array<double, 3> dkFrac{};
    for (int i = 0; i < 3; ++i) {
        const core::Vec3& a = latticeVectorsBohr[static_cast<std::size_t>(i)];
        const double component = direction == 0 ? a.x : direction == 1 ? a.y : a.z;
        dkFrac[static_cast<std::size_t>(i)] =
            component / (2.0 * dft::kPi) * stepInverseBohr;
    }
    const std::array<double, 3> kPlus{kFrac[0] + dkFrac[0], kFrac[1] + dkFrac[1],
                                      kFrac[2] + dkFrac[2]};
    const std::array<double, 3> kMinus{kFrac[0] - dkFrac[0], kFrac[1] - dkFrac[1],
                                       kFrac[2] - dkFrac[2]};

    std::vector<std::complex<double>> hPlus, sPlus, hMinus, sMinus;
    hamiltonian.blochMatrices(kPlus, hPlus, sPlus, shift);
    hamiltonian.blochMatrices(kMinus, hMinus, sMinus, shift);

    std::vector<std::complex<double>> dH(n * n), dS(n * n);
    for (std::size_t idx = 0; idx < n * n; ++idx) {
        dH[idx] = (hPlus[idx] - hMinus[idx]) / (2.0 * stepInverseBohr);
        dS[idx] = (sPlus[idx] - sMinus[idx]) / (2.0 * stepInverseBohr);
    }

    // dH*C and dS*C (orbital basis rows, eigenstate columns), then project
    // both bra and ket into the eigenbasis: v_nm = <n|dH|m> - E_m<n|dS|m>.
    std::vector<std::complex<double>> dHC(n * n, {0.0, 0.0}),
        dSC(n * n, {0.0, 0.0});
    for (std::size_t mu = 0; mu < n; ++mu) {
        for (std::size_t state = 0; state < n; ++state) {
            std::complex<double> sumH(0.0, 0.0), sumS(0.0, 0.0);
            for (std::size_t nu = 0; nu < n; ++nu) {
                sumH += dH[mu * n + nu] * eigenvectors[nu * n + state];
                sumS += dS[mu * n + nu] * eigenvectors[nu * n + state];
            }
            dHC[mu * n + state] = sumH;
            dSC[mu * n + state] = sumS;
        }
    }

    for (std::size_t nState = 0; nState < n; ++nState) {
        for (std::size_t mState = 0; mState < n; ++mState) {
            std::complex<double> sum(0.0, 0.0);
            for (std::size_t mu = 0; mu < n; ++mu)
                sum += std::conj(eigenvectors[mu * n + nState])
                    * (dHC[mu * n + mState]
                       - eigenvaluesHartree[mState] * dSC[mu * n + mState]);
            result[nState * n + mState] = sum;
        }
    }
    return result;
}

dft::Outcome computeDftbOptics(const DftbScfResult& scf,
                                const DftbHamiltonianBuilder& hamiltonian,
                                const std::array<core::Vec3, 3>& latticeVectorsBohr,
                                double cellVolumeBohr3,
                                const std::vector<double>& shift,
                                const DftbOpticsOptions& options,
                                DftbOpticsResult& out)
{
    if (scf.kpoints.empty())
        return dft::Outcome::invalid(
            "no SCF k-point results to build an optics spectrum from");
    if (cellVolumeBohr3 <= 0.0)
        return dft::Outcome::invalid("cell volume must be positive");
    if (options.direction < 0 || options.direction > 2)
        return dft::Outcome::invalid("direction must be 0 (x), 1 (y) or 2 (z)");

    out = DftbOpticsResult{};
    out.frequenciesEv = options.frequenciesEv;
    const auto nw = options.frequenciesEv.size();
    std::vector<std::complex<double>> eps(nw, {1.0, 0.0}); // vacuum background

    const double etaHartree = options.broadeningEv / dft::kHartreeToEv;
    const double prefactor = 4.0 * dft::kPi / cellVolumeBohr3;

    for (const auto& kr : scf.kpoints) {
        const auto n = kr.eigenvaluesHartree.size();
        if (n == 0)
            continue;
        const auto v = dftbVelocityMatrix(hamiltonian, latticeVectorsBohr,
                                          kr.fractional, shift,
                                          kr.eigenvectors,
                                          kr.eigenvaluesHartree,
                                          options.direction);
        for (std::size_t nState = 0; nState < n; ++nState) {
            for (std::size_t mState = 0; mState < n; ++mState) {
                if (nState == mState)
                    continue;
                // (f_m - f_n), NOT (f_n - f_m): re-derived carefully via
                // Sokhotski-Plemelj (1/(x-i*eta) -> P(1/x) + i*pi*delta(x)
                // as eta -> 0+) after a real run showed eps2 coming out
                // NEGATIVE at positive omega — a passive medium's
                // absorptive response is fundamentally non-negative there,
                // so that was a genuine sign bug, not a convention choice.
                // With n=c (empty), m=v (occupied): f_m - f_n = f_v - f_c
                // > 0, and the pole at w_nm = E_c - E_v = +w (the physical
                // absorption edge) then contributes POSITIVELY to Im[eps],
                // matching Fermi's Golden Rule directly.
                const double fDiff =
                    kr.occupations[mState] - kr.occupations[nState];
                if (fDiff == 0.0)
                    continue;
                const double wnm = kr.eigenvaluesHartree[nState]
                    - kr.eigenvaluesHartree[mState];
                if (std::fabs(wnm) < 1.0e-8)
                    continue; // degenerate pair — no coherent contribution
                const double vSq =
                    std::norm(v[nState * n + mState]);
                if (vSq == 0.0)
                    continue;
                const double weightNumerator = kr.weight * fDiff * vSq / (wnm * wnm);
                for (std::size_t iw = 0; iw < nw; ++iw) {
                    const double wHartree =
                        options.frequenciesEv[iw] / dft::kHartreeToEv;
                    const std::complex<double> denom(
                        wnm - wHartree, -etaHartree);
                    eps[iw] += prefactor * weightNumerator / denom;
                }
            }
        }
    }

    out.eps1.resize(nw);
    out.eps2.resize(nw);
    out.n.resize(nw);
    out.k.resize(nw);
    out.absorptionInverseCm.resize(nw);
    out.reflectivity.resize(nw);
    out.loss.resize(nw);
    // hbar*c = 197.3269804 eV*nm = 197.3269804e-7 eV*cm — same constant
    // OpticsScriptGenerator.cpp's own derived_spectra() uses, so the
    // absorption coefficient comes out in the same cm^-1 convention.
    constexpr double kHbarCeVCm = 197.3269804e-7;
    for (std::size_t iw = 0; iw < nw; ++iw) {
        out.eps1[iw] = eps[iw].real();
        out.eps2[iw] = eps[iw].imag();
        const std::complex<double> refractive = std::sqrt(eps[iw]);
        out.n[iw] = refractive.real();
        out.k[iw] = refractive.imag();
        out.absorptionInverseCm[iw] =
            2.0 * (options.frequenciesEv[iw] / kHbarCeVCm) * out.k[iw];
        const double nMinus1Sq = (out.n[iw] - 1.0) * (out.n[iw] - 1.0);
        const double nPlus1Sq = (out.n[iw] + 1.0) * (out.n[iw] + 1.0);
        const double kSq = out.k[iw] * out.k[iw];
        out.reflectivity[iw] = (nMinus1Sq + kSq) / (nPlus1Sq + kSq);
        const double absEps = std::abs(eps[iw]);
        const std::complex<double> denom =
            absEps > 1.0e-12 ? eps[iw] : std::complex<double>(1.0e-12, 0.0);
        out.loss[iw] = (-1.0 / denom).imag();
    }

    if (options.vacuumThicknessAngstrom > 0.0) {
        // EXACTLY OpticsScriptGenerator.cpp's twod_observables() — see the
        // header's own doc on why this is copied rather than re-derived.
        const double lz = options.vacuumThicknessAngstrom; // Angstrom
        constexpr double kHbarCeVAngstrom = 1973.269804;
        constexpr double kFineStructure = 1.0 / 137.035999084;
        const double toE2OverH = 2.0 * dft::kPi / kFineStructure;
        out.alpha2DReAngstrom.resize(nw);
        out.alpha2DImAngstrom.resize(nw);
        out.absorbance.resize(nw);
        out.sigma2DRe.resize(nw);
        out.sigma2DIm.resize(nw);
        for (std::size_t iw = 0; iw < nw; ++iw) {
            const double k = options.frequenciesEv[iw] / kHbarCeVAngstrom; // 1/A
            const double alphaRe =
                lz / (4.0 * dft::kPi) * (out.eps1[iw] - 1.0);
            const double alphaIm = lz / (4.0 * dft::kPi) * out.eps2[iw];
            out.alpha2DReAngstrom[iw] = alphaRe;
            out.alpha2DImAngstrom[iw] = alphaIm;
            out.absorbance[iw] = k * lz * out.eps2[iw];
            out.sigma2DRe[iw] = k * alphaIm * toE2OverH;
            out.sigma2DIm[iw] = -k * alphaRe * toE2OverH;
        }
    }

    return dft::Outcome::success();
}

} // namespace calango::dftb
