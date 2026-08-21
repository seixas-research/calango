#include "dftb/SlaterKosterTransform.hpp"

namespace calango::dftb {

std::array<double, 16> skBlock(double l, double m, double n,
                                const SpIntegrals& integrals)
{
    std::array<double, 16> block{};
    const auto at = [&block](int row, int col) -> double& {
        return block[static_cast<std::size_t>(row * 4 + col)];
    };

    // s-s
    at(0, 0) = integrals.ssSigma;

    // s(atom1)-p(atom2): +cosine * spSigmaForward (X-Y.skf).
    at(0, 1) = l * integrals.spSigmaForward;
    at(0, 2) = m * integrals.spSigmaForward;
    at(0, 3) = n * integrals.spSigmaForward;

    // p(atom1)-s(atom2): -cosine * spSigmaReverse (Y-X.skf — see the
    // header's derivation for why this is a SEPARATE value, not a sign flip
    // of spSigmaForward, for a heteronuclear pair).
    at(1, 0) = -l * integrals.spSigmaReverse;
    at(2, 0) = -m * integrals.spSigmaReverse;
    at(3, 0) = -n * integrals.spSigmaReverse;

    // p-p.
    const double diff = integrals.ppSigma - integrals.ppPi;
    at(1, 1) = l * l * integrals.ppSigma + (1.0 - l * l) * integrals.ppPi;
    at(2, 2) = m * m * integrals.ppSigma + (1.0 - m * m) * integrals.ppPi;
    at(3, 3) = n * n * integrals.ppSigma + (1.0 - n * n) * integrals.ppPi;
    at(1, 2) = l * m * diff;
    at(2, 1) = m * l * diff; // == at(1, 2): p-p is symmetric in the two atoms
    at(1, 3) = l * n * diff;
    at(3, 1) = n * l * diff;
    at(2, 3) = m * n * diff;
    at(3, 2) = n * m * diff;

    return block;
}

} // namespace calango::dftb
