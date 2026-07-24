#pragma once

#include "core/CalculatorConfig.hpp"

#include <string>

namespace calango::core {

/// Parameters for the Electron Localization Function (ELF) post-process, filled
/// in by the ELF wizard and consumed by generateElfScript(). Deliberately
/// UI-free so the script can also be generated headlessly.
struct ElfConfig {
    /// Engine + backend knobs (cutoff / k-points / GPAW discretization) chosen
    /// in the wizard's Calculator Settings stage. `calculator.calculator`
    /// selects which SCF engine builds the ground state; the ELF η(r) itself is
    /// evaluated through GPAW's kinetic-energy-density path.
    CalculatorConfig calculator;

    /// Absolute directory of a completed process that already holds GPAW
    /// wavefunctions (`*.gpw`). When set the script restarts GPAW from that
    /// density instead of running a fresh SCF. Empty ⇒ fresh ground state.
    std::string baselineDir;
};

/// Turns an ElfConfig into a standalone ASE/GPAW script that writes `elf.cube`
/// into the job directory and emits the `CALANGO_RESULT elf=elf.cube` marker
/// the controller watches for. Mirrors the other core script generators
/// (Optics, Phonon, …): the returned string is the source of truth a job runs.
std::string generateElfScript(const ElfConfig& cfg);

} // namespace calango::core
