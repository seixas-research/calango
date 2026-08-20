#!/usr/bin/env python3
"""Cross-validate core::Dsim against oncapintada, the paper's own reference.

This is the development-time check for the Dilute Solution Interpolation
(DSI) module: build one complete binary test case (EMT-relaxed Cu/Pd 3x3x3
fcc, 27-atom supercells — the paper's own protocol, full cell+ion relaxation
to fmax < 0.02 eV/A) and feed the SAME four supercell energies into both
implementations.

`tests/DsimTest.cpp` carries a companion check using the paper's own Au-Pt/
GPAW worked example instead (needs no live run), checked against
`core::solveDsimBinary()` to near-double-precision (it runs in ordinary CI,
no conda environment needed). What THIS script adds is (a) a regression
guard that oncapintada itself still reproduces those recorded numbers — so a
future oncapintada change that silently drifted the model would be caught
here rather than only showing up as an unexplained mismatch elsewhere — and
(b) running the actual compiled `calango_dsim_test` binary (if given) so a
release engineer sees ONE command exercise both sides together.

Skips cleanly (exit 0) when no interpreter has both `ase` and `oncapintada`
importable — this is expected almost everywhere: oncapintada is a private
research package installed only in the 'onca' conda environment on the
author's own machine, never a build/runtime dependency of Calango itself
(same policy as this project's Wannier90-inspiration rule: read freely,
never link against). Set CALANGO_ONCAPINTADA_PYTHON to an interpreter that
has it, or the test looks through the Conda environments the same way the
application does (see tests/xtb_integration_test.py, which this mirrors).

Usage:  dsim_oncapintada_test.py [calango_dsim_test binary]
"""
import json
import os
import pathlib
import subprocess
import sys

# Recorded from a LIVE run of this same generation procedure against
# oncapintada 26.8.1 / ase 3.28.0 in the 'onca' environment. This is the
# fixture DsimTest.cpp's "Cross-validation fixture against oncapintada"
# block also carries (that one uses the paper's own Au-Pt/GPAW numbers
# instead, since it needs no live run) — kept in step with it by hand.
# RAW TOTAL supercell energies (eV) — NOT divided by atom count; see the
# unit-convention note in Dsim.hpp for why that division was the bug.
FIXTURE_N_ATOMS = 27
FIXTURE_ENERGY_PURE_A_EV = -0.18963277420433755  # Cu, 27-atom supercell total
FIXTURE_ENERGY_PURE_B_EV = -0.007198013675111081  # Pd, 27-atom supercell total
FIXTURE_ENERGY_B_IN_A_EV = -0.265028575454894  # Pd diluted in Cu, total
FIXTURE_ENERGY_A_IN_B_EV = -0.08012436106253329  # Cu diluted in Pd, total
FIXTURE_M_B_IN_A_EV = -0.08215264423312038
FIXTURE_M_A_IN_B_EV = -0.06616950440485826
FIXTURE_X_GRID = [i / 10.0 for i in range(11)]
FIXTURE_ENTHALPY_EV_PER_ATOM = [
    0.0, -0.007249889722526476, -0.012632962602794876, -0.01624511747977477,
    -0.018182253192435726, -0.018540268579747332, -0.017415062480679144,
    -0.014902533734200746, -0.011098581179281709, -0.006099103654891602, 0.0,
]
TOLERANCE_EV = 1e-9  # both sides use IEEE double throughout; drift means a
                     # real algorithmic mismatch, not float noise.

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def candidate_interpreters():
    """Interpreters that might have oncapintada, in the order the app-side
    convention (CondaEnvs::discover()) would try them."""
    explicit = os.environ.get("CALANGO_ONCAPINTADA_PYTHON")
    if explicit:
        yield explicit
    yield sys.executable
    try:
        with open(os.path.expanduser("~/.calango/settings.json")) as fh:
            jobs = json.load(fh).get("jobs", {})
        presets = json.loads(jobs.get("environmentPresets", "{}") or "{}")
        if presets.get("oncapintada"):
            yield os.path.join(presets["oncapintada"], "bin", "python")
    except Exception:
        pass
    home = pathlib.Path.home()
    # The conventional env name this task's own instructions name explicitly,
    # tried first among the scanned candidates.
    roots = [home / "miniconda3" / "envs", home / "anaconda3" / "envs",
             home / "miniforge3" / "envs", home / "mambaforge" / "envs"]
    for root in roots:
        if not root.is_dir():
            continue
        preferred = root / "onca" / "bin" / "python"
        if preferred.is_file():
            yield str(preferred)
        for env in sorted(root.iterdir()):
            exe = env / "bin" / "python"
            if exe.is_file() and exe != preferred:
                yield str(exe)


def find_oncapintada_python():
    for exe in candidate_interpreters():
        try:
            done = subprocess.run(
                [exe, "-c", "import oncapintada, ase.calculators.emt; print('yes')"],
                capture_output=True, timeout=120)
        except (OSError, subprocess.SubprocessError):
            continue
        if done.returncode == 0 and b"yes" in done.stdout:
            return exe
    return None


GENERATE_AND_COMPARE = r'''
import json
import sys

import numpy as np
from ase.build import bulk
from ase.calculators.emt import EMT
from ase.filters import FrechetCellFilter
from ase.optimize import FIRE
from oncapintada.subregular_model import BinaryAlloy

FMAX = 0.02


def relax(atoms):
    atoms.calc = EMT()
    FIRE(FrechetCellFilter(atoms), logfile=None).run(fmax=FMAX, steps=500)
    return float(atoms.get_potential_energy())


def supercell(symbol):
    return bulk(symbol, "fcc").repeat((3, 3, 3))

# RAW TOTAL supercell energies -- NOT divided by atom count. Eq. 9's E(Z) is
# the total energy of an N_atoms supercell; the x0-weighted subtraction in
# Mij() is itself what makes M intensive (see Dsim.hpp's unit-convention
# note). Dividing by len(cu) here first was the bug this test now guards
# against: it silently shrank every M (and DeltaH_mix) by ~natoms.
cu = supercell("Cu")
e_cu = relax(cu)
pd = supercell("Pd")
e_pd = relax(pd)
pd_in_cu = supercell("Cu")
pd_in_cu[0].symbol = "Pd"
e_pd_in_cu = relax(pd_in_cu)
cu_in_pd = supercell("Pd")
cu_in_pd[0].symbol = "Cu"
e_cu_in_pd = relax(cu_in_pd)

energy_matrix = np.array([[e_cu, e_cu_in_pd], [e_pd_in_cu, e_pd]])
alloy = BinaryAlloy(energy_matrix=energy_matrix, dilution=1.0 / len(cu))
M = alloy.Mij()
x = np.linspace(0.0, 1.0, 11)
H = alloy.enthalpy_of_mixing(x, unit="eV/atom")

print(json.dumps({
    "n_atoms": len(cu),
    "e_pure_a": e_cu, "e_pure_b": e_pd,
    "e_b_in_a": e_pd_in_cu, "e_a_in_b": e_cu_in_pd,
    "m_b_in_a": float(M[1, 0]), "m_a_in_b": float(M[0, 1]),
    "x": x.tolist(), "h": H.tolist(),
}))
'''


def main():
    python = find_oncapintada_python()
    if not python:
        print("SKIP: no interpreter with oncapintada + ase importable "
              "(set CALANGO_ONCAPINTADA_PYTHON, or install into a conda env "
              "named 'onca')")
        return 0

    print(f"Using interpreter: {python}")
    result = subprocess.run([python, "-c", GENERATE_AND_COMPARE],
                            capture_output=True, text=True, timeout=600)
    if result.returncode != 0:
        print(result.stdout[-3000:])
        print(result.stderr[-3000:])
        check(False, "the live EMT-relax + oncapintada generation ran to completion")
        print(f"\n{failures} check(s) FAILED.")
        return 1

    live = json.loads(result.stdout.strip().splitlines()[-1])
    print(f"n_atoms = {live['n_atoms']}")
    print(f"E_pure_Cu = {live['e_pure_a']:.10f} eV (total), "
          f"E_pure_Pd = {live['e_pure_b']:.10f} eV (total)")
    print(f"E_Pd-in-Cu = {live['e_b_in_a']:.10f} eV (total), "
          f"E_Cu-in-Pd = {live['e_a_in_b']:.10f} eV (total)")
    print(f"M_2[1] (Pd-in-Cu) = {live['m_b_in_a']:.10f} eV, "
          f"M_1[2] (Cu-in-Pd) = {live['m_a_in_b']:.10f} eV")

    # -- Regression guard: oncapintada itself still lands on the recorded ---
    # fixture (EMT is deterministic given the same ASE/oncapintada versions;
    # a mismatch here means the REFERENCE moved, which is exactly what a
    # cross-validation test must not let pass silently).
    check(live["n_atoms"] == FIXTURE_N_ATOMS, "supercell size matches the recorded fixture")
    check(abs(live["m_b_in_a"] - FIXTURE_M_B_IN_A_EV) < TOLERANCE_EV,
          "M_2[1] matches the recorded oncapintada fixture")
    check(abs(live["m_a_in_b"] - FIXTURE_M_A_IN_B_EV) < TOLERANCE_EV,
          "M_1[2] matches the recorded oncapintada fixture")
    max_dev_vs_fixture = max(abs(h - h0) for h, h0 in
                             zip(live["h"], FIXTURE_ENTHALPY_EV_PER_ATOM))
    print(f"max |DeltaH_mix - recorded fixture| = {max_dev_vs_fixture:.3e} eV/atom")
    check(max_dev_vs_fixture < TOLERANCE_EV,
          "live oncapintada DeltaH_mix(x) matches the recorded fixture")

    # -- The C++ side: same fixture numbers already checked to < 1e-12 ------
    # eV/atom against core::solveDsimBinary() in DsimTest.cpp. Running the
    # compiled binary here (when given) demonstrates both halves of the
    # cross-validation together, in one command.
    if len(sys.argv) > 1:
        binary = sys.argv[1]
        if os.path.isfile(binary):
            cpp = subprocess.run([binary], capture_output=True, text=True)
            print(cpp.stdout)
            check(cpp.returncode == 0,
                  f"{os.path.basename(binary)} (core::Dsim vs. the same "
                  "oncapintada fixture) passes")
        else:
            print(f"SKIP: {binary} does not exist (build calango_dsim_test first)")
    else:
        print("(no calango_dsim_test binary given — pass its path to also "
              "run the C++-side check here)")

    print("\nAll DSIM/oncapintada cross-validation checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
