#include "python_bridge/PythonEngine.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>

namespace py = pybind11;
namespace fs = std::filesystem;

namespace calango::pybridge {

namespace {

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
