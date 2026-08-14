#pragma once

#include <pybind11/pybind11.h>

#include <stdexcept>
#include <string>

namespace calango::pybridge {

/// Convert a pybind11 exception into a std::runtime_error carrying `context`
/// plus the Python error text, so Qt-side callers never need pybind11 types
/// to report what went wrong.
[[noreturn]] inline void rethrow(const pybind11::error_already_set& e,
                                 const std::string& context)
{
    throw std::runtime_error(context + ":\n" + e.what());
}

} // namespace calango::pybridge
