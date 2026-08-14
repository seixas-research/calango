#pragma once

#include <numbers>

namespace calango::dft {

inline constexpr double kPi = std::numbers::pi;
inline constexpr double kTwoPi = 2.0 * kPi;
inline constexpr double kFourPi = 4.0 * kPi;

/// Hartree in eV (CODATA 2018). The engine works in Hartree atomic units and
/// converts once at its boundary.
inline constexpr double kHartreeToEv = 27.211386245988;

/// Bohr radii per Å (CODATA 2018).
inline constexpr double kBohrPerAngstrom = 1.8897261254578281;

} // namespace calango::dft
