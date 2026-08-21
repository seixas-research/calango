#pragma once

#include "dft/DftTypes.hpp"
#include "dftb/DftbBasis.hpp"
#include "dftb/DftbHamiltonian.hpp"
#include "dftb/DftbScf.hpp"
#include "dftb/SlaterKosterTable.hpp"

#include <map>
#include <string>
#include <vector>

/// Orbital/species-projected density of states from Mulliken projections.
///
/// CONVENTION: Mulliken, not Löwdin — matching DftbMulliken.hpp's own
/// choice (see its doc comment) and this engine's Mulliken-based SCC
/// charges, so the SAME population definition is used everywhere rather
/// than switching conventions between the charge that drove the SCF and
/// the DOS that describes its result.
///
/// INTEGRATION: sampling (Gaussian-broadened histogram) only — the linear
/// tetrahedron method is not implemented for this engine (see FUTURE.md).
/// pdos.json's own schema already distinguishes the two
/// ("integration": "sampling" | "tetrahedron"), so the shared PDOS viewer
/// reads a DFTB result exactly like it reads a GPAW sampling-mode one.
///
/// GROUPING: by "<element symbol> <shell>" (shell in {s, p} — every p
/// orbital of one element pooled together, matching pdos.json's existing
/// "<Symbol> <shell>" projection-key convention other engines already use).
namespace calango::dftb {

struct DftbPdosResult {
    std::vector<double> energiesEv;
    double fermiEv = 0.0;
    double binWidthEv = 0.0;
    /// key = "<Symbol> <shell>", value index-aligned with energiesEv.
    std::map<std::string, std::vector<double>> projections;
};

dft::Outcome computeDftbPdos(const DftbScfResult& scf,
                              const DftbHamiltonianBuilder& hamiltonian,
                              const DftbBasis& basis,
                              const SlaterKosterTable& table,
                              double broadeningHartree,
                              double binWidthHartree, DftbPdosResult& out);

} // namespace calango::dftb
