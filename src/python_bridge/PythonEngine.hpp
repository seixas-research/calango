#pragma once

#include <pybind11/embed.h>

#include <string>

namespace calango::pybridge {

/// Owns the embedded CPython interpreter (RAII around py::scoped_interpreter).
///
/// Exactly one instance must exist; create it in main() before any Python
/// use and let it outlive every consumer of Python objects.
///
/// Threading policy (v0.1): all embedded-Python calls happen on the GUI
/// thread, so no explicit GIL juggling is needed. Simulation workloads never
/// run in-process — they go through JobRunner as external `python script.py`
/// subprocesses, which keeps the GUI responsive and crashes isolated.
/// If in-process background Python is ever added, release the GIL on the
/// main thread (py::gil_scoped_release) and acquire it in workers.
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

    /// Path of the interpreter binary (sys.executable) — used by JobRunner
    /// so subprocess jobs see the same environment (and thus the same ASE).
    std::string executable() const;

    /// Append a directory to sys.path (e.g. for shipping helper modules).
    void addSysPath(const std::string& directory);

private:
    pybind11::scoped_interpreter interpreter_;
    bool aseAvailable_ = false;
    std::string aseVersion_;
    std::string pythonVersion_;
    std::string lastError_;

    static PythonEngine* s_instance;
};

} // namespace calango::pybridge
