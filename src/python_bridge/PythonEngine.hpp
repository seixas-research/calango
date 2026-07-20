#pragma once

#include <pybind11/embed.h>

#include <memory>
#include <string>

namespace calango::pybridge {

/// Owns the embedded CPython interpreter (RAII around py::scoped_interpreter).
///
/// Exactly one instance must exist; create it in main() before any Python
/// use and let it outlive every consumer of Python objects.
///
/// Interpreter resolution: an embedded interpreter does NOT inherit a
/// virtualenv by itself (sys.executable would be the Calango binary, so
/// Python would fall back to the base installation without ASE). We
/// therefore initialize with an explicit PyConfig.executable, resolved as:
///   1. $CALANGO_PYTHON            (explicit interpreter path)
///   2. $VIRTUAL_ENV/bin/python    (active virtualenv)
///   3. the interpreter Calango was configured against at build time
/// Python then performs normal venv activation via pyvenv.cfg.
///
/// Threading policy (v0.2): all embedded-Python calls happen on the GUI
/// thread, so no explicit GIL juggling is needed. Simulation workloads never
/// run in-process — they go through JobRunner as external `python script.py`
/// subprocesses, which keeps the GUI responsive and crashes isolated.
class PythonEngine {
public:
    PythonEngine();
    ~PythonEngine();

    PythonEngine(const PythonEngine&) = delete;
    PythonEngine& operator=(const PythonEngine&) = delete;

    static PythonEngine& instance();

    bool aseAvailable() const { return aseAvailable_; }
    const std::string& aseVersion() const { return aseVersion_; }
    const std::string& pythonVersion() const { return pythonVersion_; }
    const std::string& lastError() const { return lastError_; }

    /// Path of the interpreter driving the embedded runtime — also used by
    /// JobRunner so subprocess jobs see the same environment (same ASE).
    std::string executable() const;

    /// Append a directory to sys.path (e.g. for shipping helper modules).
    void addSysPath(const std::string& directory);

private:
    std::string resolvedExecutable_;
    std::unique_ptr<pybind11::scoped_interpreter> interpreter_;
    bool aseAvailable_ = false;
    std::string aseVersion_;
    std::string pythonVersion_;
    std::string lastError_;

    static PythonEngine* s_instance;
};

} // namespace calango::pybridge
