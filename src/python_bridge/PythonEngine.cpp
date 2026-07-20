#include "python_bridge/PythonEngine.hpp"

#include <cassert>
#include <stdexcept>

namespace py = pybind11;

namespace calango::pybridge {

PythonEngine* PythonEngine::s_instance = nullptr;

PythonEngine::PythonEngine()
{
    assert(s_instance == nullptr && "PythonEngine must be a singleton");
    s_instance = this;

    try {
        pythonVersion_ = py::module_::import("sys")
                             .attr("version")
                             .cast<std::string>();

        const py::module_ ase = py::module_::import("ase");
        aseVersion_ = ase.attr("__version__").cast<std::string>();
        aseAvailable_ = true;
    } catch (const py::error_already_set& e) {
        aseAvailable_ = false;
        lastError_ = e.what();
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
    return py::module_::import("sys").attr("executable").cast<std::string>();
}

void PythonEngine::addSysPath(const std::string& directory)
{
    py::module_::import("sys").attr("path").attr("append")(directory);
}

} // namespace calango::pybridge
