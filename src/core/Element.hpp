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
inline constexpr int maxZ = 118;

/// Data for atomic number `z`; out-of-range values return a gray dummy ("X").
const ElementData& data(int z);

/// Atomic number for a chemical symbol (case-insensitive); 0 if unknown.
int atomicNumber(const std::string& symbol);

/// Standard atomic weight in unified atomic mass units (u), IUPAC 2021.
/// Elements with no stable isotope carry the mass number of their longest-lived
/// one, which is the convention ASE and the MD codes use. Out-of-range Z
/// returns 0 — callers doing mass-weighted work must treat that as "unknown"
/// rather than dividing by it.
///
/// Kept as a separate table from ElementData: masses are needed by the
/// structure builders (density targets) and by vibrational analysis (F = -Mw^2u)
/// but never by rendering, and threading a fourth column through the CPK table
/// would touch all 118 rows for the benefit of neither.
double atomicMass(int z);

} // namespace Elements
} // namespace calango::core
