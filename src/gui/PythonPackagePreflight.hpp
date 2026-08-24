#pragma once

#include <QString>

namespace calango::gui {

/// Whether one Python package is importable in a GIVEN interpreter — not
/// the embedded one PythonEngine owns (which py::exec/py::import would
/// probe), but an ARBITRARY external interpreter a job is about to be
/// launched under (a conda env, a cluster's own python, ...), exactly the
/// same interpreter resolution a launched job itself uses. There was no
/// existing precedent for this in the codebase: MainWindow::
/// ensureAseAvailable() is embedded-interpreter-only and does not
/// generalize to "is package X importable under env Y".
struct PythonPackagePreflightResult {
    bool available = false;
    /// Whether the interpreter actually RAN and gave an answer.
    ///
    /// Separate from `available` because the two failures are not the same
    /// thing and must not be acted on the same way: an interpreter that
    /// answered "no such module" is evidence the package is missing, while
    /// one that could not be started or timed out is evidence of nothing.
    /// A caller that refuses to launch on the second is grounding a working
    /// setup over a probe that never happened.
    bool interpreterAnswered = false;
    /// The package's own reported `__version__`, when it has one and the
    /// import succeeded. Empty otherwise — never fabricated.
    QString version;
    /// Set only when `!available`: a human-readable reason (import error
    /// text, or "the interpreter could not be started"), never a raw
    /// traceback dump.
    QString errorMessage;
};

/// Run `<pythonExecutable> -c "import <moduleName>; print(...)"` as a real
/// subprocess and report whether it succeeded, plus the module's own
/// `__version__` when it has one. Synchronous — call this only where a
/// short (interpreter-startup-scale) block is acceptable, e.g. right before
/// launching a job, not from a constructor or anything painted every frame.
PythonPackagePreflightResult checkPythonPackage(const QString& pythonExecutable,
                                                const QString& moduleName,
                                                int timeoutMs = 15000);

/// Which compute devices PyTorch reports as available under
/// `pythonExecutable` — cpu is always true when the probe itself succeeds;
/// cuda/mps follow `torch.cuda.is_available()` / `torch.backends.mps.
/// is_available()`. All false (with `probeSucceeded` false) when torch
/// itself is not importable or the interpreter could not be probed — the
/// caller degrades to "unknown", not "none available".
struct TorchDeviceAvailability {
    bool probeSucceeded = false;
    bool cpu = false;
    bool cuda = false;
    bool mps = false;
};
TorchDeviceAvailability probeTorchDevices(const QString& pythonExecutable,
                                          int timeoutMs = 15000);

} // namespace calango::gui
