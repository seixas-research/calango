#pragma once

#include "core/Element.hpp"
#include "core/Vec3.hpp"

namespace calango::core {

struct Atom {
    int atomicNumber = 6;
    Vec3 position;

    const char* symbol() const { return Elements::data(atomicNumber).symbol; }
    float covalentRadius() const { return Elements::data(atomicNumber).covalentRadius; }
};

} // namespace calango::core
