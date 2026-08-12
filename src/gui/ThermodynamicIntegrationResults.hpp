#pragma once

// <cstdint> must stay even when clangd calls it unused: libstdc++ from GCC 13
// no longer pulls the fixed-width integer types in transitively, so removing it
// breaks the Linux .deb build while the macOS build stays green.
#include <cstdint>

#include <QString>
#include <QStringList>

#include "core/ThermodynamicIntegration.hpp"

class QWidget;

namespace calango::gui {

/// A thermodynamic-integration run, read back and assembled.
struct TiRunReport {
    /// False when the file could not be read at all (as opposed to a run that
    /// read fine and is incomplete — that is `assembly.integration.complete`).
    bool parsed = false;
    /// True only when every expected λ window reported. Nothing downstream may
    /// treat the free energies as meaningful otherwise.
    bool complete = false;
    core::TiSystem system;
    core::TiAssembly assembly;
    core::TiHysteresis hysteresis;
    /// Per-window error analysis, in λ order.
    std::vector<core::TiWindowSample> windows;
    QStringList warnings;
    /// The whole thing as text, ready to be shown.
    QString text;
};

/// Read `ti.json`, run the per-window error analysis and assemble G(T, P).
///
/// THE STATISTICS HAPPEN HERE, not in the generated script. The script writes
/// the raw ⟨∂U/∂λ⟩ series per window; this is where each series gets its
/// integrated autocorrelation time, and therefore its real error bar — the
/// naive σ/√N over correlated MD samples under-reports by √(2τ), routinely a
/// factor of three, which is exactly the size of the discrepancies that then
/// get explained away.
TiRunReport readThermodynamicIntegrationRun(const QString& jsonPath);

/// Read the run and show the report in a plain read-only window.
void showThermodynamicIntegrationResults(QWidget* parent,
                                         const QString& jsonPath);

} // namespace calango::gui
