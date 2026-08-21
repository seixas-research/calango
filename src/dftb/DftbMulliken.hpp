#pragma once

#include "dftb/DftbBasis.hpp"
#include "dftb/SlaterKosterTable.hpp"

#include <complex>
#include <vector>

/// Mulliken population analysis (Löwdin is NOT implemented — see
/// DftbPdos.hpp for the same convention choice on the projected-DOS side)
/// from a diagonalized H(k), S(k) and a set of occupation numbers.
///
/// CONVENTION: n_mu = (P S)_mu,mu, P_mu,nu = sum_i f_i C_mu,i C*_nu,i — the
/// standard Mulliken definition, real by construction once summed over a
/// time-reversal-symmetric k-set (a single k in general gives a complex
/// P*S; only the accumulated, full-BZ population is guaranteed real, which
/// is why this module accumulates into a running total rather than
/// returning a per-k population).
namespace calango::dftb {

/// Add this k-point's contribution to the running per-orbital Mulliken
/// population `population` (size == dimension, zero-initialized by the
/// caller before the first call).
///
/// `eigenvectors` is dimension x dimension, row-major, one eigenvector per
/// COLUMN (matching calango::dft::linalg's convention). `occupation[i]` is
/// the electron count (0..2, already including the spin factor and the
/// k-point's BZ weight) in eigenstate i.
void accumulateMullikenPopulation(
    const std::vector<std::complex<double>>& eigenvectors,
    const std::vector<std::complex<double>>& overlapMatrix, int dimension,
    const std::vector<double>& occupation, std::vector<double>& population);

/// Per-atom charge fluctuation dQ_A = (sum of orbital populations on A) -
/// (neutral-atom reference occupation of A, read from the .skf on-site
/// data — fs + fp).
std::vector<double> mullikenChargeFluctuation(
    const std::vector<double>& orbitalPopulation, const DftbBasis& basis,
    const SlaterKosterTable& table);

/// Total valence electron count of the neutral structure (sum of every
/// atom's fs + fp reference occupation) — the target the SCF's Fermi-level
/// search fills the bands to.
double totalValenceElectrons(const DftbBasis& basis,
                              const SlaterKosterTable& table);

} // namespace calango::dftb
