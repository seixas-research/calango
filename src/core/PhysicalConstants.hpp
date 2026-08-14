#pragma once

#include <numbers>

namespace calango::core {

/// π. Lives here (rather than each TU keeping a private copy) so that every
/// consumer agrees bit-for-bit; std::numbers::pi is the correctly rounded
/// double.
inline constexpr double kPi = std::numbers::pi;

/// Boltzmann constant, eV/K (CODATA 2018 — exact by definition of the
/// kelvin).
inline constexpr double kBoltzmannEvPerK = 8.617333262e-5;

/// Hartree, in eV (CODATA 2018). Generated Python that embeds this value as
/// text keeps its own literal — keep the two spellings in step.
inline constexpr double kHartreeEv = 27.211386245988;

} // namespace calango::core
