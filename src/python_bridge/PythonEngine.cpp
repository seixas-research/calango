#include "python_bridge/PythonEngine.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <vector>
#endif

namespace py = pybind11;
namespace fs = std::filesystem;

namespace calango::pybridge {

namespace {

/// Directory of the running executable ("" if it cannot be determined).
fs::path executableDir()
{
    std::error_code ec;
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size); // query required buffer size
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        const fs::path exe = fs::canonical(buffer.data(), ec);
        if (!ec)
            return exe.parent_path();
    }
#elif defined(__linux__)
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec)
        return exe.parent_path();
#endif
    return {};
}

/// Python environment shipped inside the installed layout, if any:
/// macOS bundle:  Calango.app/Contents/{MacOS,Resources/python/bin/python3}
/// Linux (deb):   <prefix>/{bin,lib/calango/python/bin/python3}
std::string bundledInterpreter()
{
    const fs::path exeDir = executableDir();
    if (exeDir.empty())
        return {};
#if defined(__APPLE__)
    const fs::path candidate =
        exeDir / ".." / "Resources" / "python" / "bin" / "python3";
#else
    const fs::path candidate =
        exeDir / ".." / "lib" / "calango" / "python" / "bin" / "python3";
#endif
    std::error_code ec;
    if (fs::exists(candidate, ec)) {
        const fs::path resolved = fs::canonical(candidate, ec);
        return ec ? candidate.string() : resolved.string();
    }
    return {};
}

/// See the resolution order documented in PythonEngine.hpp.
std::string resolveInterpreter()
{
    if (const char* env = std::getenv("CALANGO_PYTHON"); env && *env) {
        if (fs::exists(env))
            return env;
    }
    if (const char* venv = std::getenv("VIRTUAL_ENV"); venv && *venv) {
#ifdef _WIN32
        const fs::path candidate = fs::path(venv) / "Scripts" / "python.exe";
#else
        const fs::path candidate = fs::path(venv) / "bin" / "python";
#endif
        if (fs::exists(candidate))
            return candidate.string();
    }
    if (auto bundled = bundledInterpreter(); !bundled.empty())
        return bundled;
#ifdef CALANGO_DEFAULT_PYTHON
    if (fs::exists(CALANGO_DEFAULT_PYTHON))
        return CALANGO_DEFAULT_PYTHON;
#endif
    return {};
}

} // namespace

PythonEngine* PythonEngine::s_instance = nullptr;

PythonEngine::PythonEngine()
{
    assert(s_instance == nullptr && "PythonEngine must be a singleton");
    s_instance = this;

    resolvedExecutable_ = resolveInterpreter();

    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    if (!resolvedExecutable_.empty())
        PyConfig_SetBytesString(&config, &config.executable, resolvedExecutable_.c_str());
    interpreter_ = std::make_unique<py::scoped_interpreter>(&config);

    try {
        pythonVersion_ = py::module_::import("sys")
                             .attr("version")
                             .cast<std::string>();

        const py::module_ ase = py::module_::import("ase");
        aseVersion_ = ase.attr("__version__").cast<std::string>();
        aseAvailable_ = true;
    } catch (const py::error_already_set& e) {
        aseAvailable_ = false;
        lastError_ = std::string("Interpreter: ")
            + (resolvedExecutable_.empty() ? "<default>" : resolvedExecutable_)
            + "\n" + e.what();
    }
}

PythonEngine::~PythonEngine()
{
    s_instance = nullptr;
}

PythonEngine& PythonEngine::instance()
{
    assert(s_instance != nullptr && "PythonEngine not created yet");
    return *s_instance;
}

std::string PythonEngine::executable() const
{
    if (!resolvedExecutable_.empty())
        return resolvedExecutable_;
    return py::module_::import("sys").attr("executable").cast<std::string>();
}

void PythonEngine::addSysPath(const std::string& directory)
{
    py::module_::import("sys").attr("path").attr("append")(directory);
}

} // namespace calango::pybridge
