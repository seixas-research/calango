#pragma once

#include <QString>

#include <vector>

namespace calango::gui {

/// One Kohn-Sham state read back from a completed GPAW baseline's saved
/// wavefunctions — the common first step the LDOS, Wavefunctions and Energy
/// Diagrams wizards all need before they can show anything (an energy
/// window, a state picker, a level diagram), so it is read ONCE here rather
/// than three times in three slightly different ways.
struct GpawState {
    int spin = 0;
    int kpt = 0;
    int band = 0;
    double energyEv = 0.0;
    double occupation = 0.0; ///< -1 when the baseline reported none
    double kWeight = 1.0;
};

struct GpawEigenvalueSpectrum {
    bool ok = false;
    QString errorMessage; ///< set only when !ok
    std::vector<GpawState> states;
    double fermiLevelEv = 0.0;
    int nspins = 1;
};

/// Restarts the `*.gpw` found in `baselineDir` (same glob-and-check shape as
/// AseScriptGenerator::gpawRestartFromBaselineScript) under
/// `pythonExecutable` and reads back every stored state's eigenvalue,
/// occupation and k-point weight. No SCF runs — this is a plain restart —
/// so it is fast even though it is a real GPAW invocation.
///
/// Synchronous, like gui::checkPythonPackage(): blocks the calling thread
/// for up to `timeoutMs`. Call it from a wizard's baseline-selection
/// handler the same way that helper is called from a calculator-check
/// button; there is no asynchronous variant because none of its three
/// callers need one — a plain restart on a saved baseline finishes in low
/// single-digit seconds even for a modest system.
GpawEigenvalueSpectrum peekGpawEigenvalues(const QString& pythonExecutable,
                                          const QString& baselineDir,
                                          int timeoutMs = 60000);

} // namespace calango::gui
