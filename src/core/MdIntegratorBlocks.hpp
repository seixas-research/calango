#pragma once

// <cstdint> must stay even when clangd calls it unused: libstdc++ from GCC 13
// no longer pulls the fixed-width integer types in transitively, so removing it
// breaks the Linux .deb build while the macOS build stays green.
#include <cstdint>
#include <sstream>
#include <string>

#include "core/CalculatorConfig.hpp"

namespace calango::core {

/// The ASE integrator-construction block, in ONE place.
///
/// Every generated script that propagates ions binds a variable called `dyn` to
/// one of ASE's seven integrators, with the same timestep / temperature /
/// coupling parameters read from the same CalculatorConfig fields. There is
/// exactly one correct spelling for each of them — `friction` is per-fs for
/// Langevin, `taut` is a time for Berendsen but `tdamp` for the Nosé-Hoover
/// chain, `pfactor` is a time SQUARED times a bulk modulus for NPT — and a
/// second copy of that table would drift silently: an MD run and a
/// thermodynamic-integration window that disagree about what "thermostat
/// coupling" means are not running the same dynamics, and nothing in the output
/// says so.
///
/// Header-only, deliberately. A .cpp would have to be added to the eight CMake
/// targets that already compile AseScriptGenerator.cpp, and every one of those
/// edits is a chance to add it to seven.
namespace md_blocks {

/// Emit `dyn = <integrator>(...)` plus its import, for `config.ensemble`.
///
/// Reads the Python names `atoms`, `units` and `temperature_K` from the
/// surrounding script — the convention every generator already follows — so the
/// caller is responsible for having imported `ase.units` and defined the
/// setpoint before this runs. The temperature is a NAME rather than a literal
/// so an annealing schedule (or a TI window) can retarget it afterwards.
inline void emitIntegrator(std::ostringstream& out, const CalculatorConfig& c)
{
    switch (c.ensemble) {
    case MdEnsemble::VelocityVerletNVE:
        out << "from ase.md.verlet import VelocityVerlet\n"
               "\n"
               "dyn = VelocityVerlet(\n"
               "    atoms,\n"
            << "    timestep=" << c.timestepFs << " * units.fs,\n"
               ")\n";
        break;
    case MdEnsemble::LangevinNVT:
        out << "from ase.md.langevin import Langevin\n"
               "\n"
               "dyn = Langevin(\n"
               "    atoms,\n"
            << "    timestep=" << c.timestepFs << " * units.fs,\n"
               "    temperature_K=temperature_K,\n"
            << "    friction=" << c.frictionPerFs << " / units.fs,\n"
               ")\n";
        break;
    case MdEnsemble::AndersenNVT:
        out << "from ase.md.andersen import Andersen\n"
               "\n"
               "dyn = Andersen(\n"
               "    atoms,\n"
            << "    timestep=" << c.timestepFs << " * units.fs,\n"
               "    temperature_K=temperature_K,\n"
            << "    andersen_prob=" << c.andersenProb << ",\n"
               ")\n";
        break;
    case MdEnsemble::BerendsenNVT:
        out << "from ase.md.nvtberendsen import NVTBerendsen\n"
               "\n"
               "dyn = NVTBerendsen(\n"
               "    atoms,\n"
            << "    timestep=" << c.timestepFs << " * units.fs,\n"
               "    temperature_K=temperature_K,\n"
            << "    taut=" << c.tautFs << " * units.fs,\n"
               ")\n";
        break;
    case MdEnsemble::NoseHooverChainNVT:
        out << "from ase.md.nose_hoover_chain import NoseHooverChainNVT\n"
               "\n"
               "dyn = NoseHooverChainNVT(\n"
               "    atoms,\n"
            << "    timestep=" << c.timestepFs << " * units.fs,\n"
               "    temperature_K=temperature_K,\n"
            << "    tdamp=" << c.tautFs << " * units.fs,\n"
               ")\n";
        break;
    case MdEnsemble::BerendsenNPT:
        out << "from ase.md.nptberendsen import NPTBerendsen\n"
               "\n"
               "# EDIT ME: compressibility_au below is water-like; use your\n"
               "# material's isothermal compressibility for meaningful cell\n"
               "# dynamics.\n"
               "dyn = NPTBerendsen(\n"
               "    atoms,\n"
            << "    timestep=" << c.timestepFs << " * units.fs,\n"
               "    temperature_K=temperature_K,\n"
            << "    taut=" << c.tautFs << " * units.fs,\n"
            << "    pressure_au=" << c.pressureGPa << " * units.GPa,\n"
            << "    taup=" << c.taupFs << " * units.fs,\n"
               "    compressibility_au=4.57e-5 / units.bar,\n"
               ")\n";
        break;
    case MdEnsemble::MelchionnaNPT:
        out << "from ase.md.npt import NPT\n"
               "\n"
               "# Nosé-Hoover thermostat + Parrinello-Rahman barostat\n"
               "# (Melchionna). Requires an upper-triangular cell.\n"
               "# EDIT ME: pfactor = ptime² · bulk modulus — 100 GPa below is\n"
               "# a solid-like placeholder.\n"
               "atoms.set_cell(atoms.cell.standard_form()[0], scale_atoms=True)\n"
               "dyn = NPT(\n"
               "    atoms,\n"
            << "    timestep=" << c.timestepFs << " * units.fs,\n"
               "    temperature_K=temperature_K,\n"
            << "    externalstress=" << c.pressureGPa << " * units.GPa,\n"
            << "    ttime=" << c.tautFs << " * units.fs,\n"
            << "    pfactor=(" << c.taupFs << " * units.fs) ** 2 * 100 * units.GPa,\n"
               ")\n";
        break;
    }
}

} // namespace md_blocks

} // namespace calango::core
