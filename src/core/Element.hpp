#pragma once

#include <string>

namespace calango::core {

/// Static per-element data used for bond detection and display defaults.
struct ElementData {
    const char* symbol;
    float covalentRadius;   ///< Å (Cordero et al., Dalton Trans. 2008)
    unsigned char rgb[3];   ///< Jmol CPK color convention
};

namespace Elements {

/// Highest atomic number with tabulated data.
inline constexpr int maxZ = 86;

/// Data for atomic number `z`; out-of-range values return a gray dummy ("X").
const ElementData& data(int z);

/// Atomic number for a chemical symbol (case-insensitive); 0 if unknown.
int atomicNumber(const std::string& symbol);

} // namespace Elements
} // namespace calango::core
