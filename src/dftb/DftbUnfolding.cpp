#include "dftb/DftbUnfolding.hpp"

#include "dft/Constants.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace calango::dftb {

dft::Outcome DftbUnfoldingMap::build(const core::Structure& supercell,
                                      const core::Structure& primitive,
                                      DftbUnfoldingMap& out, double* residual)
{
    out = DftbUnfoldingMap{};
    out.matrix = core::deduceSupercellMatrix(primitive.cell(), supercell.cell(),
                                              residual);
    if (!out.matrix.valid())
        return dft::Outcome::invalid(
            "the supercell and primitive cells are not commensurate — no "
            "integer supercell matrix relates them");
    out.imageCount = std::abs(out.matrix.determinant());
    if (out.imageCount <= 0)
        return dft::Outcome::invalid("supercell matrix has zero determinant");

    const auto& primVectors = primitive.cell().vectors();
    std::array<core::Vec3, 3> primBohr{};
    for (int i = 0; i < 3; ++i)
        primBohr[static_cast<std::size_t>(i)] =
            primVectors[static_cast<std::size_t>(i)] * dft::kBohrPerAngstrom;

    const auto& m = out.matrix.m;

    out.primitiveSublatticeIndex.assign(supercell.atoms().size(), -1);
    out.imageIndex.assign(supercell.atoms().size(), {0, 0, 0});

    // A tolerance in bohr: half the shortest primitive lattice vector's
    // length is a generous cutoff that still distinguishes "this atom is
    // atom i of image n" from "this atom is genuinely somewhere else" for
    // any reasonable relaxation or defect displacement.
    double shortestPrimitiveBohr = std::numeric_limits<double>::max();
    for (const auto& v : primBohr)
        shortestPrimitiveBohr = std::min(shortestPrimitiveBohr, v.norm());
    const double tolerance = 0.5 * shortestPrimitiveBohr;

    for (std::size_t a = 0; a < supercell.atoms().size(); ++a) {
        const core::Vec3 posBohr =
            supercell.atoms()[a].position * dft::kBohrPerAngstrom;
        // f_primitive_total = f_super . M (supercell = M . primitive, so a
        // position's primitive-fractional coordinate is its supercell-
        // fractional coordinate contracted with M — see the header doc).
        const core::Vec3 fracSuper = supercell.cell().cartesianToFractional(
            supercell.atoms()[a].position);
        const double fSuper[3] = {fracSuper.x, fracSuper.y, fracSuper.z};
        double fPrimTotal[3] = {0.0, 0.0, 0.0};
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i)
                fPrimTotal[j] += fSuper[i] * static_cast<double>(m[i][j]);

        const std::array<int, 3> n{static_cast<int>(std::floor(fPrimTotal[0])),
                                   static_cast<int>(std::floor(fPrimTotal[1])),
                                   static_cast<int>(std::floor(fPrimTotal[2]))};
        const core::Vec3 translationBohr =
            primBohr[0] * static_cast<double>(n[0])
            + primBohr[1] * static_cast<double>(n[1])
            + primBohr[2] * static_cast<double>(n[2]);

        // Nearest primitive sublattice atom (Cartesian distance to
        // primitive_atom_position + T_n), NOT an exact-match requirement —
        // this is what makes a relaxed or defective supercell still
        // unfoldable (see the header's doc).
        int bestIndex = -1;
        double bestDistance = tolerance;
        for (std::size_t i = 0; i < primitive.atoms().size(); ++i) {
            const core::Vec3 idealBohr =
                primitive.atoms()[i].position * dft::kBohrPerAngstrom
                + translationBohr;
            const double d = (posBohr - idealBohr).norm();
            if (d < bestDistance) {
                bestDistance = d;
                bestIndex = static_cast<int>(i);
            }
        }
        out.primitiveSublatticeIndex[a] = bestIndex;
        out.imageIndex[a] = n;
    }

    return dft::Outcome::success();
}

std::vector<double> dftbUnfoldingWeights(
    const std::vector<std::complex<double>>& eigenvectors,
    const std::vector<std::complex<double>>& overlapMatrix,
    const DftbBasis& basis, const DftbUnfoldingMap& map,
    const std::array<double, 3>& kPrimitiveFrac)
{
    const auto n = static_cast<std::size_t>(basis.totalOrbitals);
    std::vector<double> weights(n, 0.0); // one weight per eigenstate (== n states)
    if (map.imageCount <= 0 || map.primitiveSublatticeIndex.empty())
        return weights;

    const int maxSublattice = *std::max_element(
        map.primitiveSublatticeIndex.begin(), map.primitiveSublatticeIndex.end());
    if (maxSublattice < 0)
        return weights; // nothing assigned at all

    // Group supercell atoms by their primitive-sublattice assignment.
    std::vector<std::vector<std::size_t>> atomsForSublattice(
        static_cast<std::size_t>(maxSublattice + 1));
    for (std::size_t a = 0; a < map.primitiveSublatticeIndex.size(); ++a) {
        const int idx = map.primitiveSublatticeIndex[a];
        if (idx >= 0)
            atomsForSublattice[static_cast<std::size_t>(idx)].push_back(a);
    }

    // How many orbitals each primitive sublattice site carries — the max
    // over its assigned images, so a pristine region (every image agrees)
    // is unaffected and a substituted image (fewer orbitals) just
    // contributes less to the higher orbital indices, not a crash.
    std::vector<int> orbitalCountForSublattice(atomsForSublattice.size(), 1);
    for (std::size_t sub = 0; sub < atomsForSublattice.size(); ++sub)
        for (std::size_t a : atomsForSublattice[sub])
            orbitalCountForSublattice[sub] =
                std::max(orbitalCountForSublattice[sub],
                        basis.atoms[a].orbitalCount());

    for (std::size_t state = 0; state < n; ++state) {
        // Sc = S * (this eigenvector's column) — the same S-weighted
        // contraction Mulliken populations use, reused here per the
        // header's own derivation.
        std::vector<std::complex<double>> sc(n, {0.0, 0.0});
        for (std::size_t mu = 0; mu < n; ++mu) {
            std::complex<double> sum(0.0, 0.0);
            for (std::size_t nu = 0; nu < n; ++nu)
                sum += overlapMatrix[mu * n + nu] * eigenvectors[nu * n + state];
            sc[mu] = sum;
        }

        double weightSum = 0.0;
        for (std::size_t sub = 0; sub < atomsForSublattice.size(); ++sub) {
            for (int o = 0; o < orbitalCountForSublattice[sub]; ++o) {
                std::complex<double> v(0.0, 0.0);
                for (std::size_t a : atomsForSublattice[sub]) {
                    const AtomOrbitals& ao = basis.atoms[a];
                    if (o >= ao.orbitalCount())
                        continue; // this image's atom lacks this orbital
                    const std::array<int, 3>& idx = map.imageIndex[a];
                    const double phase = 2.0 * dft::kPi
                        * (kPrimitiveFrac[0] * idx[0]
                           + kPrimitiveFrac[1] * idx[1]
                           + kPrimitiveFrac[2] * idx[2]);
                    const std::complex<double> bloch(std::cos(-phase),
                                                     std::sin(-phase));
                    const auto mu =
                        static_cast<std::size_t>(ao.firstOrbital + o);
                    v += bloch * sc[mu];
                }
                weightSum += std::norm(v); // |v|^2
            }
        }
        weights[state] = weightSum / static_cast<double>(map.imageCount);
    }

    return weights;
}

} // namespace calango::dftb
