#include "python_bridge/NebBuilder.hpp"

#include "python_bridge/AseBridge.hpp"
#include "python_bridge/PyError.hpp"

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <stdexcept>

namespace py = pybind11;

namespace calango::pybridge {

namespace {

// Executed with a single dict as both globals and locals (function bodies
// must see the script-level names).
constexpr const char* kInterpolateScript = R"PY(
try:
    from ase.mep import NEB
except ImportError:  # ASE < 3.23 kept NEB under ase.neb
    from ase.neb import NEB

n = int(n_intermediate)
images = [initial] + [initial.copy() for _ in range(n)] + [final]
neb = NEB(images)
# 'linear' is ASE's default; 'idpp' relaxes the path with the image-dependent
# pair potential for a smoother, less-overlapping initial guess.
neb.interpolate(method=method)
result_images = images
)PY";

} // namespace

std::vector<core::Structure> NebBuilder::interpolate(
    const core::Structure& initial, const core::Structure& final,
    int intermediateImages, const std::string& method)
{
    if (intermediateImages < 1)
        throw std::runtime_error("need at least one intermediate image");
    try {
        py::dict scope;
        scope["initial"] = AseBridge::toAtoms(initial);
        scope["final"] = AseBridge::toAtoms(final);
        scope["n_intermediate"] = intermediateImages;
        scope["method"] = method;
        py::exec(kInterpolateScript, scope, scope);

        std::vector<core::Structure> band;
        for (const auto& image : scope["result_images"].cast<py::list>())
            band.push_back(AseBridge::fromAtoms(image));
        return band;
    } catch (const py::error_already_set& e) {
        rethrow(e, "NEB interpolation failed (do the endpoints have the same "
                   "atoms in the same order?)");
    }
}

} // namespace calango::pybridge
