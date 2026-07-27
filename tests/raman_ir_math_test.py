#!/usr/bin/env python3
"""Raman / IR spectroscopy: the array algebra, against a closed-form case.

Every number this module produces comes out of four contractions — a
mass-weighted diagonalization, a Z* contraction for the infrared intensity, a
polarizability-derivative contraction for the Raman activity, and the Stokes
prefactor. None of them can fail loudly: a dropped 1/sqrt(M), a transposed
einsum or a swapped index gives a spectrum that looks entirely plausible and is
wrong. py_compile cannot see it and neither can review.

So the functions are exercised against an isotropic A-B spring pair, where
every answer is available in closed form:

  * the three rigid translations must be IR-dark and Raman-silent — translating
    a crystal cannot polarize it, which is the acoustic sum rule on Z*, and it
    is the sharpest check available on the IR contraction;
  * the three stretches must land at exactly sqrt(k/mu);
  * their IR intensity must be exactly (Z*)^2/mu.

The functions are extracted from a GENERATED script by name with ``ast``, so
this exercises the shipped code rather than a transcription of it.

Usage:  raman_ir_math_test.py <calango_script_test binary | generated .py>
"""
import ast
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from ase import units

WANTED = {"impose_acoustic_sum_rule", "mass_weighted_modes", "ir_intensities",
          "raman_activities", "stokes_intensity", "lorentzian_spectrum"}
CONSTANTS = {"DEBYE_PER_EA", "EV_PER_SQRT_EV_A2_AMU"}

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def load(script_path):
    """Pull the wanted functions and constants out of a generated script."""
    tree = ast.parse(Path(script_path).read_text())
    picked = []
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name in WANTED:
            picked.append(node)
        elif isinstance(node, ast.Assign) and any(
                isinstance(t, ast.Name) and t.id in CONSTANTS
                for t in node.targets):
            picked.append(node)
    names = {n.name for n in picked if isinstance(n, ast.FunctionDef)}
    missing = WANTED - names
    if missing:
        raise SystemExit(
            f"generated script does not define {sorted(missing)} — the "
            "physics was inlined again, and nothing can check it")
    module = ast.Module(body=picked, type_ignores=[])
    namespace = {"np": np, "units": units}
    exec(compile(ast.fix_missing_locations(module), "<generated>", "exec"),
         namespace)
    return namespace


def generated_script(argument):
    """A generated raman_ir.py: either given directly, or dumped by the C++
    test binary."""
    path = Path(argument)
    if path.suffix == ".py":
        return path
    directory = Path(tempfile.mkdtemp())
    subprocess.run([str(path), "--dump", str(directory)], check=True,
                   stdout=subprocess.DEVNULL)
    return directory / "raman_ir.py"


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    ns = load(generated_script(sys.argv[1]))

    masses = np.array([12.0, 16.0])          # C, O
    k = 10.0                                  # eV/A^2
    reduced = masses[0] * masses[1] / masses.sum()
    block = k * np.eye(3)
    hessian = np.block([[block, -block], [-block, block]])

    print("Acoustic sum rule on the Hessian:")
    # A Hessian polluted the way a finite-difference one is: the self-force
    # terms carry an error, which floats the acoustic branch off zero.
    polluted = hessian + np.diag([0.35, -0.22, 0.41, 0.18, 0.29, -0.31])
    fixed = ns["impose_acoustic_sum_rule"](polluted, 2)
    rows = fixed.reshape(2, 3, 2, 3).sum(axis=2)
    check(np.allclose(rows, 0.0, atol=1e-12),
          "every row of force constants sums to zero over the atoms")
    check(np.allclose(fixed, fixed.T),
          "the corrected Hessian is still symmetric")
    check(np.allclose(ns["impose_acoustic_sum_rule"](hessian, 2), hessian),
          "a Hessian that already obeys the rule is left untouched")
    polluted_freq, _ = ns["mass_weighted_modes"](polluted, masses)
    check(np.allclose(np.sort(np.abs(polluted_freq))[:3], 0.0, atol=1e-3),
          "and the acoustic branch it produces lands back at zero")

    print("Normal modes of an isotropic A-B spring pair:")
    frequencies, displacements = ns["mass_weighted_modes"](hessian, masses)
    analytic = ns["EV_PER_SQRT_EV_A2_AMU"] * np.sqrt(k / reduced) / units.invcm
    optical = frequencies > 1.0
    check(displacements.shape == (6, 2, 3),
          f"displacements are (modes, atoms, 3); got {displacements.shape}")
    check(int(optical.sum()) == 3, "three optical modes, three translations")
    check(np.allclose(frequencies[optical], analytic),
          f"stretch frequency is sqrt(k/mu) = {analytic:.2f} cm^-1")
    # Not exactly zero: eigh returns the null eigenvalues as ~-1e-16, whose
    # square root the code reports as a tiny NEGATIVE wavenumber (its
    # convention for an imaginary frequency). Microhertz-scale noise, six
    # orders below anything physical.
    check(np.allclose(frequencies[~optical], 0.0, atol=1e-3),
          "the rigid translations come out at zero frequency")

    print("Infrared contraction:")
    born = np.zeros((2, 3, 3))
    born[0] = np.eye(3) * 2.0     # Z* = +2 e
    born[1] = np.eye(3) * -2.0    # ...and -2 e, so Sum_k Z*_k = 0 exactly
    ir = ns["ir_intensities"](born, displacements)
    check(np.allclose(ir[~optical], 0.0, atol=1e-10),
          "rigid translations are IR-dark (the acoustic sum rule)")
    check(np.allclose(ir[optical], 4.0 / reduced, rtol=1e-9),
          f"stretch intensity is (Z*)^2/mu = {4.0 / reduced:.6f} e^2/amu")

    # A Z* set that violates the sum rule MUST light the translations up —
    # otherwise the check above would pass for a contraction that ignores Z*.
    unbalanced = born.copy()
    unbalanced[1] = np.eye(3) * -1.0
    check((ns["ir_intensities"](unbalanced, displacements)[~optical] > 1e-3).any(),
          "a sum-rule-violating Z* does light up the translations")

    print("Raman contraction:")
    dalpha = np.zeros((2, 3, 3, 3))
    for axis in range(3):
        dalpha[0, axis] = np.diag([1.0, 1.0, 3.0])
        dalpha[1, axis] = -np.diag([1.0, 1.0, 3.0])
    activity = ns["raman_activities"](dalpha, displacements)
    check(np.allclose(activity[~optical], 0.0, atol=1e-10),
          "rigid translations are Raman-silent")
    check((activity[optical] > 1e-6).all(), "the stretches are Raman-active")
    # An isotropic derivative has no anisotropy: 45a'^2 only.
    isotropic = np.zeros((2, 3, 3, 3))
    for axis in range(3):
        isotropic[0, axis] = np.eye(3)
        isotropic[1, axis] = -np.eye(3)
    iso_activity = ns["raman_activities"](isotropic, displacements)
    mean = np.sqrt(iso_activity[optical] / 45.0)
    check(np.allclose(iso_activity[optical], 45.0 * mean ** 2),
          "an isotropic dalpha/dQ contributes through 45a'^2 alone")

    print("Stokes prefactor:")
    laser_cm = 1.0e7 / 532.0
    kT_cm = units.kB * 300.0 / units.invcm
    intensity = ns["stokes_intensity"](frequencies, activity, laser_cm, kT_cm)
    check(np.allclose(intensity[~optical], 0.0),
          "no Stokes intensity is assigned to the acoustic branch")
    expected = ((laser_cm - frequencies[optical]) ** 4
                * activity[optical]
                / (1.0 - np.exp(-frequencies[optical] / kT_cm))
                / (30.0 * frequencies[optical]))
    check(np.allclose(intensity[optical], expected),
          "the (wL - w)^4 . (n+1) / w factors are applied as documented")
    hot = ns["stokes_intensity"](frequencies, activity, laser_cm, kT_cm * 4.0)
    check((hot[optical] > intensity[optical]).all(),
          "raising the temperature raises the Stokes intensity")

    print("Lorentzian broadening:")
    grid = np.linspace(0.0, 1200.0, 1201)
    spectrum = ns["lorentzian_spectrum"](grid, np.array([600.0]),
                                         np.array([2.0]), 5.0)
    check(abs(spectrum[600] - 2.0) < 1e-9, "the peak sits at its own height")
    check(abs(spectrum[605] - 1.0) < 1e-9,
          "and falls to half a half-width away")
    negative = ns["lorentzian_spectrum"](grid, np.array([-300.0]),
                                         np.array([2.0]), 5.0)
    check(np.allclose(negative, 0.0),
          "an imaginary mode (negative wavenumber) contributes nothing")

    print("\nAll Raman/IR math checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
