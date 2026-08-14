#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "core/Vec3.hpp"
#include "core/WannierHamiltonian.hpp"

#include <QList>
#include <QString>

#include <array>

namespace calango::gui {

/// What a completed Calango Wannier run recorded, for the three modules that
/// consume its Hamiltonian (Boltzmann Transport, Berry Phase, cRPA).
///
/// WHY THIS EXISTS. Those modules used to offer exactly two inputs: a
/// `_hr.dat` the user had from somewhere else, or a built-in demonstration
/// model. Since Calango's own Wannier run wrote no H(R), the only route to
/// real data ran through wannier90 — the opposite of the decision to implement
/// the localization natively. The run writes `wannier_hr.dat` now, and this is
/// the reader that closes the loop.
struct WannierRunData {
    core::WannierHamiltonian hamiltonian;
    /// Absolute path of the `_hr.dat`, for the modules that parse it
    /// themselves and for reporting where the numbers came from.
    QString hrPath;
    /// Lattice vectors as rows, angstrom. The integer R vectors of the hopping
    /// table are expressed in these, so a Hamiltonian without them cannot be
    /// turned into a velocity or a distance.
    std::array<std::array<double, 3>, 3> cell{
        {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    QList<core::Vec3> centres; ///< Wannier centres, angstrom
    QList<double> spreads;     ///< per-orbital spreads, angstrom squared
    int nWannier = 0;
};

/// Read the Wannier Hamiltonian (and centres, spreads and cell) that the run
/// in `runDir` wrote.
///
/// Returns false with a readable `error` when the directory holds no
/// `wannier.json`, when that run predates H(R) output, or when the table
/// cannot be parsed. The distinction matters in the message: "this run is too
/// old, re-run it" is a different instruction from "this file is corrupt".
bool loadWannierRun(const QString& runDir, WannierRunData* out,
                    QString* error = nullptr);

} // namespace calango::gui
