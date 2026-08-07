#!/usr/bin/env python3
"""The 2D charged-defect image correction, checked against limits it must obey.

This is the only new physics in the Charged Defects in 2D Materials module, and
it is exactly the kind that cannot be reviewed by reading it: a wrong prefactor
or a missing 4*pi produces a correction of plausible magnitude and the wrong
value, applied silently to every point of a formation-energy diagram. The bulk
module deliberately delegates its FNV correction to pymatgen for that reason;
there is no equivalent library for the 2D case, so this one is implemented here
and has to earn its trust from limits instead.

Each of these a plausible-but-wrong implementation fails:

  1. UNIFORM DIELECTRIC. Fill the whole cell with epsilon (no vacuum) and the
     scheme must collapse onto the isotropic result: the correction becomes the
     Madelung image energy, scaling as q^2 / (epsilon L).

  2. CHARGE SCALING. E_corr must go exactly as q^2 — it is an electrostatic
     self-energy, and any deviation means the model charge is not being scaled
     with q.

  3. DIELECTRIC SCALING. In the uniform limit E_corr must go as 1/epsilon.

  4. THE ISOLATED SELF-ENERGY, against a closed form. In a uniform medium a
     Gaussian of width sigma has self-energy q^2 / (2 sigma sqrt(pi) eps)
     exactly, so the open-boundary solve can be checked outright rather than
     only for plausibility. Two things ride along: E_isolated must not depend
     on the supercell at all (the first implementation's failure was that it
     did), and the DIFFERENCE of the two halves must come out as the
     simple-cubic Madelung energy, alpha = 2.8373. That last one is the check
     neither half passes alone — it is what shows they share a normalization,
     without which subtracting them is meaningless however good each is.

  5. ANISOTROPY. eps_perp must change the answer, or the profile is not really
     anisotropic and the sheet is being treated as a scalar medium after all.

The Python under test is extracted from the C++ that emits it, so it cannot
drift from what a run executes. Self-skips without numpy/scipy.
"""
import pathlib
import re
import sys

try:
    import numpy as np
    import scipy.special  # noqa: F401  (the profile uses erf)
except ImportError:
    print("numpy/scipy unavailable - skipping 2D defect correction checks")
    sys.exit(0)

_root = pathlib.Path(__file__).resolve().parent.parent
_src = (_root / "src/core/Defect2dScriptGenerator.cpp").read_text()
_match = re.search(r'constexpr const char\* kCorrectionHelpers = R"PY\((.*?)\)PY";',
                   _src, re.S)
if not _match:
    raise SystemExit("could not extract the correction helpers from the generator")

_ns = {"np": np}
exec(_match.group(1), _ns)
two_d_image_correction = _ns["two_d_image_correction"]

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def cubic(length):
    return np.diag([length, length, length]).astype(float)


def correction(q, length, eps_par, eps_perp, thickness, *, lz=None,
               sigma=1.0, width=1.0, n_gz=48, npar=6, uniform=False):
    # `uniform=True` fills both halves with eps and IGNORES thickness/width;
    # the thickness >= L_z passed by the callers below is left in only because
    # it says the same thing, and would still be the right value if the flag
    # were dropped.
    cell = cubic(length)
    if lz is not None:
        cell[2, 2] = lz
    value, _terms = two_d_image_correction(
        q, cell, 0.5, sigma, eps_par, eps_perp, thickness, width, n_gz, npar,
        uniform=uniform)
    return value


print("1. Uniform dielectric collapses onto the isotropic limit:")
# Filling the cell with epsilon means "thickness >= L_z", i.e. no vacuum at all.
# The correction then has to behave like the Madelung image energy of a point
# charge in a cubic cell: proportional to 1/L at fixed epsilon.
lengths = [14.0, 18.0, 22.0, 26.0]
uniform = [correction(1, L, 4.0, 4.0, thickness=3 * L, width=0.0, lz=L, uniform=True)
           for L in lengths]
print("   L (A):      " + "  ".join(f"{L:8.1f}" for L in lengths))
print("   E_corr(eV): " + "  ".join(f"{e:8.4f}" for e in uniform))
# E_corr * L should be constant if the scaling is 1/L.
products = [e * L for e, L in zip(uniform, lengths)]
spread = (max(products) - min(products)) / abs(np.mean(products))
print(f"   E_corr * L spread: {spread:.3%}")
check(all(e > 0.0 for e in uniform),
      "the correction is positive (the periodic array is over-bound)")
check(spread < 0.08, "E_corr scales as 1/L in a uniform medium")

print("\n2. The correction is quadratic in the charge:")
base = correction(1, 18.0, 4.0, 4.0, thickness=54.0, width=0.0, lz=18.0, uniform=True)
ratios = []
for q in (2, 3):
    value = correction(q, 18.0, 4.0, 4.0, thickness=54.0, width=0.0, lz=18.0, uniform=True)
    ratios.append(value / base)
    print(f"   q={q}: E_corr/E_corr(1) = {value / base:.4f}  (expect {q * q})")
check(all(abs(r - q * q) < 1e-6 for r, q in zip(ratios, (2, 3))),
      "E_corr goes exactly as q^2")

print("\n3. The correction scales as 1/epsilon in a uniform medium:")
eps_values = [2.0, 4.0, 8.0]
scaled = [correction(1, 18.0, e, e, thickness=54.0, width=0.0, lz=18.0, uniform=True)
          for e in eps_values]
products = [v * e for v, e in zip(scaled, eps_values)]
spread = (max(products) - min(products)) / abs(np.mean(products))
for e, v in zip(eps_values, scaled):
    print(f"   eps={e:4.1f}: E_corr = {v:.4f} eV   (E_corr * eps = {v * e:.4f})")
check(spread < 0.02, "E_corr * epsilon is constant, i.e. E_corr ~ 1/eps")

print("\n4. The isolated limit reproduces the analytic self-energy:")
# The strongest check available: in a UNIFORM medium the isolated energy of a
# Gaussian of width sigma is q^2 / (2 sigma sqrt(pi) eps), exactly. If the
# open-boundary solve is right this is hit to a fraction of a percent; if the
# radiation condition or the 4*pi is wrong it is not close.
BOHR = 1.0 / 0.529177210903
HARTREE_EV = 27.211386245988
_isolated = _ns["_isolated_energy"]
worst = 0.0
for sigma in (0.8, 1.0, 1.5):
    for eps in (1.0, 3.0, 6.0):
        got = _isolated(1, sigma, eps, eps, 0.0, 0.0, uniform=True)
        want = 1.0 / (2.0 * sigma * BOHR * np.sqrt(np.pi) * eps) * HARTREE_EV
        worst = max(worst, abs(got / want - 1.0))
        print(f"   sigma={sigma:.1f} A eps={eps:4.1f}:  {got:8.4f} vs "
              f"{want:8.4f} eV   ratio {got / want:.5f}")
print(f"   worst deviation: {worst:.3%}")
check(worst < 5e-3, "the open-BC solve matches q^2/(2 sigma sqrt(pi) eps)")

print("\n4b. The isolated energy does not know about the supercell:")
# This is what the first implementation got wrong: it took the in-plane area to
# infinity but kept the cell's periodicity along z, so E_isolated drifted from
# 3.8 to 1.8 eV as the box grew. It now takes no cell at all, so the only way
# it could drift is through a caller passing one -- which this pins.
vals = []
for lz in (20.0, 30.0, 40.0, 60.0):
    cell = cubic(16.0); cell[2, 2] = lz
    _corr, terms = two_d_image_correction(1, cell, 0.5, 1.0, 6.0, 1.5, 6.0,
                                          1.0, 48, 6)
    vals.append(terms["isolated_eV"])
    print(f"   L_z={lz:5.1f} A:  E_isolated = {terms['isolated_eV']:8.4f} eV")
spread = max(vals) - min(vals)
print(f"   spread over L_z: {spread:.2e} eV")
check(spread < 1e-9, "E_isolated is identical for every supercell")

print("\n4c. In a uniform medium the correction IS the Madelung energy:")
# The two halves have to be on the same normalization, or their difference is
# meaningless however good each is alone. alpha = 2.8373 for simple cubic.
ALPHA_SC = 2.8373
for L in (16.0, 24.0, 32.0):
    corr, _terms = two_d_image_correction(
        1, cubic(L), 0.5, 1.0, 4.0, 4.0, 3 * L, 0.0, 48, 8, uniform=True)
    want = ALPHA_SC / (2.0 * 4.0 * L * BOHR) * HARTREE_EV
    print(f"   L={L:5.1f} A:  E_corr = {corr:7.4f} eV   Madelung = "
          f"{want:7.4f} eV   ratio {corr / want:.4f}")
    check(abs(corr / want - 1.0) < 0.05,
          f"L={L:.0f} A: the correction is the Madelung energy to 5 %")

print("\n5. Anisotropy is not ignored:")
iso, _ = two_d_image_correction(1, cubic(16.0), 0.5, 1.0, 6.0, 6.0, 6.0, 1.0,
                                48, 6)
aniso, _ = two_d_image_correction(1, cubic(16.0), 0.5, 1.0, 6.0, 1.5, 6.0, 1.0,
                                  48, 6)
print(f"   eps_perp=6.0: {iso:+.4f} eV     eps_perp=1.5: {aniso:+.4f} eV")
check(abs(iso - aniso) > 1e-3,
      "eps_perp changes the answer, so the profile is really anisotropic")

print("\n" + ("All 2D correction checks passed." if failures == 0
              else f"{failures} check(s) FAILED."))
sys.exit(0 if failures == 0 else 1)
